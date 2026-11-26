#pragma once
#include <Arduino.h>

/* ============================================================
 * 全局配置 —— 引脚 / 网络 / 485 / 飞达阵列 / OTA
 * 所有参数集中在此，按需修改。
 * ============================================================ */

/* ---------------- 固件信息 ----------------
 * FW_VERSION 由 scripts/gen_version.py 自动生成 (include/version.h):
 *   YYYYMMDD + 当日2位编译序号, 如 "2026101007"
 * 文件名前缀 FW_VER_FULL = "DDfeeder<ver>", 用于打包产物命名。
 */
#define FW_NAME         "SMT-Feeder-Controller"
#include "version.h"
#ifndef FW_VERSION
#define FW_VERSION      FW_VER_STR
#endif

/* ---------------- GitHub 仓库 (OTA 检测/升级) ----------------
 * 发布两个资产：
 *   firmware.bin   -> ESP32 自身固件 (Partition Scheme 选 default_ota)
 *   feeder.hex     -> 485 飞达 STC8H1K17 固件 (Intel HEX 文本)
 *   命名规则参考 docs/固件打包与版本.md
 */
#define FW_GITHUB_OWNER "yzwudong"

/* 页面(FS)资产命名约定: DDfs<YYYYMMDD><hash8>.bin
 * 发布 release 时上传该文件即可被 OTA 检测为"页面更新" */
#define FS_ASSET_PREFIX "DDfs"
#define FW_GITHUB_REPO  "DD-Feeder-for-LumenPNP-openPNP"
#define ESP32_ASSET     "DDFC_*_OTA.bin"
#define DEVICE_ASSET    "DDF_*_APP.hex"

/* ---------------- WiFi ----------------
 * 首次上电: 先试 NVS 已保存网络 -> 无则试下面的默认网络 -> 都失败开热点配网
 * 配网后(web 的 WiFi 页面)自动存入 NVS，此后优先使用。
 */
#define WIFI_SSID        "YourWiFi"     /* 仅作为出厂默认 */
#define WIFI_PASS        "YourPassword"
#define WIFI_HOSTNAME    "smt-feeder"
#define WIFI_TIMEOUT_MS  20000UL     /* 普通连接(无候选 AP)最长等待 */
#define WIFI_AP_TRY_MS   10000UL     /* 扫描候选模式下, 每个 AP 最长连接等待 */
#define AP_FALLBACK      1            /* 1:连不上时开热点配网  0:只连固定SSID *///热点地址192.168.4.1
#define AP_SSID          "SMT-Feeder-AP"
#define AP_PASS          "12345678"

/* ---------------- Web 登录 ---------------- */
#define WEB_PASSWORD     "123456"      /* 登录页密码，建议首次登录后修改 */
#define WEB_AUTH_ENABLED 0             /* 0=默认免登录直接进总览(可在登录页开关,存NVS) */

/* ---------------- 串口 ---------------- */
/* UART0 = USB (CH340C)：调试日志 + MCODE 控制台 */
#define UART0_BAUD       115200

/* UART1 = 外部主机口 (输入，透传到 485)
 * 接上位机/贴片机主控串口:  本机 TX -> 主机 RX   本机 RX <- 主机 TX
 * 收到的一行 MCODE 原样转发到 485 总线(UART2)，回复回传。
 */
#define EXT_BAUD         115200
#define EXT_TX_PIN       26
#define EXT_RX_PIN       27

/* UART2 = 485 总线 (MAX485)
 * 接线:  MAX485 DI -> GPIO_TX   RO -> GPIO_RX   DE/RE(并接) -> GPIO_DE
 * DE 为高 = 发送，低 = 接收 (半双工)
 */
#define BUS_BAUD         115200
#define BUS_TX_PIN       17
#define BUS_RX_PIN       16
#define BUS_DE_PIN       4
#define BUS_CMD_TIMEOUT  80           /* 单条写命令等待回复(ms): M600/M610/M620 等 "ok ready" 短应答 */
#define BUS_QUERY_TIMEOUT 300         /* M602 查询应答等待(ms): 单点应答含 M620+N+A..T+VER+FW+CHIP(约100B),
                                       * 从机处理/分帧可能超过 80ms; 升级路径(feeder_upgrade.cpp)一直用 300ms,
                                       * 详情页配置参数始终"未知"就是被 80ms 提前判超时导致的 */
#define BUS_REPLY_TAIL_MS 80          /* 普通模式: 已见 "ok" 后等待配置续帧的窗口(拆段应答容错;
                                       * 12ms 在 100B@115200(≈9ms)+分帧抖动下偏紧, 放宽到 20ms) */
#define BUS_LINE_GUARD_MS 5          /* ???????? 5ms ??? */
#define BUS_TX_GUARD_MS  0.5f         /* 发送->切接收 防抖(0.5ms@115200≈5.7字节; 0.3ms 会回环乱码, 此值下最多丢1字节且无乱码) */
#define BUS_CMD_GUARD_MS 2            /* 相邻命令间隔保护 */

/* ---------------- 485 总线电源开关 ----------------
 * GPIO 输出 -> SGM2593AD EN -> 485 总线/飞达供电(GPIO 只发信号, 不带载)
 * 高电平=开, 低电平=关; 上电默认开。
 * 注意: 控制脚必须用非 strapping 引脚! GPIO5 复位期间内部弱上拉,
 *       高有效电路会上电瞬间误开一下(上电毛刺)。
 * >>> 临时改回 GPIO5 用旧板调试(旧板接线为 GPIO5); 新板最终必须换
 *     非 strapping 脚(推荐 GPIO21: 复位高阻 + EN 内部 1.2MΩ 下拉, 无毛刺)。
 * 已改: BUS_PWR_PIN = 21（新板非 strapping 脚）; 同步 docs/用户手册.md、
 *     docs/总线电源与nFAULT说明.md 接线图。
 * 关闭前会等总线空闲; 重新开启后等待设备上电稳定并自动触发一次扫描。
 * Web 总览页有电源开关按钮(需解锁); 串口屏可发 #PWR 1/#PWR 0。
 */
#define BUS_PWR_PIN         21       /* GPIO21: 非 strapping; -1=禁用该功能 */
#define BUS_PWR_DEFAULT_ON  1        /* 上电默认开 */
#define BUS_PWR_ON_LEVEL    HIGH     /* "开"对应的 GPIO 电平(高有效=HIGH) */
#define BUS_PWR_BOOT_MS     300      /* 重新上电后等待设备初始化(ms) */

/* ---- 485 总线电源故障检测 (SGM2593AD nFAULT) ----
 * nFAULT 开漏输出, 低=过流/过热/反压; 必须外部 10k 上拉到 3.3V。
 * 故障行为: 去抖确认 -> 蜂鸣+Web/屏幕提示 -> 自动 EN 翻转复位一次
 * (窗口内再故障则保持关断, 等人工处理)。详见 docs/总线电源与nFAULT说明.md */
#define BUS_FAULT_PIN          13       /* nFAULT 检测输入 GPIO(无 strapping); -1=禁用 */
#define BUS_FAULT_DEBOUNCE_MS  40       /* 软件去抖(芯片内部已 15ms, 滤反压瞬闪) */
#define BUS_FAULT_AUTO_RESET   1        /* 1=首次故障自动 EN 翻转复位一次 */
#define BUS_FAULT_RESET_MAX    2        /* 自动复位次数上限(超过则保持关断) */
#define BUS_FAULT_RESET_HOLD   100      /* 复位时 EN 拉低保持时间(ms) */
#define BUS_FAULT_WATCH_MS     5000     /* 复位后观察窗口: 窗口内再故障则停止重试 */

/* ---------------- 舵机校准限位开关 (直连 ESP32, 无需下位机配合) ----------------
 * 0°/90° 限位开关信号直接接 ESP32 GPIO(不经过 STC8):
 *   限位开关为常开型: 一端接 GND, 一端接 GPIO; ESP32 内部上拉,
 *   舵机到位触发开关 = 引脚读到低电平(LOW)。
 * 接线(按实际调整): CALIB_LIMIT0_PIN = GPIO32, CALIB_LIMIT90_PIN = GPIO33
 * 只装了一个开关时两个引脚可并接同一信号。
 * 校准算法(ESP32 侧): 递减逼近——发 M603 角度, 限位触发=到位/过位则减一档,
 *   减到不再触发时, 上一档即"刚好到位"的脉宽; 得到 V(0°)/P90(90°) -> W=2*P90-V,
 *   通过 M620 写回飞达。详见 docs/调测与校准.md */
#define CALIB_LIMIT0_PIN      32      /* 0° 限位开关输入; -1=无(校准0°时报错) */
#define CALIB_LIMIT90_PIN     33      /* 90° 限位开关输入; -1=无(校准90°时报错) */

/* ---------------- 蜂鸣器 (可选) ----------------
 * 无源蜂鸣器 -> GPIO, 用 LEDC PWM 驱动, 可编程音调/音量;
 * 错误/事件提示音定义见 docs/蜂鸣器说明.md
 */
#define BUZZER_PIN           25      /* 蜂鸣器 GPIO(无源); -1=禁用 */
#define BUZZER_DEF_ON        1       /* 默认开启蜂鸣提示 */
#define BUZZER_DEF_VOL       50      /* 默认音量 0~100 */

/* ---------------- 多功能维护键 (可选) ----------------
 * GPIO14: 非 strapping。短按=485电源 ON/OFF，双击=重新扫描，长按=不动作。
 * 按键一端接 GND，内部上拉；建议外部 10k 上拉 + 100nF。
 * 注意 GPIO14 同时是 JTAG MTMS，正常串口启动无影响；用 JTAG 调试时需注意。
 */
#define FUNCTION_KEY_PIN            14
#define FUNCTION_KEY_ACTIVE_LOW     1
#define FUNCTION_KEY_DEBOUNCE_MS    30
#define FUNCTION_KEY_SHORT_MAX_MS   800
#define FUNCTION_KEY_DOUBLE_MS      350

/* ---------------- 恢复出厂设置 (硬件长按 + Web + M907) ----------------
 * 三种触发方式(任一) -> 清空 NVS 全部配置 + 删除 LittleFS /apmode 标志 -> 重启:
 *   1. 硬件按键: 长按 FACTORY_RESET_PIN 保持 FACTORY_RESET_HOLD_MS(按下=低电平)
 *   2. Web: 登录设置页「恢复出厂设置」(需管理密码, 见 docs/账号与权限.md)
 *   3. M907: 透传/串口屏通道免密码(与 M799 一致), 见 docs/ESP32系统MCODE命令.md
 * 重启后回到出厂默认: AP 热点配网 + 默认管理密码(WEB_PASSWORD) + 蜂鸣器开/音量 50。
 * 接线: 轻触键一端接 GPIO34(引脚 6), 另一端接 GND;
 *        GPIO34 为只输入脚、无内部上拉, **必须外部 10k 上拉**(当前接线已加)。
 *        若改用 GPIO33 等普通输入脚则可用内部上拉免外部电阻。
 */
#define FACTORY_RESET_PIN       34      /* 恢复出厂按键 GPIO(只输入脚, 需外部上拉); -1=禁用 */
#define FACTORY_RESET_HOLD_MS   5000    /* 长按保持时间(ms) */

/* ---------------- 串口屏 (淘晶驰 TJC, 可选) ----------------
 * 屏幕只做收发 UI，所有逻辑在 ESP32：
 *   屏 -> ESP32:  M 代码行(直通 485) 或 #命令(#SCAN/#STATUS/#PWD/#PING)
 *   ESP32 -> 屏:  淘晶驰原生指令( t0.txt="..." / n0.val=5 ) 更新控件
 * 协议详见 docs/screen协议.md
 */
#define SCREEN_ENABLED        1       /* 0=禁用屏幕 1=启用 */
#define SCREEN_BACKEND_SOFT   0       /* 软串口(默认, 任意空闲 GPIO) */
#define SCREEN_BACKEND_UART0  1       /* UART0 后端: 需硬件把 RX0 切到屏幕(与 CH340 互斥) */
#ifndef SCREEN_BACKEND
#define SCREEN_BACKEND        SCREEN_BACKEND_SOFT
#endif
        /* 以后要换 UART0: 硬件 RX0(GPIO3) 加单刀双掷切到屏幕 TX,
         * TX0(GPIO1) 并联不动, 再改 SCREEN_BACKEND 为 SCREEN_BACKEND_UART0 即可
         * (或命令行 -DSCREEN_BACKEND=SCREEN_BACKEND_UART0, 免改文件)。
         * 注意: UART0 后端时 USB 调试口被屏幕占用, 日志进屏幕, 烧录需切回 CH340。 */
#define SCREEN_BAUD           115200  /* 与屏工程里设置的波特率一致 */
#define SCREEN_SOFT_RX        18      /* 软串口 RX <- 屏 TX (仅软串口后端用) */
#define SCREEN_SOFT_TX        19      /* 软串口 TX -> 屏 RX */
#define SCREEN_PUSH_INTERVAL_MS 2000  /* 状态推送周期(ms) */
/* 屏上控件 id (USART HMI 工程里按此命名) */
#define SCREEN_CTRL_SUM       "t0"    /* 汇总: 在线/推料/版本 */
#define SCREEN_CTRL_META      "t1"    /* 元信息: IP/RSSI/未初始化/冲突 */
#define SCREEN_CTRL_LOG       "t2"    /* 日志: 最后一条指令/应答 */
#define SCREEN_CTRL_LOGF      "t3"    /* 调试日志控件(需屏工程添加 t3 文本控件; 无则忽略) */
#define SCREEN_CTRL_ADDR      "n0"    /* 地址数值控件(0~63), 屏按钮用它作 N 参数 */

/* ---------------- Web 安全解锁 ----------------
 * Web 上向 485 发送控制指令(使能/送料/退料/改参/改址/设备升级)前，
 * 必须先"解锁操作"(确认飞达已停止动作)，超时自动重新锁定。
 */
#define UNLOCK_TIMEOUT_MS (5UL*60*1000)   /* 解锁后 5 分钟无操作自动锁定 */

/* ---------------- 飞达阵列 ----------------
 * 双排，每排 32 位，地址 0..63
 */
#define ROW_COUNT        2
#define SLOT_PER_ROW     32
#define FEEDER_COUNT     (ROW_COUNT * SLOT_PER_ROW)   /* 64 */
#define FEEDER_ADDR_MIN  0                /* 飞达 485 地址从 0 开始 */
#define FEEDER_ADDR_MAX  (FEEDER_COUNT - 1) /* 0~63 */

#define POLL_INTERVAL_MS  3000UL      /* 全量状态轮询周期(M602 N255 快速扫描) */
#define POLL_STAGGER_MS   30          /* 逐地址错峰间隔(ms)，已废弃(改 N255 快速扫描) */
#define BOOT_SCAN_DELAY_MS  5000UL    /* 固件启动后延时广播一次(M602 N255), 等待飞达上电初始化完成 */
#define FEEDER_OFFLINE_MS 45000UL     /* 超过该时间无回复判离线: 必须 > 心跳周期(默认30s), 否则每轮心跳之间都会误判离线并触发蜂鸣 */
#define BUS_COLLECT_IDLE_MS  650      /* N255 快速扫描: 无新字节静默判定(ms) */
#define BUS_COLLECT_TIMEOUT_MS 1200   /* N255 快速扫描: 收集总超时(ms), 覆盖 600ms 错峰窗口+余量 */
#define SCAN_IDLE_MS   500            /* 广播扫描启动条件: 总线需连续空闲(无任何收发) ≥ 该值 */
#define SCAN_ABORT_MS  5000           /* 空闲等待超时: 生产持续占用总线时自动取消待发广播 */
#define HEARTBEAT_DEFAULT_MS 30000UL  /* 定向心跳默认周期(ms), M908/NVS "hb" 可覆盖 */
#define HB_IDLE_MS     200            /* 心跳同样要求总线连续空闲(单播 80ms 窗口, 阈值可小于广播) */
#define HB_MAX_MISS    2              /* 连续 N 次心跳无回复 -> 判离线 */
#define FUP_QUERY_IDLE_MS 300         /* 升级页 M602/M788 查询: 需总线连续空闲 ≥ 该值(广播错峰应答最长 600ms,
                                         300ms 兜住最晚应答尾流, 防止查询插入广播应答造成 485 乱码) */

/* ---------------- Web / OTA ---------------- */
#define WEB_PORT         80
#define OTA_CHECK_INTERVAL_MS (6UL*3600*1000)   /* 每 6h 自动查一次版本 */
#define OTA_STREAM_CHUNK  8192
#define ISP_BAUD_DOWNLOAD 115200      /* STC8 下载波特率 */

/* ---------------- MCODE 协议说明 ----------------
 * 命令:  定向  M<code> A<addr> S<val>
 *        广播  M<code> S<val>      (无 A 即广播, 飞达错峰回 ok)
 * 回复:  单地址回  "ok" / "err:<n>"
 *        查询 M114 回  "E<b> B<b> S<val> P<val>"  (E=使能,B=喂料忙,S=速度,P=节距)
 *        查询 M115 回  "I<chipid> V<ver>"
 */


