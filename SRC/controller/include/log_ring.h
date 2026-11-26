#pragma once
#include <Arduino.h>

/* ============================================================
 * 485 总线收发日志（环形缓冲）
 * 记录网关在 UART2(485 总线) 上发送(TX) 与接收(RX) 的完整行，
 * 供 Web 实时日志页轮询显示。
 * 条目带全局递增 seq，前端用 ?since= 增量拉取。
 * ============================================================ */

#define LOG_RING_CAP 200      /* 最多保留 200 条 */

void logPush(char dir, const String& data);   /* dir: 'T'=发送 'R'=接收 */
uint32_t logNextSeq();
void logClear();                        /* 当前最新 seq */
String logJson(uint32_t sinceSeq, uint32_t& latestSeq);
