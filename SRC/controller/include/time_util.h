#pragma once
#include <Arduino.h>
#include <time.h>

/* ============================================================
 * 日志时间戳统一口径（events 日志 / 终端环形日志共用）
 *   有网(NTP 已同步) -> 实时时间 "YYYY-MM-DD HH:MM:SS"（configTime 东八区, 见 wifi_utils.cpp）
 *   无网(AP/未同步) -> "T+<秒>" 运行时长, 前端可读
 * ============================================================ */
static inline String wallClockOrUptime() {
    time_t t = time(nullptr);
    if (t > 1600000000) {                       /* 2020-09 之后 = NTP 已同步 */
        struct tm* tm = localtime(&t);
        char b[24];
        snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d:%02d",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
        return String(b);
    }
    return "T+" + String((unsigned long)(millis() / 1000)) + "s";
}
