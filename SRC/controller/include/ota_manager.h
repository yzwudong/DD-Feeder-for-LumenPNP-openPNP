#pragma once
#include <Arduino.h>
#include "feeder_bus.h"
#include "feeder_manager.h"

/* ============================================================
 * OTA 管理器（后台任务 + 进度上报）
 *  - 检测 GitHub 最新 release (api.github.com)
 *  - 升级 ESP32 自身：后台任务流式下载 firmware.bin -> Update
 *  - 升级 485 飞达：后台任务下载 feeder.hex(Intel HEX) -> STC8H ISP
 * 升级过程不阻塞 Web 服务器，进度经 /api/ota/progress 轮询。
 * GitHub 未认证 API 限速 60 次/小时，注意频率。
 * ============================================================ */

/* 检查 GitHub 最新版本，返回 JSON:
 * {"ok":true,"latest":"1.0.1","esp32_asset":"...","device_asset":"...",
 *  "esp32_url":"...","device_url":"...","need_esp32":true,"msg":"..."}  */
String otaCheckGitHub();

/* 启动 ESP32 自身升级(后台任务)。返回 true=已开始。 */
bool otaStartEsp32();

/* 启动页面(FS)升级(后台任务): 下载 DDfs 资产写 LittleFS 分区。返回 true=已开始。 */
bool otaStartFS();

/* 启动指定地址(addr)飞达升级(后台任务)。返回 true=已开始。 */
bool otaStartDevice(uint8_t addr);

/* ESP32 本地 .bin 分片升级：begin(total) -> write()* -> end()。 */
bool otaLocalBegin(size_t total);
bool otaLocalBeginFs(size_t total);
bool otaLocalWrite(const uint8_t* data, size_t len);
bool otaLocalEnd();
size_t otaLocalOffset();
void otaLocalAbort();

/* 是否有 OTA/ISP 任务进行中 */
bool otaBusy();

/* 进度 JSON:
 * {"state":"idle|downloading|flashing|done|error","kind":"esp32|fs|device",
 *  "progress":0..100,"msg":"..."} */
String otaProgressJson();

