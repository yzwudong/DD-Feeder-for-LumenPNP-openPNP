#pragma once
#include <Arduino.h>

/* ============================================================
 * 485 总线电源开关 (GPIO -> MOS 管, GPIO 只发信号)
 *   高电平 = 开, 低电平 = 关 (config.h BUS_PWR_ON_LEVEL 可改)
 *   上电默认开(BUS_PWR_DEFAULT_ON)
 *   关电源前等总线空闲; 开电源后延时稳定并自动触发一次扫描
 * Web:  GET/POST /api/power   (POST 需解锁)
 * 屏幕: #PWR 1 / #PWR 0
 * ============================================================ */

void busPowerInit();            /* setup 调用: 配置引脚 + 应用默认状态 */
bool busPowerIsOn();            /* 当前电源状态 */
bool busPowerSet(bool on);      /* 开关电源; 返回是否执行成功 */

/* ---- nFAULT 故障检测 (SGM2593AD, config.h BUS_FAULT_PIN) ----
 * busPowerLoop() 在 main loop 周期调用: 40ms 去抖 + 可选自动 EN 复位状态机 */
void      busPowerLoop();       /* 非阻塞; 检测故障/自动复位/超过上限保持关断 */
bool      busPowerFault();      /* 当前是否处于故障(去抖后, 低有效) */
uint16_t  busPowerFaultCount(); /* 累计故障次数(掉电清零) */
