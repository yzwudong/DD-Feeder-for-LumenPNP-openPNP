#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include "config.h"
#include "feeder_manager.h"

/* ============================================================
 * Web 服务器（ESP32 官方核心内置 WebServer，零第三方依赖）
 *
 * 页面(文件系统 data/):
 *   /login.html   登录（公开）
 *   /index.html   总览(双排 2x32)     （需登录）
 *   /wifi.html    WiFi 扫描/配置      （需登录）
 *   /ota.html     ESP32/飞达升级      （需登录）
 *   /feeder.html  单飞达控制           （需登录）
 * 公开静态: /css/* /js/*
 *
 * API(除 /api/login、/api/auth/*、/api/creds/*、/api/unlock* 外均需鉴权):
 *   POST /api/login {"username","password","keep"} -> 下发 token+role(Set-Cookie)
 *                  admin(固定) 或 operator(自定义); keep=true 永久 / false 20 分钟
 *   POST /api/logout
 *   GET  /api/auth/status                 -> {"ok":true,"auth":bool,"role":"admin|operator|"}
 *   POST /api/auth/enable {"enabled":bool}-> 登录开关(仅 admin, 存NVS)
 *   GET  /api/creds/status | POST /api/creds/adminpw | POST /api/creds/operator (仅 admin)
 *   GET  /api/unlock/status               -> {"ok":true,"unlocked":bool}
 *   POST /api/unlock {"unlocked":bool}    -> 解锁/手动锁定(仅 admin, 解锁5分钟自动重锁)
 *   GET  /api/feeders | /api/feeder?slot=
 *   POST /api/command | /api/scan         (写操作需先解锁)
 *   GET  /api/wifi/status
 *   POST /api/wifi/scan | /api/wifi/connect {"ssid","pass"}
 *   GET  /api/ota/check | /api/ota/progress
 *   POST /api/ota/esp32 | /api/ota/device?slot=   (device 需先解锁)
 *   GET  /api/log?since=N                 -> 485 收发实时日志(增量)
 *
 * 注意: 同步服务器单连接处理；WiFi 扫描期间页面请求会短暂等待，
 *       对单操作员设备无影响。
 * ============================================================ */
class WebServerApp {
public:
    void begin(FeederManager* mgr);
    void loop();                       // 需在主循环高频调用

private:
    FeederManager* _mgr = nullptr;
    /* 保持默认构造(不传第二参数): 2.0.17/3.0.5 WebServer 并无 maxConnections 构造参数,
       "4" 是 WiFiServer listen backlog, 调它不改变单线程行为。
       真正的瓶颈: WebServer 单线程, 服务完一个请求在 keep-alive 连接上最多空等
       HTTP_MAX_DATA_WAIT(5s) 才 accept 下一个 -> 前端并发请求按 5s 一跳排队。
       治本方案在前端: app.js 全局 FIFO 队列, 同一时刻仅 1 个请求在飞(审查报告 4.2/1.4)。 */
    WebServer _server{ WEB_PORT };
    String   _authToken;               // 登录会话 token
    String   _authRole;                // "admin" / "operator" / ""（未登录）
    unsigned long _authExpiry = 0;     // 0=永久；>0 = millis() 到期时间(20 分钟会话)
    bool     _authEnabled = true;      // 登录开关(默认 WEB_AUTH_ENABLED,可NVS覆盖)

    bool     _unlocked = false;        // Web 写操作解锁状态
    unsigned long _unlockAt = 0;

    String   _upBuf;                   // /api/feeder/up/upload multipart 流式收集(hex 文本)
    bool     _upGot = false;           // 本次上传已完整收到

    void _setupRoutes();
    void _setupNotFound();
    void _loadAuthSetting();
    bool _authOk();                    // token 校验(header 或 cookie)；关闭时恒通过
    bool _needAuth();                  // 校验失败返回 401
    bool _isAdmin();                   // 当前会话可操作（auth 关闭=恒通过；开启=admin 会话）
    bool _needUnlock();                // 非 admin 会话或未解锁/超时返回 403
    void _sendJson(const String& s, int code = 200);
    void _servePage(const char* path); // 需登录页面(文件缺失回退提示页)
    String _metaJson();        /* /api/status: 顶部状态+诊断(不含 64 台) */
    String _statusJson();
    String _feederJson(uint8_t slot);
    String _tempFeederJson(uint8_t idx);   /* 临时飞达(addr=99)详情 */
};
