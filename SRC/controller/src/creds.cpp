#include "creds.h"
#include "config.h"
#include "log_util.h"
#include <Preferences.h>

static const char* NS = "creds";

void credsInit() {
    Preferences p;
    if (!p.begin(NS, false)) return;
    if (!p.isKey("admin_pw")) p.putString("admin_pw", WEB_PASSWORD);  /* 首次: 默认密码 */
    if (!p.isKey("op_user"))  p.putString("op_user", "");             /* 默认无操作员 */
    if (!p.isKey("op_pw"))    p.putString("op_pw", "");
    p.end();
    LOGD("[Creds] init done (admin default pw %s)\n", credsAdminPwOk(WEB_PASSWORD) ? "written" : "changed");
}

String credsAdminUser() { return "admin"; }

/* 调试用: 读 NVS 中存储的管理密码; 命名空间读不到/键不存在 -> 返回空 */
String credsStoredAdminPw() {
    Preferences p;
    String v;
    if (p.begin(NS, true)) { v = p.getString("admin_pw", ""); p.end(); }
    return v;
}

/* 调试用: 存储密码是否为默认值(未改过密码) */
bool credsIsDefaultPw() {
    String s = credsStoredAdminPw();
    return s.length() > 0 && s == WEB_PASSWORD;
}

static String _get(const char* key, const char* def) {
    Preferences p;
    String v;
    if (p.begin(NS, true)) { v = p.getString(key, def); p.end(); }
    return v;
}

static void _put(const char* key, const String& v) {
    Preferences p;
    if (p.begin(NS, false)) { p.putString(key, v); p.end(); }
}

bool credsCheckLogin(const String& user, const String& pw, String& role) {
    if (user == credsAdminUser() && pw == _get("admin_pw", WEB_PASSWORD)) {
        role = "admin"; return true;
    }
    String opU = _get("op_user", "");
    if (opU.length() > 0 && user == opU && pw == _get("op_pw", "")) {
        role = "operator"; return true;
    }
    return false;
}

bool credsAdminPwOk(const String& pw) {
    return pw == _get("admin_pw", WEB_PASSWORD);
}

bool credsSetAdminPw(const String& oldPw, const String& fresh) {
    if (!credsAdminPwOk(oldPw)) return false;
    if (fresh.length() < 4) return false;              /* 最短 4 位 */
    _put("admin_pw", fresh);
    LOGD("[Creds] admin password changed\n");
    return true;
}

bool credsSetOperator(const String& user, const String& pw) {
    if (user.length() == 0) {                          /* 清除操作员 */
        _put("op_user", ""); _put("op_pw", "");
        LOGD("[Creds] operator account cleared\n");
        return true;
    }
    if (user.length() < 2 || pw.length() < 4) return false;
    if (user == credsAdminUser()) return false;        /* 不能占用 admin */
    _put("op_user", user); _put("op_pw", pw);
    LOGD("[Creds] operator account set: %s\n", user.c_str());
    return true;
}

String credsOpUser() { return _get("op_user", ""); }
