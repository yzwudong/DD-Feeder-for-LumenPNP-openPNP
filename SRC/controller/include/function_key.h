#pragma once
#include <Arduino.h>

/* 多功能维护键 (GPIO14, 低有效)
 *   短按  : 485 总线电源 ON/OFF (仅总线空闲、非生产/OTA/校准)
 *   双击  : 重新扫描飞达
 *   长按  : 不动作 (Factory Reset 仍用 GPIO34)
 */
void functionKeyInit();
void functionKeyLoop();
