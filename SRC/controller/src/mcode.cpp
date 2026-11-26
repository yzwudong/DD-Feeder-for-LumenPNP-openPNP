#include "mcode.h"
#include "buzzer.h"
#include "bus_power.h"
#include "feeder_manager.h"
#include "version.h"
#include "log_util.h"
#include "log_ring.h"
#include "factory_reset.h"
#include <WiFi.h>

extern FeederManager g_mgr;

/* ---------------- 组包 ---------------- */
String mcodeBuildMsg(const MCmdMsg& m) {
    String out = "M" + String(m.code);
    /* P 定点时不再带 N: 单台飞达统一走 ChipID，避免 N99/P 同时出现。
     * 只有未指定 P 时才用 N 定向；N255 仍是显式广播。 */
    if (m.hasP) {
        /* P only */
    } else if (m.hasN) {
        out += " N" + String(m.addr);
    } else if (m.explicitBroadcast) {
        out += " N255";
    }
    if (m.hasR) out += " R" + String(m.r);   /* M620 改址: N<当前> R<新地址> */
    if (m.hasA) out += " A" + String(m.a);
    if (m.hasB) out += " B" + String(m.b);
    if (m.hasC) out += " C" + String(m.c);
    if (m.hasF) out += " F" + String(m.f);
    if (m.hasU) out += " U" + String(m.u);
    if (m.hasV) out += " V" + String(m.v);
    if (m.hasW) out += " W" + String(m.w);
    if (m.hasX) out += " X" + String(m.x);
    if (m.hasT) out += " T" + String(m.t);   /* T N20 超时参数(不同电机转速超时不同) */
    if (m.hasS) out += " S" + String(m.s);
    if (m.hasO) out += " O" + String(m.o);
    if (m.hasP) {
        char buf[9];
        snprintf(buf, sizeof(buf), "%06lX", (unsigned long)m.chipid);
        out += " P" + String(buf);
    }
    out += '\n';
    return out;
}

String mcodeBuild(uint16_t code, int32_t addr, int32_t s, bool hasS, bool includeAddr) {
    MCmdMsg m;
    m.code = code;
    if (addr > 0) { m.addr = addr; m.hasN = true; }
    else if (includeAddr) m.explicitBroadcast = true;
    m.s = s; m.hasS = hasS;
    return mcodeBuildMsg(m);
}

String mcodeQuery(uint16_t code, int32_t addr) {
    MCmdMsg m;
    m.code = code;
    if (addr > 0) { m.addr = addr; m.hasN = true; }
    else m.explicitBroadcast = true;
    return mcodeBuildMsg(m);
}

/* ---------------- 工具 ---------------- */
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static bool isHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* ---------------- 解析 ---------------- */
bool mcodeParse(const String& line, MCmdMsg& out) {
    out = MCmdMsg();
    out.raw = line;
    if (line.length() < 2 || line[0] != 'M') return false;

    int p = 1;
    long code = 0; bool haveCode = false;
    while (p < (int)line.length() && isDigit(line[p])) { code = code * 10 + (line[p] - '0'); p++; haveCode = true; }
    if (!haveCode) return false;
    out.code = (uint16_t)code;

    while (p < (int)line.length()) {
        char c = line[p];
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') { p++; continue; }
        char up = (char)toupper(c);
        p++;
        if (up == 'N') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.addr = v; out.hasN = true;
            if (v == 255) out.explicitBroadcast = true;   // N255 = 广播
            // 注意: N0 是地址 0 的设备, 不是广播
        } else if (up == 'R') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.r = v; out.hasR = true;                    // M620 改址: R<新地址>
        } else if (up == 'P') {
            uint32_t v = 0; bool hv = false;
            while (p < (int)line.length() && isHexDigit(line[p])) { v = (v << 4) | (hexval(line[p])); p++; hv = true; }
            if (!hv) return false;
            out.chipid = v; out.hasP = true;
        } else if (up == 'A') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.a = v; out.hasA = true;
        } else if (up == 'B') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.b = v; out.hasB = true;
        } else if (up == 'C') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.c = v; out.hasC = true;
        } else if (up == 'F') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.f = v; out.hasF = true;
        } else if (up == 'U') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.u = v; out.hasU = true;
        } else if (up == 'V') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.v = v; out.hasV = true;
        } else if (up == 'W') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.w = v; out.hasW = true;
        } else if (up == 'X') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.x = v; out.hasX = true;
        } else if (up == 'T') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.t = v; out.hasT = true;   /* T N20 超时参数(M620 写入) */
        } else if (up == 'S') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.s = v; out.hasS = true;
        } else if (up == 'O') {
            long v = 0; bool hv = false;
            while (p < (int)line.length() && isDigit(line[p])) { v = v * 10 + (line[p] - '0'); p++; hv = true; }
            if (!hv) return false;
            out.o = v; out.hasO = true;
        } else {
            return false;   // 未知参数
        }
    }
    return true;
}

/* ---------------- 应答 ---------------- */

/* 前置声明(供 mcodeParseConfig 使用, 定义在其后) */
static uint32_t _hexAfter(const String& reply, const char* key, int maxDigits);
static long _keyVal(const String& reply, const char* key);

/* 解析 FW:<major>.<minor> 固件版本, 返回 major*100+minor (26.10 -> 2610); 未找到返回 -1 */
static long _fwVer(const String& reply) {
    int i = reply.indexOf("FW:");
    if (i < 0) return -1;
    i += 3;
    while (i < (int)reply.length() && !isdigit((unsigned char)reply[i])) i++;
    if (i >= (int)reply.length()) return -1;
    long major = 0;
    while (i < (int)reply.length() && isdigit((unsigned char)reply[i])) { major = major * 10 + (reply[i] - '0'); i++; }
    while (i < (int)reply.length() && (reply[i] == '.' || reply[i] == ' ')) i++;
    long minor = 0;
    while (i < (int)reply.length() && isdigit((unsigned char)reply[i])) { minor = minor * 10 + (reply[i] - '0'); i++; }
    /* 编码: major*10000+minor, 兼容 FW:26.10(旧) / FW:2610.2001(X56 yymm.ddbn) / FW:8.57 三格式;
     * 前端显示时 /10000 与 %10000 还原。 */
    return major * 10000 + minor;
}

int mcodeReplyType(const String& reply) {
    String r = reply;
    r.trim();
    if (r.length() == 0) return -1;
    if (r.startsWith("ok") || r.startsWith("OK")) return 0;
    if (r.startsWith("error") || r.startsWith("ERR")) return 1;
    return -1;
}

/* M602 应答解析。
 * 单点(N5):  "ok error:0 M620 N5 A90 B44 C15 F4 U240 V550 W240 X0 T800 VER:3 FW:26.10 CHIP:112233"
 * 全局(N255):"ok addr:5 error:0 VER:3 ChipID:112233 feederEnabled:1"  (错峰异步回复, 含 VER+使能, 无配置参数/T/FW) */
bool mcodeParseConfig(const String& reply, FeederCfg& cfg) {
    cfg = FeederCfg();

    /* 只接受正常的 ok/error 应答；总线噪声/半截帧里可能碰巧带 CHIP:，
     * 不能让它生成随机 ChipID 的临时飞达。 */
    String head = reply;
    head.trim();
    if (!head.startsWith("ok") && !head.startsWith("error")) return false;
    long e  = _keyVal(reply, "error:");
    long n  = _keyVal(reply, "N");
    long a  = _keyVal(reply, "A");
    long b  = _keyVal(reply, "B");
    long c  = _keyVal(reply, "C");
    long f  = _keyVal(reply, "F");
    long u  = _keyVal(reply, "U");
    long v  = _keyVal(reply, "V");
    long w  = _keyVal(reply, "W");
    long x  = _keyVal(reply, "X");
    long t  = _keyVal(reply, "T");               // T=N20 超时参数(单点应答含)
    long lim = _keyVal(reply, "L");              // L=限位状态 bit0:0°限位 bit1:90°限位(校准用)
    long ver = _keyVal(reply, "VER:");
    long fwv = _fwVer(reply);                    // FW=下位机固件版本(单点应答含)
    uint32_t cid = _hexAfter(reply, "CHIP:", 6);       // 单点: 大写 CHIP:
    if (cid == 0) cid = _hexAfter(reply, "ChipID:", 6); // 全局: 小写 ChipID:
    long en  = _keyVal(reply, "feederEnabled");         // 全局: feederEnabled:1/0 使能状态
    if (en < 0) en = _keyVal(reply, "EN:");              // 单点 M602 短键 EN:1/0

    /* 至少要能识别出 error 或 N 或任一配置项，才认为这是 M602 应答 */
    if (e < 0 && n < 0 && a < 0 && b < 0 && c < 0 && f < 0 &&
        u < 0 && v < 0 && w < 0 && x < 0 && t < 0 && ver < 0 && fwv < 0 && cid == 0) return false;

    cfg.err = (e >= 0) ? (int)e : -1;
    long addrK = _keyVal(reply, "addr:");
    if (addrK < 0) addrK = n;                  // 单点回复无 addr:, 用 N
    if (addrK >= 0) cfg.addr = (int)addrK;
    if (a >= 0) cfg.a = (int)a;
    if (b >= 0) cfg.b = (int)b;
    if (c >= 0) cfg.c = (int)c;
    if (f >= 0) cfg.f = (int)f;
    if (u >= 0) cfg.u = (int)u;
    if (v >= 0) cfg.v = (int)v;
    if (w >= 0) cfg.w = (int)w;
    if (x >= 0) cfg.x = (int)x;
    if (t >= 0) cfg.t = (int)t;
    if (lim >= 0) cfg.limit = (int)lim;
    if (ver >= 0) cfg.ver = (uint16_t)ver;
    if (fwv >= 0) cfg.fw = (int)fwv;
    if (cid) { cfg.chipid = cid; cfg.hasChip = true; }
    if (en >= 0) { cfg.enabled = (en != 0); cfg.hasEnable = true; }
    cfg.boot = (reply.indexOf("ok boot") >= 0);
    return true;
}

/* 提取 chipid: 新格式 "ChipID:xx xx xx" / 旧格式 "I12345678" */
static uint32_t _hexAfter(const String& reply, const char* key, int maxDigits) {
    int i = reply.indexOf(key);
    if (i < 0) return 0;
    i += strlen(key);
    uint32_t v = 0; int n = 0;
    while (i < (int)reply.length() && n < maxDigits) {
        char c = reply[i];
        if (c == ' ' || c == ':' || c == ',' || c == '\t') { i++; continue; }
        if (isHexDigit(c)) { v = (v << 4) | (uint32_t)hexval(c); n++; i++; }
        else break;
    }
    return v;
}

bool mcodeParseInfo(const String& reply, uint32_t& chipid, uint16_t& ver) {
    chipid = _hexAfter(reply, "ChipID:", 8);
    if (chipid == 0) chipid = _hexAfter(reply, "CHIPID:", 8);

    // 版本: "ver:xxx" / "ver xxx" / "Vxxx" 均可(宽松)
    uint16_t v = 0;
    int skip = 1, vi = reply.indexOf("ver:");
    if (vi >= 0) skip = 4;
    else {
        vi = reply.indexOf("ver ");
        if (vi >= 0) skip = 4;
        else vi = reply.indexOf('V');
    }
    if (vi >= 0) {
        for (int k = vi + skip; k < (int)reply.length(); k++) {
            char c = reply[k];
            if (isDigit(c)) v = v * 10 + (c - '0');
            else break;
        }
    }
    ver = v;
    return chipid != 0 || ver != 0;
}

/* 键值 "key:123" / 旧格式单字母 "E1 B0 S120 P8"
 * 必须按"前一个字符是分隔符 + key 后紧跟数字"匹配, 且跳过不匹配的出现继续找:
 *   - F 不能匹配 CHIP:F724C4(前导':' 不是分隔符), 也不能被 FW: 卡住;
 *   - V 不能被 VER: 卡住; 旧格式 E1/B0/S120/P8 仍可解析。
 * 之前只看首个 indexOf -> 偶发把 CHIP:F724C4 解析成 F=724。 */
static long _keyVal(const String& reply, const char* key) {
    const int klen = (int)strlen(key);
    const int n = (int)reply.length();
    int from = 0;
    while (from <= n) {
        int i = reply.indexOf(key, from);
        if (i < 0) return -1;
        char prev = (i > 0) ? reply[i - 1] : ' ';
        bool boundary = (i == 0) || prev == ' ' || prev == ',' || prev == '(' || prev == '\t';
        int p = i + klen;
        while (p < n && (reply[p] == ':' || reply[p] == ' ')) p++;
        if (boundary && p < n && isDigit(reply[p])) {
            long v = 0;
            while (p < n && isDigit(reply[p])) { v = v * 10 + (reply[p] - '0'); p++; }
            /* 数字后必须是分隔符/行尾: 防止 F724C4 这类十六进制被截成 724 */
            char after = (p < n) ? reply[p] : '\0';
            if (after == '\0' || after == ' ' || after == '\r' || after == '\n' ||
                after == '\t' || after == ',' || after == ')') return v;
        }
        from = i + 1;                       /* 这次出现不合法(如 FW:/CHIP:F...), 继续找下一个 */
    }
    return -1;
}

bool mcodeParseStatus(const String& reply, bool& enabled, bool& busy,
                      int& speed, int& pitch) {
    long e = _keyVal(reply, "enabled");
    long b = _keyVal(reply, "busy");
    long s = _keyVal(reply, "speed");
    long p = _keyVal(reply, "pitch");
    if (e < 0) {   // 旧格式 E1 B0 S120 P8
        e = _keyVal(reply, "E");
        b = _keyVal(reply, "B");
        s = _keyVal(reply, "S");
        p = _keyVal(reply, "P");
    }
    if (e < 0) return false;
    enabled = (e != 0);
    busy    = (b > 0);
    speed   = (s >= 0) ? (int)s : -1;
    pitch   = (p >= 0) ? (int)p : -1;
    return true;
}

/* ============================================================
 * ESP32 系统命令 (M9xx) —— 本地处理, 不透传 485
 * 入口: 外部主机(UART1) / 串口屏 发 M9xx 行
 * 回复格式与飞达一致: "ok ...\r\n" / "error ...\r\n"
 * 完整说明: docs/ESP32系统MCODE命令.md
 * ============================================================ */
bool mcodeHandleSystem(const String& line, String& reply) {
    if (line.length() < 2) return false;
    if (line[0] != 'M' && line[0] != 'm') return false;
    int code = 0, i = 1;
    while (i < (int)line.length() && isDigit(line[i])) { code = code * 10 + (line[i] - '0'); i++; }
    if (code < 900 || code > 908) return false;   /* 非系统命令区段 → 交给调用方透传 */

    long s = _keyVal(line, "S");
    long v = _keyVal(line, "V");

    switch (code) {
    case MCMD_SYS_STATUS: {   /* M900 系统状态 */
        bool ap = (WiFi.getMode() & WIFI_AP) != 0;
        reply = "ok sys ver:" + String(FW_VERSION) +
                " buzzer:" + String(buzzerIsOn() ? "1" : "0") +
                " vol:" + String(buzzerVol()) +
                " power:" + String(busPowerIsOn() ? "1" : "0") +
                " log:" + String(logIsEnabled() ? "1" : "0") +
                " online:" + String(g_mgr.onlineCount()) +
                " total:" + String(FEEDER_COUNT) +
                " ip:" + WiFi.localIP().toString() +
                (ap ? " ap:1" : "");
        return true;
    }
    case MCMD_SYS_BUZZER:     /* M901 S0|S1 蜂鸣器开关 */
        if (s < 0 || s > 1) { reply = "error S must be 0 or 1"; return true; }
        buzzerSetOn(s == 1);
        reply = "ok buzzer:" + String(buzzerIsOn() ? "1" : "0");
        return true;
    case MCMD_SYS_VOL:        /* M902 V0~100 音量 */
        if (v < 0 || v > 100) { reply = "error V must be 0~100"; return true; }
        buzzerSetVol((int)v);
        reply = "ok vol:" + String(buzzerVol());
        return true;
    case MCMD_SYS_POWER:      /* M903 S0|S1 485 总线电源 */
        if (s < 0 || s > 1) { reply = "error S must be 0 or 1"; return true; }
        busPowerSet(s == 1);
        reply = "ok power:" + String(busPowerIsOn() ? "1" : "0");
        return true;
    case MCMD_SYS_SCAN:       /* M904 触发全量扫描 */
        g_mgr.requestScan();
        reply = "ok scan";
        return true;
    case MCMD_SYS_CLEAR:      /* M905 清零推料计数 */
        g_mgr.clearFeed();
        g_mgr.saveFeedCount();
        reply = "ok clear";
        return true;
    case MCMD_SYS_LOG:        /* M906 运行时日志开关 S0|S1 */
        if (s < 0 || s > 1) { reply = "error S must be 0 or 1"; return true; }
        logSetEnabled(s == 1);
        logPush('D', String("M906 debug log ") + (logIsEnabled() ? "ON" : "OFF"));
        reply = String(logIsEnabled() ? "ok log:1" : "ok log:0");
        return true;
    case MCMD_SYS_FACTORY:    /* M907 恢复出厂设置(清 NVS + /apmode -> 重启) */
        reply = "ok factory reset";
        factoryResetRequest();   /* 回复先发出, 稍后执行清配置重启 */
        return true;
    case MCMD_SYS_HEARTBEAT:  /* M908 定向心跳周期: "M908 S30000" / "M908"(查询) */
        if (s >= 0) {
            if (s < 1000 || s > 3600000) { reply = "error S must be 1000~3600000"; return true; }
            g_mgr.setHeartbeatMs((uint32_t)s);
        }
        reply = "ok heartbeat:" + String(g_mgr.heartbeatMs());
        return true;
    }
    return false;
}



