#include "wifi_utils.h"
#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "log_util.h"
#include <Preferences.h>
#include <LittleFS.h>

static Preferences g_prefs;
static bool g_apActive = false;

/* ---- 已保存网络列表 ----
 * NVS "wifi" 命名空间: cnt(u8) + ssidN/passN(N=0..7, 0=最新)
 * 旧版单条键(ssid/pass)自动迁移为列表第 0 条。 */
#define WIFI_MAX_SAVED 8

static int _savedCount() { return g_prefs.getUChar("cnt", 0); }
static String _savedSsid(int i) {
    String k = "ssid" + String(i);
    return g_prefs.isKey(k.c_str()) ? g_prefs.getString(k.c_str(), "") : "";
}
static String _savedPass(int i) {
    String k = "pass" + String(i);
    return g_prefs.isKey(k.c_str()) ? g_prefs.getString(k.c_str(), "") : "";
}

static void _savedWrite(int cnt, const String (&ssids)[WIFI_MAX_SAVED], const String (&passes)[WIFI_MAX_SAVED]) {
    g_prefs.putUChar("cnt", (uint8_t)cnt);
    for (int i = 0; i < cnt; i++) {
        g_prefs.putString(("ssid" + String(i)).c_str(), ssids[i]);
        g_prefs.putString(("pass" + String(i)).c_str(), passes[i]);
    }
    for (int i = cnt; i < WIFI_MAX_SAVED; i++) {   /* 清掉多余旧槽 */
        String k;
        k = "ssid" + String(i);
        if (g_prefs.isKey(k.c_str())) g_prefs.remove(k.c_str());
        k = "pass" + String(i);
        if (g_prefs.isKey(k.c_str())) g_prefs.remove(k.c_str());
    }
    if (g_prefs.isKey("ssid")) g_prefs.remove("ssid");   /* 旧单条键(迁移后) */
    if (g_prefs.isKey("pass")) g_prefs.remove("pass");
}

static void _savedMigrate() {
    if (!g_prefs.isKey("ssid")) return;
    String old = g_prefs.getString("ssid", "");
    if (old.length() > 0 && g_prefs.getUChar("cnt", 0) == 0) {
        String oldPass = g_prefs.getString("pass", "");
        String ss[WIFI_MAX_SAVED] = { old }, ps[WIFI_MAX_SAVED] = { oldPass };
        _savedWrite(1, ss, ps);
        LOGD("[WiFi] legacy config migrated to saved list\n");
    }
}

/* mDNS: 启动后可用 http://smt-feeder.local 访问(IP 变化无影响) */
static void _startMDNS() {
    if (MDNS.begin(WIFI_HOSTNAME)) {
        bool ok = MDNS.addService("http", "tcp", WEB_PORT);
        if (!ok) {                       /* 刚连上时注册偶发失败, 1s 后重试一次 */
            delay(1000);
            ok = MDNS.addService("http", "tcp", WEB_PORT);
        }
        if (ok) LOGD("[mDNS] http://%s.local (port %d)\n", WIFI_HOSTNAME, WEB_PORT);
        else    LOGD("[mDNS] service add failed\n");
    } else {
        LOGD("[mDNS] start failed\n");
    }
}

/* ---- AP 模式持久标志(存 LittleFS, 不受 NVS 影响) ----
 * 用户从 web 切换 AP 模式 -> 写 /apmode -> 重启后以 AP 启动(不连 STA)。
 * 在 AP 页面连接 WiFi 成功后自动删除标志 -> 回到 STA。 */
bool wifiApModeRequested() {
    return LittleFS.exists("/apmode");
}
void wifiSetApMode(bool on) {
    if (on) {
        File f = LittleFS.open("/apmode", "w");
        if (f) { f.print("1"); f.close(); }
    } else {
        LittleFS.remove("/apmode");
    }
}

static void _startAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    g_apActive = true;
    LOGD("[WiFi] AP %s IP=%s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
    _startMDNS();   /* AP 模式同样启用 mDNS */
}

/* 同 SSID 候选 AP(按信号排序后依次尝试, 解决"总是连上弱信号 AP") */
struct ApItem { uint8_t bssid[6]; int8_t rssi; uint8_t ch; };

static bool _connect(const String& ssid, const String& pass) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(WIFI_HOSTNAME);

    /* 先扫描: 收集同 SSID 的所有 AP, 按 RSSI 从强到弱依次尝试 */
    ApItem list[16];
    int cnt = 0;
    int n = WiFi.scanNetworks();
    LOGD("[WiFi] scan done: %d networks\n", n);
    for (int i = 0; i < n && cnt < 16; i++) {
        if (WiFi.SSID(i) == ssid) {
            memcpy(list[cnt].bssid, WiFi.BSSID(i), 6);
            list[cnt].rssi = (int8_t)WiFi.RSSI(i);
            list[cnt].ch   = (uint8_t)WiFi.channel(i);
            cnt++;
        }
    }
    WiFi.scanDelete();

    /* 按 RSSI 降序(选择排序) */
    for (int i = 0; i < cnt - 1; i++) {
        int best = i;
        for (int j = i + 1; j < cnt; j++)
            if (list[j].rssi > list[best].rssi) best = j;
        if (best != i) { ApItem t = list[i]; list[i] = list[best]; list[best] = t; }
    }

    LOGD("[WiFi] target '%s': %d APs: ", ssid.c_str(), cnt);
    for (int i = 0; i < cnt; i++) LOGD("#%d ch%d %ddBm  ", i+1, list[i].ch, list[i].rssi);
    LOGD("\n");

    unsigned long t0 = millis();
    if (cnt == 0) {
        /* 扫描不到(隐藏 SSID/瞬态): 退回普通连接, 由驱动自行选择 */
        LOGD("[WiFi] SSID not found (hidden?), plain connect\n");
        WiFi.begin(ssid.c_str(), pass.c_str());
        while (WiFi.status() != WL_CONNECTED) {
            if ((long)(millis() - t0) >= (long)WIFI_TIMEOUT_MS) { LOGD("\n"); return false; }
            delay(300); LOGD(".");
        }
    } else {
        for (int k = 0; k < cnt; k++) {
            LOGD("[WiFi] trying AP#%d (ch=%d rssi=%ddBm)\n", k+1, list[k].ch, list[k].rssi);
            WiFi.begin(ssid.c_str(), pass.c_str(), list[k].ch, list[k].bssid);
            t0 = millis();
            while (WiFi.status() != WL_CONNECTED) {
                if ((long)(millis() - t0) >= (long)WIFI_AP_TRY_MS) {
                    LOGD("[WiFi] AP#%d timeout, next\n", k+1);
                    break;
                }
                delay(300); LOGD(".");
            }
            if (WiFi.status() == WL_CONNECTED) break;
        }
        LOGD("\n");
    }

    if (WiFi.status() != WL_CONNECTED) return false;
    LOGD("[WiFi] connected -> IP=%s RSSI=%ddBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    _startMDNS();   /* STA 连接成功后启用 mDNS */
    /* 连网后启用 SNTP: 事件日志/页面可用实时时间; 无网时自动回退运行时长 */
    configTime(8 * 3600, 0, "ntp.aliyun.com", "cn.pool.ntp.org");
    return true;
}

void wifiInit() {
    // AP 模式标志存在 -> 直接以热点启动(用户从 web 切换)
    if (wifiApModeRequested()) {
        LOGD("[WiFi] AP flag exists, start AP");
        _startAP();
        return;
    }

    /* 已保存列表: 从最新(0)开始依次尝试, 先连上的即用 */
    g_prefs.begin("wifi", false);
    _savedMigrate();
    String ss[WIFI_MAX_SAVED], ps[WIFI_MAX_SAVED];
    int cnt = _savedCount();
    for (int i = 0; i < cnt; i++) { ss[i] = _savedSsid(i); ps[i] = _savedPass(i); }
    g_prefs.end();

    bool ok = false;
    for (int i = 0; i < cnt && !ok; i++) {
        if (ss[i].length() == 0) continue;
        LOGD("[WiFi] saved[%d] '%s'\n", i + 1, ss[i].c_str());
        ok = _connect(ss[i], ps[i]);
    }
    if (!ok && cnt == 0) {
        // 出厂默认网络(不写入列表)
        ok = _connect(WIFI_SSID, WIFI_PASS);
    }
    if (!ok) {
#if AP_FALLBACK
        _startAP();
#else
        LOGD("[WiFi] no network (serial only)");
#endif
    }
}

bool wifiSaveAndConnect(const String& ssid, const String& pass) {
    if (ssid.length() == 0) return false;

    g_prefs.begin("wifi", false);
    _savedMigrate();
    String ss[WIFI_MAX_SAVED], ps[WIFI_MAX_SAVED];
    int cnt = _savedCount();
    for (int i = 0; i < cnt; i++) { ss[i] = _savedSsid(i); ps[i] = _savedPass(i); }
    int hit = -1;
    for (int i = 0; i < cnt; i++) if (ss[i] == ssid) { hit = i; break; }
    if (hit >= 0) {                       /* 去重: 已有则删旧位, 再插到最前 */
        for (int k = hit; k < cnt - 1; k++) { ss[k] = ss[k+1]; ps[k] = ps[k+1]; }
        cnt--;
    }
    if (cnt >= WIFI_MAX_SAVED) cnt = WIFI_MAX_SAVED - 1;   /* 溢出丢最旧 */
    for (int k = cnt - 1; k >= 0; k--) { ss[k+1] = ss[k]; ps[k+1] = ps[k]; }
    ss[0] = ssid; ps[0] = pass; cnt++;
    _savedWrite(cnt, ss, ps);
    g_prefs.end();
    LOGD("[WiFi] saved '%s' (%d saved)\n", ssid.c_str(), cnt);

    bool ok = _connect(ssid, pass);
    if (ok) wifiSetApMode(false);       // 连上 STA 后清除 AP 标志, 回到 STA 模式
    if (!ok) {
        WiFi.mode(WIFI_OFF);
#if AP_FALLBACK
        _startAP();
#else
        LOGD("[WiFi] connect failed");
#endif
    }
    return ok;
}

/* 从已保存列表删除一个 SSID(连密码一起)。返回是否找到并删除。 */
bool wifiDelete(const String& ssid) {
    bool found = false;
    g_prefs.begin("wifi", false);
    String ss[WIFI_MAX_SAVED], ps[WIFI_MAX_SAVED];
    int cnt = _savedCount();
    for (int i = 0; i < cnt; i++) { ss[i] = _savedSsid(i); ps[i] = _savedPass(i); }
    int hit = -1;
    for (int i = 0; i < cnt; i++) if (ss[i] == ssid) { hit = i; break; }
    if (hit >= 0) {
        for (int k = hit; k < cnt - 1; k++) { ss[k] = ss[k+1]; ps[k] = ps[k+1]; }
        cnt--;
        found = true;
    }
    if (found) _savedWrite(cnt, ss, ps);
    g_prefs.end();
    LOGD("[WiFi] delete '%s' -> %s\n", ssid.c_str(), found ? "ok" : "not found");
    return found;
}

String wifiStatusJson() {
    String s = "{\"sta\":{";
    if (WiFi.status() == WL_CONNECTED) {
        s += "\"connected\":true,\"ssid\":\"" + String(WiFi.SSID()) +
             "\",\"ip\":\"" + WiFi.localIP().toString() +
             "\",\"rssi\":" + String(WiFi.RSSI());
    } else {
        s += "\"connected\":false";
    }
    s += "},\"ap\":{\"on\":" + String(g_apActive ? "true" : "false") +
         ",\"ssid\":\"" AP_SSID "\",\"ip\":\"" + (g_apActive ? WiFi.softAPIP().toString() : String("")) + "\"}";

    g_prefs.begin("wifi", true);
    String saved = "[";
    int cnt = _savedCount();
    bool first = true;
    for (int i = 0; i < cnt; i++) {
        String s = _savedSsid(i);
        if (s.length() == 0) continue;
        String esc; for (unsigned k = 0; k < s.length(); k++) {
            char c = s[k];
            if (c == '"' || c == '\\') { esc += '\\'; esc += c; }
            else esc += c;
        }
        if (!first) saved += ',';
        saved += "{\"ssid\":\"" + esc + "\"}";
        first = false;
    }
    saved += "]";
    g_prefs.end();
    s += ",\"saved\":" + saved + "}";
    return s;
}

String wifiScanJson() {
    // AP 模式下先启用 STA 才能扫描
    if (!(WiFi.getMode() & WIFI_STA)) {
        WiFi.enableSTA(true);
        delay(100);
    }
    int n = WiFi.scanNetworks();
    String s = "{\"networks\":[";
    for (int i = 0; i < n; i++) {
        if (i) s += ',';
        String ssid = WiFi.SSID(i);
        String esc; for (unsigned k = 0; k < ssid.length(); k++) {
            char c = ssid[k];
            if (c == '"' || c == '\\') { esc += '\\'; esc += c; }
            else esc += c;
        }
        s += "{\"ssid\":\"" + esc + "\",\"rssi\":" + String(WiFi.RSSI(i)) +
             ",\"secure\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
    }
    s += "]}";
    WiFi.scanDelete();
    return s;
}
