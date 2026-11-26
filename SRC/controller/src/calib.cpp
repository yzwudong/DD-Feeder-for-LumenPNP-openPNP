#include "calib.h"
#include "feeder_manager.h"
#include "feeder_bus.h"
#include "mcode.h"
#include "config.h"
#include "buzzer.h"

extern FeederManager g_mgr;

/* ---------------- 校准参数 ----------------
 * 算法(角度递加逼近, 防过冲 + 全程不落 EEPROM):
 *   1. 从"远离限位的安全角度"起步: 0° 从 30° 位置、90° 从 60° 位置
 *   2. 每步只发 M603 A<当前角度> P<chipid>(舵机直驱, 不写任何配置, EEPROM 零磨损)
 *   3. 未触发 -> 朝目标方向加一档(1°), 舵机每次只小幅移动, 不会撞击限位扫齿
 *   4. 触发(到位) -> 反向 1° 回退确认, 最后一次触发的角度档 = "刚好到位"角度
 *   5. 用校准开始时备份的 V0/W0 线性换算: pulse(a)=V0+(W0-V0)*a/180
 *      0° 触发档 -> V_new; 90° 触发档 -> W_new
 *   6. 成功后一次性 M620 全参数写回(失败/过程均不保存)
 * 限位开关直连 ESP32(常开->GND, 内部上拉, LOW=触发), 下位机零改动。 */
static const int CALIB_START_A0     = 30;    /* 0° 校准安全起点(°), 远离限位 */
static const int CALIB_START_A90    = 60;    /* 90° 校准安全起点(°) */
static const int CALIB_STEP         = 1;     /* 逼近/回退步进(°) */
static const int CALIB_CONFIRM_MAX  = 15;    /* 触发后回退确认最多档数(过冲缓冲) */
static const int CALIB_SETTLE_MS    = 220;   /* 舵机到位稳定等待 ms */
static const int CALIB_TOTAL_MS     = 60000; /* 校准总超时 ms */
static const int CALIB_RETRY_MAX    = 3;     /* 单步发送重试次数 */
static const int CALIB_WATCHDOG_MS  = 20000; /* 看门狗: 超过该时长无状态轮询(页面关闭/断线) -> 自动中止校准 */

enum Step { CS_IDLE, CS_INIT, CS_LOOP, CS_WRITE, CS_DONE };

static struct {
    bool      running = false;
    int       mode = -1;            /* 0=0° 1=90° 2=both */
    uint32_t  chipid = 0;
    Step      step = CS_IDLE;
    int       angle = 0;            /* 当前校准角度 0=0° 1=90° */
    int       phase = 0;            /* 0=逼近(朝目标) 1=确认(触发后回退) */
    long      cur = 0;              /* 当前探测角度(°) */
    long      dir = -1;             /* 逼近方向: 0°=-1(角度减小) 90°=+1(角度增大) */
    long      lastTrig = 0;         /* 最后一次触发角度档 */
    int       confirmCnt = 0;       /* 回退确认计数 */
    bool      triggered = false;    /* 该角度是否曾触发过限位 */
    bool      initEnabled = false;  /* 校准前 M610 S1 是否已成功 */
    long      cfgA=-1, cfgB=-1, cfgC=-1, cfgF=-1, cfgU=-1;
    long      cfgV0=-1, cfgW0=-1, cfgX=-1, cfgT=-1;   /* 校准基准配置(换算用) */
    bool      cfgLoaded = false;
    long      v = -1, w = -1;                /* 结果(脉宽 μs) */
    unsigned long t0 = 0, tSettle = 0;
    unsigned long tNextSend = 0;     /* 下一步发送最早时刻(避开 HB_IDLE) */
    unsigned long lastPoll = 0;              /* 看门狗: 最后一次状态轮询时刻(web GET) */
    int       retry = 0;
    String    msg;
} S;

CalibStatus g_status;

/* ---------------- 内部 ---------------- */
static bool _send(MCmdMsg m, std::function<void(const String&)> cb) {
    if (!g_mgr.command(m, [cb](uint8_t, const String& r){ cb(r); })) {
        if (S.retry < 250) S.retry++;   /* 总线忙/队列满: 由 calibLoop 重试 */
        return false;
    }
    S.retry = 0;
    return true;
}

/* M620 全参数组包(V/W 用调用值, 其余保持基准值; 带 P<chipid>) */
static MCmdMsg _cfgMsg(long v, long w) {
    MCmdMsg m;
    m.code = MCMD_CONFIG;
    m.chipid = S.chipid; m.hasP = true;
    if (S.cfgA >= 0) { m.a = S.cfgA; m.hasA = true; }
    if (S.cfgB >= 0) { m.b = S.cfgB; m.hasB = true; }
    if (S.cfgC >= 0) { m.c = S.cfgC; m.hasC = true; }
    if (S.cfgF >= 0) { m.f = S.cfgF; m.hasF = true; }
    if (S.cfgU >= 0) { m.u = S.cfgU; m.hasU = true; }
    if (v >= 0)      { m.v = v;      m.hasV = true; }
    if (w >= 0)      { m.w = w;      m.hasW = true; }
    if (S.cfgX >= 0) { m.x = S.cfgX; m.hasX = true; }
    if (S.cfgT >= 0) { m.t = S.cfgT; m.hasT = true; }
    return m;
}

static bool _queryP(std::function<void(const FeederCfg&)> cb);

/* 校准前必须先使能: M603 在 DIS 状态会被飞达拒绝。 */
static bool _enableP(std::function<void(const FeederCfg&)> cb) {
    MCmdMsg m;
    m.code = MCMD_ENABLE;
    m.s = 1; m.hasS = true;
    m.chipid = S.chipid; m.hasP = true;
    return _send(m, [cb](const String&){
        /* M610 刚回包时总线还处于刚活动状态，立刻 M602 会被 HB_IDLE 节流拒绝。
         * 这里只置状态，由 calibLoop 等 500ms 再发 M602。 */
        S.initEnabled = true;
        S.t0 = millis();
    });
}

/* 角度 -> 脉宽(用校准基准 V0/W0 线性映射, 180° 跨度) */
static long _pulseOf(long angle) {
    long span = (S.cfgW0 > S.cfgV0) ? (S.cfgW0 - S.cfgV0) : 2000;
    long v0   = (S.cfgV0 >= 0) ? S.cfgV0 : 500;
    return v0 + span * angle / 180;
}

static void _finish(bool ok, const String& msg) {
    S.running = false;
    S.step = CS_DONE;
    S.msg = msg;
    g_status.running = false;
    g_status.stage = ok ? 3 : -1;
    g_status.v = S.v; g_status.w = S.w;
    g_status.iter = S.phase + (S.angle ? 10 : 0);   /* 简易进度显示 */
    g_status.limitSeen = true;                      /* 限位为 GPIO 直读, 恒可用 */
    g_status.msg = msg;
    if (ok) buzzerBeep(BEEP_OK, 1);      /* 成功: 短促 1 次 */
    else    buzzerBeep(BEEP_CMD_ERR, 2); /* 失败: 短鸣 2 次 */
}

/* 失败: 不写回任何配置(校准过程只发 M603 直驱, EEPROM 零写入;
 * 仅成功后一次性 M620 写回精确 V/W)。 */
static void _fail(const String& msg) {
    _finish(false, msg);
}

/* 当前限位是否触发(直读 GPIO, LOW=触发) */
static bool _limitOn() {
    int pin = (S.angle == 0) ? CALIB_LIMIT0_PIN : CALIB_LIMIT90_PIN;
    if (pin < 0) return false;
    return digitalRead(pin) == LOW;
}

/* M602 P<chipid> 单点查询(备份配置用) */
static bool _queryP(std::function<void(const FeederCfg&)> cb) {
    MCmdMsg m;
    m.code = MCMD_QUERY;
    m.chipid = S.chipid; m.hasP = true;   /* M602 按 chipid 定位 */
    return _send(m, [cb](const String& r){
        FeederCfg c;
        if (!mcodeParseConfig(r, c)) { cb(FeederCfg()); return; }
        cb(c);
    });
}

static void _scanSend();   /* 前置声明 */

/* 每步只发 M603 A<当前角度> 直驱舵机(不写配置, EEPROM 零磨损) -> 等待到位 */
static void _scanSend() {
    /* 先置 CS_LOOP: 若 M603 入队失败，calibLoop 会重发 M603，而不是退回去重发 M602。 */
    S.step = CS_LOOP;
    MCmdMsg m;
    m.code = MCMD_SERVO;
    m.a = (int)S.cur; m.hasA = true;
    m.chipid = S.chipid; m.hasP = true;
    _send(m, [](const String&){
        S.tSettle = millis() + CALIB_SETTLE_MS;
    });
}

/* 该角度完成: 换算脉宽并进入下一角度或一次性写回 */
static void _angleDone(long angle) {
    if (S.angle == 0) {
        S.v = _pulseOf(angle);                 /* 0° 触发档角度 -> V(μs) */
        if (S.mode == 2) {
            S.angle = 1;
            S.phase = 0;
            S.dir = 1;                          /* 90°: 角度增大方向逼近 */
            S.triggered = false;
            S.confirmCnt = 0;
            S.cur = CALIB_START_A90;            /* 90° 安全起点: 60°(未触发侧) */
            g_status.stage = 1;
            _scanSend();
            return;
        }
        if (S.mode == 0) {                      /* 只校 0°: W 保持原基准 */
            S.w = S.cfgW0;
            if (S.w < S.v + 50) { _fail("W 基准无效 (W<V)"); return; }
        }
    } else {
        S.w = _pulseOf(angle);                 /* 90° 触发档角度 -> W(μs) */
        long vRef = (S.mode == 2 && S.v > 0) ? S.v : S.cfgV0;
        if (S.w < vRef + 50) { _fail("90° 结果无效 (W<V)"); return; }
        if (S.mode == 1 || S.v < 0) S.v = vRef;   /* 只校 90° 或 0° 失败: V 保持原基准 */
    }
    /* 成功后一次性写回最终 V/W(过程/失败均不保存) */
    S.step = CS_WRITE;
    _send(_cfgMsg(S.v, S.w), [](const String&){ _finish(true, "V=" + String(S.v) + " W=" + String(S.w)); });
}

/* 到位判定回调(由 calibLoop 在 settle 后触发)
 * phase0 逼近: 未触发 -> 朝目标方向加一档(1°); 触发 -> 转确认, 反向回退
 * phase1 确认: 回退档未触发 -> 上一次触发档即边界; 仍触发 -> 继续回退 */
static void _onSettled() {
    if (S.phase == 0) {
        if (!_limitOn()) {
            /* 未触发: 朝目标方向加一档(小幅移动, 不撞击) */
            S.cur += S.dir * CALIB_STEP;
            /* 范围保护: 0° 走到 0 / 90° 走到 90 仍未触发 -> 异常(开关/行程/基准偏差) */
            if (S.angle == 0 && S.cur < 0) {
                /* 0° 限位没触发时，90° 仍可独立校准；不要整场 abort。 */
                if (S.mode == 2) {
                    S.v = -1;
                    S.angle = 1; S.phase = 0; S.dir = 1;
                    S.triggered = false; S.confirmCnt = 0;
                    S.cur = CALIB_START_A90;
                    S.msg = "0° limit missed; continue 90°";
                    g_status.stage = 1;
                    _scanSend();
                    return;
                }
                _fail("0° 限位未触发(角度已到 0°, 检查开关/行程)");
                return;
            }
            if (S.angle == 1 && S.cur > 90) { _fail("90° 限位未触发(角度已到 90°, 检查开关/行程)"); return; }
            g_status.iter++;
            _scanSend();
            return;
        }
        /* 触发(到位): 转确认阶段, 反向回退找"最后一次触发档" */
        S.triggered = true;
        S.phase = 1;
        S.lastTrig = S.cur;
        S.confirmCnt = 0;
        S.cur = S.cur - S.dir * CALIB_STEP;     /* 回退一档(远离限位方向) */
        _scanSend();
        return;
    }

    /* phase1 确认 */
    if (_limitOn()) {
        S.lastTrig = S.cur;
        S.confirmCnt++;
        if (S.confirmCnt >= CALIB_CONFIRM_MAX) {
            /* 触发带过宽(机械卡死/开关粘连): 取最后一次触发档并提示 */
            _angleDone(S.lastTrig);
            return;
        }
        S.cur = S.cur - S.dir * CALIB_STEP;     /* 继续回退 */
        if (S.cur < 0 || S.cur > 180) { _angleDone(S.lastTrig); return; }
        _scanSend();
        return;
    }
    /* 回退档未触发: 最后一次触发档 = "刚好到位"角度 */
    _angleDone(S.lastTrig);
}

/* ---------------- 对外接口 ---------------- */

void calibInit() {
    g_status.running = false;
    g_status.stage = -1;
    g_status.msg = "";
    if (CALIB_LIMIT0_PIN >= 0)  pinMode(CALIB_LIMIT0_PIN, INPUT_PULLUP);
    if (CALIB_LIMIT90_PIN >= 0) pinMode(CALIB_LIMIT90_PIN, INPUT_PULLUP);
}

static void _initCb(const FeederCfg& c);   /* 前置声明 */

/* 启动第一步回调: 备份配置后进入对应角度粗扫 */
static void _initCb(const FeederCfg& c) {
    S.t0 = millis();
    if (c.err < 0 && !c.hasChip) { _fail("M602 查询失败(设备无回复)"); return; }
    S.cfgA = c.a; S.cfgB = c.b; S.cfgC = c.c; S.cfgF = c.f;
    S.cfgU = c.u; S.cfgV0 = c.v; S.cfgW0 = c.w; S.cfgX = c.x; S.cfgT = c.t;
    S.cfgLoaded = true;

    /* 检查限位引脚可用 */
    if (S.mode != 1 && CALIB_LIMIT0_PIN < 0)  { _fail("CALIB_LIMIT0_PIN 未配置"); return; }
    if (S.mode != 0 && CALIB_LIMIT90_PIN < 0) { _fail("CALIB_LIMIT90_PIN 未配置"); return; }

    if (S.mode != 1) {   /* 0° 或全部 */
        S.angle = 0; S.phase = 0; S.dir = -1; S.triggered = false; S.confirmCnt = 0;
        /* 0° 安全起点: 30°(未触发侧, 不会撞击限位) */
        S.cur = CALIB_START_A0;
        g_status.stage = 0;
    } else {           /* 仅 90° */
        S.angle = 1; S.phase = 0; S.dir = 1; S.triggered = false; S.confirmCnt = 0;
        /* 90° 安全起点: 60°(未触发侧) */
        S.cur = CALIB_START_A90;
        g_status.stage = 1;
    }
    /* M602 刚回包，总线仍处于活动状态；M603 立刻发会被 HB_IDLE(200ms) 拒绝。
     * 等 500ms 再由 calibLoop 发第一步 M603。 */
    S.step = CS_LOOP;
    S.tNextSend = millis() + 500;
}

bool calibStart(uint32_t chipid, int mode) {
    if (S.running) return false;
    if (mode < 0 || mode > 2) return false;

    S.running = true;
    S.mode = mode;
    S.chipid = chipid;
    S.step = CS_INIT;
    S.t0 = millis();
    S.initEnabled = false;
    S.angle = 0; S.phase = 0; S.dir = -1;
    S.triggered = false;
    S.confirmCnt = 0;
    S.lastTrig = 0;
    S.cur = 0;
    S.cfgLoaded = false;
    S.v = S.w = -1;
    S.cfgA = S.cfgB = S.cfgC = S.cfgF = S.cfgU = -1;
    S.cfgV0 = S.cfgW0 = S.cfgX = S.cfgT = -1;
    S.t0 = millis();
    S.lastPoll = millis();       /* 看门狗起点: 启动时刷新 */
    S.retry = 0;
    S.tNextSend = 0;
    S.msg = "";

    g_status.running = true;
    g_status.mode = mode;
    g_status.chipid = chipid;
    g_status.stage = 0;
    g_status.iter = 0;
    g_status.msg = "init";

    /* 第一步: M602 读当前配置(作换算基准, 不改动任何值)。
     * 如果总线忙/队列满，不能静默等到 60s 超时，要进入重试。 */
    if (!_enableP(_initCb) && S.retry == 0) S.retry = 1;
    return true;
}

bool calibActive() { return S.running; }

void calibAck() {
    S.msg = "";
    g_status.msg = "";
}

void calibStop() {
    if (!S.running) return;
    S.running = false;
    S.step = CS_IDLE;
    S.msg = "";
    g_status.running = false;
    g_status.stage = -1;
    g_status.msg = "";
}

void calibLoop() {
    if (!S.running) return;

    /* 总体超时 */
    if ((long)(millis() - S.t0) >= (long)CALIB_TOTAL_MS) { _fail("calib timeout"); return; }

    /* 看门狗: 页面关闭/断线后不再轮询状态 -> 自动中止校准(防止舵机无人值守持续动作)。
     * 任何来源(网页 /api/calib/status、串口屏周期读状态)都会刷新 lastPoll。 */
    if ((long)(millis() - S.lastPoll) >= (long)CALIB_WATCHDOG_MS) {
        _fail("calib aborted: no status poll (page closed?)");
        return;
    }

    /* 单步发送重试(队列满/总线忙) */
    if (S.retry > 0) {
        if (S.retry > CALIB_RETRY_MAX) { _fail("bus busy: M603/M602 no response"); return; }
        switch (S.step) {
        case CS_INIT:  if (S.initEnabled) _queryP(_initCb); else _enableP(_initCb); return;
        case CS_LOOP:  _scanSend(); return;
        case CS_WRITE: _send(_cfgMsg(S.v, S.w), [](const String&){ _finish(true, "V=" + String(S.v) + " W=" + String(S.w)); });
                       return;
        default: break;
        }
    }

    /* M610 回包后等总线连续空闲(>HB_IDLE_MS)再发 M602；M602 3s 无应答才报错。 */
    if (S.step == CS_INIT && S.initEnabled && S.t0 && (millis() - S.t0) > 500) {
        S.t0 = millis();
        _queryP(_initCb);
        return;
    }
    if (S.step == CS_INIT && S.t0 && (millis() - S.t0) > 3000) {
        _fail("calib init no response (M610/M602)");
        return;
    }

    /* M602 后/M603 后的下一步等待窗口 */
    if (S.step == CS_LOOP && S.tNextSend && (long)(millis() - S.tNextSend) >= 0) {
        S.tNextSend = 0;
        _scanSend();
        return;
    }

    /* 舵机到位延时推进 -> 判定限位 */
    if (S.tSettle && (long)(millis() - S.tSettle) >= 0) {
        S.tSettle = 0;
        _onSettled();
    }
}

const CalibStatus& calibStatus() {
    if (S.running) S.lastPoll = millis();    /* 看门狗刷新: 每次状态读取都视为"有人看着" */
    g_status.running = S.running;
    if (S.running) {
        g_status.stage = (S.angle == 0) ? 0 : 1;
        g_status.iter = S.phase + (S.triggered ? 2 : 0);
        g_status.lo = S.cur; g_status.hi = S.lastTrig ? S.lastTrig : S.cur;
        g_status.limitSeen = true;
    }
    return g_status;
}

