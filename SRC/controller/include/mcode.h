#pragma once
#include <Arduino.h>
#include <stdint.h>

/* ============================================================
 * MCODE 指令集 —— 与 485 飞达(STC8H1K17) 交互
 * 协议(下位机定义, 完整示例见工程 docs/mcode示例.md):
 *   寻址:   N<id> 定向(0..63, N0 是地址 0 的设备, 不是广播)   无 N = 隐式广播   N255 = 显式广播
 *           P<chipid> 按芯片ID匹配(可与 N 组合 AND)
 *   应答:   error=0 -> "ok <内容>\r\n"    error=1 -> "error <内容>\r\n"
 * ============================================================ */

/* M-代码 命令编号 (与下位机一致) */
enum MCmd : uint16_t {
    MCMD_ADC_RAW        = 143,   /* M143 读ADC原始值: "M143 A0 N5"(读通道0) / "M143 N5"(默认通道) */
    MCMD_ADC_SCALED     = 144,   /* M144 读ADC换算值: "M144 A1 N5" */
    MCMD_SET_SCALING    = 145,   /* M145 设缩放: "M145 A0 S1 O0 N5"(通道0 factor=1 offset=0) */
    MCMD_POWER_OUTPUT   = 155,   /* M155 电源输出: "M155 S1 N5" / "M155 S0"(广播关) */
    MCMD_ADVANCE        = 600,   /* M600 进料(F 必填): "M600 N5 F10"(地址5进10mm) / "M600 F10"(广播) / "M600 N255 F10"(显式广播) / "M600 P112233 F10"(按ID) */
    MCMD_RETRACT        = 601,   /* M601 取料后回退(不带 S): "M601 N1" / "M601 P112233" / "M601 N1 P112233"(N AND P) / "M601 N255 P112233"(广播按ID) */
    MCMD_QUERY          = 602,   /* M602 查询配置和状态: "M602 N5"(单点) / "M602 N255"(全局查询,设备按chipid错峰0~63ms异步回复) / 无N会回 "error feederAddr missing or invalid" */
    MCMD_SERVO          = 603,   /* M603 舵机角度: "M603 A90 N5" / "M603 A90"(广播) */
    MCMD_ENABLE         = 610,   /* M610 使能/禁用(S仅0|1): "M610 S1"(隐式广播全使能) / "M610 S0"(广播全禁用) / "M610 N255 S1"(显式广播) / "M610 N3 S1"(定向) / "M610 N3 P112233 S1"(N AND P) / "M610 P112233 S1"(按ID) */
    MCMD_BLINK          = 611,   /* M611 闪烁定位(冲突/未配置排障): "M611 P112233"(按ID精确定位) / "M611 N5"(按地址定位) */
    MCMD_BLINK_OFF      = 612,   /* M612 停止闪烁(处理完地址恢复常亮): "M612 P112233" / "M612 N5" */
    MCMD_CONFIG         = 620,   /* M620 更新配置: "M620 N0 A90 B44 C15 F4 U240 V550 W240 X0 P112233"(N0=地址0, 广播用N255; X0=有反馈线(不忽略)/X1=无反馈线(忽略); 不带 P 则广播全部) */
    MCMD_FACTORY_RESET  = 799,   /* M799 恢复出厂(广播, Web 需管理密码): "M799" */
    MCMD_ISP_RESET      = 788,   /* M788 MCU ISP 复位, 进入 ISP 模式: 单台升级 "M788 N5"; 广播 "M788" (原 M997, 统一飞达 M 代码段, 避免与 ESP32 本地 M9xx 混淆) */

    /* ---- ESP32 系统命令 (M9xx) —— 由 ESP32 本地处理, 不透传 485 ----
     * 入口: 外部主机 UART1 / 串口屏 发 M9xx 行; 回复格式与飞达一致 "ok .../error ..."
     * 完整说明见 docs/ESP32系统MCODE命令.md */
    MCMD_SYS_STATUS     = 900,   /* M900 系统状态查询(无参): "M900" */
    MCMD_SYS_BUZZER     = 901,   /* M901 蜂鸣器开关: "M901 S0|S1" */
    MCMD_SYS_VOL        = 902,   /* M902 蜂鸣器音量: "M902 V<0~100>" */
    MCMD_SYS_POWER      = 903,   /* M903 485总线电源开关: "M903 S0|S1"(关=所有飞达断电) */
    MCMD_SYS_SCAN       = 904,   /* M904 触发全量扫描(等价 Web 扫描): "M904" */
    MCMD_SYS_CLEAR      = 905,   /* M905 清零全部推料计数: "M905" */
    MCMD_SYS_LOG        = 906,   /* M906 运行时调试日志开关: "M906 S0|S1"(默认关, 开时输出 UART0+屏幕) */
    MCMD_SYS_FACTORY    = 907,   /* M907 恢复出厂设置(清 NVS + 删 /apmode -> 重启) */
    MCMD_SYS_HEARTBEAT  = 908,   /* M908 定向心跳周期(ms): "M908 S30000" 设置 / "M908" 查询 */
};

/* 一条解析后的 M 指令 */
struct MCmdMsg {
    uint16_t code = 0;
    int32_t  addr = 0;            /* N<id> 寻址; 0 = 无 N(隐式广播) */
    bool     hasN = false;        /* 显式带 N(0~63 为定向地址) */
    bool     explicitBroadcast = false;  /* N255 显式广播 */
    int32_t  r = 0;  bool hasR = false;  /* R 新地址(M620 改址: N<当前> R<新> ...) */
    uint32_t chipid = 0;          /* P<chipid> 按芯片ID匹配 */
    bool     hasP = false;
    int32_t  a = 0;  bool hasA = false;  /* A 参数(舵机角度/ADC通道) */
    int32_t  b = 0;  bool hasB = false;  /* B 参数(M620) */
    int32_t  c = 0;  bool hasC = false;  /* C 参数(M620) */
    int32_t  f = 0;  bool hasF = false;  /* F 参数(进料 mm) */
    int32_t  u = 0;  bool hasU = false;  /* U 参数(M620) */
    int32_t  v = 0;  bool hasV = false;  /* V 参数(M620) */
    int32_t  w = 0;  bool hasW = false;  /* W 参数(M620) */
    int32_t  x = 0;  bool hasX = false;  /* X 参数(M620, 反馈线 0/1) */
    int32_t  t = 0;  bool hasT = false;  /* T 参数(M620 N20 超时 0~5000, 默认800; 不同电机转速超时不同) */
    int32_t  s = 0;  bool hasS = false;  /* S 参数 */
    int32_t  o = 0;  bool hasO = false;  /* O 参数(偏移) */
    String   raw;
};

/* 组包(兼容旧调用): addr>0 -> "M610 N5 S1"; addr==0 且 includeAddr -> "M610 N255 S1"; 否则隐式广播 */
String mcodeBuild(uint16_t code, int32_t addr, int32_t s = 0,
                  bool hasS = false, bool includeAddr = false);

/* 完整组包: 按 MCmdMsg 的字段生成, 顺序 N A B C F U V W X S O P (设备按字母解析) */
String mcodeBuildMsg(const MCmdMsg& m);

/* 查询类便捷组包: 定向 N<addr>；addr==0 时显式广播 N255 */
String mcodeQuery(uint16_t code, int32_t addr);

/* 解析一行 M 命令文本(支持 N/P/A/B/C/F/U/V/W/X/S/O 参数); 成功返回 true */
bool mcodeParse(const String& line, MCmdMsg& out);

/* ESP32 系统命令 (M9xx) 处理: 命中(900~905)返回 true 并填充 reply(含 ok/error 行);
 * 未命中返回 false —— 调用方(外部主机透传/串口屏)按"飞达命令"继续原流程。 */
bool mcodeHandleSystem(const String& line, String& reply);

/* M602 单点查询应答解析结果
 * 回复格式(下位机源码): "ok error:<err> M620 N<addr> A.. B.. C.. F.. U.. V.. W.. [X..] [T..] VER:<ver> FW:<yy>.<mm> CHIP:<chipid>"
 * 例: ok error:0 M620 N3 A180 B90 C0 F8 U200 V500 W2500 X0 T800 VER:86 FW:26.10 CHIP:AABBCC
 * X/T 仅在 HAS_FEEDBACKLINES 时输出; T=N20 超时参数(0~5000, T0=周期完成立即检查限位, 默认800)
 * FW=下位机固件版本(YYMM.DD_BN, 如 26.10), 在线升级判断用
 * 全局(N255)应答: "ok addr:5 error:0 VER:86 ChipID:AABBCC feederEnabled:1" (无 T/FW/配置参数, 含使能状态) */
struct FeederCfg {
    int32_t addr = -1;          /* 设备地址(全局回复 addr: / 单点回复 N) */
    int32_t err = -1;           /* 设备错误状态 */
    int32_t a = -1, b = -1, c = -1, f = -1,   /* A全进料角度 B半进料角度 C回退角度 F进料长度 */
            u = -1, v = -1, w = -1, x = -1;   /* U稳定时间 V最小脉宽 W最大脉宽 X忽略反馈 */
    int32_t t = -1;             /* T N20 超时参数(仅 HAS_FEEDBACKLINES 应答含) */
    int32_t limit = -1;         /* L 限位状态(bit0=0°限位触发, bit1=90°限位触发, -1=下位机未回显) */
    uint16_t ver = 0;           /* VER: 配置版本(config_ver) */
    int32_t fw = -1;            /* FW: 下位机固件版本 major*100+minor (26.10 -> 2610) */
    uint32_t chipid = 0;        /* CHIP/ChipID: 6位十六进制 */
    bool hasChip = false;
    bool boot = false;          /* 应答为 ok boot ChipID: (BL 等待升级) */
    bool enabled = false;       /* feederEnabled:1/0 使能状态(全局/单点应答均可含) */
    bool hasEnable = false;
};

/* 解析 M602 单点应答; 成功返回 true */
bool mcodeParseConfig(const String& reply, FeederCfg& cfg);

/* 应答类型: 0=ok, 1=error, -1=无法识别 */
int mcodeReplyType(const String& reply);

/* 从 M602 回复提取 chipid / 版本。
 * 新格式: "ok addr:5 error:0 ChipID:A1B2C3 ..." (chipid 可为空格/冒号分隔)
 * 兼容旧格式: "I12345678 V100" */
bool mcodeParseInfo(const String& reply, uint32_t& chipid, uint16_t& ver);

/* 从 M602 回复提取状态(使能/忙/速度/节距)。
 * 兼容: 新格式键值 "enabled:1 busy:0 speed:120 pitch:8" 与旧格式 "E1 B0 S120 P8" */
bool mcodeParseStatus(const String& reply, bool& enabled, bool& busy,
                      int& speed, int& pitch);

