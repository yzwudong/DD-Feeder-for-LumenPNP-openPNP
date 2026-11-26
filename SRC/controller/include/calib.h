#pragma once
#include <Arduino.h>
#include <stdint.h>

/* ============================================================
 * 舵机限位校准（ESP32 侧实现，下位机零改动）
 *
 * 原理: 限位开关直连 ESP32 GPIO(LOW=触发)。
 *       用 M603 A<角度> 逐步逼近限位: 0° 从 30° 递减 / 90° 从 60° 递增,
 *       每步 1°, 触发后反向回退确认, 最后一次触发档 = "刚好到位"角度。
 *       用校准开始时 M602 读回的 V0/W0 线性换算:
 *         pulse(a) = V0 + (W0-V0)*a/180
 *       0° 触发档 -> V_new; 90° 触发档 -> W_new
 *       成功后一次性 M620 全参数(A B C F U V_new W_new X T P<chipid>) 写回。
 *
 * 关键: 校准过程只发 M603 直驱舵机, 不写任何配置 —— EEPROM 零磨损,
 *       过程/失败均不保存, 仅成功落盘。
 * 命令一律带 P<chipid> 定位(地址可改可重复)。
 * ============================================================ */
void calibInit();
bool calibStart(uint32_t chipid, int mode);   /* mode: 0=仅0° 1=仅90° 2=全部 */
void calibStop();                              /* 终止校准，不写回配置 */
void calibAck();                               /* 状态页读取后清除一次性消息 */
bool calibActive();
void calibLoop();   /* 主循环驱动(延时/超时/重试), 高频调用 */

struct CalibStatus {
    bool running = false;
    int  mode = -1;              /* 0/1/2 */
    uint32_t chipid = 0;
    int  stage = -1;             /* 0:0°逼近 1:90°逼近 3:完成 -1:失败/未开始 */
    int  iter = 0;
    long lo = 0, hi = 0;         /* 当前探测角度(°) / 最后一次触发角度(°) */
    long v = -1, w = -1;         /* 结果(μs) */
    bool limitSeen = false;      /* 限位为 GPIO 直读, 恒可用 */
    String msg;
};
const CalibStatus& calibStatus();

