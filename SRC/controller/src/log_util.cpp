#include "log_util.h"
#include "config.h"
#include "screen.h"
#include "log_ring.h"
#include <Preferences.h>

bool g_logEnabled = false;

/* 开关状态持久化(NVS "log"): 重启后保持上次设置, 便于常开调试看连接日志 */
void logInit() {
    Preferences p;
    /* 读写模式打开: 自动创建 "log" 命名空间, 避免首次启动 nvs_open NOT_FOUND 刷屏 */
    if (p.begin("log", false)) {
        g_logEnabled = p.getBool("en", false);
        p.end();
    }
}

void logSetEnabled(bool en) {
    g_logEnabled = en;
    Preferences p;
    if (p.begin("log", false)) {
        p.putBool("en", en);
        p.end();
    }
}
bool logIsEnabled() { return g_logEnabled; }

void logPrintf(const char* fmt, ...) {
    char tmp[192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);

    /* UART0 常开(屏幕若复用 UART0 则跳过, 避免裸文本混入屏幕指令流) */
#if SCREEN_ENABLED && SCREEN_BACKEND == SCREEN_BACKEND_UART0
    (void)tmp;
#else
    Serial.print(tmp);
#endif

    /* 附加通道随开关: UART1(外部主机口)日志转发 + Web 终端环形缓冲 + 串口屏 LOG 控件 */
    if (g_logEnabled) {
        Serial1.print(tmp);
        /* 环形缓冲(dir='D' 调试行): 供 Web 终端页(/api/log)实时查看 */
        logPush('D', tmp);
        if (SCREEN_ENABLED) screenLogLine(tmp);   /* 同时推送屏幕 LOG 控件 */
    }
}
