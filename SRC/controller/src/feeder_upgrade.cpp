#include "feeder_upgrade.h"
#include "intel_hex.h"
#include "config.h"
#include "mcode.h"
#include "ota_manager.h"
#include "iap_client.h"
#include "buzzer.h"
#include "evlog.h"
#include "log_util.h"
#include "log_ring.h"
#include "feeder_bus.h"
#include "bus_power.h"
#include "feeder_manager.h"

#include <HTTPClient.h>

extern FeederBus g_bus;
extern FeederManager g_mgr;

/* ISP 烧写期间总线独占标志: main loop 据此暂停 g_bus.loop()/透传,
 * 避免 STC8 原始收发(Serial2)与 485 状态机抢同一串口。 */
volatile bool g_ispActive = false;

/* ---------------- 升级状态（Web 轮询 /api/feeder/up/status） ---------------- */
static volatile bool  g_fBusy   = false;   // 升级任务运行中
static volatile int   g_prog    = 0;       // 0..100
static String         g_state   = "idle";  // idle|downloading|parsing|comparing|isp|verify|done|error
static String         g_msg;
static String         g_hexCache;          // 本地上传 / GitHub 下载的 hex 文本
static HexImage_t     g_img;               // 解析后的固件镜像
static HexFwVer_t     g_pkgVer;            // 包版本
static bool           g_pkgReady = false;  // 本地包已上传且校验通过
static String         g_pkgVerStr;         // 包版本字符串 "2610.2402"
static String         g_boardVerStr;       // 板上版本字符串
static uint8_t        g_upAddr = 0;
static uint32_t       g_upChipid = 0;       // 0=未知; 非0 时 M602/M788 带 P 定点(多个 N99 必需)
static volatile bool  g_boardQBusy = false;   /* 板上版本查询进行中 */
static bool           g_boardQDone = false;   /* 最近一次查询已有结果 */
static bool           g_upForce = false;

/* ---------------- 内部工具 ---------------- */

/* 从 M602 应答提取 "FW:" 后的版本文本（如 "2610.2402"，取 0-9/. 字符） */
static String _fwFromReply(const String& r) {
    int p = r.indexOf("FW:");
    if (p < 0) return "";
    String s = r.substring(p + 3);
    s.trim();
    int e = 0;
    while (e < (int)s.length()) {
        char c = s[e];
        if (!((c >= '0' && c <= '9') || c == '.')) break;
        e++;
    }
    return s.substring(0, e);
}

/* M602/M788 应答缓冲: 回调写全局静态 String(不捕获调用方栈引用, 应答晚到也安全)。
 * bus 由 main loop 驱动, 回调在 loop 上下文执行; 调用方只能 delay 轮询 g_qDone。
 *   - fupTask(独立任务): delay 不阻塞 loopTask, 应答正常处理, 同步等待语义成立;
 *   - Web 路径: 不能在 handler 栈同步等(会阻塞 loopTask 饿死应答), 走异步查询(见 fupBoardQuery*) */
static String        g_qReply;
static volatile bool g_qDone = false;

/* 升级用单点命令组包: 地址 0 也必须发 N0(legacy mcodeBuild 会把 0 当广播 N255);
 * chipid 非 0 时同时带 P<chipid>, 多个 addr=99 临时飞达时只有目标会响应。 */
static String _cmdFor(uint16_t code, uint8_t addr, uint32_t chipid = 0) {
    MCmdMsg m;
    m.code = code;
    m.addr = addr;
    m.hasN = true;
    if (chipid) {
        m.chipid = chipid;
        m.hasP = true;
    }
    return mcodeBuildMsg(m);
}

/* 发起 M602 单点查询(异步) */
static bool _queryFwStart(uint8_t addr) {
    if (g_bus.busy() ||
        (long)(millis() - g_bus.lastActivityMs()) < (long)FUP_QUERY_IDLE_MS) return false;
    g_qReply = "";
    g_qDone = false;
    return g_bus.send(_cmdFor(MCMD_QUERY, addr, g_upChipid), 300,
                      [](const String& r){ g_qReply = r; g_qDone = true; });
}

/* 发起 M788 定向进 ISP(异步) */
static bool _m788Start(uint8_t addr) {
    if (g_bus.busy() ||
        (long)(millis() - g_bus.lastActivityMs()) < (long)FUP_QUERY_IDLE_MS) return false;
    g_qDone = false;
    return g_bus.send(_cmdFor(MCMD_ISP_RESET, addr, g_upChipid), 300,
                      [](const String&){ g_qDone = true; });
}

/* 同步等待应答(仅供 fupTask 等非 loopTask 上下文使用) */
static bool _queryFw(uint8_t addr, String& fw) {
    if (!_queryFwStart(addr)) return false;
    unsigned long t0 = millis();
    while (!g_qDone && (long)(millis() - t0) < 400) delay(2);
    fw = _fwFromReply(g_qReply);
    return g_qDone && fw.length() > 0;
}

static bool _m788(uint8_t addr, String& err) {
    /* 上一条 M602 刚结束, 总线还处于"刚活动"状态: 先等连续空闲 ≥ FUP_QUERY_IDLE_MS。 */
    unsigned long t0 = millis();
    while ((g_bus.busy() ||
            (long)(millis() - g_bus.lastActivityMs()) < (long)FUP_QUERY_IDLE_MS) &&
           (millis() - t0) < 2000) {
        delay(5);
    }
    if (g_bus.busy()) { err = "总线忙"; return false; }

    String cmd = _cmdFor(MCMD_ISP_RESET, addr, g_upChipid);
    cmd.trim();                         /* _cmdFor 末尾已有 \n，不能再拼出 \n\r\n */
    String wire = cmd + "\r\n";

    /* fupTask 与 loopTask 不同核，FeederBus 的 _tx/_state 没有锁；
     * 升级任务不能用 g_bus.send()。这里暂停普通轮询，直接写 485 并本地收应答。 */
    g_ispActive = true;
    logPush('T', cmd);
    g_bus.writeRaw((const uint8_t*)wire.c_str(), wire.length());

    String rx;
    bool ack = false;
    unsigned long tr = millis();
    while ((long)(millis() - tr) < 1500) {
        while (Serial2.available()) {
            char c = (char)Serial2.read();
            if (c == '\r') continue;
            if (c == '\n') {
                if (rx.length()) {
                    logPush('R', rx);
                    if (rx.indexOf("ok ISP reset") >= 0 || rx.indexOf("IAP") >= 0 || rx.indexOf("DDFBL") >= 0) ack = true;
                    rx = "";
                }
            } else if (rx.length() < 80) {
                rx += c;
            }
        }
        if (ack) break;
        delay(1);
    }
    if (rx.length()) {
        logPush('R', rx);
        if (rx.indexOf("ok ISP reset") >= 0 || rx.indexOf("IAP") >= 0 || rx.indexOf("DDFBL") >= 0) ack = true;
    }
    if (!ack) err = "飞达未应答 M788";
    return ack;
}

/* 下载 GitHub Release 的飞达 hex 资产到 cache */
static bool _downloadHex(String& hex, String& err) {
    String j = otaCheckGitHub();
    int p = j.indexOf("\"device_url\":\"");
    if (p < 0) { err = "GitHub 无飞达固件资产(" DEVICE_ASSET ")"; return false; }
    p += 15;
    int e = j.indexOf('"', p);
    if (e < 0) { err = "GitHub 响应异常"; return false; }
    String url = j.substring(p, e);

    HTTPClient http;
    http.setTimeout(20000);
    http.useHTTP10(true);
    http.setUserAgent("ESP32-OTA/" FW_VERSION);
    if (!http.begin(url)) { err = "连接失败"; return false; }
    int code = http.GET();
    if (code != 200) { err = "HTTP " + String(code); http.end(); return false; }
    hex = http.getString();
    http.end();
    if (hex.length() == 0) { err = "下载为空"; return false; }
    return true;
}

static void _setState(const char* s) { g_state = s; }

/* 升级过程日志缓冲(环形, 最后 40 条), 供升级页实时 log 框轮询显示 */
#define FUP_LOG_MAX 40
static String  g_log[FUP_LOG_MAX];
static uint8_t g_logN = 0;   /* 累计条数 */
static uint8_t g_logW = 0;   /* 写指针 */

static void _setMsg(const String& m) {
    g_msg = m;
    g_log[g_logW] = m;
    g_logW = (uint8_t)((g_logW + 1) % FUP_LOG_MAX);
    if (g_logN < FUP_LOG_MAX) g_logN++;
}

/* ---------------- 升级任务 ---------------- */
static void fupTask(void* param) {
    bool fromGitHub = ((uintptr_t)param) != 0;
    uint8_t addr = g_upAddr;
    bool force   = g_upForce;
    g_fBusy = true;
    g_prog = 0;
    g_pkgVerStr = "";
    g_boardVerStr = "";
    g_boardQBusy = false;
    g_boardQDone = false;
    bool ok = true;

    do {
        /* 1) hex 来源 */
        if (fromGitHub) {
            _setState("downloading");
            _setMsg("从 GitHub 下载固件…");
            String err;
            if (!_downloadHex(g_hexCache, err)) { _setMsg(err); ok = false; break; }
            g_prog = 12;
            if (g_hexCache.length() == 0) { _setMsg("未上传固件包"); ok = false; break; }

            /* 2) 解析 + DDFW 包版本（协议 §7.11：扫不到 = 旧格式包，拒绝进 ISP） */
            _setState("parsing");
            _setMsg("解析固件包…");
            int pr = intel_hex_parse(g_hexCache.c_str(), g_hexCache.length(), &g_img);
            if (pr != 0) { _setMsg("HEX 解析失败(格式/校验和/地址越界)"); ok = false; break; }
            if (intel_hex_read_fw_ver(&g_img, &g_pkgVer) != 0) {
                _setMsg("包内无 DDFW: 版本标识，拒绝升级(旧格式包)");
                ok = false; break;
            }
            char vb[16];
            snprintf(vb, sizeof(vb), "%04u.%04u", g_pkgVer.yymm, g_pkgVer.ddbn);
            g_pkgVerStr = vb;
        } else {
            /* 本地上传: fupUploadHex 已把 HEX 解析到 g_img/g_pkgVer, 不再复制/解析 g_hexCache。
             * 现场 40KB 的 g_hexCache 复制在堆紧张时可能失败, 会出现"上传校验通过但开始升级
             * 提示未上传固件"的假象; 直接使用 g_img 规避。 */
            if (!g_pkgReady || !g_img.valid) { _setMsg("未上传固件包"); ok = false; break; }
        }
        g_prog = 25;

        /* 3) M602 读板上版本 + 比对决策 */
        _setState("comparing");
        _setMsg("读取板上版本…");
        g_boardVerStr = "";
        if (_queryFw(addr, g_boardVerStr)) {
            HexFwVer_t bv;
            if (intel_hex_parse_ver_text((const uint8_t*)g_boardVerStr.c_str(),
                                         g_boardVerStr.length(), &bv) != 0) {
                _setMsg("板上版本格式非法(" + g_boardVerStr + ")");
                ok = false; break;
            }
            int cmp = intel_hex_ver_cmp(&g_pkgVer, &bv);
            /* 强制升级: 同版本/降级都放行; 未勾选时才拦截 */
            if (cmp == 0 && !force) {
                g_prog = 100; _setState("done");
                _setMsg("板上已是最新(" + g_boardVerStr + ")，无需升级；如确需重刷请勾选强制降级");
                ok = false; break;   // 不进 ISP（ok=false 仅避免走失败蜂鸣路径）
            }
            if (cmp < 0 && !force) {
                _setMsg("包版本(" + g_pkgVerStr + ")低于板上(" + g_boardVerStr + ")，需勾选强制降级");
                ok = false; break;
            }
        }
        /* 板上读不到(未配址 addr=99/离线): 按协议跳过比对，视为 包>板 处理 */
        g_prog = 35;

        /* 4) App 在跑就先发 M788；已在 BL 才跳过。
         * 绝不能先发 IAP HELLO：HELLO 是二进制 A5 5A，会污染 App 的文本行缓冲，
         * 导致后面的 M788 被拼成乱码 -> error unknown cmd。 */
        _setState("isp");
        bool targetInBoot = false;
        {
            int ti = g_mgr.tempIdxForChip(g_upChipid);
            if (ti >= 0 && g_mgr.tempAt(ti)->boot) targetInBoot = true;
        }
        if (targetInBoot) {
            _setMsg("Bootloader 已就绪，跳过 M788");
        } else {
            _setMsg("发送 M788 进入 ISP 引导…");
            String merr;
            if (!_m788(addr, merr)) { g_bus.endExclusive(); g_ispActive = false; _setMsg("M788 " + merr); ok = false; break; }
            delay(300);
        }

        g_bus.beginExclusive();
        g_ispActive = true;
        IapClient iap;
        bool fl = iap.begin(&g_bus);
        if (fl) fl = iap.hello(3000);

        /* 5) IAP Bootloader 升级 */
        _setMsg("IAP 升级中(请勿断电)…");
        if (fl) { _setMsg("IAP: HELLO 完成"); }
        if (fl) fl = iap.erase(g_img.size);
        if (fl) { _setMsg("IAP: ERASE 完成，开始写 DATA"); }
        const uint8_t* app = g_img.data + g_img.base;   /* HexImage 按绝对地址存 */
        if (fl) fl = iap.write(app, g_img.size,
                               [](int pct){ g_prog = 40 + pct * 45 / 100; });
        if (fl) { _setMsg("IAP: DATA 完成，校验 END"); }
        if (fl) fl = iap.end(g_img.size, IapClient::crc32(app, g_img.size));
        if (fl) { _setMsg("IAP: END 校验通过，REBOOT"); }
        if (fl) fl = iap.reboot();
        g_bus.endExclusive();
        g_ispActive = false;
        if (!fl) {
            /* 失败/断电: 总览不能继续显示旧在线状态，板上版本也立即失效 */
            g_mgr.invalidateChip(g_upChipid);
            g_boardVerStr = "";
            g_boardQBusy = false;
            g_boardQDone = false;
            _setMsg("IAP 升级失败: " + iap.lastError());
            ok = false; break;
        }
        g_prog = 90;

        /* 6) 复位后回校（协议 §7.11：FW 必须与包版本逐段相等） */
        _setState("verify");
        _setMsg("等待设备复位…");
        delay(1500);
        String fw2;
        if (_queryFw(addr, fw2)) {
            HexFwVer_t bv2;
            if (intel_hex_parse_ver_text((const uint8_t*)fw2.c_str(), fw2.length(), &bv2) == 0) {
                g_mgr.onBusMessage(g_qReply);   /* 回校结果同步到总览卡片缓存 */
                g_boardVerStr = fw2;            /* 升级页板上版本同步 */
            if (intel_hex_ver_cmp(&g_pkgVer, &bv2) != 0) {
                    _setMsg("回校版本不一致(期望 " + g_pkgVerStr + "，读到 " + fw2 + ")");
                    ok = false; break;
                }
            } else {
                _setMsg("回校版本解析失败(" + fw2 + ")");
                ok = false; break;
            }
        } else {
            _setMsg("回校 M602 无应答");
            ok = false; break;
        }

        g_prog = 100;
        _setState("done");
        _setMsg("升级成功(" + g_pkgVerStr + ")");
        buzzerBeep(BEEP_OTA_OK, 1);
        evlogAdd("OTA", "Feeder ok addr=%u %s", addr, g_pkgVerStr.c_str());
    } while (0);

    if (!ok && g_state != "done") {
        _setState("error");
        evlogAdd("OTA", "Feeder fail addr=%u: %s", addr, g_msg.c_str());
        buzzerBeep(BEEP_OTA_FAIL, 5);
    }
    g_fBusy = false;
    vTaskDelete(nullptr);
}

/* ---------------- 对外接口 ---------------- */

/* 板上版本查询(Web 路径, 异步): handler 只触发查询, 立即返回; 成功返回 true。
 * 结果由 status 轮询经 fupBoardQueryPoll 带回。
 * 不能在 handler 栈上同步等待: 应答处理在 main loop(handler 也在其中), 阻塞即饿死应答;
 * 也不能在回调里写 handler 栈引用(超时返回后应答晚到 -> 悬空 String -> LoadProhibited 崩溃)。 */
bool fupBoardQueryStart(uint8_t addr) {
    if (g_boardQBusy) return true;           /* 已在查询中, 视为已触发 */
    g_boardQDone = false;
    g_boardVerStr = "";                      /* ???????????? */
    if (!_queryFwStart(addr)) return false;  /* 总线忙(周期广播等), 稍后再试 */
    g_boardQBusy = true;
    return true;
}

/* status 轮询调用: 查询完成则收口; 返回 busy/done/fw */
void fupBoardQueryPoll(bool& busy, bool& done, String& fw) {
    if (g_boardQBusy && g_qDone) {
        g_boardQBusy = false;
        fw = _fwFromReply(g_qReply);
        g_boardQDone = fw.length() ? true : false;   /* 空应答不算成功 */
        if (g_boardQDone) g_boardVerStr = fw;
    }
    busy = g_boardQBusy;
    done = g_boardQDone;
    if (done && fw.length() == 0) fw = _fwFromReply(g_qReply);
    if (!g_fBusy && done && fw.length()) g_boardVerStr = fw;
}

int fupUploadHex(const char* hex, size_t len, String& verOut, String& errOut) {
    if (g_fBusy) { errOut = "升级进行中，请稍后再试"; return -3; }
    /* 直接解析原始静态缓冲，不再构造 60KB String 副本。 */
    int r = intel_hex_parse(hex, len, &g_img);
    LOGD("[UP] hex parse r=%d len=%u size=%u\n", r, (unsigned)len, (unsigned)g_img.size);
    if (r != 0) {
        errOut = "HEX 解析失败(code " + String(r) + ", len " + String((unsigned)len) + ")";
        return -1;
    }
    HexFwVer_t v;
    if (intel_hex_read_fw_ver(&g_img, &v) != 0) { errOut = "包内无 DDFW: 版本标识，拒绝(旧格式包)"; return -2; }
    char vb[16];
    snprintf(vb, sizeof(vb), "%04u.%04u", v.yymm, v.ddbn);
    verOut = vb;
    g_pkgVer = v;
    g_pkgVerStr = vb;
    g_pkgReady = true;
    return 0;
}

int fupUploadHex(const String& hex, String& verOut, String& errOut) {
    return fupUploadHex(hex.c_str(), hex.length(), verOut, errOut);
}

bool fupStart(uint8_t addr, uint32_t chipid, bool force, bool fromGitHub, String& errOut) {
    if (g_fBusy) { errOut = "升级任务进行中"; return false; }
    if (otaBusy()) { errOut = "已有 OTA 任务进行中"; return false; }
    if (!fromGitHub && !g_pkgReady) { errOut = "请先上传并校验固件包"; return false; }
    g_upAddr = addr;
    g_upChipid = chipid;
    g_upForce = force;
    g_state = "idle";
    g_logN = 0; g_logW = 0;                 /* 新任务清空日志缓冲 */
    _setMsg("准备升级 Addr " + String(addr) +
            (chipid ? (" ChipID " + String(chipid, HEX)) : String()) + " …");
    return xTaskCreatePinnedToCore(fupTask, "fup", 12288,
                                   (void*)(uintptr_t)(fromGitHub ? 1 : 0), 1, nullptr, 0) == pdPASS;
}

bool fupBusy() { return g_fBusy; }

String fupPkgVer() { return g_pkgReady ? g_pkgVerStr : ""; }

String fupStatusJson() {
    bool bq = false, bd = false;
    String bf;
    fupBoardQueryPoll(bq, bd, bf);
    String j = "{\"busy\":" + String(g_fBusy ? "true" : "false") +
           ",\"state\":\"" + g_state + "\"" +
           ",\"progress\":" + String(g_prog) +
           ",\"msg\":\"" + g_msg + "\"" +
           ",\"pkg_ver\":\"" + g_pkgVerStr + "\"" +
           ",\"board_ver\":\"" + g_boardVerStr + "\"" +
           ",\"board_busy\":" + String(bq ? "true" : "false") +
           ",\"board_done\":" + String(bd ? "true" : "false") +
           ",\"board_fw\":\"" + bf + "\"" +
           ",\"pkg_ready\":" + String(g_pkgReady ? "true" : "false") +
           ",\"log\":[";
    /* 环形缓冲按时间序输出(最多 FUP_LOG_MAX 条), JSON 转义 " \ */
    uint8_t start = (g_logN < FUP_LOG_MAX) ? 0 : g_logW;
    for (uint8_t i = 0; i < g_logN; i++) {
        const String& s = g_log[(uint8_t)((start + i) % FUP_LOG_MAX)];
        String esc;
        for (unsigned k = 0; k < s.length(); k++) {
            char c = s.charAt(k);
            if (c == '"' || c == '\\') esc += '\\';
            esc += c;
        }
        if (i) j += ",";
        j += "\"" + esc + "\"";
    }
    j += "]}";
    return j;
}

String fupCheckGitHub() {
    String j = otaCheckGitHub();
    bool ok = j.indexOf("\"ok\":true") >= 0;
    String devUrl, devVer;
    int p = j.indexOf("\"device_url\":\"");
    if (p >= 0) {
        p += 15;
        int e = j.indexOf('"', p);
        if (e > p) devUrl = j.substring(p, e);
    }
    p = j.indexOf("\"device_ver\":\"");
    if (p >= 0) {
        p += 14;
        int e = j.indexOf('"', p);
        if (e > p) devVer = j.substring(p, e);
    }
    return "{\"ok\":" + String(ok ? "true" : "false") +
           ",\"device_url\":\"" + devUrl +
           ",\"device_ver\":\"" + devVer + "\"" +
           ",\"pkg_ready\":" + String(g_pkgReady ? "true" : "false") +
           ",\"pkg_ver\":\"" + g_pkgVerStr + "\"" +
           ",\"board_ver\":\"" + g_boardVerStr + "\"}";
}






/* ---------------- UART0 手动 IAP 分步调试 ---------------- */
bool fupConsole(const String& line, String& out)
{
    String s = line;
    s.trim();
    if (!s.startsWith("iap")) return false;
    if (fupBusy()) { out = "[IAP] Web 升级进行中，拒绝手动操作\n"; return true; }

    String rest = s.substring(3);
    rest.trim();
    if (rest.length() == 0 || rest == "help") {
        out = "[IAP] iap m788 [addr] | iap hello | iap erase | iap reboot | iap end\n";
        return true;
    }

    if (rest.startsWith("m788")) {
        int addr = 99;
        int sp = rest.indexOf(' ');
        if (sp >= 0) addr = rest.substring(sp + 1).toInt();
        g_bus.beginExclusive();
        g_ispActive = true;
        String err;
        bool ok = _m788((uint8_t)addr, err);
        out = String("[IAP] m788 addr=") + addr + (ok ? " ok\n" : (" FAIL " + err + "\n").c_str());
        return true;
    }

    if (rest.startsWith("hello")) {
        g_bus.beginExclusive();
        g_ispActive = true;
        IapClient iap;
        iap.begin(&g_bus);
        bool ok = iap.hello(3000);
        out = String("[IAP] hello ") + (ok ? "ACK ok\n" : ("FAIL " + iap.lastError() + "\n").c_str());
        return true;
    }

    if (rest.startsWith("erase")) {
        g_bus.beginExclusive();
        g_ispActive = true;
        IapClient iap;
        iap.begin(&g_bus);
        bool ok = iap.erase(0, 15000);
        out = String("[IAP] erase ") + (ok ? "ACK ok\n" : ("FAIL " + iap.lastError() + "\n").c_str());
        return true;
    }

    if (rest.startsWith("reboot")) {
        IapClient iap;
        iap.begin(&g_bus);
        bool ok = iap.reboot(2000);
        out = String("[IAP] reboot ") + (ok ? "ACK ok\n" : ("FAIL " + iap.lastError() + "\n").c_str());
        return true;
    }

    if (rest.startsWith("end")) {
        g_bus.endExclusive();
        g_ispActive = false;
        out = "[IAP] exclusive off\n";
        return true;
    }

    out = "[IAP] unknown\n";
    return true;
}

















