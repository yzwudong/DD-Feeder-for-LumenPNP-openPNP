#ifndef __GCODE_H__
#define __GCODE_H__

#include <stdint.h>
#include <stdbool.h>
#include "feeder.h" // 引入feeder.h，直接拿到 Feeder_t、FeederSettings_t



/* ========================================================================
 *  M-Code 指令编号定义
 * ========================================================================
 *  与上位机固件（openpnp）中的定义必须一一对应
 */

#define MCODE_ADVANCE              600   // 进料指令：M600 [F<mm>] [N<id>]
#define MCODE_RETRACT_POST_PICK    601   // 取料后回退：M601 [N<id>]
#define MCODE_FEEDER_IS_OK         602   // 查询飞达配置和状态：M602 [N<id>] 或者M602 P<chipid>
#define MCODE_SERVO_SET_ANGLE      603   // 手动设置舵机角度：M603 A<angle> [N<id>]
#define MCODE_SET_FEEDER_ENABLE    610   // 启用/禁用飞达：M610 S<0|1> [N<id>]
#define MCODE_ALARM_FLASH_ADDR     611   // 地址告警：持续闪烁数码管显示地址 M611 [N<id>] [P<chipid>]
#define MCODE_CLEAR_ALARM          612   // 停止地址告警闪烁：M612 [N<id>] [P<chipid>]
#define MCODE_UPDATE_FEEDER_CONFIG 620   // 更新飞达配置参数：M620 ...

// --- ADC 模拟量采集相关 M-Code ---
#ifndef HAS_ANALOG_IN
    #define MCODE_GET_ADC_RAW       143  // 获取 ADC 原始值：M143 [A<channel>]
    #define MCODE_GET_ADC_SCALED    144  // 获取 ADC 换算后的物理量：M144 [A<channel>]
    #define MCODE_SET_SCALING       145  // 设置 ADC 缩放因子：M145 A<ch> S<factor> O<offset>
#else
    #define MCODE_GET_ADC_RAW       10   // M10: 获取 ADC 原始值 (0~4095)
    #define MCODE_GET_ADC_SCALED    11   // M11: 获取 ADC 缩放后的物理量值
    #define MCODE_SET_SCALING       12   // M12: 设置 ADC 通道的缩放系数 (斜率+偏移)
#endif

#define MCODE_SET_POWER_OUTPUT     155   // 设置电源输出：M155 S<value>
// --- 系统控制相关 M-Code ---
#define MCODE_FACTORY_RESET        799   // 恢复出厂设置：M799 (标记EEPROM无效，重启后加载默认值)
#define MCODE_ISP_RESET            788   // 进入ISP升级模式：M788 (应答ok ISP reset后软复位进ISP引导区；原M997，X53改号)




/* ========================================================================
 *  常量与缓冲区配置
 * ======================================================================== */

// G-Code 单行最大接收长度 (字节)
// 包含 '\0' 结束符，实际有效字符为 MAX-1
// 典型的飞达指令如 "M620 N1 A120 B60 C30 F8 U50 V500 W2400" 约 45 字节
// 预留 64 字节足够应对绝大多数指令及注释
#define MAX_BUFFFER_MCODE_LINE      64


/* ========================================================================
 *  外部全局变量声明
 *  这些变量在 main.c 或其他模块中定义，供 gcode.c 访问
 * ======================================================================== */

// 飞达实例数组 (在 main.c 中定义)
// 注意：Feeder_t 类型来自 feeder.h，确保 gcode.c 已包含该头文件
extern __xdata Feeder_t feeders[];

// 对全部飞达执行统一操作的回调函数指针或函数声明
// 【X17修复·评审§1.1】删除 cmdEnable=1/cmdDisable=0 宏——文本替换后与
// feeder.h FeederCmd_t 枚举（cmdSetup=0, cmdUpdate=1, cmdEnable=2, cmdDisable=3）
// 冲突：cmdEnable→1=cmdUpdate（M610 S1 变成 Feeder_Update 假使能）、
// cmdDisable→0=cmdSetup（M610 S0 变成 Feeder_Setup 反而使能）。统一用枚举。
extern void executeCommandOnAllFeeder(FeederCmd_t cmd);


/* ========================================================================
 *  公共函数接口
 * ======================================================================== */

/**
 * @brief  初始化 G-Code 处理器
 * @note   清空接收缓冲区，应在系统上电初始化时调用一次
 */
void setupGCodeProc(void);

/**
 * @brief  串口数据监听与缓冲
 * @note   ⚠️ 必须在主循环 while(1) 中持续调用
 *         内部自动完成：接收 → 去注释 → Trim → 触发 processCommand()
 */
void listenToSerialStream(void);

/**
 * @brief  解析当前 inputBuffer 中的 G/M 命令并执行
 * @note   由 listenToSerialStream() 在收到完整 '\n' 后自动调用
 *         不建议外部直接调用此函数
 */
void processCommand(void);

/**
 * @brief  从输入缓冲区中提取指定字母参数后的浮点数值
 * @param  code        参数字母标识 (如 'M', 'N', 'F', 'A')
 * @param  defaultVal  未找到该参数时返回的默认值
 * @return 解析到的整数值，或 defaultVal
 * @example parseParameter('F', 8.0f) → 从 "M2 N1 F12" 中提取出 12.0
 */
int16_t parseParameter(char code, int16_t defaultVal);

/**
 * @brief  向上位机发送标准应答
 * @param  error    0=成功(ok), 非0=失败(error)
 * @param  message  应答消息字符串 (C 字符串字面量或 char 数组)
 * @note   格式遵循 Marlin/G-Code 标准: "ok xxx\r\n" 或 "error xxx\r\n"
 */
void sendAnswer(uint8_t error, const char *message);

void sendAnswerFmt(uint8_t error, const char *fmt, ...);
/**
 * @brief  检查当前命令是否针对本机飞达
 * @param  nRaw      M-Code 中的 N 参数原始值 (-1 表示未提供)
 * @param  chipidStr M-Code 中的 P 参数字符串 (如 "112233")，若未提供则为 NULL
 * @return true=命中本机飞达，false=不命中
 * @note   规则：
 *
N 不存在：地址条件自动满足
N 存在：先校验地址合法性；N=255 广播，或地址等于本机地址 →地址匹配
P 不存在：chip 条件自动满足
P 存在：调用Comm_MatchChipID比对，成功则 chip 匹配
最终：地址匹配 && chip匹配 →命中
*/
uint8_t CheckTargetFeeder(int16_t nRaw, char *chipidStr);

// 【X47】620 设置快闪(250ms/3s)结束后的显示恢复（main 循环按当前状态执行）
extern uint8_t  g_dispRestorePending;
extern uint16_t g_dispRestoreAt;

bool validFeederAddr(uint8_t addr);

/**
 * @brief  校验飞达编号是否合法
 * @param  signedFeederNo     待校验的飞达编号 (-1 表示未提供)
 * @param  feederNoMandatory  1=编号必须提供, 0=编号可选
 * @return true=合法, false=非法或缺失
 */
bool validFeederNo(int8_t signedFeederNo, uint8_t feederNoMandatory);

#endif /* __GCODE_H__ */