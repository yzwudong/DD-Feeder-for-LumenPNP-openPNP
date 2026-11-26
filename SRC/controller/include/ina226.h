#pragma once
#include <Arduino.h>

/* ============================================================
 * INA226 总线电压/电流检测（新板）
 *   I²C: SDA=GPIO22, SCL=GPIO23, 地址 0x40
 *   分流: 10mΩ, PGA ±40mV, CAL=5120 → 电流分辨率 0.1mA
 * 未检测到芯片时 inaRead() 返回 false, 调用方应降级显示。
 * ============================================================ */

void  inaInit();                    /* setup 调用一次, 探测失败不影响其他功能 */
bool  inaPresent();                 /* 芯片是否在线 */
bool  inaRead(float* volt, float* amp);   /* 读总线电压(V)与电流(A); 失败返回 false */
