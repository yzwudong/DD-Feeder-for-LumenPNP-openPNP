#include "web_server.h"
#include "op_lock.h"
#include "config.h"
#include "mcode.h"
#include "feeder_bus.h"
#include "ota_manager.h"
#include "wifi_utils.h"
#include "log_ring.h"
#include "bus_power.h"
#include "buzzer.h"
#include "evlog.h"
#include "calib.h"
#include "log_util.h"
#include "creds.h"
#include "factory_reset.h"
#include "feeder_upgrade.h"
#include "feeder_manager.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <Preferences.h>

extern FeederBus g_bus;
extern FeederManager g_mgr;   /* 终端 /api/term 直发原始 M-code 用(main.cpp 定义) */

/* ---------------- 兜底提示页（文件系统未上传时） ---------------- */
static const char FALLBACK_HTML[] PROGMEM = R"RAWHTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SMT Feeder</title>
<style>body{background:#0f172a;color:#e2e8f0;font:14px/1.6 system-ui,sans-serif;display:flex;min-height:100vh;align-items:center;justify-content:center;margin:0}
.box{background:#1e293b;border:1px solid #334155;border-radius:12px;padding:28px 32px;max-width:560px}
h1{font-size:18px;color:#38bdf8;margin:0 0 10px}
code{background:#0f172a;border:1px solid #334155;border-radius:6px;padding:2px 8px;color:#22c55e}
ol{margin:10px 0 0;padding-left:20px}</style></head>
<body><div class="box">
<h1>Web 页面尚未上传</h1>
<p>固件运行正常，但文件系统中没有页面文件。请上传文件系统：</p>
<ol>
<li>VSCode：<code>PlatformIO: Upload Filesystem Image</code></li>
<li>或命令行：<code>pio run -t uploadfs</code></li>
</ol>
<p style="color:#94a3b8;margin-top:14px">上传后刷新本页即可。</p>
</div></body>
</html>
)RAWHTML";

/* ---------------- 辅助 ---------------- */
/* JSON 字符串转义(双引号/反斜杠/控制符) */
static String _escJson(const String& s) {
    String o;
    for (unsigned k = 0; k < s.length(); k++) {
        char c = s[k];
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if ((uint8_t)c < 0x20) { /* 其它控制符跳过 */ }
        else o += c;
    }
    return o;
}

static String _genToken() {    String t;
    for (int i = 0; i < 24; i++) t += "0123456789abcdef"[(esp_random() >> 4) & 0xF];
    return t;
}

/* 分片上传缓冲(飞达 hex, 头部 X-Chunk 逐片追加)。
 * 现场 POST body 会丢, 大 multipart 更容易空文件; 改用 1.2KB/片的请求头传输,
 * 最后一片在服务端拼成完整 hex 再走 fupUploadHex()。 */
static char*  s_upHex = nullptr;   /* 上传期间临时 60KB，解析后释放 */
static size_t s_upLen = 0;

static int jsonInt(const String& b, const char* key, int def) {
    String k = String("\"") + key + "\":";
    int p = b.indexOf(k);
    if (p < 0) return def;
    p += k.length();
    while (p < (int)b.length() && (b[p] == ' ' || b[p] == '\t')) p++;
    bool neg = false; if (p < (int)b.length() && b[p] == '-') { neg = true; p++; }
    int v = 0; bool any = false;
    while (p < (int)b.length() && isDigit(b[p])) { v = v * 10 + (b[p] - '0'); p++; any = true; }
    return any ? (neg ? -v : v) : def;
}

static const char* _mime(const String& path) {
    if (path.endsWith(".css")) return "text/css; charset=utf-8";
    if (path.endsWith(".js"))  return "text/javascript; charset=utf-8";
    if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html; charset=utf-8";
    return "application/octet-stream";
}

/* ---------------- 鉴权 ---------------- */
void WebServerApp::_loadAuthSetting() {
    Preferences prefs;
    prefs.begin("auth", true);
    _authEnabled = prefs.getBool("enabled", WEB_AUTH_ENABLED);
    prefs.end();
    LOGD("[Auth] login check: %s\n", _authEnabled ? "on" : "off");
}

bool WebServerApp::_authOk() {
    if (!_authEnabled) return true;           // 登录开关关闭 -> 直接放行
    if (_authToken.length() == 0) return false;
    /* 20 分钟会话到期: 清空会话 */
    if (_authExpiry > 0 && (long)(millis() - _authExpiry) >= 0) {
        _authToken = ""; _authRole = ""; _authExpiry = 0;
        return false;
    }
    String t = _server.header("X-Auth-Token");
    if (t.length() == 0) t = _server.arg("token");          /* URL ?token= (页面间整页导航用, cookie 失效兜底) */
    if (t.length() == 0) {
        String ck = _server.header("Cookie");
        int p = ck.indexOf("auth=");
        if (p >= 0) {
            t = ck.substring(p + 5);
            int q = t.indexOf(';');
            if (q >= 0) t = t.substring(0, q);
            t.trim();
        }
    }
    return t.length() > 0 && t == _authToken;
}

bool WebServerApp::_needAuth() {
    if (_authOk()) return true;
    /* 调试直打(不经 LOGD): 哪个 API 401 + token 来源(URL/Header/Cookie) + 服务器会话, 一锤定音 */
    String h = _server.header("X-Auth-Token");
    String u = _server.arg("token");
    String ck = _server.header("Cookie");
    String hdrs = ""; int hn = _server.headers();
    for (int i = 0; i < hn; i++) { if (i) hdrs += ","; hdrs += _server.headerName(i); }
    Serial.printf("[AUTH] api401 %s url=%d:%.4s hdr=%d:%.4s ck=%s sess=%d:%.4s hdrs(%d)=%s\n",
                  _server.uri().c_str(), (int)u.length(), u.c_str(),
                  (int)h.length(), h.c_str(), ck.length() > 0 ? "y" : "n",
                  (int)_authToken.length(), _authToken.c_str(), hn, hdrs.c_str());
    _sendJson("{\"ok\":false,\"msg\":\"unauthorized\"}", 401);
    return false;
}

/* 当前会话是否具备操作权限:
 *   auth 关闭(免登录模式) = 恒为 admin(与旧版行为一致)
 *   auth 开启           = 必须为 admin 会话 */
bool WebServerApp::_isAdmin() {
    if (!_authEnabled) return true;
    if (!_authOk()) return false;
    return _authRole == "admin";
}

/* Web 写操作解锁: 非 admin 会话 / 未解锁或超时 -> 403。
 * 通过后续期 _unlockAt: 真正实现"5 分钟无操作自动重锁"(文档语义),
 * 之前从解锁时刻起算, 长流程中途会过期(审查 P1#5)。 */
bool WebServerApp::_needUnlock() {
    if (!_isAdmin()) {
        _sendJson("{\"ok\":false,\"msg\":\"admin required\",\"unlocked\":false}", 403);
        return false;
    }
    if (_unlocked && (long)(millis() - _unlockAt) < (long)UNLOCK_TIMEOUT_MS) {
        _unlockAt = millis();                 // 写操作续期
        return true;
    }
    _unlocked = false;
    _sendJson("{\"ok\":false,\"msg\":\"locked\",\"unlocked\":false}", 403);
    return false;
}

void WebServerApp::_sendJson(const String& s, int code) {
    /* 调试日志已屏蔽: 页面轮询 /api/status、/api/unlock/status 会刷屏,
     * 现场看 485/ISP 日志时干扰太大。需要时临时打开这一行。 */
    // LOGD("[Web] API -> %d, %u bytes (%s)\n", code, (unsigned)s.length(), _server.uri().c_str());
    /* 保持默认 keep-alive。注意: WebServer 单线程, 服务完一个请求会在该连接上
       最多空等 HTTP_MAX_DATA_WAIT(5s) 才 accept 下一个 —— 前端必须串行请求
       (app.js 全局队列已保证), 调连接数/keep-alive 均不治本(审查报告 4.2)。 */
    _server.send(code, "application/json", s);
}

void WebServerApp::_servePage(const char* path) {
    // LOGD("[Web] PAGE: %s\n", path);   // 调试日志已屏蔽
    /* 登录页豁免鉴权: 否则未登录访问 /login.html 会被 302 回自己 -> 无限重定向, 页面永远打不开 */
    if (_authEnabled && !_authOk() && strcmp(path, "/login.html") != 0) {
        /* 调试直打(不经 LOGD 门控): 谁被拦/为什么拦.  t4=URL token 前4位, s4=会话 token 前4位, ck4=cookie 前4位 */
        Serial.printf("[AUTH] block %s tok=%d:%s sess=%d:%s ck=%s\n",
                      path, (int)_server.arg("token").length(),
                      _server.arg("token").length() >= 4 ? _server.arg("token").substring(0, 4).c_str() : "-",
                      (int)_authToken.length(),
                      _authToken.length() >= 4 ? _authToken.substring(0, 4).c_str() : "-",
                      _server.header("Cookie").length() > 0 ? _server.header("Cookie").substring(0, 12).c_str() : "none");
        _server.sendHeader("Location", "/login.html");
        _server.send(302, "text/plain", "");
        return;
    }
    /* 登录跳转目标: 打印 URL token 实况, 判断"跳转 URL 是否真的带 token"(对应前端 hdr=0 的 401 谜团) */
    if (strcmp(path, "/index.html") == 0) {
        String u = _server.arg("token");
        Serial.printf("[AUTH] page %s url_tok=%d:%.4s ck=%s sess=%d:%.4s\n",
                      path, (int)u.length(), u.c_str(),
                      _server.header("Cookie").length() > 0 ? "y" : "n",
                      (int)_authToken.length(), _authToken.c_str());
    }
    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    File f = LittleFS.open(path, "r");
    if (f) {
        /* 总览页/登录页注入会话 token + 当前 IP: 浏览器 localSearch/localStorage/cookie 在隐私/无痕
         * 模式均不可靠, 服务器把鉴权通过的 token 直接写进 HTML, 前端读 window.__TOKEN 100% 可靠;
         * IP 显示在页面顶栏, 刷机/断线重连后方便知道新地址 */
        if ((strcmp(path, "/index.html") == 0 || strcmp(path, "/login.html") == 0) && f.size() < 32768) {
            String html = f.readString();
            f.close();
            String tok = _server.arg("token");
            bool hasMarker = html.indexOf("window.__TOKEN") >= 0;
            bool hasPlaceholder = html.indexOf("<!--TOKEN-->") >= 0;
            html.replace("<!--TOKEN-->", tok);
            /* 注入当前 IP: 首页顶栏显示, 刷机/断线重连后方便知道新地址 */
            html.replace("<!--IP-->", WiFi.localIP().toString());
            Serial.printf("[AUTH] inject idx=%d size=%u marker=%d ph=%d tok=%d:%.4s\n",
                          hasMarker ? 1 : 0, (unsigned)html.length(),
                          hasMarker ? 1 : 0, hasPlaceholder ? 1 : 0,
                          (int)tok.length(), tok.c_str());
            _server.send(200, "text/html", html);
            return;
        }
        _server.streamFile(f, _mime(path));
        f.close();
    } else {
        _server.send_P(200, "text/html; charset=utf-8", FALLBACK_HTML);
    }
}

/* ---------------- 生命周期 ---------------- */
void WebServerApp::begin(FeederManager* mgr) {
    _mgr = mgr;
    credsInit();                       /* 账号凭据 NVS(默认管理密码) */
    _loadAuthSetting();
    /* multipart 上传流式收集(不经过 arg("plain")/5s 阻塞读取, hex 大 body 上传更可靠)
       必须先于 _setupRoutes() 注册: ESP32 WebServer 在两参 on() 注册路由时把当时的
       _fileUploadHandler 快照进该路由的 _ufn(见 WebServer.cpp on() -> FunctionRequestHandler),
       后注册的回调对已挂载路由不生效 -> upload 事件永不触发 -> 恒"空文件"400 */
    _server.onFileUpload([this]() {
        HTTPUpload& u = _server.upload();
        if (_server.uri() != "/api/feeder/up/upload") return;   /* 只收集飞达升级接口 */
        if (u.status == UPLOAD_FILE_START) { _upBuf.clear(); _upGot = false; }
        else if (u.status == UPLOAD_FILE_WRITE) {
            if ((size_t)_upBuf.length() + u.currentSize <= 65536)
                _upBuf.concat((const char*)u.buf, u.currentSize);
        }
        else if (u.status == UPLOAD_FILE_END) { _upGot = true; }
    });
    _setupRoutes();
    /* ESP32 WebServer 默认不收集请求头, 必须显式注册才可 header() 读取:
       不注册则 X-Auth-Token/Cookie 永远读不到 -> API 恒 401(曾误判为连接层丢 header)。
       额外收集 Content-Length/Transfer-Encoding/Content-Type/Expect/X-Admin-Pass:
       现场 POST body 偶发丢失, 需要这些头定位是 chunked/Expect 还是代理问题;
       X-Admin-Pass 作为 body 丢失时 M799/终端密码的兜底通道。 */
    const char* authHdrs[] = {
        "X-Auth-Token", "Cookie", "Content-Length", "Transfer-Encoding",
        "Content-Type", "Expect", "X-Admin-Pass", "X-Unlock", "X-User",
        /* 现场 body/query 都会丢, 自定义头稳定到达: 所有命令字段镜像成 X-<key> */
        "X-code", "X-chipid", "X-addr", "X-a", "X-b", "X-c", "X-f",
        "X-u", "X-v", "X-w", "X-x", "X-t", "X-s", "X-o", "X-r",
        "X-mode", "X-on", "X-vol", "X-force", "X-src", "X-cmd",
        "X-keep", "X-ssid", "X-wifi-pass", "X-old", "X-fresh", "X-pass", "X-enabled",
        /* 分片上传: 头部携带 hex 片段(避开 POST body/multipart) */
        "X-Upload-Seq", "X-Upload-Last", "X-Chunk", "X-Ota-Total", "X-Ota-Off", "X-Target-Ver", "X-Target-Fs", "X-Force"
    };
    _server.collectHeaders(authHdrs, sizeof(authHdrs) / sizeof(authHdrs[0]));
    _server.begin();
}

void WebServerApp::loop() {
    _server.handleClient();
}

/* ---------------- JSON ---------------- */
/* 顶部状态 + 诊断(不含 64 台明细, 约 200B, 弱网也能秒回) */
/* 设备当前页面(FS)版本: 读 LittleFS /fsver.txt (归一化: 兼容旧版带 DDfs 前缀)
 * 注: LittleFS 已在 setup 挂载, 直接 open, 勿再 begin()(重复 begin 会刷 "Already Mounted" 警告) */
static String _fsVer() {
    File f = LittleFS.open("/fsver.txt", "r");
    if (!f) return "-";
    String s = f.readString();
    f.close();
    s.trim();
    if (s.startsWith(FS_ASSET_PREFIX)) s = s.substring(sizeof(FS_ASSET_PREFIX) - 1);
    return s.length() ? s : "-";
}

/* 蜂鸣触发: 轮询 /api/status 时检测状态变化(防重: 只在变化瞬间响) */
static uint32_t s_lastErrTotal = 0;   /* 上次统计: 处于错误状态的飞达台数 */
static uint32_t s_lastOnline   = 0;   /* 上次统计: 在线台数 */
static uint32_t s_lastUninit   = 0;   /* 上次统计: 未初始化(addr=99 等)台数 */
static uint32_t s_lastConflict = 0;   /* 上次统计: 地址冲突组数 */
static bool     s_offlinePending = false; /* 在线数下降待确认(防单次丢帧误报) */
static unsigned long s_offlineSince = 0;
static unsigned long s_offlineBeepAt = 0;

static void _beepOnStateChange(FeederManager* mgr, uint32_t online) {
    if (!mgr) return;
    uint32_t errTotal = 0;
    for (int i = 0; i < FEEDER_COUNT; i++) {
        /* ERR_OK/ERR_OK_NO_FEEDBACK/ERR_IGNORED(0/1/2) 不蜂鸣；
         * 只有 ERR_FAULT(3)/ERR_PWR_FAULT(4) 才计入。 */
        if (mgr->state(i)->lastErr > 2) errTotal++;
    }
    if (errTotal > s_lastErrTotal) {                       /* 新增错误 → 短鸣(次数=新增台数, 上限3) */
        int n = (int)(errTotal - s_lastErrTotal);
        if (n > 3) n = 3;
        buzzerBeep(BEEP_FEEDER_ERR, n);
    }
    /* 离线增多 → 长鸣 2 次。先确认 6s(可能是单次 485 丢帧/心跳间隔),
     * 并限制 60s 内最多一次, 避免一台慢飞达周期性响 2 声。 */
    if (online < s_lastOnline) {
        if (!s_offlinePending) { s_offlinePending = true; s_offlineSince = millis(); }
    } else {
        s_offlinePending = false;
    }
    if (s_offlinePending && (long)(millis() - s_offlineSince) > 6000 &&
        (s_offlineBeepAt == 0 || (long)(millis() - s_offlineBeepAt) > 60000)) {
        buzzerBeep(BEEP_BUS_OFFLINE, 2);
        s_offlineBeepAt = millis();
        s_offlinePending = false;
    }
    /* 地址冲突出现 → 短鸣 3 次(未配址新增已在 FeederManager 上报/广播路径直接蜂鸣, 不依赖 Web 轮询) */
    uint32_t conf = mgr->conflictCount();
    uint32_t uni  = mgr->uninitCount();
    if (conf > s_lastConflict)
        buzzerBeep(BEEP_FEEDER_ERR, 3);
    s_lastErrTotal = errTotal;
    s_lastOnline = online;
    s_lastUninit = uni;
    s_lastConflict = conf;
}

String WebServerApp::_metaJson() {
    uint32_t on = _mgr ? _mgr->onlineCount() : 0;
    _beepOnStateChange(_mgr, on);
    bool u = _unlocked && (long)(millis() - _unlockAt) < (long)UNLOCK_TIMEOUT_MS;
    /* 信号: 仅 STA(连上路由器)时有意义; AP 热点/未连 STA 返回 -128, 前端显示 "-" */
    int rssi = (WiFi.status() == WL_CONNECTED) ? (int)WiFi.RSSI() : -128;
    String s; s.reserve(512);   // 预分配, 避免拼接碎片
    s = "{\"fw\":\"" FW_VERSION "\",\"online\":" + String(on) +
               ",\"total\":" + String(FEEDER_COUNT) +
               ",\"unlocked\":" + (u ? "true" : "false") +
               ",\"feedTotal\":" + String((uint32_t)(_mgr ? _mgr->feedTotal() : 0)) +
               ",\"ip\":\"" + WiFi.localIP().toString() +
               "\",\"rssi\":" + String(rssi) +
               ",\"fsver\":\"" + _fsVer() + "\"" +
               ",\"log\":" + String(logIsEnabled() ? "true" : "false") +
               ",\"prod\":" + String(_mgr && _mgr->productionLocked() ? "true" : "false") +
               ",\"manualRelease\":" + String(_mgr && _mgr->manualReleased() ? "true" : "false");
    /* 未初始化(addr=99 等)/地址冲突 诊断 */
    s += ",\"uninit\":" + String(_mgr ? _mgr->uninitCount() : 0);
    if (_mgr && _mgr->uninitCount()) {
        s += ",\"uninitChip\":\"" + String(_mgr->uninitChip(), HEX) + "\"";
    }
    s += ",\"conflicts\":[";
    if (_mgr) {
        for (int i = 0; i < (int)_mgr->conflictCount() && i < 8; i++) {
            if (i) s += ',';
            s += String(_mgr->conflictAddr(i));
        }
    }
    s += "]}";
    return s;
}

String WebServerApp::_statusJson() {
    String s = _metaJson();
    s = s.substring(0, s.length() - 1);          // 去掉结尾 "}", 接 feeders
    s.reserve(5120);                             // 预分配 64 台明细 + 临时飞达空间(须在 substring 之后才生效, 防堆碎片/偶发坏JSON)
    s += ",\"feeders\":[";
    if (_mgr) {
        for (int i = 0; i < FEEDER_COUNT; i++) {
            const FeederState* f = _mgr->state(i);
            bool pr = f->present;   // 只在扫描时更新, 不随时间过期(避免无周期扫描时误灰)
            s += "{\"addr\":" + String(f->addr) +
                 ",\"present\":" + (pr ? "true" : "false") +
                 ",\"conflict\":" + (f->conflict ? "true" : "false") +
                 ",\"enabled\":" + String(f->hasEnable ? (f->enabled ? 1 : 0) : -1) +
                 ",\"chipid\":" + String(f->chipid) +
                 ",\"fwVer\":" + String(f->fwVer) +
                 ",\"fw\":" + String(f->fw) +
                 ",\"feed\":" + String(_mgr->feedCountAt(i)) +
                 "}";
            if (i < FEEDER_COUNT - 1) s += ',';
        }
    }
    s += "]";

    /* 临时飞达(addr=99, 未配置): 单独一栏 */
    s += ",\"temp\":[";
    if (_mgr) {
        for (int i = 0; i < (int)_mgr->tempCount(); i++) {
            const FeederState* tf = _mgr->tempAt(i);
            if (!tf) continue;
            if (i) s += ',';
            s += "{\"addr\":" + String(tf->addr) +
                 ",\"boot\":" + (tf->boot ? "true" : "false") +
                 ",\"present\":" + (tf->present ? "true" : "false") +
                 ",\"enabled\":" + String(tf->hasEnable ? (tf->enabled ? 1 : 0) : -1) +
                 ",\"chipid\":" + String(tf->chipid) +
                 ",\"fwVer\":" + String(tf->fwVer) +
                 ",\"fw\":" + String(tf->fw) +
                 "}";
        }
    }
    s += "]}";
    return s;
}

String WebServerApp::_tempFeederJson(uint8_t idx) {
    const FeederState* f = _mgr ? _mgr->tempAt(idx) : nullptr;
    if (!f) return "{\"error\":\"no feeder\"}";
    bool pr = f->present &&
              (long)(millis() - f->lastSeenMs) < (long)FEEDER_OFFLINE_MS;
    return "{\"addr\":" + String(f->addr) +                       /* 99: 临时飞达 */
           ",\"temp\":true" +
           ",\"boot\":" + (f->boot ? "true" : "false") +
           ",\"present\":" + (pr ? "true" : "false") +
           ",\"enabled\":" + (f->enabled ? "true" : "false") +
           ",\"hasEnable\":" + (f->hasEnable ? "true" : "false") +
           ",\"chipid\":" + String(f->chipid) +
           ",\"fwVer\":" + String(f->fwVer) +
           ",\"feed\":" + String(_mgr->feedCountFor(f->chipid)) +
           ",\"cfgA\":" + String(f->cfgA) +
           ",\"cfgB\":" + String(f->cfgB) +
           ",\"cfgC\":" + String(f->cfgC) +
           ",\"cfgF\":" + String(f->cfgF) +
           ",\"cfgU\":" + String(f->cfgU) +
           ",\"cfgV\":" + String(f->cfgV) +
           ",\"cfgW\":" + String(f->cfgW) +
           ",\"cfgX\":" + String(f->cfgX) +
           ",\"cfgT\":" + String(f->cfgT) +
           ",\"cfgSeq\":" + String(f->cfgSeq) +
           ",\"fw\":" + String(f->fw) +
           "}";
}

String WebServerApp::_feederJson(uint8_t slot) {
    const FeederState* f = _mgr ? _mgr->state(slot) : nullptr;
    if (!f) return "{\"error\":\"no feeder\"}";
    bool pr = f->present &&
              (long)(millis() - f->lastSeenMs) < (long)FEEDER_OFFLINE_MS;
    return "{\"addr\":" + String(f->addr) +
           ",\"present\":" + (pr ? "true" : "false") +
           ",\"enabled\":" + (f->enabled ? "true" : "false") +
           ",\"hasEnable\":" + (f->hasEnable ? "true" : "false") +
           ",\"busy\":" + (f->busy ? "true" : "false") +
           ",\"chipid\":" + String(f->chipid) +
           ",\"fwVer\":" + String(f->fwVer) +
           ",\"feed\":" + String(_mgr->feedCountAt(slot)) +
           ",\"cfgA\":" + String(f->cfgA) +
           ",\"cfgB\":" + String(f->cfgB) +
           ",\"cfgC\":" + String(f->cfgC) +
           ",\"cfgF\":" + String(f->cfgF) +
           ",\"cfgU\":" + String(f->cfgU) +
           ",\"cfgV\":" + String(f->cfgV) +
           ",\"cfgW\":" + String(f->cfgW) +
           ",\"cfgX\":" + String(f->cfgX) +
           ",\"cfgT\":" + String(f->cfgT) +
           ",\"cfgSeq\":" + String(f->cfgSeq) +
           ",\"fw\":" + String(f->fw) +
           ",\"speed\":" + String(f->speed) +
           ",\"pitch\":" + String(f->pitch) +
           "}";
}

/* ---------------- 路由 ---------------- */
void WebServerApp::_setupRoutes() {
    /* 页面 */
    _server.on("/", HTTP_GET, [this](){ _servePage("/index.html"); });
    _server.on("/index.html", HTTP_GET, [this](){ _servePage("/index.html"); });
    _server.on("/wifi.html", HTTP_GET, [this](){ _servePage("/wifi.html"); });
    _server.on("/ota.html", HTTP_GET, [this](){ _servePage("/ota.html"); });
    _server.on("/feeder.html", HTTP_GET, [this](){ _servePage("/feeder.html"); });
    _server.on("/log.html", HTTP_GET, [this](){ _servePage("/log.html"); });
    _server.on("/events.html", HTTP_GET, [this](){ _servePage("/events.html"); });
    _server.on("/debug.html", HTTP_GET, [this](){ _servePage("/index.html"); });  /* 调测已并入飞达详情页, 旧链接回总览 */

    _server.on("/login.html", HTTP_GET, [this](){ _servePage("/login.html"); });
    _server.on("/feeder_upgrade.html", HTTP_GET, [this](){ _servePage("/feeder_upgrade.html"); });

    /* 登录: {"username":"admin","password":"...","keep":true/false}
     *   keep=true  -> 永久会话; keep=false -> 20 分钟失效
     *   admin 固定用户名; operator 由管理员在设置页自定义(默认未启用) */
    _server.on("/api/login", HTTP_POST, [this](){
        String body = _server.arg("plain");   // JSON body
        String user, pw;
        int p = body.indexOf("\"username\":\"");
        if (p >= 0) {
            int s = p + 12, e = body.indexOf('"', s);
            if (e > s) user = body.substring(s, e);
        }
        p = body.indexOf("\"password\":\"");
        if (p >= 0) {
            int s = p + 12, e = body.indexOf('"', s);
            if (e > s) pw = body.substring(s, e);
        }
        /* body 丢失兜底: 登录页把用户名放 X-User、密码放 X-Admin-Pass, keep 放 query */
        if (user.length() == 0) user = _server.header("X-User");
        if (user.length() == 0) user = _server.arg("username");
        if (pw.length() == 0) pw = _server.header("X-Admin-Pass");
        if (pw.length() == 0) pw = _server.arg("password");
        bool keep = body.indexOf("\"keep\":true") >= 0 || body.indexOf("\"keep\": true") >= 0 ||
                    _server.arg("keep") == "1";
        String role;
        if (user.length() > 0 && credsCheckLogin(user, pw, role)) {
            /* 调试直打(不经 LOGD 门控): 登录成功可见 */
            Serial.printf("[AUTH] login ok user=%s role=%s keep=%d\n", user.c_str(), role.c_str(), keep ? 1 : 0);
            _authToken = _genToken();
            _authRole  = role;
            _authExpiry = keep ? 0 : millis() + 20UL * 60 * 1000;   /* 20 分钟 */
            long cookieMaxAge = keep ? 31536000L : 1200L;
            _server.sendHeader("Set-Cookie", "auth=" + _authToken + "; Path=/; Max-Age=" + String(cookieMaxAge) + "; SameSite=Lax");
            _unlocked = false;                                        /* 登录后需重新解锁 */
            _sendJson("{\"ok\":true,\"token\":\"" + _authToken + "\",\"role\":\"" + role + "\",\"keep\":" +
                      (keep ? "true" : "false") + "}");
        } else {
            /* 调试直打(不经 LOGD 门控): 登录失败原因可见. stored=读到的 admin_pw(len:前2位), 空=creds 命名空间读不到 */
            String stored = credsStoredAdminPw();
            Serial.printf("[AUTH] login FAIL user=%s(%d) pw=%d stored=%d:%s default=%s\n",
                          user.c_str(), (int)user.length(), (int)pw.length(),
                          (int)stored.length(),
                          stored.length() >= 2 ? stored.substring(0, 2).c_str() : "-",
                          credsIsDefaultPw() ? "yes" : "no");
            _sendJson("{\"ok\":false,\"msg\":\"bad user or password\"}", 401);
        }
    });

    _server.on("/api/logout", HTTP_POST, [this](){
        _authToken = ""; _authRole = ""; _authExpiry = 0;
        _server.sendHeader("Set-Cookie", "auth=; Path=/; Max-Age=0");
        _sendJson("{\"ok\":true}");
    });

    /* 当前会话(登录页/各页面顶部状态用) */
    _server.on("/api/auth/status", HTTP_GET, [this](){
        _sendJson(String("{\"ok\":true,\"auth\":") + (_authEnabled ? "true" : "false") +
                  ",\"role\":\"" + (_authOk() ? _authRole : "") + "\"}");
    });

    _server.on("/api/auth/enable", HTTP_POST, [this](){
        String body = _server.arg("plain");
        bool en = body.indexOf("\"enabled\":true") >= 0 ||
                  body.indexOf("\"enabled\": true") >= 0 ||
                  _server.header("X-enabled") == "1";
        /* 关闭登录验证：已开启且未登录时允许用管理员密码关闭，避免自锁 */
        if (_authEnabled && !_isAdmin() && !(en == false && credsAdminPwOk(_server.header("X-Admin-Pass")))) {
            _sendJson("{\"ok\":false,\"msg\":\"admin required\"}", 403); return;
        }
        _authEnabled = en;
        Preferences prefs;
        prefs.begin("auth", false);
        prefs.putBool("enabled", _authEnabled);
        prefs.end();
        if (!_authEnabled) _authToken = "";   // 关闭时清掉会话
        LOGD("[Auth] login check toggled: %s\n", _authEnabled ? "on" : "off");
        _sendJson(String("{\"ok\":true,\"auth\":") + (_authEnabled ? "true" : "false") + "}");
    });

    /* 账号管理(仅管理员): GET 当前账号配置; POST 修改管理密码 / 设置操作员 */
    _server.on("/api/creds/status", HTTP_GET, [this](){
        if (!_isAdmin()) { _sendJson("{\"ok\":false,\"msg\":\"admin required\"}", 403); return; }
        String op = credsOpUser();
        _sendJson("{\"ok\":true,\"adminUser\":\"" + credsAdminUser() +
                  "\",\"opUser\":\"" + op + "\"}");
    });
    _server.on("/api/creds/adminpw", HTTP_POST, [this](){
        if (!_isAdmin()) { _sendJson("{\"ok\":false,\"msg\":\"admin required\"}", 403); return; }
        String body = _server.arg("plain"), oldPw, fresh;
        oldPw = _server.header("X-old");
        fresh = _server.header("X-fresh");
        int p = body.indexOf("\"old\":\"");
        if (p >= 0) { int s = p + 6, e = body.indexOf('"', s); if (e > s) oldPw = body.substring(s, e); }
        p = body.indexOf("\"fresh\":\"");
        if (p >= 0) { int s = p + 8, e = body.indexOf('"', s); if (e > s) fresh = body.substring(s, e); }
        if (credsSetAdminPw(oldPw, fresh))
            _sendJson("{\"ok\":true}");
        else
            _sendJson("{\"ok\":false,\"msg\":\"old password wrong or too short(>=4)\"}", 400);
    });
    _server.on("/api/creds/operator", HTTP_POST, [this](){
        if (!_isAdmin()) { _sendJson("{\"ok\":false,\"msg\":\"admin required\"}", 403); return; }
        String body = _server.arg("plain"), user, pw;
        user = _server.header("X-user");
        pw = _server.header("X-pass");
        int p = body.indexOf("\"user\":\"");
        if (p >= 0) { int s = p + 7, e = body.indexOf('"', s); if (e > s) user = body.substring(s, e); }
        p = body.indexOf("\"pass\":\"");
        if (p >= 0) { int s = p + 7, e = body.indexOf('"', s); if (e > s) pw = body.substring(s, e); }
        if (credsSetOperator(user, pw))
            _sendJson("{\"ok\":true}");
        else
            _sendJson("{\"ok\":false,\"msg\":\"user>=2 chars, pass>=4 chars, cannot be admin\"}", 400);
    });

    /* 恢复出厂设置(仅管理员): 需解锁 + 管理密码; 清 NVS + /apmode -> 重启 */
    _server.on("/api/factory/reset", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_isAdmin()) { _sendJson("{\"ok\":false,\"msg\":\"admin required\"}", 403); return; }
        if (!_needUnlock()) return;                          /* 写操作, 需解锁 */
        String body = _server.arg("plain"), pw;
        int p = body.indexOf("\"password\":\"");
        if (p >= 0) { int s = p + 11, e = body.indexOf('"', s); if (e > s) pw = body.substring(s, e); }
        if (pw.length() == 0) pw = _server.header("X-Admin-Pass");   // body 丢失兜底
        if (pw.length() == 0) pw = _server.arg("password");
        if (!credsAdminPwOk(pw)) { _sendJson("{\"ok\":false,\"msg\":\"admin required\"}", 403); return; }
        _sendJson("{\"ok\":true,\"msg\":\"factory reset\"}");
        factoryResetRequest();                               /* 回复先发出, 稍后清配置重启 */
    });

    /* Web 写操作解锁(供总览/单飞达页使用) */
    _server.on("/api/unlock/status", HTTP_GET, [this](){
        if (!_needAuth()) return;
        bool u = _unlocked && (long)(millis() - _unlockAt) < (long)UNLOCK_TIMEOUT_MS;
        if (!u) _unlocked = false;
        _sendJson(String("{\"ok\":true,\"unlocked\":") + (u ? "true" : "false") + "}");
    });

    _server.on("/api/unlock", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_isAdmin()) { _sendJson("{\"ok\":false,\"msg\":\"admin required\"}", 403); return; }
        String body = _server.arg("plain");
        String xu = _server.header("X-Unlock");
        /* 解锁意图三通道: JSON body + query(?unlocked=1) + X-Unlock 头。
         * 现场实测 body 会为空(浏览器/代理把 POST body 弄丢), query 也可能被改写;
         * 三通道任一命中即可, 不再依赖单一的 arg("plain")。 */
        bool hasIntent = body.indexOf("unlocked") >= 0 || _server.hasArg("unlocked") || xu.length() > 0;
        bool en = body.indexOf("\"unlocked\":true") >= 0 ||
                  body.indexOf("\"unlocked\": true") >= 0 ||
                  _server.arg("unlocked") == "1" ||
                  _server.arg("unlocked") == "true" ||
                  xu == "1";
        if (body.length() < 16)   /* 诊断: body 空/异常时打印实况, 定位解锁假失败 */
            Serial.printf("[Web] unlock body(%u): %s q=%s x=%s\n", (unsigned)body.length(),
                          body.substring(0, 64).c_str(), _server.arg("unlocked").c_str(), xu.c_str());
        if (!hasIntent) {
            /* 空 body 且无 query/header: 不要把它当成"锁定"(旧逻辑会主动锁上) */
            _sendJson("{\"ok\":false,\"msg\":\"no unlock intent\",\"unlocked\":false}", 400);
            return;
        }
        _unlocked = en;
        _unlockAt = millis();
        if (en) {
            LOGD("[Unlock] web write unlocked (5min timeout)");
        } else {
            LOGD("[Unlock] manually locked");
        }
        _sendJson(String("{\"ok\":true,\"unlocked\":") + (_unlocked ? "true" : "false") + "}");
    });

    /* 飞达 API */
    _server.on("/api/feeders", HTTP_GET, [this](){
        if (!_needAuth()) return;
        _sendJson(_statusJson());
    });

    /* 轻量顶部状态(不含 64 台明细, 弱网下 meta 区独立快速更新) */
    /* 手动解除生产锁：独立于普通解锁，保持到下一条 UART1 M600。
     * 需要 admin；允许 Web M600/M602/扫描，直到 UART1 M600 重新加锁。 */
    _server.on("/api/production/release", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_isAdmin()) { _sendJson("{\"ok\":false,\"msg\":\"admin required\"}", 403); return; }
        if (_mgr) _mgr->releaseProduction();
        _sendJson("{\"ok\":true,\"manualRelease\":true}");
    });

    _server.on("/api/status", HTTP_GET, [this](){
        if (!_needAuth()) return;
        _sendJson(_metaJson());
    });

    /* 轻量生产状态: 自动送料每轮只查这个，避免反复拉 /api/status 的 64 台明细。 */
    _server.on("/api/feed/status", HTTP_GET, [this](){
        if (!_needAuth()) return;
        bool prod = _mgr && _mgr->productionLocked();
        bool manual = _mgr && _mgr->manualReleased();
        _sendJson(String("{\"ok\":true,\"prod\":") + (prod ? "true" : "false") +
                  ",\"manualRelease\":" + (manual ? "true" : "false") + "}");
    });

    /* 停止 Web 自动送料: 清普通队列中的 M600；不打断已经发到总线上的 M600。 */
    _server.on("/api/feed/stop", HTTP_POST, [this](){
        if (!_needAuth()) return;
        uint8_t n = _mgr ? _mgr->cancelNormalFeed() : 0;
        _sendJson(String("{\"ok\":true,\"cancelled\":") + String(n) + "}");
    });

    /* 推料计数清零: 带 slot=地址 清单个(经该地址 ChipID); 不带则清全部。需解锁 */
    _server.on("/api/feed/clear", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        String body = _server.arg("plain");
        int slot = -1;                                   // 不能默认 0，否则会清错槽位
        uint32_t chip = 0;
        if (_server.hasArg("chipid")) chip = (uint32_t)_server.arg("chipid").toInt();
        else if (_server.hasHeader("X-chipid")) chip = (uint32_t)_server.header("X-chipid").toInt();
        else if (body.indexOf("\"chipid\"") >= 0) chip = (uint32_t)jsonInt(body, "chipid", 0);
        if (_server.hasArg("addr")) slot = _server.arg("addr").toInt();
        else if (_server.hasHeader("X-addr")) slot = _server.header("X-addr").toInt();
        else if (body.indexOf("\"addr\"") >= 0) slot = jsonInt(body, "addr", -1);
        if (chip && _mgr) {                              // 先按 ChipID（正式槽）
            int a = _mgr->addrForChip(chip);
            if (a >= 0) slot = a;
            else if (_mgr->tempIdxForChip(chip) >= 0) {  // 临时 N99
                _mgr->clearFeed(chip);
                _sendJson("{\"ok\":true,\"cleared\":\"one\"}");
                return;
            }
        }
        if (slot >= 0 && slot < FEEDER_COUNT && _mgr) {
            uint32_t cid = chip ? chip : _mgr->state(slot)->chipid;
            if (!cid) { _sendJson("{\"ok\":false,\"msg\":\"no chipid\"}", 404); return; }
            _mgr->clearFeed(cid);                        // 按 ChipID 清(地址可改)
            _sendJson("{\"ok\":true,\"cleared\":\"one\"}");
        } else if (!chip && !_server.hasArg("addr")) {
            if (_mgr) _mgr->clearFeed();                 // 无目标才清全部
            _sendJson("{\"ok\":true,\"cleared\":\"all\"}");
        } else {
            _sendJson("{\"ok\":false,\"msg\":\"feeder not found\"}", 404);
        }
    });

    _server.on("/api/feeder", HTTP_GET, [this](){
        if (!_needAuth()) return;
        int slot = -1;
        if (_server.hasArg("chipid")) {                     // 按 ChipID 定位(地址可改)
            uint32_t cid = (uint32_t)_server.arg("chipid").toInt();
            /* 临时飞达(addr=99)优先：旧正式槽位可能残留同一 ChipID，
             * 若先查 addrForChip 会把 N99 详情页错误解析成旧地址。 */
            if (_mgr) {
                int ti = _mgr->tempIdxForChip(cid);
                if (ti >= 0) { _sendJson(_tempFeederJson(ti)); return; }
            }
            int a = _mgr ? _mgr->addrForChip(cid) : -1;
            if (a < 0) { _sendJson("{\"error\":\"no feeder\"}", 404); return; }
            slot = a;
        } else {
            slot = _server.arg("addr").toInt();             // 按地址
        }
        if (slot < 0 || slot >= FEEDER_COUNT) {
            /* addr=99 等未配置/临时飞达: 查临时表(冲突设备也在临时表) */
            if (_mgr) {
                for (uint8_t i = 0; i < _mgr->tempCount(); i++) {
                    if (_mgr->tempAt(i)->addr == (uint8_t)slot) { _sendJson(_tempFeederJson(i)); return; }
                }
            }
            _sendJson("{\"error\":\"bad addr\"}", 400); return;
        }
        _sendJson(_feederJson(slot));
    });

    /* ---- 飞达固件在线升级（独立升级页 feeder_upgrade.html） ---- */

    /* 本地上传 hex 包: multipart 文件流 -> 校验/校验和/地址越界 + DDFW 版本扫描 */
    _server.on("/api/feeder/up/upload", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        String hex = _upGot ? _upBuf : String();
        _upBuf.clear(); _upGot = false;                  /* 单连接同步处理, 读完即清 */
        if (hex.length() == 0) { _sendJson("{\"ok\":false,\"msg\":\"空文件\"}", 400); return; }
        String ver, err;
        int r = fupUploadHex(hex, ver, err);
        if (r != 0) { _sendJson("{\"ok\":false,\"msg\":\"" + err + "\"}", 400); return; }
        _sendJson("{\"ok\":true,\"ver\":\"" + ver + "\",\"msg\":\"固件包已校验，可升级\"}");
    });

    /* 分片上传(推荐): 每片 hex 文本放 X-Chunk 请求头, 不依赖 POST body/multipart。
     * 现场 body 会丢, 但自定义头稳定到达; 1.2KB/片, 最后一片拼好再走 fupUploadHex。
     * headers: X-Upload-Seq(0..), X-Upload-Last(0/1), X-Chunk(仅 hex 字符, 去空白)。 */
    _server.on("/api/feeder/up/chunk", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        int seq = _server.header("X-Upload-Seq").toInt();
        bool last = _server.header("X-Upload-Last") == "1";
        String chunk = _server.header("X-Chunk");
        if (seq == 0) {
            s_upLen = 0;
            if (!s_upHex) s_upHex = (char*)malloc(60001);
            if (!s_upHex) { _sendJson("{\"ok\":false,\"msg\":\"内存不足，无法分配上传缓冲\"}", 500); return; }
        }
        if (chunk.length() == 0) {
            _sendJson("{\"ok\":false,\"msg\":\"空分片\"}", 400); return;
        }
        if (s_upLen + chunk.length() > 60000) {
            s_upLen = 0;
            _sendJson("{\"ok\":false,\"msg\":\"固件包超过 60KB（只能传 APP.hex）\"}", 400); return;
        }
        memcpy(s_upHex + s_upLen, chunk.c_str(), chunk.length());
        s_upLen += chunk.length();
        s_upHex[s_upLen] = 0;
        if (!last) {
            _sendJson(String("{\"ok\":true,\"got\":") + String((unsigned)s_upLen) + "}");
            return;
        }
        if (s_upLen < 11 || strcmp(s_upHex + s_upLen - 11, ":00000001FF") != 0) {
            unsigned got = (unsigned)s_upLen;
            s_upLen = 0;
            _sendJson("{\"ok\":false,\"msg\":\"上传不完整(got " + String(got) + "), 请重试\"}", 400);
            return;
        }
        String ver, err;
        int r = fupUploadHex(s_upHex, s_upLen, ver, err);
        s_upLen = 0;
        free(s_upHex); s_upHex = nullptr;
        if (r != 0) { _sendJson("{\"ok\":false,\"msg\":\"" + err + "\"}", 400); return; }        _sendJson("{\"ok\":true,\"ver\":\"" + ver + "\",\"msg\":\"固件包已校验，可升级\"}");
    });

    /* 发起升级: body {"addr":N,"force":0|1,"src":"local"|"github"} */
    _server.on("/api/feeder/up/start", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        String body = _server.arg("plain");
        int slot = jsonInt(body, "addr", -1);
        if (slot < 0 && _server.hasArg("addr")) slot = _server.arg("addr").toInt();
        if (slot < 0 && _server.hasHeader("X-addr")) slot = _server.header("X-addr").toInt();  // body/query 丢失兜底
        bool hasChip = body.indexOf("\"chipid\"") >= 0 || _server.hasArg("chipid") || _server.hasHeader("X-chipid");
        int chipArg = _server.hasArg("chipid") ? _server.arg("chipid").toInt()
                    : (_server.hasHeader("X-chipid") ? _server.header("X-chipid").toInt()
                                                     : jsonInt(body, "chipid", 0));
        if (hasChip && _mgr) {
            int a = _mgr->addrForChip((uint32_t)chipArg);
            if (a >= 0) slot = a;
            else {                                      /* 正式槽位无 -> 临时飞达(addr=99) */
                int ti = _mgr->tempIdxForChip((uint32_t)chipArg);
                if (ti >= 0) slot = _mgr->tempAt(ti)->addr;
            }
        }
        if (slot < 0 || slot > 255) { _sendJson("{\"ok\":false,\"msg\":\"bad addr\"}", 400); return; }
        /* 前端有时只带 addr 不带 chipid; 用在线表反查, 保证 M602/M788 都带 P<chipid>,
         * 多个 N99 时只命中目标。 */
        if (chipArg == 0 && slot >= 0 && slot < 64 && _mgr) {
            const FeederState& f = _mgr->at((uint8_t)slot);
            if (f.present && f.chipid) chipArg = (int)f.chipid;
            if (chipArg == 0) {
                for (uint8_t ti = 0; ti < 32; ti++) {
                    const FeederState* t = _mgr->tempAt(ti);
                    if (!t) break;
                    if (t->addr == slot && t->chipid) { chipArg = (int)t->chipid; break; }
                }
            }
        }
        bool force = (_server.hasArg("force") ? _server.arg("force").toInt()
                    : (_server.hasHeader("X-force") ? _server.header("X-force").toInt()
                                                    : jsonInt(body, "force", 0))) != 0;
        bool gh = body.indexOf("\"src\":\"github\"") >= 0 || _server.arg("src") == "github" ||
                  _server.header("X-src") == "github";
        String err;
        if (!fupStart((uint8_t)slot, (uint32_t)chipArg, force, gh, err)) { _sendJson("{\"ok\":false,\"msg\":\"" + err + "\"}", 400); return; }
        _sendJson("{\"ok\":true,\"msg\":\"升级已启动\"}");
    });

    /* 升级进度/状态轮询 */
    _server.on("/api/feeder/up/status", HTTP_GET, [this](){
        if (!_needAuth()) return;
        _sendJson(fupStatusJson());
    });

    /* 检查 GitHub 飞达固件资产(最新 release 的 feeder.hex) + 本地包状态 */
    _server.on("/api/feeder/up/github", HTTP_GET, [this](){
        if (!_needAuth()) return;
        _sendJson(fupCheckGitHub());
    });

    /* 查询板上固件版本(M602 FW: 异步触发, 结果由 /api/feeder/up/status 的 board_fw 轮询带回;
       支持临时飞达 addr=99/chipid)。
       注意不能同步等待: 应答处理在 main loop, handler 阻塞即饿死应答; 回调也不能写 handler 栈引用
       (超时返回后应答晚到 -> 悬空 String -> LoadProhibited 崩溃) */
    _server.on("/api/feeder/up/board", HTTP_GET, [this](){
        if (!_needAuth()) return;
        int slot = -1;
        if (_server.hasArg("chipid")) {                 /* 按 ChipID 定位(地址可改/临时飞达) */
            uint32_t cid = (uint32_t)_server.arg("chipid").toInt();
            if (_mgr) {
                int a = _mgr->addrForChip(cid);
                if (a >= 0) slot = a;
                else {
                    int ti = _mgr->tempIdxForChip(cid);
                    if (ti >= 0) slot = _mgr->tempAt(ti)->addr;
                }
            }
        } else {
            slot = _server.arg("addr").toInt();
        }
        if (slot < 0 || slot > 255) { _sendJson("{\"ok\":false,\"msg\":\"bad addr\"}", 400); return; }
        if (!fupBoardQueryStart((uint8_t)slot)) { _sendJson("{\"ok\":false,\"msg\":\"总线忙，请稍后再试\"}"); return; }
        _sendJson("{\"ok\":true,\"pending\":true}", 200);
    });

    _server.on("/api/command", HTTP_POST, [this](){
        String body = _server.arg("plain");
        /* 现场: Chrome/代理可能把 POST body 发成 chunked 或直接丢 body,
         * ESP32 WebServer 只支持 Content-Length plain body -> arg("plain")=="",
         * 表现为 F12 有 payload 但设备 body(0)/400。前端 api() 已把 body 标量字段
         * 镜像到 URL query, 这里 body 优先、query 兜底, 彻底绕开 body 解析。 */
        auto hasKey = [&](const char* k) {
            String hk = String("X-") + k;
            return body.indexOf(String("\"") + k + "\"") >= 0 || _server.hasArg(k) || _server.hasHeader(hk);
        };
        auto getInt = [&](const char* k, int def) -> int {
            String hk = String("X-") + k;
            if (_server.hasHeader(hk)) return (int)_server.header(hk).toInt();  // header 优先(最可靠)
            if (_server.hasArg(k)) return (int)_server.arg(k).toInt();
            return jsonInt(body, k, def);
        };
        int code = getInt("code", -1);
        if (code < 0) {
            /* 诊断: 400 时打印 body/关键请求头/query 实况, 用于定位 POST body 丢失 */
            Serial.printf("[Web] bad cmd body(%u) cl=%s te=%s ct=%s expect=%s q=%s\n",
                          (unsigned)body.length(),
                          _server.header("Content-Length").c_str(),
                          _server.header("Transfer-Encoding").c_str(),
                          _server.header("Content-Type").c_str(),
                          _server.header("Expect").c_str(),
                          _server.arg("code").c_str());
            _sendJson("{\"ok\":false,\"msg\":\"bad cmd\"}", 400); return;
        }
        // M602 只读查询 / M611 M612 闪烁定位(仅 LED, 不动飞达): 免登录免解锁(现场调试可用);
        // 其余写操作需登录 + 解锁
        bool readOnly = (code == MCMD_QUERY || code == MCMD_BLINK || code == MCMD_BLINK_OFF);
        if (!readOnly && !_needAuth()) return;
        /* M788/M799 属于复位/恢复出厂, operator 不允许 */
        if ((code == MCMD_ISP_RESET || code == MCMD_FACTORY_RESET) && !_isAdmin()) {
            _sendJson("{\"ok\":false,\"msg\":\"admin required\"}", 403); return;
        }
        if (!readOnly && code != MCMD_ISP_RESET && !_needUnlock()) return;
        // 舵机校准进行中: 拒绝其它写命令, 避免与二分流程抢总线/改参数
        if (calibActive() && code != MCMD_QUERY) {
            _sendJson("{\"ok\":false,\"msg\":\"calib running\"}", 409);
            return;
        }
        MCmdMsg m;
        m.code = (uint16_t)code;

        /* 生产锁期间 Web 不允许插生产/查询命令。手动解除后放行。 */
        if (_mgr && _mgr->productionLocked() &&
            (code == MCMD_ADVANCE || code == MCMD_RETRACT || code == MCMD_SERVO ||
             code == MCMD_ENABLE || code == MCMD_QUERY)) {
            _sendJson("{\"ok\":false,\"msg\":\"production lock\"}", 409);
            return;
        }

        // M799 恢复出厂为广播命令，需管理页密码确认
        if (code == MCMD_FACTORY_RESET) {
            String pw;
            int p = body.indexOf("\"password\":\"");
            if (p >= 0) {
                int s = p + 12, e = body.indexOf('"', s);
                if (e >= 0) pw = body.substring(s, e);
            }
            if (pw.length() == 0) pw = _server.header("X-Admin-Pass");  // body 丢失时走请求头
            if (pw.length() == 0) pw = _server.arg("password");         // 最后兜底
            if (!credsAdminPwOk(pw)) {
                _sendJson("{\"ok\":false,\"msg\":\"admin required\"}", 403);
                return;
            }
        }

        if (hasKey("addr")) {   // N0 是合法设备地址, 非广播
            m.addr = getInt("addr", 0);
            m.hasN = true;
        }
        if (body.indexOf("\"broadcast\":true") >= 0 || _server.arg("broadcast") == "true")
            m.explicitBroadcast = true;  // N255 广播
        /* 单台互斥: 屏幕/上位机正在操作同一台时拒绝(查询不占锁) */
        if (m.hasN && m.addr >= 0 && m.addr < FEEDER_COUNT && m.code != MCMD_QUERY) {
            OpOwner cur = opLockOwner((uint8_t)m.addr);
            if (cur != OP_FREE && cur != OP_WEB) {
                _sendJson(String("{\"ok\":false,\"err\":\"busy\",\"by\":\"") +
                          opOwnerName(cur) + "\"}", 409);
                return;
            }
            opLockTake((uint8_t)m.addr, OP_WEB);
        }

        if (hasKey("chipid")) {
            m.chipid = (uint32_t)getInt("chipid", 0);
            m.hasP = (m.chipid != 0);
            // chipid 定位(未带 addr): 查当前地址, 自动带 P 校验 —— 设备改地址也能正确操作
            if (!m.hasN && m.chipid && _mgr) {
                /* 临时飞达(addr=99)优先，避免旧正式槽位残留同一 ChipID */
                if (_mgr->tempIdxForChip(m.chipid) >= 0) {
                    m.addr = 99; m.hasN = true;
                } else {
                    int a = _mgr->addrForChip(m.chipid);
                    if (a < 0) { _sendJson("{\"ok\":false,\"msg\":\"device not found\"}", 404); return; }
                    m.addr = (uint8_t)a; m.hasN = true;
                }
            }
        }
        if (hasKey("a")) { m.a = getInt("a", 0); m.hasA = true; }
        if (hasKey("b")) { m.b = getInt("b", 0); m.hasB = true; }
        if (hasKey("c")) { m.c = getInt("c", 0); m.hasC = true; }
        if (hasKey("f")) { m.f = getInt("f", 0); m.hasF = true; }
        if (hasKey("u")) { m.u = getInt("u", 0); m.hasU = true; }
        if (hasKey("v")) { m.v = getInt("v", 0); m.hasV = true; }
        if (hasKey("w")) { m.w = getInt("w", 0); m.hasW = true; }
        if (hasKey("x")) { m.x = getInt("x", 0); m.hasX = true; }
        if (hasKey("t")) { m.t = getInt("t", 0); m.hasT = true; }  /* T N20 超时(M620 写入) */
        if (hasKey("s")) { m.s = getInt("s", 0); m.hasS = true; }
        if (hasKey("o")) { m.o = getInt("o", 0); m.hasO = true; }
        if (hasKey("r")) { m.r = getInt("r", 0); m.hasR = true; }  /* M620 改址 R<新地址> */
        /* 改址前做占用检查: 新地址槽位/临时表已有别的 ChipID -> 拒绝, 防止地址冲突 */
        if (code == MCMD_CONFIG && m.hasR) {
            if (m.r < 0 || m.r >= (int)FEEDER_COUNT) {
                _sendJson("{\"ok\":false,\"msg\":\"bad new addr\"}", 400); return;
            }
            if (!m.hasN && !m.hasP) {
                _sendJson("{\"ok\":false,\"msg\":\"target required\"}", 400); return;
            }
            uint32_t curChip = m.hasP ? m.chipid : 0;
            if (!curChip && m.hasN && _mgr && m.addr >= 0 && m.addr < (int)FEEDER_COUNT)
                curChip = _mgr->state((uint8_t)m.addr)->chipid;
            int curSlot = m.hasN ? (int)m.addr : -1;   /* 临时飞达(无 N): 新地址一律检查占用 */
            if (_mgr && (curSlot < 0 || (int)m.r != curSlot)) {
                const FeederState* nf = _mgr->state((uint8_t)m.r);
                if (nf->present && nf->chipid && nf->chipid != curChip) {
                    _sendJson("{\"ok\":false,\"msg\":\"地址已被其它飞达占用\"}", 409); return;
                }
                for (uint8_t i = 0; i < _mgr->tempCount(); i++) {
                    const FeederState* tf = _mgr->tempAt(i);
                    if (tf && tf->addr == (uint8_t)m.r && tf->chipid && tf->chipid != curChip) {
                        _sendJson("{\"ok\":false,\"msg\":\"地址已被其它飞达占用\"}", 409); return;
                    }
                }
            }
        }
        /* Web 推料兜底: 某些旧页面/缓存没有带 F，M600 会回 invalid F。
         * Web/API 的 M600 默认使用 4mm；UART1 透传仍按上位机原始报文。 */
        if (code == MCMD_ADVANCE && !m.hasF) { m.f = 4; m.hasF = true; }
        /* DIS 飞达不允许推料: 后端也要拦，不能只靠前端。
         * 否则 M600 会入队，且某些旧缓存会让计数看起来 +1。 */
        if ((m.code == MCMD_ADVANCE || m.code == MCMD_RETRACT || m.code == MCMD_SERVO) &&
            _mgr && m.hasN && m.addr >= 0 && m.addr < (int)FEEDER_COUNT) {
            const FeederState* fs = _mgr->state((uint8_t)m.addr);
            if (fs->hasEnable && !fs->enabled) {
                _sendJson("{\"ok\":false,\"msg\":\"feeder disabled, press S1/enable first\",\"disabled\":1}", 409);
                return;
            }
        }
        FeederCmdCb cb = nullptr;
        if (code == MCMD_CONFIG && m.hasR) {
            uint32_t cid = m.hasP ? m.chipid : 0;
            if (!cid && _mgr && m.hasN && m.addr >= 0 && m.addr < (int)FEEDER_COUNT)
                cid = _mgr->state((uint8_t)m.addr)->chipid;
            int from = (m.hasN && m.addr >= 0 && m.addr < (int)FEEDER_COUNT) ? (int)m.addr : -1;
            uint8_t to = (uint8_t)m.r;
            /* M620 回 ok 后再迁移映射: 旧槽位不清掉, addrForChip 会一直返回旧地址,
             * 后续 M602/M799 仍发 N<旧> -> 超时/改址看起来没生效。 */
            cb = [this, cid, from, to](uint8_t, const String& r) {
                if (_mgr && r.startsWith("ok")) _mgr->moveAddress(cid, from, to);
            };
        } else if (code == MCMD_FACTORY_RESET && m.hasP) {
            uint32_t cid = m.chipid;
            int from = (m.hasN && m.addr >= 0 && m.addr < (int)FEEDER_COUNT) ? (int)m.addr : -1;
            /* M799 回 ok 后设备地址回到 99: 迁到临时表, 否则后续按 chipid 仍解析旧地址 */
            cb = [this, cid, from](uint8_t, const String& r) {
                if (_mgr && r.startsWith("ok")) _mgr->moveToTemp(cid, from);
            };
        }
        bool ok = _mgr->command(m, cb);
        /* R 改址: 入队成功即迁移映射, 这样页面紧接着的 M602/M799 不会再解析到旧地址
         * (M620 与后续命令在同一队列, 设备会先收到 R 再收到 M602); 回调里再迁移一次是幂等的。 */
        if (ok && code == MCMD_CONFIG && m.hasR && _mgr) {
            uint32_t cid = m.hasP ? m.chipid : 0;
            if (!cid && m.hasN && m.addr >= 0 && m.addr < (int)FEEDER_COUNT)
                cid = _mgr->state((uint8_t)m.addr)->chipid;
            int from = (m.hasN && m.addr >= 0 && m.addr < (int)FEEDER_COUNT) ? (int)m.addr : -1;
            _mgr->moveAddress(cid, from, (uint8_t)m.r);
        }
        /* M610 只回 ok ready, 没有 enabled 字段: 入队成功即更新缓存, 否则总览卡片仍是旧状态 */
        if (ok && code == MCMD_ENABLE && m.hasS && _mgr) {
            _mgr->setEnabled(m.hasN ? (int)m.addr : -1, m.hasP ? m.chipid : 0, m.s != 0);
        }
        /* M799 恢复出厂: 入队即把 chipid 迁到临时表(99), 页面随后按 chipid 查到的就是 99 */
        if (ok && code == MCMD_FACTORY_RESET && m.hasP && _mgr) {
            int from = (m.hasN && m.addr >= 0 && m.addr < (int)FEEDER_COUNT) ? (int)m.addr : -1;
            _mgr->moveToTemp(m.chipid, from);
        }
        _sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false,\"msg\":\"bus busy\"}");
    });

    _server.on("/api/scan", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (_mgr && _mgr->productionLocked()) {
            _sendJson("{\"ok\":false,\"msg\":\"production lock\"}", 409);
            return;
        }
        bool ok = _mgr->requestScan();        // M602 N255 只读查询, 无需解锁
        _sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false}");
    });

    /* 舵机限位校准(ESP32 侧二分, 见 docs/调测与校准.md)
     * POST /api/calib {"chipid":<dec>,"mode":0|1|2}  需解锁(写操作)
     * GET  /api/calib/status -> 进度/结果 */
    _server.on("/api/calib", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (_mgr && _mgr->productionLocked()) {
            _sendJson("{\"ok\":false,\"msg\":\"production lock\"}", 409);
            return;
        }
        if (!_needUnlock()) return;                 // 校准驱动舵机动作, 需解锁
        if (calibActive()) { _sendJson("{\"ok\":false,\"msg\":\"calib running\"}", 409); return; }
        String body = _server.arg("plain");
        int mode = jsonInt(body, "mode", -1);
        if (mode < 0 && _server.hasArg("mode")) mode = (int)_server.arg("mode").toInt();
        if (mode < 0 && _server.hasHeader("X-mode")) mode = (int)_server.header("X-mode").toInt();
        if (mode < 0) mode = 2;
        long cid = 0;
        int p = body.indexOf("\"chipid\":");
        if (p >= 0) {
            int s = p + 9; bool hv = false;
            while (s < (int)body.length() && isDigit(body[s])) { cid = cid * 10 + (body[s] - '0'); s++; hv = true; }
            if (!hv) cid = -1;
        }
        if (cid <= 0 && _server.hasArg("chipid")) cid = _server.arg("chipid").toInt();  // body 丢失兜底
        if (cid <= 0 && _server.hasHeader("X-chipid")) cid = _server.header("X-chipid").toInt();
        if (cid <= 0) { _sendJson("{\"ok\":false,\"msg\":\"bad chipid\"}", 400); return; }
        bool ok = calibStart((uint32_t)cid, mode);
        _sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false,\"msg\":\"start failed(设备未识别或忙)\"}");
    });
    _server.on("/api/calib/stop", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        calibStop();
        _sendJson("{\"ok\":true}");
    });
    _server.on("/api/calib/status", HTTP_GET, [this](){
        if (!_needAuth()) return;
        const CalibStatus& s = calibStatus();
        _sendJson(String("{\"running\":") + (s.running ? "true" : "false") +
                  ",\"mode\":" + String(s.mode) +
                  ",\"stage\":" + String(s.stage) +
                  ",\"iter\":" + String(s.iter) +
                  ",\"lo\":" + String(s.lo) +
                  ",\"hi\":" + String(s.hi) +
                  ",\"v\":" + String(s.v) +
                  ",\"w\":" + String(s.w) +
                  ",\"limit\":" + (s.limitSeen ? "true" : "false") +
                  ",\"msg\":\"" + _escJson(s.msg) + "\"}");
        calibAck();   /* 一次性消息：本次返回后清掉，刷新/下次轮询不再重复提示 */
    });

    /* NVS 环形事件日志(电源故障/OTA/启动): 最近 32 条, 掉电保留 */
    _server.on("/api/evlog", HTTP_GET, [this](){
        if (!_needAuth()) return;
        _sendJson(evlogJson());
    });

    /* 485 总线电源开关 (GPIO->MOS/SGM2593AD): GET 读状态 / POST 切换(需解锁) */
    _server.on("/api/power", HTTP_GET, [this](){
        if (!_needAuth()) return;
        _sendJson(String("{\"on\":") + (busPowerIsOn() ? "1" : "0") +
                  ",\"fault\":" + (busPowerFault() ? "1" : "0") +
                  ",\"faultCnt\":" + String(busPowerFaultCount()) + "}");
    });
    _server.on("/api/power", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;            // 关/开电源=写操作, 需先解锁
        String body = _server.arg("plain");
        int on = jsonInt(body, "on", -1);
        if (on < 0 && _server.hasArg("on")) on = _server.arg("on").toInt();  // body 丢失兜底
        if (on < 0 && _server.hasHeader("X-on")) on = _server.header("X-on").toInt();
        if (on < 0 || on > 1) { _sendJson("{\"ok\":false,\"msg\":\"bad on\"}", 400); return; }
        busPowerSet(on == 1);
        _sendJson(String("{\"ok\":true,\"on\":") + (busPowerIsOn() ? "1" : "0") +
                  ",\"fault\":" + (busPowerFault() ? "1" : "0") + "}");
    });

    /* 蜂鸣器: GET 读开关/音量 / POST 设置(纯设置, 不涉及飞达安全, 无需解锁) */
    _server.on("/api/buzzer", HTTP_GET, [this](){
        if (!_needAuth()) return;
        _sendJson(String("{\"on\":") + (buzzerIsOn() ? "1" : "0") +
                  ",\"vol\":" + String(buzzerVol()) + "}");
    });
    _server.on("/api/buzzer", HTTP_POST, [this](){
        if (!_needAuth()) return;
        /* 蜂鸣器是纯本地提示设置，不涉及飞达动作，不要求解锁；NVS 持久化。 */
        String body = _server.arg("plain");
        int on = jsonInt(body, "on", -1);
        int vol = jsonInt(body, "vol", -1);
        if (on < 0 && _server.hasArg("on")) on = _server.arg("on").toInt();
        if (vol < 0 && _server.hasArg("vol")) vol = _server.arg("vol").toInt();
        if (on < 0 && _server.hasHeader("X-on")) on = _server.header("X-on").toInt();
        if (vol < 0 && _server.hasHeader("X-vol")) vol = _server.header("X-vol").toInt();
        if (on >= 0 && on <= 1) buzzerSetOn(on == 1);
        if (vol >= 0 && vol <= 100) buzzerSetVol(vol);
        _sendJson(String("{\"ok\":true,\"on\":") + (buzzerIsOn() ? "1" : "0") +
                  ",\"vol\":" + String(buzzerVol()) + "}");
    });

    /* 语言包: 纯静态资源, 登录页也要用 → 免登录.
     * 数据源 data/lang/<code>.txt, 格式 key=value, 见 docs/串口屏界面设计.md 第 16 节 */
    _server.on("/api/lang", HTTP_GET, [this](){
        String code = _server.arg("code");
        code.trim(); code.toLowerCase();
        if (!code.length()) code = "zh";
        bool ok = (code.length() <= 8);
        for (size_t i = 0; ok && i < code.length(); i++) {
            char c = code[i];
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) ok = false;
        }
        if (!ok) { _server.send(400, "text/plain; charset=utf-8", "bad code"); return; }
        File f = LittleFS.open(String("/lang/") + code + ".txt", "r");
        if (!f) { _server.send(404, "text/plain; charset=utf-8", "not found"); return; }
        _server.sendHeader("Cache-Control", "max-age=3600");
        _server.streamFile(f, "text/plain; charset=utf-8");
        f.close();
    });

    /* 485 收发实时日志: 只读信息, 免登录(现场调试必须始终可用, 不受登录/会话过期影响) */
    _server.on("/api/log", HTTP_GET, [this](){
        if (!_needAuth()) return;
        uint32_t since = (uint32_t)_server.arg("since").toInt();
        uint32_t latest = 0;
        String j = logJson(since, latest);
        _sendJson("{\"ok\":true,\"latest\":" + String(latest) + ",\"items\":" + j + "}");
    });
    /* 日志最新序号(轻量, 1 个数字): 送料等待应答前只取游标, 不再整包拉 200 条日志(审查 P2#10) */
    _server.on("/api/log/seq", HTTP_GET, [this](){
        if (!_needAuth()) return;
        _sendJson("{\"ok\":true,\"latest\":" + String(logNextSeq()) + "}");
    });
    _server.on("/api/log/clear", HTTP_POST, [this](){
        if (!_needAuth()) return;
        /* 日志清空不是配置写操作；登录用户即可清，否则详情页清空常被 403 拒绝。 */
        logClear();
        _sendJson("{\"ok\":true}");
    });

    /* 终端: 发送原始 M-code 行(如 "M610 S1" / "M602 N5")
     *   - M9xx 系统命令本地执行并回显
     *   - 其余命令解析后发 485(回复进 /api/log 环形日志)
     *   - 危险命令(M799恢复出厂/M788 ISP复位/M903电源/M905清零/无定位广播写)需管理密码 */
    _server.on("/api/term", HTTP_POST, [this](){
        String body = _server.arg("plain");
        String cmd, pw;
        int p = body.indexOf("\"cmd\":\"");
        if (p >= 0) {
            int s = p + 7, e = body.indexOf('"', s);
            if (e > s) cmd = body.substring(s, e);
        }
        p = body.indexOf("\"password\":\"");
        if (p >= 0) {
            int s = p + 12, e = body.indexOf('"', s);
            if (e > s) pw = body.substring(s, e);
        }
        /* body 丢失/被代理 chunked 时, 前端 api() 已把 cmd 镜像到 query、password 放请求头 */
        if (cmd.length() == 0) cmd = _server.header("X-Cmd");   /* JSON body 可能被丢 */
        if (cmd.length() == 0) cmd = _server.arg("cmd");
        if (pw.length() == 0) pw = _server.header("X-Admin-Pass");
        if (pw.length() == 0) pw = _server.arg("password");
        cmd.trim();
        cmd.toUpperCase();          /* 工业 M-code 统一大写，终端允许小写输入 */
        if (cmd.length() == 0) { _sendJson("{\"ok\":false,\"msg\":\"empty cmd\"}", 400); return; }
        if (cmd.indexOf("N99") >= 0) {
            _sendJson("{\"ok\":false,\"msg\":\"N99 is shared; use P<chipid>\"}", 400);
            return;
        }

        // 提取 M 代码数字
        int q = cmd.indexOf('M');
        if (q < 0) q = cmd.indexOf('m');
        if (q < 0) { _sendJson("{\"ok\":false,\"msg\":\"bad mcode\"}", 400); return; }
        int code = 0; q++;
        while (q < (int)cmd.length() && isDigit(cmd[q])) { code = code * 10 + (cmd[q] - '0'); q++; }
        if (code <= 0) { _sendJson("{\"ok\":false,\"msg\":\"bad mcode\"}", 400); return; }

        if (_mgr && _mgr->productionLocked() &&
            (code == MCMD_ADVANCE || code == MCMD_RETRACT || code == MCMD_SERVO ||
             code == MCMD_ENABLE || code == MCMD_QUERY)) {
            _sendJson("{\"ok\":false,\"msg\":\"production lock\"}", 409);
            return;
        }

        // 只读/查询命令免登录(现场调试可用); 写命令需登录
        bool readOnly = (code == MCMD_QUERY || code == MCMD_BLINK || code == MCMD_BLINK_OFF ||
                         code == 900 || code == 901 || code == 902 || code == 904 || code == 906);
        if (!readOnly && !_needAuth()) return;
        /* 写命令纳入解锁联锁(审查 P1#6/4.3: 之前 /api/term 完全绕过解锁, 会驱动舵机的
         * M600/M610/M620 等可被任何人直发)。只读/查询(M602/611/612/M9xx 只读)不受影响。 */
        if (!readOnly && code != MCMD_ISP_RESET && !_needUnlock()) return;

        // 危险判定: 恢复出厂 / ISP 复位 / 电源开关 / 清零 / 无 N/P 定位的广播写
        bool hasN = cmd.indexOf('N') >= 0, hasP = cmd.indexOf('P') >= 0;
        bool danger = (code == MCMD_FACTORY_RESET || code == MCMD_ISP_RESET ||
                       code == 903 || code == 905 ||
                       (!hasN && !hasP && (code == 600 || code == 601 || code == 603 ||
                                           code == 610 || code == 620)));
        if (danger && !credsAdminPwOk(pw)) {
            _sendJson("{\"ok\":false,\"msg\":\"admin required\"}", 403);
            return;
        }
        /* 操作员会话(登录开启时): 只允许只读/查询命令 */
        if (_authEnabled && _authRole != "admin") {
            bool readOnly = (code == MCMD_QUERY || code == MCMD_BLINK || code == MCMD_BLINK_OFF ||
                             code == 900 || code == 901 || code == 902 || code == 904 || code == 906);
            if (!readOnly) {
                _sendJson("{\"ok\":false,\"msg\":\"read only operator\"}", 403);
                return;
            }
        }

        // 本地系统命令(M9xx): 直接执行并回显
        String reply;
        if (mcodeHandleSystem(cmd, reply)) {
            reply.trim();
            String esc; for (unsigned k = 0; k < reply.length(); k++) {
                char c = reply[k];
                if (c == '"' || c == '\\') { esc += '\\'; esc += c; }
                else esc += c;
            }
            _sendJson("{\"ok\":true,\"reply\":\"" + esc + "\"}");
            return;
        }

        // 485 命令: 解析重建后发送, 回复自动进 /api/log
        MCmdMsg m;
        if (!mcodeParse(cmd, m)) { _sendJson("{\"ok\":false,\"msg\":\"cannot parse\"}", 400); return; }

        /* M600 是长动作(推料要几个 settle 才回 ok)，不能用 g_bus.send 的
         * 80ms 超时来判断成败：命令已经发出、舵机已经动作，却会报 "bus busy"。
         * 统一走 FeederManager 队列，按 ChipID 定位并计数。 */
        if (m.code == MCMD_ADVANCE) {
            if (m.hasP && m.chipid && !m.hasN && _mgr) {
                int ti = _mgr->tempIdxForChip(m.chipid);
                if (ti >= 0) { m.addr = 99; m.hasN = true; }
                else {
                    int a = _mgr->addrForChip(m.chipid);
                    if (a >= 0) { m.addr = (uint8_t)a; m.hasN = true; }
                }
            }
            if (!m.hasF) { m.f = 4; m.hasF = true; }  /* 终端没写 F 时按默认 4mm */
            bool ok = _mgr && _mgr->command(m, nullptr);
            String s = mcodeBuildMsg(m);
            _sendJson(ok ? "{\"ok\":true,\"sent\":\"" + s + "\"}"
                         : "{\"ok\":false,\"msg\":\"queue busy\"}");
            return;
        }

        String s = mcodeBuildMsg(m);
        bool ok = g_bus.send(s, BUS_CMD_TIMEOUT, [](const String&){});
        _sendJson(ok ? "{\"ok\":true,\"sent\":\"" + s + "\"}"
                     : "{\"ok\":false,\"msg\":\"bus busy\"}");
    });

    /* WiFi API */
    _server.on("/api/wifi/status", HTTP_GET, [this](){
        if (!_needAuth()) return;
        _sendJson(wifiStatusJson());
    });

    _server.on("/api/wifi/scan", HTTP_POST, [this](){
        if (!_needAuth()) return;
        _sendJson(wifiScanJson());
    });

    _server.on("/api/wifi/connect", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;   // WiFi 配置属于管理操作
        String body = _server.arg("plain");
        String ssid, pass;
        int p = body.indexOf("\"ssid\":\"");
        if (p >= 0) {
            int s = p + 8, e = body.indexOf('"', s);
            if (e >= 0) ssid = body.substring(s, e);
        }
        p = body.indexOf("\"pass\":\"");
        if (p >= 0) {
            int s = p + 8, e = body.indexOf('"', s);
            if (e >= 0) pass = body.substring(s, e);
        }
        bool ok = wifiSaveAndConnect(ssid, pass);
        _sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false,\"msg\":\"connect failed\"}");
    });

    /* 删除已保存网络(列表): 删除存储, 下次重启不再尝试; 不影响当前连接 */
    _server.on("/api/wifi/delete", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;   // WiFi 配置属于管理操作
        String body = _server.arg("plain");
        String ssid;
        int p = body.indexOf("\"ssid\":\"");
        if (p >= 0) {
            int s = p + 8, e = body.indexOf('"', s);
            if (e >= 0) ssid = body.substring(s, e);
        }
        bool ok = wifiDelete(ssid);
        _sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false,\"msg\":\"not found\"}");
    });

    /* 切换到 AP 模式: 写标志 + 重启 */
    _server.on("/api/wifi/apmode", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;   // WiFi 配置属于管理操作
        String body = _server.arg("plain");
        if (body.indexOf("\"on\":true") >= 0) {
            wifiSetApMode(true);       // 写 /apmode 标志
            _server.send(200, "text/plain", "ok");   // 先回包再重启
            delay(200);
            ESP.restart();
            return;
        }
        wifiSetApMode(false);          // 关闭 AP 模式标志(回到 STA)
        _sendJson("{\"ok\":true}");
    });

    /* OTA API */
    _server.on("/api/ota/check", HTTP_GET, [this](){
        if (!_needAuth()) return;
        _sendJson(otaCheckGitHub());
    });

    _server.on("/api/ota/progress", HTTP_GET, [this](){
        if (!_needAuth()) return;
        _sendJson(otaProgressJson());
    });

    _server.on("/api/ota/esp32", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;            // 刷 ESP32 固件, 需解锁
        bool ok = otaStartEsp32();
        _sendJson(ok ? "{\"ok\":true,\"msg\":\"started\"}" : "{\"ok\":false,\"msg\":\"busy\"}");
    });

    /* ESP32 本地 .bin: 分片写 OTA 分区，避免 body/multipart。 */
    _server.on("/api/ota/esp32/local/begin", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        int total = _server.header("X-Ota-Total").toInt();
        if (total < 8) { _sendJson("{\"ok\":false,\"msg\":\"bad size\"}", 400); return; }
        String tv = _server.header("X-Target-Ver");
        bool force = _server.header("X-Force") == "1";
        if (tv.length()) {
            String cur = FW_VER_STR;
            if (cur.length() == 10) cur = cur.substring(2);
            if (!force && (long)tv.toInt() <= (long)cur.toInt()) {
                _sendJson("{\"ok\":false,\"msg\":\"target version not newer; force required\"}", 409); return;
            }
        }
        bool ok = otaLocalBegin((size_t)total);
        _sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false,\"msg\":\"busy or begin failed\"}");
    });
    _server.on("/api/ota/esp32/local/chunk", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        String hex = _server.header("X-Chunk");
        size_t off = (size_t)_server.header("X-Ota-Off").toInt();
        if (hex.length() == 0 || (hex.length() & 1)) {
            _sendJson("{\"ok\":false,\"msg\":\"bad chunk\"}", 400); return;
        }
        size_t written = otaLocalOffset();
        if (off < written) { _sendJson("{\"ok\":true,\"offset\":" + String((unsigned)written) + ",\"dup\":true}"); return; }
        if (off != written) { _sendJson("{\"ok\":false,\"msg\":\"offset mismatch\",\"offset\":" + String((unsigned)written) + "}", 409); return; }
        uint8_t buf[2048];
        size_t n = hex.length() / 2;
        if (n > sizeof(buf)) { _sendJson("{\"ok\":false,\"msg\":\"chunk too large\"}", 400); return; }
        for (size_t i = 0; i < n; i++) {
            char hc = hex[i * 2], lc = hex[i * 2 + 1];
            int hi = (hc >= '0' && hc <= '9') ? hc - '0' :
                     (hc >= 'A' && hc <= 'F') ? hc - 'A' + 10 :
                     (hc >= 'a' && hc <= 'f') ? hc - 'a' + 10 : -1;
            int lo = (lc >= '0' && lc <= '9') ? lc - '0' :
                     (lc >= 'A' && lc <= 'F') ? lc - 'A' + 10 :
                     (lc >= 'a' && lc <= 'f') ? lc - 'a' + 10 : -1;
            if (hi < 0 || lo < 0) { _sendJson("{\"ok\":false,\"msg\":\"bad hex\"}", 400); return; }
            buf[i] = (uint8_t)((hi << 4) | lo);
        }
        bool ok = otaLocalWrite(buf, n);
        _server.sendHeader("Connection", "close");
        _sendJson(ok ? ("{\"ok\":true,\"offset\":" + String((unsigned)otaLocalOffset()) + "}")
                     : "{\"ok\":false,\"msg\":\"write failed\"}");
    });
    _server.on("/api/ota/esp32/local/end", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        bool ok = otaLocalEnd();
        _sendJson(ok ? "{\"ok\":true,\"msg\":\"done\"}" : "{\"ok\":false,\"msg\":\"end failed\"}");
    });
    _server.on("/api/ota/esp32/local/abort", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        otaLocalAbort();
        _sendJson("{\"ok\":true}");
    });

    /* 页面 FS 本地 .bin：同样分片写 U_SPIFFS 分区。 */
    _server.on("/api/ota/fs/local/begin", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        int total = _server.header("X-Ota-Total").toInt();
        if (total < 8) { _sendJson("{\"ok\":false,\"msg\":\"bad size\"}", 400); return; }
        String tf = _server.header("X-Target-Fs");
        bool force = _server.header("X-Force") == "1";
        if (tf.length() && !force && tf == _fsVer()) {
            _sendJson("{\"ok\":false,\"msg\":\"same FS version; force required\"}", 409); return;
        }
        bool ok = otaLocalBeginFs((size_t)total);
        _sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false,\"msg\":\"busy or begin failed\"}");
    });
    _server.on("/api/ota/fs/local/chunk", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        String hex = _server.header("X-Chunk");
        size_t off = (size_t)_server.header("X-Ota-Off").toInt();
        if (hex.length() == 0 || (hex.length() & 1)) {
            _sendJson("{\"ok\":false,\"msg\":\"bad chunk\"}", 400); return;
        }
        size_t written = otaLocalOffset();
        if (off < written) { _sendJson("{\"ok\":true,\"offset\":" + String((unsigned)written) + ",\"dup\":true}"); return; }
        if (off != written) { _sendJson("{\"ok\":false,\"msg\":\"offset mismatch\",\"offset\":" + String((unsigned)written) + "}", 409); return; }
        uint8_t buf[2048];
        size_t n = hex.length() / 2;
        if (n > sizeof(buf)) { _sendJson("{\"ok\":false,\"msg\":\"chunk too large\"}", 400); return; }
        for (size_t i = 0; i < n; i++) {
            char hc = hex[i * 2], lc = hex[i * 2 + 1];
            int hi = (hc >= '0' && hc <= '9') ? hc - '0' :
                     (hc >= 'A' && hc <= 'F') ? hc - 'A' + 10 :
                     (hc >= 'a' && hc <= 'f') ? hc - 'a' + 10 : -1;
            int lo = (lc >= '0' && lc <= '9') ? lc - '0' :
                     (lc >= 'A' && lc <= 'F') ? lc - 'A' + 10 :
                     (lc >= 'a' && lc <= 'f') ? lc - 'a' + 10 : -1;
            if (hi < 0 || lo < 0) { _sendJson("{\"ok\":false,\"msg\":\"bad hex\"}", 400); return; }
            buf[i] = (uint8_t)((hi << 4) | lo);
        }
        bool ok = otaLocalWrite(buf, n);
        _server.sendHeader("Connection", "close");
        _sendJson(ok ? ("{\"ok\":true,\"offset\":" + String((unsigned)otaLocalOffset()) + "}")
                     : "{\"ok\":false,\"msg\":\"write failed\"}");
    });
    _server.on("/api/ota/fs/local/end", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        bool ok = otaLocalEnd();
        _sendJson(ok ? "{\"ok\":true,\"msg\":\"done\"}" : "{\"ok\":false,\"msg\":\"end failed\"}");
    });
    _server.on("/api/ota/fs/local/abort", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;
        otaLocalAbort();
        _sendJson("{\"ok\":true}");
    });

    /* 页面(FS)升级: 下载 DDfs 资产写 LittleFS 分区 */
    _server.on("/api/ota/fs", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;            // 刷页面 FS, 需解锁
        bool ok = otaStartFS();
        _sendJson(ok ? "{\"ok\":true,\"msg\":\"started\"}" : "{\"ok\":false,\"msg\":\"busy\"}");
    });

    _server.on("/api/ota/device", HTTP_POST, [this](){
        if (!_needAuth()) return;
        if (!_needUnlock()) return;            // 设备烧写会 boot 飞达，需解锁
        int slot = -1;
        if (_server.hasArg("chipid") && _mgr) {                 // chipid 定位(地址可改)
            int a = _mgr->addrForChip((uint32_t)_server.arg("chipid").toInt());
            if (a < 0) { _sendJson("{\"ok\":false,\"msg\":\"device not found\"}", 404); return; }
            slot = a;
        } else {
            slot = _server.arg("addr").toInt();                 // 按地址
        }
        if (slot < 0 || slot >= FEEDER_COUNT) { _sendJson("{\"ok\":false}", 400); return; }
        bool ok = otaStartDevice(slot + 1);
        _sendJson(ok ? "{\"ok\":true,\"msg\":\"started\"}" : "{\"ok\":false,\"msg\":\"busy\"}");
    });

    _setupNotFound();
}

void WebServerApp::_setupNotFound() {
    _server.onNotFound([this](){
        String uri = _server.uri();
        int q = uri.indexOf('?');                // 页面带版本号 ?v=4, 剥掉再查文件
        if (q >= 0) uri = uri.substring(0, q);
        LOGD("[Web] REQ: %s\n", uri.c_str());   // 调试: 收到每个未注册路由的请求
        // 公开静态资源: /css/* /js/*
        if (uri.startsWith("/css/") || uri.startsWith("/js/")) {
            _server.sendHeader("Cache-Control", "public, max-age=86400");   // 1 天长缓存, 版本号控制更新
            File f = LittleFS.open(uri, "r");
            if (f) { LOGD("[Web] 200 STATIC: %s\n", uri.c_str()); _server.streamFile(f, _mime(uri)); f.close(); return; }
            LOGD("[Web] 404 STATIC: %s\n", uri.c_str());
        }
        _server.send(404, "text/plain", "404");
    });
}

















