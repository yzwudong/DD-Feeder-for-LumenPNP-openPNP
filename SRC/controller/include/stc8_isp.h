#pragma once
#include <Arduino.h>
#include <functional>
#include "feeder_bus.h"

/* ============================================================
 * STC8H1K17 飞达 ISP 在线升级（官方 STC8G/8H/8A 下载协议）
 *
 * 帧格式(发送):
 *   0x46 0xB9 0x6A 0x00 <len> <payload...> <sumH> <sumL> 0x16
 *   len = payload 长度 + 6
 *   sum = 0x6A + len + sum(payload)    (16bit)
 * 回复同构，payload[0] 为回显命令码。
 *
 * 流程: 0x7F 握手 -> 设波特率 -> 准备 -> 擦除 -> 分页下载
 * 数据位: 9 位 + 偶校验 (ESP32 用 SERIAL_8E1 自动生成偶校验位)
 * ============================================================ */

#define ISP_PAGE_SIZE   128          /* STC8 分页下载块大小 */

/* 执行整片 ISP 烧写。
 * 调用前：目标飞达已通过 M788 软复位进入引导(由调用方负责)。
 * 全程阻塞(数秒)。onProgress(0..100) 可选进度回调。成功返回 true。 */
bool stc8Flash(FeederBus* bus, const uint8_t* code, uint32_t size,
               uint32_t startBaud, uint32_t dlBaud,
               std::function<void(int)> onProgress = nullptr);
