#pragma once
#include <Arduino.h>

/* 运行时日志开关(非宏编译): 默认关闭, 状态存 NVS("log" 命名空间), **重启保持**。
 * 开启方式: UART0 控制台 / 外部主机透传口 / 串口屏 / Web 终端 发 "M906 S1"; 关闭 "M906 S0";
 *           串口屏可发 "#LOG 1|0"。
 * UART0 是常开控制台: LOGD 恒输出到 Serial(屏幕复用 UART0 时除外, 由屏 LOG 控件显示)。
 * 开关只控制附加通道: UART1(外部主机口)日志转发 + Web 终端环形缓冲(/api/log) + 串口屏 LOG 控件。
 * 注意: 透传/控制台的命令与应答回显是功能性输出, 不受本开关控制。 */

extern bool g_logEnabled;

void logInit();                  /* setup 调用: 从 NVS 读开关状态 */
void logSetEnabled(bool en);     /* 写开关 + 持久化 */
bool logIsEnabled();

/* 写一行调试日志(带换行)。内部输出 UART0(常开); 附加通道随 g_logEnabled。 */
void logPrintf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#define LOGD(...) logPrintf(__VA_ARGS__)
