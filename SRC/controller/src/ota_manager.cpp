#include "ota_manager.h"
#include "config.h"
#include "mcode.h"
#include "stc8_isp.h"
#include "buzzer.h"
#include "evlog.h"
#include "log_util.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <LittleFS.h>

/* ---------------- 进度状态 ----------------
 * 由 OTA 后台任务更新，web 端轮询 /api/ota/progress */
static volatile bool   g_otaBusy = false;
static volatile int    g_prog    = 0;      // 0..100
static volatile bool   g_done    = false;  // 成功结束(即将重启)
static volatile bool   g_error   = false;
static volatile bool   g_devOta  = false;  // true=设备(飞达)升级
static volatile bool   g_fsOta   = false;  // true=页面(FS)升级
static String   g_otaMsg;

static String g_latestTag, g_espUrl, g_devUrl, g_fsUrl, g_fsVer;
static bool   g_haveLatest = false;
static bool   g_localOta = false;
static size_t g_localTotal = 0;
static size_t g_localWritten = 0;

// ---- GitHub Release 元信息 ----
struct GitHubRelease {
    bool ok = false;
    String tag, esp32Url, esp32Ver, devUrl, devVer, fsUrl, fsVer;
};

static GitHubRelease _fetchLatest() {
    GitHubRelease rel;
    String api = "https://api.github.com/repos/" FW_GITHUB_OWNER "/" FW_GITHUB_REPO "/releases/latest";
    HTTPClient http;
    http.setTimeout(8000);
    http.useHTTP10(true);
    http.setUserAgent("ESP32-OTA/" FW_VERSION);
    if (!http.begin(api)) return rel;
    int code = http.GET();
    if (code != 200) { http.end(); return rel; }
    String body = http.getString();
    http.end();

    int p = body.indexOf("\"tag_name\":\"");
    if (p >= 0) rel.tag = body.substring(p + 12, body.indexOf('"', p + 12));

    p = 0;
    while ((p = body.indexOf("\"name\":\"", p)) >= 0) {
        int ns = p + 8;
        int ne = body.indexOf('"', ns);
        String name = body.substring(ns, ne);
        int u = body.indexOf("\"browser_download_url\":\"", ne);
        if (u >= 0) {
            int us = u + 24, ue = body.indexOf('"', us);
            String url = body.substring(us, ue);
            if (name.endsWith("_OTA.bin")) {
                rel.esp32Url = url;
                int ds = name.indexOf("DDFC_");
                int de = name.indexOf("_OTA.bin");
                if (ds >= 0 && de > ds) rel.esp32Ver = name.substring(ds + 5, de);
            }
            if (name.endsWith("_APP.hex")) {
                rel.devUrl = url;
                int ds = name.indexOf("DDF_");
                int de = name.indexOf("_APP.hex");
                if (ds >= 0 && de > ds) rel.devVer = name.substring(ds + 4, de);
            }
            /* 页面(FS)资产: DDFC_<fwver>_<fsver>_FS.bin */
            if (name.endsWith("_FS.bin")) {
                rel.fsUrl = url;
                int fe = name.indexOf("_FS.bin");
                int fs = name.lastIndexOf('_', fe - 1);
                if (fe > fs && fs >= 0) rel.fsVer = name.substring(fs + 1, fe);
            }
        }
        p = ne + 1;
    }
    rel.ok = rel.tag.length() > 0;
    return rel;
}

/* 设备当前页面版本: 读 LittleFS /fsver.txt (归一化: 兼容旧版带 DDfs 前缀) */
static String _currentFsVer() {
    if (!LittleFS.begin(false)) return "-";
    File f = LittleFS.open("/fsver.txt", "r");
    if (!f) return "-";
    String s = f.readString();
    f.close();
    s.trim();
    if (s.startsWith(FS_ASSET_PREFIX)) s = s.substring(sizeof(FS_ASSET_PREFIX) - 1);
    return s.length() ? s : "-";
}

/* 版本号数值比较: 纯数字 YYYYMMDDNN, 位数可能 2/3/4 位(序号可超 99)。
 * 不能用 String 直接比较: "2026101099" vs "20261010100" 会误判 99 > 100。
 * 先比长度(位数多者大), 再比字典序 —— 等价于数值比较且无 32 位溢出风险。 */
/* 包名是 YYMMDDNN(8位)，固件内 FW_VERSION 是 YYYYMMDDNN(10位)。
 * 比较前统一去掉年份前导 "20"，否则 8 位永远被判成比 10 位旧。 */
static String _normVer(const String& v) {
    if (v.length() == 10 && v.startsWith("20")) return v.substring(2);
    return v;
}

static bool _verNewer(const String& a0, const String& b0) {
    String a = _normVer(a0), b = _normVer(b0);
    if (a.length() != b.length()) return a.length() > b.length();
    return a > b;
}

String otaCheckGitHub() {
    GitHubRelease r = _fetchLatest();
    if (!r.ok)
        return "{\"ok\":false,\"msg\":\"GitHub 请求失败(限速或仓库不存在)\",\"cur_esp\":\"" FW_VERSION
               "\",\"cur_fs\":\"" + _currentFsVer() + "\"}";
    g_latestTag = r.tag; g_espUrl = r.esp32Url; g_devUrl = r.devUrl;
    g_fsUrl = r.fsUrl; g_fsVer = r.fsVer; g_haveLatest = true;
    String curFs = _currentFsVer();
    return "{\"ok\":true,\"latest\":\"" + r.tag +
           "\",\"cur_esp\":\"" FW_VERSION "\"" +
           ",\"esp32_asset\":\"" ESP32_ASSET "\",\"device_asset\":\"" DEVICE_ASSET
           "\",\"esp32_url\":\"" + r.esp32Url +
           "\",\"esp32_ver\":\"" + r.esp32Ver +
           "\",\"device_url\":\"" + r.devUrl +
           "\",\"device_ver\":\"" + r.devVer +
           "\",\"need_esp32\":" + (r.esp32Ver.length() && _verNewer(r.esp32Ver, FW_VERSION) ? "true" : "false") +
           ",\"fs_ver\":\"" + r.fsVer +
           "\",\"fs_url\":\"" + r.fsUrl +
           "\",\"cur_fs\":\"" + curFs +
           "\",\"need_fs\":" + ((r.fsVer.length() > 0 && r.fsVer != curFs) ? "true" : "false") + "}";
}

/* ---------------- ESP32 自身升级任务 ---------------- */
static void otaEspTask(void*) {
    g_otaBusy = true; g_devOta = false;
    g_prog = 0; g_done = false; g_error = false; g_otaMsg = "";
    bool ok = true;

    do {
        if (!g_haveLatest) {
            GitHubRelease r = _fetchLatest();
            if (!r.ok) { g_otaMsg = "GitHub 请求失败"; ok = false; break; }
            g_latestTag = r.tag; g_espUrl = r.esp32Url; g_devUrl = r.devUrl; g_haveLatest = true;
        }
        if (g_espUrl.length() == 0) { g_otaMsg = "发布包中无 " ESP32_ASSET; ok = false; break; }

        LOGD("[OTA] ESP32 download %s\n", g_espUrl.c_str());
        HTTPClient http;
        http.setTimeout(30000);
        http.useHTTP10(true);
        http.setUserAgent("ESP32-OTA/" FW_VERSION);
        if (!http.begin(g_espUrl)) { g_otaMsg = "连接失败"; ok = false; break; }
        int code = http.GET();
        if (code != 200) { g_otaMsg = "HTTP " + String(code); ok = false; http.end(); break; }

        int total = http.getSize();
        WiFiClient* stream = http.getStreamPtr();

        /* 防烧错预检: 先读首块校验 esp_image_header.magic(0xE9)+段数, 通过才写 Update */
        static uint8_t buf[OTA_STREAM_CHUNK];
        size_t n0 = stream->readBytes(buf, sizeof(buf));
        if (n0 < 8 || buf[0] != 0xE9 || buf[1] < 1 || buf[1] > 16) {
            g_otaMsg = "固件包校验失败(非 ESP32 镜像头)";
            ok = false; http.end(); break;
        }

        if (!Update.begin(total)) { g_otaMsg = Update.errorString(); ok = false; http.end(); break; }
        if (Update.write(buf, n0) != n0) { g_otaMsg = "写入失败"; ok = false; http.end(); break; }

        size_t done = n0;
        while (http.connected() && done < (size_t)total) {
            size_t n = stream->readBytes(buf, sizeof(buf));
            if (n == 0) { delay(2); continue; }
            if (Update.write(buf, n) != n) break;
            done += n;
            g_prog = (int)((uint64_t)done * 100 / total);
            if ((done >> 16) != ((done - n) >> 16)) LOGD("[OTA] %d/%d\n", done, total);
        }
        http.end();

        if (done != (size_t)total) { g_otaMsg = "下载中断"; ok = false; break; }
        if (!Update.end()) { g_otaMsg = Update.errorString(); ok = false; break; }

        g_prog = 100; g_done = true; g_otaMsg = "升级成功，重启中…";
        LOGD("[OTA] ESP32 upgrade done, reboot");
        buzzerBeep(BEEP_OTA_OK, 1);      // 成功上扬双音
        evlogAdd("OTA", "ESP ok %s -> %s", FW_VERSION, g_latestTag.c_str());
        delay(500);
        ESP.restart();
    } while (0);

    if (!ok) { g_error = true; evlogAdd("OTA", "ESP fail: %s", g_otaMsg.c_str()); }
    if (!ok) buzzerBeep(BEEP_OTA_FAIL, 5);   // 失败急促连鸣
    g_otaBusy = false;
    vTaskDelete(nullptr);
}

/* ---------------- 飞达设备升级任务 ---------------- */
static bool _hexToBin(const String& hex, uint8_t*& bin, size_t& binLen) {
    int p = 0;
    while (p < (int)hex.length()) {
        int e = hex.indexOf('\n', p);
        if (e < 0) e = hex.length();
        String ln = hex.substring(p, e); p = e + 1;
        ln.trim();
        if (ln.length() < 11 || ln[0] != ':') continue;
        String t = ln.substring(7, 9);
        if (!t.equalsIgnoreCase("00") && !t.equalsIgnoreCase("01")) continue;
        int reclen = (int)strtol(ln.substring(1, 3).c_str(), nullptr, 16);
        int off    = (int)strtol(ln.substring(3, 7).c_str(), nullptr, 16);
        int type   = (int)strtol(t.c_str(), nullptr, 16);
        if (type == 0) {
            if (off + reclen > (int)binLen) return false;
            for (int k = 0; k < reclen; k++) {
                String b = ln.substring(9 + k * 2, 11 + k * 2);
                bin[off + k] = (uint8_t)strtol(b.c_str(), nullptr, 16);
            }
        } else if (type == 1) break;
    }
    return true;
}

static void otaDevTask(void* param) {
    uint8_t addr = (uint8_t)(uintptr_t)param;
    extern FeederBus g_bus;
    g_otaBusy = true; g_devOta = true;
    g_prog = 0; g_done = false; g_error = false; g_otaMsg = "";
    bool ok = true;

    do {
        if (!g_haveLatest) {
            GitHubRelease r = _fetchLatest();
            if (!r.ok) { g_otaMsg = "GitHub 请求失败"; ok = false; break; }
            g_latestTag = r.tag; g_espUrl = r.esp32Url; g_devUrl = r.devUrl; g_haveLatest = true;
        }
        if (g_devUrl.length() == 0) { g_otaMsg = "发布包中无 " DEVICE_ASSET; ok = false; break; }

        LOGD("[OTA] device upgrade Addr %u\n", addr);
        g_otaMsg = "下载固件…";
        HTTPClient http;
        http.setTimeout(20000);
        http.useHTTP10(true);
        http.setUserAgent("ESP32-OTA/" FW_VERSION);
        if (!http.begin(g_devUrl)) { g_otaMsg = "连接失败"; ok = false; break; }
        int code = http.GET();
        if (code != 200) { g_otaMsg = "HTTP " + String(code); ok = false; http.end(); break; }
        String hex = http.getString();
        http.end();
        g_prog = 20;

        size_t binLen = 0x10000;
        uint8_t* bin = (uint8_t*)calloc(binLen, 1);
        if (!bin) { g_otaMsg = "内存不足"; ok = false; break; }
        if (!_hexToBin(hex, bin, binLen)) { free(bin); g_otaMsg = "HEX 解析失败"; ok = false; break; }
        g_prog = 30;

        // 通知目标飞达进入 ISP 引导(M788 MCU ISP 复位, 定向 N<addr>)
        g_otaMsg = "进入 ISP 引导…";
        g_bus.send(mcodeBuild(MCMD_ISP_RESET, addr, 0, false, true), 300, [](const String&){});
        unsigned long t0 = millis();
        while (g_bus.busy() && (long)(millis() - t0) < 600) { g_bus.loop(); delay(1); }
        delay(50);

        g_otaMsg = "ISP 烧写中…";
        bool fl = stc8Flash(&g_bus, bin, binLen, BUS_BAUD, ISP_BAUD_DOWNLOAD,
                            [](int pct){ g_prog = 30 + pct * 65 / 100; });
        free(bin);
        if (!fl) { g_otaMsg = "ISP 烧写失败"; ok = false; break; }

        g_prog = 100; g_done = true; g_otaMsg = "飞达升级成功";
        LOGD("[OTA] device flash OK");
        buzzerBeep(BEEP_OTA_OK, 1);
        evlogAdd("OTA", "DEV ok addr=%u", addr);
    } while (0);

    if (!ok) { g_error = true; evlogAdd("OTA", "DEV fail addr=%u: %s", addr, g_otaMsg.c_str()); }
    if (!ok) buzzerBeep(BEEP_OTA_FAIL, 5);
    g_otaBusy = false;
    vTaskDelete(nullptr);
}

/* ---------------- 页面(FS)升级任务 ----------------
 * 下载 DDfs<版本>.bin -> Update 写 FS 分区(U_SPIFFS) -> 重启
 * 防烧错: 双阶段下载——阶段1 流式校验(LittleFS superblock 标记 + 包内 fsver 明文
 * 必须与 release 资产名版本一致), 通过后阶段2 才实际写分区; 防止错误包覆盖页面。 */
static bool _lfsSuperblockOk(const uint8_t* hb, size_t n) {
    for (size_t i = 0; i + 8 <= n; i++)
        if (memcmp(hb + i, "littlefs", 8) == 0) return true;
    return false;
}

/* 流式校验 fsver 明文: 下载流中寻找 == expect 的 YYYYMMDD+8hex 标识。
 * 注意必须按期望值匹配(镜像内可能存在巧合命中的 16 字符数字串, 取首个候选会误拒)。 */
static bool _scanFsVerOk(WiFiClient* stream, int total, const String& expect) {
    String win;
    uint8_t buf[512];
    int done = 0;
    while (done < total) {
        int n = stream->readBytes(buf, sizeof(buf));
        if (n <= 0) { delay(2); continue; }
        done += n;
        win += String((const char*)buf, n);
        if (win.length() > 48) win = win.substring(win.length() - 48);
        for (int i = 0; i + 16 <= (int)win.length(); i++) {
            bool ok = true;
            for (int k = 0; k < 8 && ok; k++)
                ok = (win[i + k] >= '0' && win[i + k] <= '9');
            for (int k = 8; k < 16 && ok; k++) {
                char c = win[i + k];
                ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
            }
            if (ok && win.substring(i, i + 16) == expect) return true;
        }
    }
    return false;
}

static void otaFsTask(void*) {
    g_otaBusy = true; g_devOta = false; g_fsOta = true;
    g_prog = 0; g_done = false; g_error = false; g_otaMsg = "";
    bool ok = true;

    do {
        if (!g_haveLatest || g_fsUrl.length() == 0) {
            GitHubRelease r = _fetchLatest();
            if (!r.ok) { g_otaMsg = "GitHub 请求失败"; ok = false; break; }
            g_latestTag = r.tag; g_espUrl = r.esp32Url; g_devUrl = r.devUrl;
            g_fsUrl = r.fsUrl; g_fsVer = r.fsVer; g_haveLatest = true;
        }
        if (g_fsUrl.length() == 0) { g_otaMsg = "发布包中无 " FS_ASSET_PREFIX " 资产"; ok = false; break; }

        /* ---- 阶段1: 下载校验(不写盘) ---- */
        g_prog = 5; g_otaMsg = "校验页面包…";
        LOGD("[OTA] FS verify pass1\n");
        HTTPClient h1;
        h1.setTimeout(30000);
        h1.useHTTP10(true);
        h1.setUserAgent("ESP32-OTA/" FW_VERSION);
        if (!h1.begin(g_fsUrl)) { g_otaMsg = "连接失败"; ok = false; break; }
        int c1 = h1.GET();
        if (c1 != 200) { g_otaMsg = "HTTP " + String(c1); ok = false; h1.end(); break; }
        int total1 = h1.getSize();
        WiFiClient* s1 = h1.getStreamPtr();
        uint8_t hb[32];
        size_t n32 = s1->readBytes(hb, sizeof(hb));
        if (n32 < 32) { g_otaMsg = "页面包过小"; ok = false; h1.end(); break; }
        bool lfsOk = _lfsSuperblockOk(hb, n32);
        bool fsvOk = _scanFsVerOk(s1, total1 - (int)n32, g_fsVer);
        h1.end();
        if (!lfsOk) { g_otaMsg = "页面包校验失败(非 LittleFS 镜像)"; ok = false; break; }
        if (!fsvOk) {
            g_otaMsg = "页面包校验失败(fsver 与发布资产不符)";
            ok = false; break;
        }
        LOGD("[OTA] FS verify ok ver=%s\n", g_fsVer.c_str());
        g_prog = 40;

        /* ---- 阶段2: 重新下载并写 FS 分区 ---- */
        LOGD("[OTA] FS download %s\n", g_fsUrl.c_str());
        HTTPClient http;
        http.setTimeout(30000);
        http.useHTTP10(true);
        http.setUserAgent("ESP32-OTA/" FW_VERSION);
        if (!http.begin(g_fsUrl)) { g_otaMsg = "连接失败"; ok = false; break; }
        int code = http.GET();
        if (code != 200) { g_otaMsg = "HTTP " + String(code); ok = false; http.end(); break; }

        int total = http.getSize();
        if (!Update.begin(total, U_SPIFFS)) { g_otaMsg = Update.errorString(); ok = false; http.end(); break; }

        WiFiClient* stream = http.getStreamPtr();
        static uint8_t buf[OTA_STREAM_CHUNK];
        size_t done = 0;
        while (http.connected() && done < (size_t)total) {
            size_t n = stream->readBytes(buf, sizeof(buf));
            if (n == 0) { delay(2); continue; }
            if (Update.write(buf, n) != n) break;
            done += n;
            g_prog = 40 + (int)((uint64_t)done * 60 / total);
        }
        http.end();

        if (done != (size_t)total) { g_otaMsg = "下载中断"; ok = false; break; }
        if (!Update.end()) { g_otaMsg = Update.errorString(); ok = false; break; }

        g_prog = 100; g_done = true; g_otaMsg = "页面升级成功，重启中…";
        LOGD("[OTA] FS upgrade done, reboot");
        buzzerBeep(BEEP_OTA_OK, 1);
        evlogAdd("OTA", "FS ok %s", g_fsVer.c_str());
        delay(500);
        ESP.restart();
    } while (0);

    if (!ok) { g_error = true; evlogAdd("OTA", "FS fail: %s", g_otaMsg.c_str()); }
    if (!ok) buzzerBeep(BEEP_OTA_FAIL, 5);
    g_otaBusy = false; g_fsOta = false;
    vTaskDelete(nullptr);
}

/* ---------------- ESP32 本地 .bin 分片升级 ---------------- */
static void otaLocalRestartTask(void*) {
    delay(800);
    ESP.restart();
    vTaskDelete(nullptr);
}

bool otaLocalBegin(size_t total) {
    if (g_otaBusy || g_localOta || total < 8) return false;
    g_otaBusy = true;
    g_localOta = true;
    g_localTotal = total;
    g_localWritten = 0;
    g_prog = 0;
    g_done = false;
    g_error = false;
    g_fsOta = false;
    g_devOta = false;
    g_otaMsg = "本地固件写入中…";
    if (!Update.begin(total, U_FLASH)) {
        g_otaMsg = Update.errorString();
        g_error = true;
        g_otaBusy = false;
        g_localOta = false;
        return false;
    }
    return true;
}

bool otaLocalBeginFs(size_t total) {
    if (g_otaBusy || g_localOta || total < 8) return false;
    g_otaBusy = true;
    g_localOta = true;
    g_localTotal = total;
    g_localWritten = 0;
    g_prog = 0;
    g_done = false;
    g_error = false;
    g_fsOta = true;
    g_devOta = false;
    g_otaMsg = "本地页面写入中…";
    if (!Update.begin(total, U_SPIFFS)) {
        g_otaMsg = Update.errorString();
        g_error = true;
        g_otaBusy = false;
        g_localOta = false;
        g_fsOta = false;
        return false;
    }
    return true;
}

bool otaLocalWrite(const uint8_t* data, size_t len) {
    if (!g_localOta || !data || len == 0) return false;
    if (g_localWritten + len > g_localTotal) return false;
    if (Update.write((uint8_t*)data, len) != len) {
        g_otaMsg = Update.errorString();
        g_error = true;
        Update.abort();
        g_otaBusy = false;
        g_localOta = false;
        return false;
    }
    g_localWritten += len;
    g_prog = (int)((uint64_t)g_localWritten * 100 / g_localTotal);
    return true;
}

bool otaLocalEnd() {
    if (!g_localOta) return false;
    if (g_localWritten != g_localTotal || !Update.end(true)) {
        g_otaMsg = Update.errorString();
        g_error = true;
        Update.abort();
        g_otaBusy = false;
        g_localOta = false;
        return false;
    }
    g_prog = 100;
    g_done = true;
    g_otaMsg = g_fsOta ? "页面升级完成，重启中…" : "固件写入完成，重启中…";
    g_otaBusy = false;
    LOGD("[OTA] local %s done %u bytes, reboot\n", g_fsOta ? "FS" : "bin", (unsigned)g_localWritten);
    buzzerBeep(BEEP_OTA_OK, 1);
    evlogAdd("OTA", "%s local ok %u", g_fsOta ? "FS" : "ESP", (unsigned)g_localWritten);
    xTaskCreatePinnedToCore(otaLocalRestartTask, "otaLocalReboot", 2048, nullptr, 1, nullptr, 0);
    return true;
}

size_t otaLocalOffset() { return g_localWritten; }

void otaLocalAbort() {
    if (g_localOta) Update.abort();
    g_localOta = false;
    g_localTotal = 0;
    g_localWritten = 0;
    g_otaBusy = false;
    g_fsOta = false;
    g_error = true;
    g_otaMsg = "本地升级已中止";
}

/* ---------------- 对外接口 ---------------- */
bool otaStartEsp32() {
    if (g_otaBusy) return false;
    return xTaskCreatePinnedToCore(otaEspTask, "otaEsp", 8192, nullptr, 1, nullptr, 0) == pdPASS;
}

bool otaStartFS() {
    if (g_otaBusy) return false;
    return xTaskCreatePinnedToCore(otaFsTask, "otaFs", 8192, nullptr, 1, nullptr, 0) == pdPASS;
}

bool otaStartDevice(uint8_t addr) {
    if (g_otaBusy) return false;
    return xTaskCreatePinnedToCore(otaDevTask, "otaDev", 8192, (void*)(uintptr_t)addr, 1, nullptr, 0) == pdPASS;
}

bool otaBusy() { return g_otaBusy; }

String otaProgressJson() {
    const char* st = "idle";
    if (g_error) st = "error";
    else if (g_done) st = "done";
    else if (g_otaBusy) st = (g_devOta ? "flashing" : "downloading");
    return "{\"state\":\"" + String(st) +
           "\",\"kind\":\"" + String(g_fsOta ? "fs" : (g_devOta ? "device" : "esp32")) +
           "\",\"progress\":" + String(g_prog) +
           ",\"msg\":\"" + g_otaMsg + "\"}";
}




