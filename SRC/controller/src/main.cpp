#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "config.h"
#include "feeder_bus.h"
#include "feeder_manager.h"
#include "web_server.h"
#include "ota_manager.h"
#include "mcode.h"
#include "wifi_utils.h"
#include "log_ring.h"
#include "screen.h"
#include "bus_power.h"
#include "ina226.h"
#include "op_lock.h"
#include "calib.h"
#include "log_util.h"
#include "buzzer.h"
#include "evlog.h"
#include "function_key.h"
#include "factory_reset.h"
#include "feeder_upgrade.h"

/* ---------------- 全局对象 ---------------- */
FeederBus       g_bus;      // 485 总线(UART2)
FeederManager   g_mgr;
WebServerApp    g_web;

extern volatile bool g_ispActive;   // 飞达 ISP 烧写期间总线独占(定义于 feeder_upgrade.cpp)

/* Arduino-ESP32 3.x 的 Preferences::begin 只读模式不创建命名空间(读写模式自动创建),
 * logInit/auth/hb/evlog 等只读访问未创建过的命名空间必报 nvs_open NOT_FOUND.
 * 启动时用读写模式统一预创建全部命名空间根治(本 SDK nvs_open 读写自动建, 无需 CREATE 宏). */
static void _nvsEnsureNamespaces() {
    static const char* ns[] = {"buzzer", "log", "wifi", "hb", "evlog", "auth", "creds"};
    for (auto n : ns) {
        nvs_handle_t h;
        if (nvs_open(n, NVS_READWRITE, &h) == ESP_OK) {
            nvs_close(h);
        }
    }
}

/* ============================================================
 * UART1 -> UART2 透传 (外部主机 -> 485 总线)
 * 外部主机(上位机) 通过 UART1 发送 MCODE 行，原样转发到 485；
 * 485 上设备的回复回传到 UART1。
 * 透传活动期间暂停 Web/轮询对总线的使用，避免冲突。
 * ============================================================ */
static String        ptBuf;
static bool          ptActive = false;
static unsigned long ptLastAct = 0;
static const uint32_t PT_IDLE_MS   = 400;   /* 无新数据判定行结束/退出透传 */
static const uint32_t PT_LINE_MAX  = 256;   /* 单行最大长度 */
static const uint32_t PT_BUS_WAIT  = 100;   /* 等总线空闲上限(ms) */

/* UART1(外部主机口)安全写: 对端可能不读(未接 / 上位机未开 / 调试器只收不读),
 * 直接 print 会写满 TX 缓冲永久阻塞 loopTask —— Web/监听/心跳/广播全停,
 * 表现为"过段时间看不到 ready、log 开关点了没反应"。
 * 只写剩余可写字节, 放不下就丢弃: 转发数据非关键, 宁可丢不能卡死。 */
static void extWrite(const String& s) {
    if (s.length() == 0) return;
    int room = Serial1.availableForWrite();
    if (room <= 0) return;
    size_t n = ((size_t)room < s.length()) ? (size_t)room : s.length();
    Serial1.write((const uint8_t*)s.c_str(), n);
}

static void ptForwardLine(String line) {
    line.trim();
    if (line.length() == 0) return;

    /* ESP32 系统命令 (M9xx): 本地处理, 不透传 485, 回复直接回外部主机 */
    String sysReply;
    if (mcodeHandleSystem(line, sysReply)) {
        extWrite(sysReply);
        if (sysReply.length() && sysReply[sysReply.length() - 1] != '\n') extWrite("\n");
        return;
    }

    /* UART1 的 M-code 全部进 FeederManager 队列：
     *   M600/M601 -> 生产队列（最高优先级）
     *   M610 S1   -> 优先级队列（生产准备，不启动生产锁）
     *   其它      -> 普通队列（生产锁期间暂停）
     * 这样 UART1 的 M602/M620 等也不会绕开生产锁直接插总线。 */
    MCmdMsg pm;
    if (mcodeParse(line, pm)) {
        bool isProd = (pm.code == MCMD_ADVANCE || pm.code == MCMD_RETRACT);
        bool isEnable = (pm.code == MCMD_ENABLE && pm.hasS && pm.s == 1);
        auto reply = [](uint8_t, const String& r){
            extWrite(r);
            if (r.length() && r[r.length() - 1] != '\n') extWrite("\n");
        };
        bool ok;
        if (isProd)         ok = g_mgr.commandProduction(pm, reply);
        else if (isEnable)  ok = g_mgr.commandPriority(pm, reply);
        else                ok = g_mgr.command(pm, reply);
        if (!ok) extWrite("error queue busy\n");
        return;
    }

    /* 非 M-code 原样透传 */
    unsigned long w = millis();
    while (g_bus.busy() && (long)(millis() - w) < (long)PT_BUS_WAIT) { g_bus.loop(); delay(1); }
    g_bus.send(line, BUS_CMD_TIMEOUT, [](const String& r){
        /* 设备回复回传外部主机(防阻塞) */
        extWrite(r);
        if (r.length() && r[r.length() - 1] != '\n') extWrite("\n");
    });
}

static void ptLoop() {
    /* 读外部主机字节，按行转发 */
    while (Serial1.available()) {
        ptActive = true;
        ptLastAct = millis();
        char c = (char)Serial1.read();
        ptBuf += c;
        if (c == '\n' || ptBuf.length() >= PT_LINE_MAX) {
            String line = ptBuf;
            ptBuf = "";
            ptForwardLine(line);
        }
    }

    /* 行尾兜底: 有残留但长时间无新数据 -> 当作一行转发 */
    if (ptActive && ptBuf.length() && (long)(millis() - ptLastAct) >= (long)PT_IDLE_MS) {
        String line = ptBuf;
        ptBuf = "";
        ptForwardLine(line);
    }

    /* 空闲超时 -> 退出透传，恢复轮询 */
    if (ptActive && (long)(millis() - ptLastAct) >= (long)PT_IDLE_MS) {
        ptActive = false;
    }
}

/* 递归枚举 LittleFS 文件系统(诊断) */
static void _listFs(const char* dir) {
    File root = LittleFS.open(dir);
    if (!root) return;
    File f = root.openNextFile();
    while (f) {
        if (f.isDirectory()) {
            LOGD("[FS] %s/ (dir)\n", f.name());
            _listFs((String("/") + f.name()).c_str());   // LittleFS 要求绝对路径
        } else {
            LOGD("[FS] %s (%u bytes)\n", f.name(), (unsigned)f.size());
        }
        f = root.openNextFile();
    }
}

void setup() {
    // 延长 loopTask 看门狗: WebServer 在弱网(AP/手机)下发送响应可能阻塞超过默认 5s,
    // 否则会被 WDT 重启导致 AP 突然断开。30s 足够覆盖最慢的整页+资源加载。
    esp_task_wdt_config_t wdtCfg = {
        .timeout_ms  = 30000,   // 30s
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdtCfg);

    Serial.begin(UART0_BAUD);           // UART0 = USB 调试
    Serial.setDebugOutput(true);

    /* NVS 必须先初始化, 否则 Preferences/NVS 全报 nvs_open NOT_FOUND */
    {
        esp_err_t er = nvs_flash_init();
        if (er == ESP_ERR_NVS_NO_FREE_PAGES || er == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            nvs_flash_erase();          // NVS 已损坏/版本不符: 擦除重建(配置丢, 属首次预期)
            nvs_flash_init();
        }
    }
    _nvsEnsureNamespaces();             // 预创建全部 Preferences 命名空间(3.x 不自动创建)

    logInit();                          // 读 M906 日志开关状态(NVS), 重启后保持
    /* 版本 banner: Serial 直打(不经 LOGD, 不受 M906 日志开关门控), 每次启动必见, 便于确认刷机版本 */
    Serial.printf("\n==== %s v%s ====  (%s)\n", FW_NAME, FW_VERSION, FW_VER_FULL);
    LOGD("\n");
    // 诊断: 打印上次重启原因(0上电 1外部 2软复位 3看门狗 4panic 5断电brownout 等)
    LOGD("[Boot] reset reason: %d\n", (int)esp_reset_reason());
    LOGD("\n==== %s v%s ====\n", FW_NAME, FW_VERSION);
    LOGD("[VER] %s\n", FW_VER_FULL);   // 完整版本串, 供打包脚本从固件内提取

    // 外部主机口 (UART1)
    Serial1.begin(EXT_BAUD, SERIAL_8N1, EXT_RX_PIN, EXT_TX_PIN);
    LOGD("[UART1] host port %d baud (TX=%d RX=%d)\n", EXT_BAUD, EXT_TX_PIN, EXT_RX_PIN);

    // 485 总线 (UART2 + MAX485)
    g_bus.begin(BUS_TX_PIN, BUS_RX_PIN, BUS_DE_PIN, BUS_BAUD);
    /* X56 上电主动上报监听: 飞达 ready. 后自发广播 "ok ready addr:<a> ChipID:<HHHHHH>"
     * (仅 ok/error 前缀的上报行, ready. 就绪标记不算主动上报)
     * -> 转发外部主机(上位机空闲窗口监听) + ESP32 内部更新在线表/冲突检测(秒级发现) */
    g_bus.onUnsolicited([](const String& line){
        /* 放行 ok/error 前缀(就绪/状态上报) + N 前缀(整行应答被 bus 提前完成的余帧兜底,
         * 如 "N99 A90...": 参数帧不丢, 仍更新临时表配置) */
        if (line.startsWith("ok ") || line.startsWith("error ") || line.startsWith("N")) {
            extWrite(line);
            extWrite("\n");
            g_mgr.onBusMessage(line);
        }
    });
    busPowerInit();
    inaInit();
    opLockInit();              // 485 总线电源(GPIO->MOS): 上电默认开, 必须在总线活动前
    buzzerInit();                // 蜂鸣器(无源, LEDC PWM): 错误/事件提示, 从 NVS 读开关/音量
    factoryResetInit();          // 恢复出厂按键(GPIO34 长按 5s), 见 config.h / docs
    functionKeyInit();           // GPIO14 多功能维护键: 短按 485 电源, 双击扫描
    g_mgr.begin(&g_bus);

    // 文件系统(LittleFS)：存放 data/ 下的 Web 页面; 失败重试(偶发分区未就绪)
    bool fsOk = false;
    for (int i = 0; i < 3 && !fsOk; i++) {
        if (LittleFS.begin()) { fsOk = true; }
        else { delay(200); }
    }
    if (!fsOk) {
        LOGD("[FS] LittleFS mount FAILED, Web shows fallback (pio run -t uploadfs)\n");
        Serial.printf("[FS] LittleFS MOUNT FAILED - Web pages unavailable (fsver=--)\n");
    } else {
        LOGD("[FS] LittleFS OK\n");
        /* 页面版本直打(与固件版本同规则, 不经 LOGD): 刷页面包后一眼确认 FS 版本是否对版 */
        String fsv = "-";
        File f = LittleFS.open("/fsver.txt", "r");
        if (f) { fsv = f.readString(); f.close(); }
        fsv.trim();
        Serial.printf("[FS] pages fsver=%s\n", fsv.length() ? fsv.c_str() : "-");
        _listFs("/");   // 递归枚举, 核对 css/js 内部文件是否齐全
    }

    // NVS 环形事件日志(电源故障/OTA/启动, 见 docs/总线电源与nFAULT说明.md)
    evlogInit();
    evlogAdd("SYS", "BOOT ver=" FW_VERSION " reason=%d", (int)esp_reset_reason());
    calibInit();                 // 舵机限位校准(ESP32 侧二分)

    // WiFi: 读 NVS -> 连已存/默认网络 -> 失败开热点
    wifiInit();

    // Web
    g_web.begin(&g_mgr);
    LOGD("[Web] server started, listening on http://%s:%d\n",
         WiFi.localIP().toString().c_str(), WEB_PORT);

    // 串口屏(淘晶驰): 软串口/UART0 双后端, 详见 config.h / docs/screen协议.md
    screenBegin();
    if (SCREEN_ENABLED) LOGD("[Screen] display %d baud (backend=%s)\n", SCREEN_BAUD,
        SCREEN_BACKEND == SCREEN_BACKEND_UART0 ? "UART0" : "soft-serial");
}

void loop() {
    /* ISP 烧写(飞达升级)期间 485 串口被 STC8 协议独占, 暂停总线状态机避免冲突 */
    if (!g_ispActive) g_bus.loop();

    // OTA(含设备 ISP 烧写)进行中时暂停总线调度，避免冲突
    if (!otaBusy()) {
        // 透传活动期间暂停飞达轮询，避免与外部主机抢总线
        if (!ptActive) g_mgr.loop();
    }

    ptLoop();                           // UART1 -> UART2 透传

    calibLoop();                        // 舵机限位校准状态机(非阻塞, 见 calib.cpp)

    busPowerLoop();                     // 485 电源 nFAULT 检测 + 自动复位(非阻塞)
    buzzerLoop();                       // 蜂鸣状态机(非阻塞)
    factoryResetLoop();                 // 恢复出厂: 长按检测 + M907/Web 延迟执行
    functionKeyLoop();                  // GPIO14: 短按 485 电源/双击扫描
    g_web.loop();
    screenLoop();                       // 串口屏: 读屏指令 + 周期推状态
    yield();   // 让出 CPU 给 WiFi/lwIP 栈, 避免大文件 streamFile 阻塞时并发连接超时

    // 后台每 6h 自动检测一次版本（注释掉可只保留手动检测，避免 GitHub 限速）
    static unsigned long lastCheck = 0;
    if ((long)(millis() - lastCheck) >= (long)OTA_CHECK_INTERVAL_MS) {
        lastCheck = millis();
        otaCheckGitHub();
    }

    // ---- MCODE 串口控制台：在 USB 串口输入命令，广播/定向到 485 ----
    if (Serial.available()) {
        static String line;
        char c = (char)Serial.read();
        if (c == '\n') {
            line.trim();
            if (line.length() > 0) {
                String iapOut;
                if (fupConsole(line, iapOut)) {
                    Serial.print(iapOut);
                } else {
                /* ESP32 系统命令(M9xx)本地处理, 不回 485 (与透传/Web 终端一致) */
                String sysReply;
                if (mcodeHandleSystem(line, sysReply)) {
                    Serial.printf("[sys] %s", sysReply.c_str());
                    if (sysReply.length() && sysReply[sysReply.length()-1] != '\n') Serial.println();
                } else {
                    MCmdMsg m;
                    if (mcodeParse(line, m)) {
                        String cmd = mcodeBuildMsg(m);   // 按解析结果原样重建(N/P/A/S/F/O)
                        g_bus.send(cmd, BUS_CMD_TIMEOUT, [](const String& r){
                            Serial.printf("[485]< %s\n", r.length() ? r.c_str() : "(无回复)");
                        });
                        Serial.printf("[485]> %s", cmd.c_str());
                    } else {
                        Serial.println("[console] 无法解析，示例: M610 N5 S1 / M602 N5");
                    }
                }
                }
            }
            line = "";
        } else if (c != '\r') {
            line += c;
        }
    }
}
