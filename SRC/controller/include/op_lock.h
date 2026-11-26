#pragma once
#include <Arduino.h>

/* ============================================================
 * 单台操作互斥（屏幕 / 网页 / 上位机 三方）
 *
 * 背景: 飞达 M602 应答没有 busy 字段, 无法靠硬件判断"谁在动这台",
 *       只能软件加锁. 锁是"认领窗口"模型:
 *       发命令前认领该地址, 别人在窗口内再认领会失败, 窗口到期自动释放。
 *
 * 规则:
 *   1) 谁先认领谁持有, 另一方拿到 OP_BUSY 并提示持有者
 *   2) 窗口 3 秒; 连续操作会续期
 *   3) 校准是全局独占, 走 calibSource() 单独判断, 不占本表
 * ============================================================ */

enum OpOwner : uint8_t {
    OP_FREE = 0,
    OP_SCREEN = 1,   /* 屏幕 */
    OP_WEB = 2,      /* 网页 */
    OP_HOST = 3,     /* UART1 上位机 */
};

#define OP_LOCK_MS 3000UL   /* 认领窗口: 3 秒无新操作自动释放 */

void        opLockInit();
bool        opLockTake(uint8_t addr, OpOwner owner);   /* false = 被别人占用 */
void        opLockRelease(uint8_t addr, OpOwner owner);
OpOwner     opLockOwner(uint8_t addr);                 /* 当前持有者(过期返回 FREE) */
const char* opOwnerName(OpOwner o);                    /* "屏幕"/"网页"/"主机"/"" */
