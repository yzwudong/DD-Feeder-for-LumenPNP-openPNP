#include "screen.h"
#include "config.h"
#include "feeder_manager.h"
#include "mcode.h"
#include "bus_power.h"
#include "creds.h"
#include "calib.h"
#include "ina226.h"
#include "op_lock.h"
#include "log_util.h"
#include <WiFi.h>

#if SCREEN_ENABLED

/* ---------------- 后端选择 ---------------- */
#if SCREEN_BACKEND == SCREEN_BACKEND_UART0
/* UART0 后端: 直接复用 Serial。
 * 前提: 硬件把 RX0(GPIO3) 经单刀双掷切到屏幕 TX, TX0(GPIO1) 并联。
 * 注意: 此时 USB 调试口被屏幕占用, Serial 日志进屏幕, 烧录需切回 CH340。 */
#else
#include <SoftwareSerial.h>
static SoftwareSerial g_scr;   /* 引脚/反相在 begin() 里指定 */
#endif

extern FeederManager g_mgr;

static String        g_inBuf;       /* 屏幕输入行缓冲 */
static bool          g_pwdOk = false;   /* #PWD 通过后才允许 M799 */
static unsigned long g_lastPush = 0;

/* ---------------- 多语言 ----------------
 * 语言包: LittleFS /lang/<code>.txt, 格式 key=value
 * 见 docs/串口屏界面设计.md 第 16 节。只加载屏幕用到的键, 控制 RAM 占用。 */
#include <LittleFS.h>
#include <Preferences.h>

#define LANG_MAX_KEYS 64
#define LANG_KEY_LEN  20
#define LANG_VAL_LEN  56

struct LangSlot { char key[LANG_KEY_LEN]; char val[LANG_VAL_LEN]; };
static LangSlot g_lang[LANG_MAX_KEYS];
static uint8_t  g_langCount = 0;
static String   g_langCode  = "zh";

/* 屏幕用到的控件名(即语言包 key)。新增控件时同步添加。 */
static const char* const UI_IDS[] = {
    "bTabRun", "bTabPar", "bTabLog", "bTabSet", "bTabOta",
    "b_feed", "b_en", "b_dis", "b_wind", "b_scan", "b_refresh",
    "l_addr0", "b_read", "b_save", "b_reset",
    "l_addr1", "l_step", "l_n20", "l_angle",
    "b_clr", "b_logon", "b_logoff", "b_back", "l_loghint",
    "l_fw", "l_ip", "l_online", "l_dim", "l_slp",
    "b_issue", "b_factory", "b_tft", "l_fachint", "t_title6",
    "b_msk_yes", "b_msk_no", "b_msk_ok", "t_msk_dev",
    /* Page 5 升级 */
    "l_esp", "l_fs", "l_ddf", "l_tft", "l_font",
    "t_hdr1", "t_hdr2", "t_hdr3", "l_otahint",
    "b_up_esp", "b_up_fs", "b_up_ddf", "b_up_tft", "b_up_font",
    "b_pwr_on", "b_pwr_off",
};

static bool langWant(const String& k) {
    for (unsigned n = 0; n < sizeof(UI_IDS) / sizeof(UI_IDS[0]); n++)
        if (k == UI_IDS[n]) return true;
    return false;
}

static void screenLangLoad() {
    g_langCount = 0;
    Preferences p;
    if (p.begin("cfg", true)) {
        String c = p.getString("lang", "zh");
        if (c.length()) g_langCode = c;
        p.end();
    }
    File f = LittleFS.open(String("/lang/") + g_langCode + ".txt", "r");
    if (!f) return;
    while (f.available() && g_langCount < LANG_MAX_KEYS) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (!line.length() || line[0] == '#') continue;
        int i = line.indexOf('=');
        if (i <= 0) continue;
        String k = line.substring(0, i); k.trim();
        String v = line.substring(i + 1); v.trim();
        if (!k.length() || !v.length() || !langWant(k)) continue;
        strncpy(g_lang[g_langCount].key, k.c_str(), LANG_KEY_LEN - 1);
        strncpy(g_lang[g_langCount].val, v.c_str(), LANG_VAL_LEN - 1);
        g_lang[g_langCount].key[LANG_KEY_LEN - 1] = 0;
        g_lang[g_langCount].val[LANG_VAL_LEN - 1] = 0;
        g_langCount++;
    }
    f.close();
}

/* ---------------- 问题飞达（Page 6 诊断） ----------------
 * 列表来源: 99 未配置(temp 表) / 地址冲突(_conflictAddr) / 离线(addr<99 且 !present)
 * 改址走 M620 N<源> R<新> P<ChipID>, 由飞达固件按 ChipID 定向写入(见 ddfeeder gcode.c)。 */
#define ISSUE_MAX 32
struct IssueEnt { uint8_t kind; uint8_t addr; uint32_t chip; };   /* 0=99 1=冲突 2=离线 */
static IssueEnt g_issue[ISSUE_MAX];
static uint8_t  g_issueN   = 0;
static uint8_t  g_issueIdx = 0;
static uint8_t  g_curAddr  = 0;      /* 当前操作地址(参数页/校准页共用) */
static uint8_t  g_curPage  = 0;
static uint8_t  g_gridFilter = 0;  /* 0=全部 1=正常 2=未配置 3=冲突 */      /* 屏幕当前页, 指令只对当前页生效 */
static uint32_t g_lastCfgSeq = 0;    /* 上次回填参数时的 cfgSeq */
static uint32_t g_feedBase   = 0;    /* 开机时的推料总数, 用于算"开机后" */

/* 把某个地址的参数回填到 Page 1 的输入框(只在 Page 1 时才发) */
static void pushParams(uint8_t addr) {
    const FeederState* f = g_mgr.state(addr);
    if (!f) return;
    char b[8];
    snprintf(b, sizeof(b), "%d", addr);
    screenSetText("t_addr1", b);
    screenSetText("t_f", String(f->cfgF >= 0 ? f->cfgF : 4));
    screenSetText("t_t", String(f->cfgT >= 0 ? f->cfgT : 1000));
    screenSetText("t_v", String(f->cfgV >= 0 ? f->cfgV : 0));
    screenSetText("t_w", String(f->cfgW >= 0 ? f->cfgW : 0));
    screenSetText("t_a", String(f->cfgA >= 0 ? f->cfgA : 90));
    screenSetText("t_b", String(f->cfgB >= 0 ? f->cfgB : 44));
    screenSetText("t_c", String(f->cfgC >= 0 ? f->cfgC : 30));
    screenSetText("t_u", String(f->cfgU >= 0 ? f->cfgU : 0));
    g_lastCfgSeq = f->cfgSeq;
}

static void issueBuild() {
    g_issueN = 0;
    for (uint8_t i = 0; i < g_mgr.tempCount() && g_issueN < ISSUE_MAX; i++) {
        const FeederState* f = g_mgr.tempAt(i);
        if (f && f->addr == 99 && f->chipid) { g_issue[g_issueN++] = IssueEnt{0, 99, f->chipid}; }
    }
    for (uint16_t i = 0; i < g_mgr.conflictCount() && g_issueN < ISSUE_MAX; i++) {
        uint8_t a = (uint8_t)g_mgr.conflictAddr(i);
        const FeederState* f = g_mgr.state(a);
        g_issue[g_issueN++] = IssueEnt{1, a, f ? f->chipid : 0};
    }
    for (uint8_t s = 0; s < 64 && g_issueN < ISSUE_MAX; s++) {
        const FeederState* f = g_mgr.state(s);
        if (f && f->addr < 99 && !f->present) { g_issue[g_issueN++] = IssueEnt{2, (uint8_t)s, f->chipid}; }
    }
    if (g_issueIdx >= g_issueN) g_issueIdx = 0;
}

static void issuePushSel() {
    if (!g_issueN) {
        screenSetText("t_issue", "无问题飞达");
        screenSetText("t_sel", "--");
        screenSetText("t_chip", "------");
        return;
    }
    const IssueEnt& e = g_issue[g_issueIdx];
    const char* kind = (e.kind == 0) ? "未配置" : (e.kind == 1 ? "地址冲突" : "离线");
    screenSetText("t_issue", String("共 ") + g_issueN + " 项");
    screenSetText("t_sel", String(g_issueIdx + 1) + "/" + g_issueN + "  " + kind + "  N" + e.addr);
    char cb[12];
    snprintf(cb, sizeof(cb), "%06X", (unsigned)e.chip);
    screenSetText("t_chip", cb);
}

/* 把 ChipID(6 位 hex) + 新地址写下去; 目标地址被别的 ChipID 占用则拒绝 */
static void issueSetAddr(const String& chipHex, int newAddr) {
    uint32_t chip = strtoul(chipHex.c_str(), nullptr, 16);
    if (!chip || newAddr < 0 || newAddr > 63) {
        screenSetText("t_msg", "参数无效");
        screenPrint("ERR SETADDR");
        return;
    }
    const FeederState* t = g_mgr.state((uint8_t)newAddr);
    if (t && t->chipid && t->chipid != chip) {
        screenSetText("t_msg", String("地址 ") + newAddr + " 已被占用");
        screenPrint("ERR BUSY");
        return;
    }
    int src = g_mgr.addrForChip(chip);
    if (src < 0) src = 99;
    String cmd = "M620 N" + String(src) + " R" + String(newAddr) + " P" + chipHex;
    MCmdMsg m;
    if (!mcodeParse(cmd, m)) { screenSetText("t_msg", "命令解析失败"); screenPrint("ERR PARSE"); return; }
    g_mgr.commandProduction(m);
    g_curAddr = (uint8_t)newAddr;      /* 改址成功后, 后续「配置」直接指向新地址 */
    screenSetText("t_msg", String("已写入 N") + newAddr);
    screenPrint("OK SETADDR");
}


static const char* langGet(const char* key) {
    for (uint8_t i = 0; i < g_langCount; i++)
        if (strcmp(g_lang[i].key, key) == 0) return g_lang[i].val;
    return key;
}

/* 淘晶驰没有 Tab 控件: 每页各放 4 个按钮(bTab<页号><名称>), 激活项靠静态配色区分。
 * 指令只作用于"当前页"的控件, 所以文字按页下发(切页时屏发 #PAGE n)。 */
static const char* const P0_IDS[] = { "b_single" };
static const char* const P1_IDS[] = { "l_addr1", "l_step", "l_n20", "l_angle", "b_read", "b_save", "b_reset" };
static const char* const P2_IDS[] = { "b_clr", "b_logon", "b_logoff", "b_back", "l_loghint",
                                      "b_msk_yes", "b_msk_no", "b_msk_ok", "t_msk_dev" };
static const char* const P3_IDS[] = { "l_fw", "l_ip", "l_online", "l_dim", "l_slp",
                                      "b_issue", "b_pwr", "b_scan", "b_cal", "b_tft",
                                      "b_factory", "l_fachint", "t_msk5", "t_msk6",
                                      "b_myes", "b_mno" };
static const char* const P8_IDS[] = { "t_title8", "b_back8", "l_uaddr", "b_feed", "b_en",
                                      "b_dis", "b_wind", "l_uhint" };
static const char* const P5_IDS[] = { "l_esp", "l_fs", "l_ddf", "l_tft", "l_font",
                                      "t_hdr1", "t_hdr2", "t_hdr3", "l_otahint",
                                      "b_up_esp", "b_up_fs", "b_up_ddf", "b_up_tft", "b_up_font" };
static const char* const P7_IDS[] = { "t_title7", "b_back7", "l_caddr", "b_calq", "b_calmov",
                                      "b_cal0", "b_cal90", "b_calall", "b_calstop", "l_calhint" };
static const char* const P6_IDS[] = { "t_title6", "b_back6", "b_cfg6", "b_prev", "b_next", "b_blink", "b_set",
                                      "l_new", "l_hint6" };

#define ARR_N(a) (sizeof(a) / sizeof((a)[0]))

static void applyIds(const char* const* ids, size_t n) {
    for (size_t i = 0; i < n; i++) screenSetText(ids[i], String(langGet(ids[i])));
}

/* 每页的 5 个 Tab 按钮: bTab0Run / bTab0Par / ... / bTab5Ota */
static void applyTabs(uint8_t pg) {
    static const char* const SUF[5] = { "Run", "Par", "Log", "Set", "Ota" };
    static const char* const KEY[5] = { "bTabRun", "bTabPar", "bTabLog", "bTabSet", "bTabOta" };
    char id[16];
    for (int i = 0; i < 5; i++) {
        snprintf(id, sizeof(id), "bTab%d%s", (int)pg, SUF[i]);
        screenSetText(id, String(langGet(KEY[i])));
    }
}

/* 只刷当前页(命令不跨页生效) */
static void screenApplyPage(uint8_t pg) {
    g_curPage = pg;
    applyTabs(pg);
    switch (pg) {
    case 0: applyIds(P0_IDS, ARR_N(P0_IDS)); break;
    case 1: applyIds(P1_IDS, ARR_N(P1_IDS)); pushParams(g_curAddr); break;
    case 2: applyIds(P2_IDS, ARR_N(P2_IDS)); break;
    case 3: applyIds(P3_IDS, ARR_N(P3_IDS)); break;
    case 5: applyIds(P5_IDS, ARR_N(P5_IDS)); break;
    case 6: applyIds(P6_IDS, ARR_N(P6_IDS)); issueBuild(); issuePushSel(); break;
    case 7: applyIds(P7_IDS, ARR_N(P7_IDS)); break;
    case 8: applyIds(P8_IDS, ARR_N(P8_IDS)); break;
    default: break;
    }
}


/* ---------------- 输出 ---------------- */
void screenPrint(const char* fmt, ...) {
    char tmp[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
#if SCREEN_BACKEND == SCREEN_BACKEND_UART0
    Serial.print(tmp);
    Serial.print("\r\n");
#else
    g_scr.print(tmp);
    g_scr.print("\r\n");
#endif
}

void screenSetText(const char* ctrl, const String& v) {
    screenPrint("%s.txt=\"%s\"", ctrl, v.c_str());
}

void screenSetVal(const char* ctrl, long v) {
    screenPrint("%s.val=%ld", ctrl, v);
}

/* 调试日志行 -> 屏幕 LOG 控件(单行文本, 自动转义引号与截断) */
void screenLogLine(const char* line) {
    String esc;
    for (const char* p = line; *p && esc.length() < 100; p++) {
        char c = *p;
        if (c == '"' || c == '\\') esc += '\\';
        if (c != '\r' && c != '\n') esc += c;   /* 淘晶驰 txt 单行, 剥换行 */
    }
    screenPrint("%s.txt=\"%s\"", SCREEN_CTRL_LOGF, esc.c_str());
}

/* 汇总状态 -> 屏控件 (淘晶驰原生指令) */
void screenPushStatus() {
    uint32_t on = g_mgr.onlineCount();
    /* 首页第一行拆成三段: 在线 / 问题 / 生产状态(前两段可点) */
    uint16_t issues = (uint16_t)(g_mgr.uninitCount() + g_mgr.conflictCount());
    screenSetText("t_online", String("在线 ") + on + "/" + FEEDER_COUNT);
    screenSetText("t_prob", String("问题 ") + issues);
    screenSetText("t_state", g_mgr.productionLocked() ? "生产中" : "空闲");

    /* WiFi 状态: 区分 STA/AP 与 已连/未连 */
    String meta;
    {
        wifi_mode_t wm = WiFi.getMode();
        bool hasSta = (wm == WIFI_MODE_STA || wm == WIFI_MODE_APSTA);
        bool hasAp  = (wm == WIFI_MODE_AP  || wm == WIFI_MODE_APSTA);
        if (hasSta) {
            if (WiFi.status() == WL_CONNECTED) {
                meta = "STA 已连 " + WiFi.localIP().toString() +
                       " " + String(WiFi.RSSI()) + "dBm";
            } else {
                meta = "STA 未连(重连中)";
            }
        }
        if (hasAp) {
            if (meta.length()) meta += "   ";
            meta += "AP " + WiFi.softAPIP().toString();
        }
        if (!meta.length()) meta = "WiFi 关闭";
    }
    screenSetText(SCREEN_CTRL_META, meta);
    if (busPowerFault()) meta += " 电源故障!";   /* nFAULT: 过流/过热/反压 */
    if (g_mgr.uninitCount()) meta += " 未初始化:" + String(g_mgr.uninitCount());
    if (g_mgr.conflictCount()) meta += " 冲突:" + String(g_mgr.conflictCount());

    /* 校准进行中: 屏上同步显示进度, 同时刷新校准看门狗(周期性读取即"有人在看") */
    const CalibStatus& cs = calibStatus();
    if (cs.running) {
        meta += " 校准中:" + String(cs.mode == 1 ? "90°" : (cs.stage == 1 ? "90°" : "0°")) +
                "#" + String(cs.iter);
        screenSetText(SCREEN_CTRL_LOG, "校准进行中… 请勿关闭页面/切页");
    }
    screenSetText(SCREEN_CTRL_META, meta);
    /* 电源按钮文字随语言+状态变化(屏幕脚本不再写死中文) */
    /* 总线电源 + 实时电压/电流(新板 INA226; 老板无芯片则只显示开/关) */
    {
        float v = 0, a = 0;
        char vb[48];
        if (inaRead(&v, &a)) {
            snprintf(vb, sizeof(vb), "总线电源：%s  %.1fV  %.2fA",
                     busPowerIsOn() ? "开" : "关", v, a < 0 ? 0.0f : a);
        } else {
            snprintf(vb, sizeof(vb), "总线电源：%s", busPowerIsOn() ? "开" : "关");
        }
        screenSetText("t_volt", vb);
    }
    screenSetText("t_feed", String("总推料：") + g_mgr.feedTotal() +
                            "   开机后：" + (g_mgr.feedTotal() - g_feedBase));
    {
        unsigned long mins = millis() / 60000UL;
        char ub[48];
        snprintf(ub, sizeof(ub), "故障 %u 次   运行 %02lu:%02lu",
                 (unsigned)busPowerFaultCount(), mins / 60, mins % 60);
        screenSetText("t_stat2", ub);
    }
    screenSetText("t5", String("推料 ") + g_mgr.feedTotal() + "  v" + FW_VERSION);
    screenSetText("b_pwr", String(langGet(busPowerIsOn() ? "b_pwr_on" : "b_pwr_off")));
    /* 校准页进度 */
    if (calibStatus().running) {
        int it = (int)calibStatus().iter;
        screenSetVal("j0", it > 20 ? 100 : it * 5);
        screenSetText("t_cal", String("校准中 ") + it);
        if (g_curPage == 7) calibAck();   /* 在校准页=有人在看, 喂看门狗 */
    } else {
        screenSetText("t_cal", "空闲");
    }
}

/* ---------------- 选择飞达网格(Page 9-12) ----------------
 * 进入某组时推 16 个格子的底色: 在线/离线/问题/占用 */
static void pushGrid(uint8_t grp) {
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t a = (uint8_t)(grp * 16 + i);
        const FeederState* f = g_mgr.state(a);
        uint16_t bco;
        /* 配色与网页端一致: 紫=未配置(99) 红=冲突 灰=离线 绿=在线 */
        bool isConflict = (f && f->conflict);
        bool isUninit   = (f && f->addr == 99);
        bool isOffline  = (!f || !f->present);
        if (isConflict)      bco = 59944;
        else if (isUninit)   bco = 35582;
        else if (isOffline)  bco = 25521;
        else                 bco = 9771;
        /* 筛选: 不匹配的格子压暗 */
        if (g_gridFilter == 1 && (isConflict || isUninit || isOffline)) bco = 2200;
        if (g_gridFilter == 2 && !isUninit)  bco = 2200;
        if (g_gridFilter == 3 && !isConflict) bco = 2200;
        screenPrint("b_g%02u.bco=%u", a, bco);
    }
}

/* 问题飞达网格(Page 13-14): 压缩显示, 每页 16 槽 */
static void pushProblemGrid(uint8_t half) {
    issueBuild();
    for (uint8_t i = 0; i < 16; i++) {
        uint8_t idx = (uint8_t)(half * 16 + i);
        char id[8];
        snprintf(id, sizeof(id), "b_p%02u", i);
        if (idx < g_issueN) {
            const IssueEnt& e = g_issue[idx];
            char lab[8];
            if (e.kind == 0) snprintf(lab, sizeof(lab), "99");
            else             snprintf(lab, sizeof(lab), "%02u", (unsigned)e.addr);
            screenPrint("%s.txt=\"%s\"", id, lab);
            screenPrint("%s.bco=%u", id, e.kind == 1 ? 59944u : (e.kind == 0 ? 35582u : 25521u));
        } else {
            screenPrint("%s.txt=\"\"", id);
            screenPrint("%s.bco=%u", id, 2200u);
        }
    }
}

/* ---------------- 输入: 一行屏幕指令 ---------------- */
static void screenHandleLine(String line) {
    line.trim();
    if (!line.length()) return;

    /* # 命令 */
    if (line[0] == '#') {
        String up = line.substring(1);
        up.trim();
        up.toUpperCase();
        if      (up == "SCAN")   { g_mgr.requestScan();  screenPrint("OK SCAN"); }
        else if (up == "STATUS") { screenPushStatus();    screenPrint("OK STATUS"); }
        else if (up.startsWith("PAGE")) {
            /* 切页上报: 屏幕指令只对"当前页"生效, 所以切页后由 ESP32 补发该页文字.
             * 每个 Tab 按钮脚本: printf("#PAGE 1\n") 然后 page 1 */
            int n = line.substring(4).toInt();
            if (n >= 0 && n <= 8) {
                /* 离开校准页立即中止校准, 不等 20 秒看门狗 */
                if (g_curPage == 7 && n != 7 && calibActive()) calibStop();
                screenApplyPage((uint8_t)n);
                if (n >= 9 && n <= 12) pushGrid((uint8_t)(n - 9));
                if (n >= 13 && n <= 14) pushProblemGrid((uint8_t)(n - 13));
                screenPrint("OK PAGE");
            } else screenPrint("ERR PAGE");
        }
        else if (up.startsWith("ISSUE")) {
            /* #ISSUE ±1: 在问题飞达列表里切换 */
            int d = line.substring(5).toInt();
            issueBuild();
            if (g_issueN > 1) {
                int idx = (int)g_issueIdx + (d >= 0 ? 1 : -1);
                if (idx < 0) idx = g_issueN - 1;
                if (idx >= g_issueN) idx = 0;
                g_issueIdx = (uint8_t)idx;
            }
            issuePushSel();
            if (g_issueN) g_curAddr = g_issue[g_issueIdx].addr;
            screenPrint("OK ISSUE");
        }
        else if (up.startsWith("SETADDR")) {
            /* #SETADDR <ChipID-hex> <新地址>: 手工改址, 重复由 ESP32 校验 */
            String rest = line.substring(8);
            rest.trim();
            int sp = rest.indexOf(' ');
            if (sp <= 0) { screenPrint("ERR SETADDR"); }
            else {
                String chipHex = rest.substring(0, sp);
                chipHex.trim();
                chipHex.toUpperCase();
                issueSetAddr(chipHex, rest.substring(sp + 1).toInt());
                issueBuild();
            }
        }
        else if (up == "PING")   { screenPrint("PONG"); }
        else if (up.startsWith("CALIB")) {
            /* #CALIB STOP  或  #CALIB <地址> <0|1|2> */
            String rest = line.substring(6);
            rest.trim();
            rest.toUpperCase();
            if (rest == "STOP") {
                calibStop();
                screenPrint("OK CALIB STOP");
            } else {
                int sp2 = rest.indexOf(' ');
                int a = (sp2 > 0) ? rest.substring(0, sp2).toInt() : rest.toInt();
                int md = (sp2 > 0) ? rest.substring(sp2 + 1).toInt() : 2;
                if (calibActive()) { screenPrint("ERR BUSY 校准中"); return; }
                /* 校准必须单 P 定位: 地址要能解析出唯一 ChipID */
                const FeederState* f = (a >= 0 && a < 64) ? g_mgr.state((uint8_t)a) : nullptr;
                if (!f || !f->chipid) {
                    screenPrint("ERR CALIB NOCHIP");     /* 99 / 离线 / 未识别 */
                } else if (f->conflict) {
                    screenPrint("ERR CALIB CONFLICT");   /* 地址冲突, 先修地址 */
                } else {
                    calibStart(f->chipid, md);
                    screenPrint("OK CALIB");
                }
            }
        }
        else if (up.startsWith("GRIDF")) {
            int v = line.substring(5).toInt();
            if (v >= 0 && v <= 3) {
                g_gridFilter = (uint8_t)v;
                if (g_curPage >= 9 && g_curPage <= 12) pushGrid((uint8_t)(g_curPage - 9));
                screenPrint("OK GRIDF");
            } else screenPrint("ERR GRIDF");
        }
        else if (up.startsWith("PSEL")) {
            int i = line.substring(4).toInt();
            issueBuild();
            if (i >= 0 && i < g_issueN) {
                g_issueIdx = (uint8_t)i;
                g_curAddr = g_issue[i].addr;
                issuePushSel();
                screenPrint("OK PSEL");
            } else screenPrint("ERR PSEL");
        }
        else if (up.startsWith("PGRIDF")) {
            g_gridFilter = (uint8_t)line.substring(7).toInt();
            if (g_curPage >= 13 && g_curPage <= 14) pushProblemGrid((uint8_t)(g_curPage - 13));
            screenPrint("OK PGRIDF");
        }
        else if (up.startsWith("SEL")) {
            int a = line.substring(3).toInt();
            if (a >= 0 && a < 64) {
                g_curAddr = (uint8_t)a;
                char b[8];
                snprintf(b, sizeof(b), "%d", a);
                screenSetText("t_uaddr", b);
                const FeederState* f = g_mgr.state((uint8_t)a);
                snprintf(b, sizeof(b), "%s %s",
                         (f && f->present) ? "在线" : "离线",
                         (f && f->enabled) ? "EN开" : "EN关");
                screenSetText("t_ustat", b);
                screenPrint("OK SEL");
            } else screenPrint("ERR SEL");
        }
        else if (up == "PWRT") {
            /* 电源翻转: 屏幕不知道当前状态, 由 ESP32 决定 */
            busPowerSet(!busPowerIsOn());
            screenPushStatus();
            screenPrint("OK PWR");
        }
        else if (up.startsWith("PWR")) {
            /* 485 总线电源: #PWR 1 开 / #PWR 0 关(人在屏旁, 不强制解锁) */
            String v = line.substring(4);
            v.trim();
            if (v == "1" || v == "0") {
                busPowerSet(v == "1");
                screenPushStatus();          /* 立刻刷 b_pwr 文字, 不等 2 秒周期 */
                screenPrint("OK PWR");
            } else {
                screenPrint("ERR PWR");
            }
        }
        else if (up.startsWith("PWD")) {
            String pw = line.substring(4);
            pw.trim();
            g_pwdOk = credsAdminPwOk(pw);   /* 管理密码(NVS, 默认 WEB_PASSWORD) */
            screenPrint(g_pwdOk ? "OK PWD" : "ERR PWD");
        }
        else if (up.startsWith("LOG")) {
            /* 运行时日志开关: #LOG 1 开 / #LOG 0 关(同 M906) */
            String v = line.substring(4);
            v.trim();
            if (v == "1" || v == "0") {
                logSetEnabled(v == "1");
                screenPrint(logIsEnabled() ? "OK LOG ON" : "OK LOG OFF");
            } else {
                screenPrint("ERR LOG");
            }
        }
        else screenPrint("ERR CMD");
        return;
    }

    /* ESP32 系统命令 (M9xx): 本地处理, 结果回屏, 不透传 485 */
    {
        String sysReply;
        if (mcodeHandleSystem(line, sysReply)) {
            screenSetText(SCREEN_CTRL_LOG, "> " + line);
            screenPrint("%s", sysReply.c_str());
            return;
        }
    }

    /* M 代码行 -> 485
     * 屏操作不强制解锁(人在屏旁操作, 安全由人工保证); M799 恢复出厂需 #PWD 密码 */
    MCmdMsg m;
    if (!mcodeParse(line, m)) { screenPrint("ERR PARSE"); return; }
    if (m.code == MCMD_FACTORY_RESET && !g_pwdOk) { screenPrint("ERR PWD"); return; }

    /* 从 M 码里记下地址(M600 N5 ...), 供参数页同步 t_addr1 */
    {
        int ni = line.indexOf('N');
        if (ni >= 0) {
            int v = 0; bool any = false;
            for (int j = ni + 1; j < (int)line.length(); j++) {
                char ch = line[j];
                if (ch >= '0' && ch <= '9') { v = v * 10 + (ch - '0'); any = true; }
                else break;
            }
            if (any && v >= 0 && v <= 63) {
                g_curAddr = (uint8_t)v;
                if (g_curPage == 1) {
                    char b[8];
                    snprintf(b, sizeof(b), "%d", v);
                    screenSetText("t_addr1", b);
                }
            }
        }
    }

    /* 单台互斥: 网页/上位机正在操作同一台时拒绝, 防止双推料叠料 */
    if (g_curAddr < 64) {
        OpOwner cur = opLockOwner(g_curAddr);
        if (cur != OP_FREE && cur != OP_SCREEN) {
            screenPrint("ERR BUSY %s", opOwnerName(cur));
            return;
        }
        opLockTake(g_curAddr, OP_SCREEN);
    }

    /* 地址 99 的飞达不能直接改参数: 会命中所有未配置飞达, 必须先设地址 */
    if (line.startsWith("M620") && g_curAddr == 99 && line.indexOf('R') < 0) {
        screenPrint("ERR N99");
        return;
    }

    /* 校准互斥: 舵机校准进行中, 屏幕只能发查询(M602), 其余命令一律拒绝,
     * 防止插队驱动舵机导致校准判定错乱(校准为全局唯一状态机, 网页端可同时查看进度)。 */
    if (calibActive() && m.code != MCMD_QUERY) {
        screenPrint("ERR CALIB");
        return;
    }

    bool ok = g_mgr.command(m);
    screenSetText(SCREEN_CTRL_LOG, (ok ? "> " : "! ") + line);
    screenPrint(ok ? "OK" : "ERR BUSY");
}

/* ---------------- 初始化 / 轮询 ---------------- */
void screenBegin() {
    g_feedBase = g_mgr.feedTotal();
#if SCREEN_BACKEND == SCREEN_BACKEND_UART0
    Serial.begin(SCREEN_BAUD);        /* 复用 UART0(需硬件切 RX0, 放弃 USB 调试) */
#else
    g_scr.begin(SCREEN_BAUD, SWSERIAL_8N1, SCREEN_SOFT_RX, SCREEN_SOFT_TX, false);
#endif
    screenPrint("page 0");            /* 切到屏上 0 号主页面 */
    screenLangLoad();                 /* 加载语言包(优先 LittleFS, 缺失则用 key 原文) */
    screenApplyPage(0);               /* 刷当前页(Page 0)的文字 */
    screenPushStatus();
}

void screenLoop() {
#if SCREEN_BACKEND == SCREEN_BACKEND_UART0
    Stream& s = Serial;
#else
    Stream& s = g_scr;
#endif
    while (s.available()) {
        char c = (char)s.read();
        if (c == '\n' || g_inBuf.length() >= 128) {
            screenHandleLine(g_inBuf);
            g_inBuf = "";
        } else if (c != '\r') {
            g_inBuf += c;
        }
    }
    /* 状态跳变立即推送。覆盖所有"离散事件", 不必等 2 秒周期:
     *   ① 总线电源 开/关(短路保护/功能键/Web/M 码)
     *   ② 总线故障 出现/恢复
     *   ③ 扫描结束(在线数、未初始化、冲突数一次性刷新)
     *   ④ 扫描之外在线数变化(飞达掉线/恢复)
     *   ⑤ 生产锁 加锁/释放
     *   ⑥ 校准 开始/结束
     * 推料计数不在此列: 生产中推送会和 485 抢串口, 交给周期兜底。 */
    static bool     lastPwr = false, lastFault = false, lastProd = false;
    static bool     lastCalib = false, lastScan = false, inited = false;
    static uint16_t lastOnline = 0, lastUninit = 0, lastConflict = 0;

    bool     pwr      = busPowerIsOn();
    bool     flt      = busPowerFault();
    bool     scanning = g_mgr.isScanning();
    bool     prod     = g_mgr.productionLocked();
    bool     calib    = calibStatus().running;
    uint16_t online   = g_mgr.onlineCount();
    uint16_t uninit   = g_mgr.uninitCount();
    uint16_t conflict = g_mgr.conflictCount();

    bool dirty = false;
    if (!inited) {
        inited = true;
        lastPwr = pwr; lastFault = flt; lastScan = scanning;
        lastProd = prod; lastCalib = calib;
        lastOnline = online; lastUninit = uninit; lastConflict = conflict;
    } else {
        if (pwr != lastPwr || flt != lastFault) dirty = true;
        if (prod != lastProd || calib != lastCalib) dirty = true;
        if (lastScan && !scanning) dirty = true;          /* 扫描刚结束 */
        if (!scanning && online != lastOnline) dirty = true;
        if (!scanning && (uninit != lastUninit || conflict != lastConflict)) dirty = true;

        lastPwr = pwr; lastFault = flt; lastScan = scanning;
        lastProd = prod; lastCalib = calib;
        if (!scanning) { lastOnline = online; lastUninit = uninit; lastConflict = conflict; }
    }
    if (dirty) {
        g_lastPush = millis();
        screenPushStatus();
    }

    /* 参数页: M602 应答到达(cfgSeq 变化)后回填输入框 */
    if (g_curPage == 1) {
        const FeederState* f = g_mgr.state(g_curAddr);
        if (f && f->cfgSeq != g_lastCfgSeq) pushParams(g_curAddr);
    }
    /* 周期推送汇总(兜底) */
    if ((long)(millis() - g_lastPush) >= (long)SCREEN_PUSH_INTERVAL_MS) {
        g_lastPush = millis();
        screenPushStatus();
    }
}

#else /* SCREEN_ENABLED == 0 */
void screenBegin() {
    g_feedBase = g_mgr.feedTotal();}
void screenLoop() {}
void screenPrint(const char*, ...) {}
void screenSetText(const char*, const String&) {}
void screenSetVal(const char*, long) {}
void screenPushStatus() {}
#endif
