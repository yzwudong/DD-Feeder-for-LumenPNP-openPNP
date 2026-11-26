#pragma once
#include <Arduino.h>

/* ============================================================
 * 串口屏模块 (淘晶驰 TJC / USART HMI)
 * 屏幕只做收发 UI，所有逻辑在 ESP32：
 *   屏 -> ESP32:  M 代码行(直通 485) 或 #命令(#SCAN/#STATUS/#PWD/#PING)
 *   ESP32 -> 屏:  淘晶驰原生指令( t0.txt=".." / n0.val=5 ) 更新控件
 * 双后端编译期切换(config.h SCREEN_BACKEND):
 *   SCREEN_BACKEND_SOFT  = 软串口(默认, 任意空闲 GPIO)
 *   SCREEN_BACKEND_UART0 = 复用 UART0(需硬件把 RX0 切到屏幕, 与 CH340 互斥)
 * 协议详见 docs/screen协议.md
 * ============================================================ */

void screenBegin();
void screenLoop();

/* 向屏幕发一条指令行(自动补 \r\n) */
void screenPrint(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

/* 淘晶驰控件驱动: 文本/数值 */
void screenSetText(const char* ctrl, const String& v);   /* t0.txt=".." */
void screenSetVal(const char* ctrl, long v);             /* n0.val=123 */

/* 立即推送一次汇总状态(周期推送在 screenLoop 内自动做) */
void screenPushStatus();

/* 调试日志行 -> 屏幕 LOG 控件(SCREEN_CTRL_LOGF, 自动转义/截断; 屏工程无该控件则无操作) */
void screenLogLine(const char* line);
