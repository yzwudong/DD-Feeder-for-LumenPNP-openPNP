#include "evlog.h"
#include "time_util.h"
#include <Preferences.h>
#include <stdarg.h>

static Preferences g_pref;
static uint16_t    g_idx = 0;      /* 下一写入槽位 (环形) */

void evlogInit() {
    if (g_pref.begin("evlog", false)) {
        g_idx = g_pref.getUShort("idx", 0);
        g_pref.end();
    }
}

void evlogAdd(const char* type, const char* fmt, ...) {
    String ts = wallClockOrUptime();            /* 时间戳: 见 time_util.h */
    char line[EVLOG_STR];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    line[EVLOG_STR - 1] = '\0';

    char key[8];
    snprintf(key, sizeof(key), "e%d", g_idx % EVLOG_MAX);
    char entry[EVLOG_STR + 32];
    snprintf(entry, sizeof(entry), "%s %s %s", ts.c_str(), type, line);

    if (g_pref.begin("evlog", false)) {
        g_pref.putString(key, entry);
        g_idx++;
        g_pref.putUShort("idx", g_idx);
        g_pref.end();
    }
}

String evlogJson() {
    String out = "[";
    uint16_t n = g_idx > EVLOG_MAX ? EVLOG_MAX : g_idx;   /* 已写入条数 */
    uint16_t start = g_idx >= EVLOG_MAX ? (uint16_t)(g_idx % EVLOG_MAX) : 0;
    bool first = true;
    for (uint16_t i = 0; i < n; i++) {
        char key[8];
        snprintf(key, sizeof(key), "e%d", (start + i) % EVLOG_MAX);
        String v;
        if (g_pref.begin("evlog", true)) {
            v = g_pref.getString(key, "");
            g_pref.end();
        }
        if (v.length() == 0) continue;      /* 未写入的槽位 */
        if (!first) out += ",";
        first = false;
        out += "\"" + v + "\"";
    }
    out += "]";
    return out;
}
