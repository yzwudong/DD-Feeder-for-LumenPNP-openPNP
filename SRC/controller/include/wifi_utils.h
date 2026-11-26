#pragma once
#include <Arduino.h>

/* ============================================================
 * WiFi 管理：NVS 保存/读取 + 连接 + AP 回退 + 扫描
 * 供 main 初始化与 web 的 WiFi 设置页调用。
 * ============================================================ */

/* 上电初始化: 读 NVS -> 连默认/已存网络 -> 失败开热点 */
void wifiInit();

/* AP 模式: 读/写 LittleFS 标志(/apmode)。true=以热点启动, 不连 STA */
bool wifiApModeRequested();
void wifiSetApMode(bool on);

/* 保存并切换到指定网络。返回 true=已开始连接。
 * 连接失败时保持/重新打开热点。 */
bool wifiSaveAndConnect(const String& ssid, const String& pass);

/* 从已保存列表删除一个 SSID(连密码)。返回是否找到并删除 */
bool wifiDelete(const String& ssid);

/* 当前状态 JSON: {sta:{ssid,ip,rssi},ap:{on,ssid,ip},saved:[{ssid}...]} */
String wifiStatusJson();

/* 扫描结果 JSON: {networks:[{ssid,rssi,secure}]} */
String wifiScanJson();
