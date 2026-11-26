#pragma once
#include <Arduino.h>

/* ============================================================
 * 恢复出厂设置 (三种触发: 硬件长按 / Web 按钮 / M907)
 * 行为: 清空 NVS 全部配置 + 删除 LittleFS /apmode 标志 -> 重启,
 *       设备回到出厂默认(AP 热点 + 默认管理密码 + 蜂鸣器默认)。
 * 说明见 docs/恢复出厂设置说明.md
 * ============================================================ */

void factoryResetInit();   /* 初始化按键 GPIO(INPUT_PULLUP) */
void factoryResetLoop();   /* 长按检测状态机(需在 loop 高频调用) */
void doFactoryReset();     /* 立即执行: 清 NVS + 删 /apmode + 重启 */
void factoryResetRequest();/* 请求稍后执行(让 M907/Web 的 ok 回复先发出) */
