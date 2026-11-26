#include "bus_power.h"
#include "config.h"
#include "feeder_bus.h"
#include "feeder_manager.h"
#include "buzzer.h"
#include "evlog.h"

#if BUS_PWR_PIN >= 0

extern FeederBus     g_bus;
extern FeederManager g_mgr;

static bool g_on = true;
/* nFAULT 自动复位状态(文件级: busPowerSet 人工重开时清零) */
static uint16_t      g_resetCnt  = 0;
static bool          g_watchActive = false;

static void _applyLevel() {
    digitalWrite(BUS_PWR_PIN, g_on ? BUS_PWR_ON_LEVEL
                                   : (BUS_PWR_ON_LEVEL == HIGH ? LOW : HIGH));
}

void busPowerInit() {
    pinMode(BUS_PWR_PIN, OUTPUT);
#if BUS_FAULT_PIN >= 0
    pinMode(BUS_FAULT_PIN, INPUT_PULLUP);   /* nFAULT 开漏: 内部上拉兜底; 外部 10k 推荐, 漏贴仍可工作 */
#endif
    g_on = BUS_PWR_DEFAULT_ON;          /* 上电默认开 */
    _applyLevel();
}

bool busPowerIsOn() {
    return g_on;
}

bool busPowerSet(bool on) {
    if (on == g_on) return true;

    if (!on) {
        /* 关电源: 等总线空闲, 避免发送/等待回复中被断电 */
        unsigned long w = millis();
        while (g_bus.busy() && (long)(millis() - w) < 200) { g_bus.loop(); delay(1); }
        delay(2);
    }

    g_on = on;
    _applyLevel();

    /* 断电/上电都清空在线表与 en；上电后等扫描/上报重新建立。
     * 否则关总电源后表格仍显示在线，重新上电后又残留旧 en。 */
    g_mgr.clearRuntime();

#if BUS_FAULT_PIN >= 0
    if (on) {
        g_resetCnt  = 0;            /* 人工重开后重新允许自动复位 */
        g_watchActive = false;
        evlogAdd("PWR", "ON manual");           /* 人工重新开启 */
    }
#endif

    if (on) {
        delay(BUS_PWR_BOOT_MS);         /* 设备上电初始化(300ms) */
        g_mgr.requestScan();            /* 刷新在线表 */
    }
    return true;
}

/* ============================================================
 * nFAULT 故障检测 (SGM2593AD)
 *   - 40ms 软件去抖(芯片内部 15ms; 再滤掉 485 关断瞬间反压闪低)
 *   - 故障上升沿: 蜂鸣 3 声; 若开了自动复位且未超限 -> EN 翻转复位一次
 *   - 复位后观察窗口(5s)内再次故障 -> 停止重试, 保持关断, 等人工
 *   - 人工重新开启(busPowerSet(true)) -> 复位计数清零
 * ============================================================ */
#if BUS_FAULT_PIN >= 0
static bool           g_fault     = false;
static bool           g_rawLow    = false;
static uint16_t       g_faultCnt  = 0;
static unsigned long  g_lowSince  = 0;
static unsigned long  g_watchStart= 0;

bool busPowerFault() { return g_fault; }
uint16_t busPowerFaultCount() { return g_faultCnt; }

void busPowerLoop() {
    bool low = (digitalRead(BUS_FAULT_PIN) == LOW);
    if (low != g_rawLow) {
        g_rawLow = low;
        if (low) g_lowSince = millis();        /* 刚变低: 开始去抖计时 */
    }
    bool debounced = g_rawLow && (long)(millis() - g_lowSince) >= (long)BUS_FAULT_DEBOUNCE_MS;

    if (debounced && !g_fault) {
        /* ---- 故障上升沿(去抖确认) ---- */
        g_fault = true;
        g_faultCnt++;
        buzzerBeep(BEEP_POWER_FAULT, 3);       /* 电源故障: 中长鸣 3 声 */

        if (BUS_FAULT_AUTO_RESET && g_on && g_resetCnt < BUS_FAULT_RESET_MAX) {
            /* 自动 EN 翻转复位一次 */
            g_resetCnt++;
            digitalWrite(BUS_PWR_PIN, BUS_PWR_ON_LEVEL == HIGH ? LOW : HIGH);  /* EN 拉低 */
            delay(BUS_FAULT_RESET_HOLD);
            _applyLevel();                     /* 恢复 g_on 电平 */
            g_watchStart = millis();
            g_watchActive = true;
            Serial.printf("[PWR] nFAULT low -> auto-reset #%u (faultCnt=%u)\n", g_resetCnt, g_faultCnt);
            evlogAdd("PWR", "FAULT reset#%u cnt=%u", g_resetCnt, g_faultCnt);
        } else if (g_on) {
            /* 超限/关闭自动复位: 保持关断, 等人工处理(防打嗝冲击) */
            g_on = false;
            _applyLevel();
            Serial.printf("[PWR] nFAULT low -> BUS POWER OFF (faultCnt=%u, auto-reset exceeded)\n", g_faultCnt);
            evlogAdd("PWR", "FAULT off cnt=%u (auto-reset exceeded)", g_faultCnt);
        }
        /* g_on 原本就是关 -> 不动作, 仅提示 */
    }

    if (!debounced && g_fault) {
        /* ---- 故障清除 ---- */
        g_fault = false;
        g_watchActive = false;
        Serial.printf("[PWR] nFAULT recovered, bus power %s\n", g_on ? "ON" : "OFF");
        evlogAdd("PWR", "RECOVER");            /* 过流消失/复位成功 */
    }

    /* 复位后观察窗口内再次故障 -> 停止重试并保持关断 */
    if (g_watchActive && g_fault &&
        (long)(millis() - g_watchStart) < (long)BUS_FAULT_WATCH_MS) {
        if (g_on) {
            g_on = false;
            _applyLevel();
        }
        g_watchActive = false;                 /* 本次故障已按"窗口内复发"处理 */
        buzzerBeep(BEEP_POWER_LOCK, 2);        /* 低频长鸣: 已停止自动重试 */
        Serial.printf("[PWR] repeat fault in watch window -> LOCK OFF\n");
        evlogAdd("PWR", "LOCK off (repeat fault within watch window)");
    }
}
#else /* BUS_FAULT_PIN < 0: 无故障检测 */
void busPowerLoop() {}
bool busPowerFault() { return false; }
uint16_t busPowerFaultCount() { return 0; }
#endif

#else /* BUS_PWR_PIN < 0: 禁用电源开关功能 */
void busPowerInit() {}
bool busPowerIsOn() { return true; }
bool busPowerSet(bool on) { (void)on; return true; }
void busPowerLoop() {}
bool busPowerFault() { return false; }
uint16_t busPowerFaultCount() { return 0; }
#endif
