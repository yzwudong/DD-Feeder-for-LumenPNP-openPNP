#pragma once
#include <Arduino.h>

/* ============================================================
 * 飞达固件在线升级（Web 独立升级页 / GitHub / 本地上传）
 *  - 包版本：hex 镜像内扫描 "DDFW:<yymm>.<ddbn>" 静态标识（协议 §7 第 11 条）
 *  - 板上版本：M602 单点应答 FW: 字段
 *  - 比对：两段整数数值序（yymm 主序、ddbn 次序），禁止字符串字典序
 *  - 决策：包>板 升级 / =板 跳过 / <板 默认拒绝(force 才放行) / 板上读不到按 > 处理
 *  - 烧录：M788 进 ISP → stc8Flash(STC8 协议) → 复位后 M602 回校 FW 一致
 * 升级在独立 FreeRTOS 任务中执行，不阻塞 WebServer；进度经 /api/feeder/up/status 轮询。
 * ============================================================ */

/* 上传并校验本地 hex 包（解析/校验和/地址越界 + DDFW 版本扫描）。
 * 成功: 缓存包，返回 0，verOut="2610.2402"；失败: 返回 <0，errOut 描述原因。 */
int fupUploadHex(const String& hex, String& verOut, String& errOut);
/* 静态缓冲版本: 避免 60KB String 在堆上拼接失败 */
int fupUploadHex(const char* hex, size_t len, String& verOut, String& errOut);

/* 发起升级。fromGitHub=true 时忽略本地缓存、从 GitHub 下载 DEVICE_ASSET hex。
 * force=强制降级放行；chipid!=0 时 M602/M788 都带 P<chipid> 定点（多个 addr=99 时必需）。 */
bool fupStart(uint8_t addr, uint32_t chipid, bool force, bool fromGitHub, String& errOut);

/* 升级任务进行中？ */
bool fupBusy();

/* 板上版本查询(异步触发): 只在 Web handler 栈调用, 立即返回; 成功返回 true(失败=总线忙)。
 * 结果由 status 的 board_fw 轮询带回。不能在 handler 上同步等(bus 应答由 main loop 处理,
 * 阻塞即饿死), 回调也不许写调用方栈引用。 */
bool fupBoardQueryStart(uint8_t addr);

/* 已上传包的版本字符串（未上传为空）。 */
String fupPkgVer();

/* 升级状态 JSON:
 * {"busy":bool,"state":"idle|downloading|parsing|comparing|isp|verify|done|error",
 *  "progress":0..100,"msg":"...","pkg_ver":"...","board_ver":"...",
 *  "board_busy":bool,"board_done":bool,"board_fw":"...","pkg_ready":bool,"log":[...]} */
String fupStatusJson();

/* 检查 GitHub 最新 release（复用 ota_manager），返回飞达 hex 资产信息 + 本地包状态 JSON */
String fupCheckGitHub();

/* UART0 手动 IAP 调试:
 *   iap m788 [addr]   进入 BL（addr 默认 99）
 *   iap hello         发 HELLO 并等 ACK
 *   iap erase         发 ERASE 并等 ACK
 *   iap reboot        发 REBOOT
 *   iap end           结束独占，恢复普通总线
 * 仅用于台架手动分步，不允许和 Web 升级同时使用。 */
bool fupConsole(const String& line, String& out);
