#ifndef EVLOG_H
#define EVLOG_H
#include <Arduino.h>

/* ---- NVS 环形事件日志 ----
 * 关键事件(电源故障/OTA/启动)存 NVS 命名空间 "evlog", 掉电保留。
 * 环形覆盖最旧条目; 低频事件下 NVS 磨损可忽略。
 * 查看: GET /api/evlog (Web), 详见 docs/总线电源与nFAULT说明.md */

#define EVLOG_MAX   32      /* 环形条数: 32 条, NVS 容量安全) */
#define EVLOG_STR   96      /* 单条最大字符数 */

void evlogInit();                             /* setup 调用: 读环形指针 */
void evlogAdd(const char* type, const char* fmt, ...);  /* 追加事件, type 如 "PWR"/"OTA"/"SYS" */
String evlogJson();                           /* 全部事件 JSON 数组(旧->新) */
#endif
