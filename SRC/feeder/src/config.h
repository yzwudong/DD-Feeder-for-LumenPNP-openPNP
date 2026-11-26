#ifndef _CONFIG_H
#define _CONFIG_H

/* ============================================================
 * 【重要】FwLib_STC8 配置宏 - 必须在 #include "stc8h.h" 之前定义
 * ============================================================ */
// FwLib_STC8 MCU与时钟宏，兜底定义，可被EIDE的-D参数覆盖
// 【X51】换 STC8H1K28：型号宏必须同步——驱动 fw_cid_stc8h.h 的 CHIPID 地址
//   （1K17→0x43F9；1K28→0x6FE7+18=0x6FF9，即 28K ROM 末 7 字节）及寄存器族定义
#ifndef __CONF_MCU_MODEL
#define __CONF_MCU_MODEL    MCU_MODEL_STC8H1K28
#endif
#ifndef __CONF_FOSC
#define __CONF_FOSC         24000000UL   // 系统时钟 24MHz
#endif
#ifndef __CONF_IRCBAND
// IRCBAND 档位：0=20M档 1=35M档 2=24M档 3=40M档
// 【实测结论】用 24M 档（0x02）+ IRTRIM=0（不写校准）频率即足够准（UART 115200 正常）
// 勿改成 20M 档+T24M 校准：实测会频率错配导致串口乱码（26.10.13 验证）
#define __CONF_IRCBAND      0x02
#endif


#define USER_PUTCHAR_OVERRIDE //fwlib的putchar函数会被用户自定义的putchar覆盖，避免重复定义

// ?? SDCC + FwLib_STC8 适配说明：
// FwLib_STC8 库不提供独立的 stc8h.h，统一使用 fw_lib.h 作为主入口
// 该头文件内部会根据 __CONF_MCU_MODEL 宏自动包含对应芯片的寄存器定义
#include "fw_conf.h"
#include <stdint.h>     // 提供 uint8_t, uint16_t 等标准整数类型
#include <stdbool.h>    // 提供 bool, true, false 布尔类型支持
#include <string.h>     // 提供 memcpy, memset 等内存操作函数

//系统时钟频率
//#define SYS_CLOCK       24000000UL
/* ========================================================================
 *  调试开关
 * ======================================================================== */

// 串口调试输出开关
// 取消注释可在运行时通过串口输出额外调试信息
// 正式量产时请保持注释状态以节省 Flash 和 RAM
//#define DEBUG

#ifndef ENABLE
#define ENABLE  1
#endif

#ifndef DISABLE
#define DISABLE 0
#endif

/* ========================================================================
 *  数码管调试开关（seg.c）
 * ======================================================================== */
#define SEG_SELF_TEST       0   // 1=上电自检：14段逐段常亮300ms（约4.2s后自动恢复）
                                // 0=跳过自检，直接正常显示
                                // 【调试期 2026-10-14】=0：配合 SEG_DISABLE 查 LVR 复位
#define SEG_ONLY_A1         0   // 0=恢复正常流程（已确认：极性反转后a1显示正确）
#define SEG_POLARITY_INVERT 1   // 1=极性反转（实测：画a1(阳2高阴1低)实际亮e1位置，
                                //    说明LED方向与segMap假设相反，全局反转阳极/阴极）
                                // 0=正常（阳极节点高、阴极节点低点亮）
#define SEG_DISABLE         0   // 【调试期 2026-10-14】1=屏蔽数码管驱动（Seg_Refresh 直接消隐，
                                //    不点亮任何段）——验证数码管扫描电流是否导致 VCC 跌落/复位。
                                //    已实测：屏蔽后 M610 S1 仍死机→数码管电流排除。0=恢复正常显示。
                                //    当前=0：数码管作"MCU 活着"指示器，区分死机 vs 命令未处理。


#define NUMBER_OF_FEEDER 1  // 飞达数量，这是一个飞达的控制板只支持1
#define HAS_ENABLE_PIN      //是否带使能脚


/* ========================================================================
 *  EEPROM 配置
 * ======================================================================== */

// EEPROM 数据结构版本号（最多3个字符）
// 当 sFeederSettings 或公共设置结构体成员发生变化时，必须修改此值
// 系统启动时会比对此版本，不匹配则执行出厂重置，防止读取到旧格式脏数据
#define CONFIG_VERSION  0x56


/* ========================================================================
 *  RS485&串口通信配置
 * ======================================================================== */
 //  RS485 DE/RE 引脚控制
#define HAS_RS485_DE_PIN
#ifdef HAS_RS485_DE_PIN
    #ifndef RS485_DE_PORT_NUM
        #define RS485_DE_PORT_NUM  3      // 端口号：3=P3（改端口只需改这里+下方对应分支）
    #endif
    #ifndef RS485_DE_PIN_NUM
        #define RS485_DE_PIN_NUM   6      // 引脚号：bit位 0~7（与 PIN_MASK 保持一致）
    #endif
    #ifndef RS485_DE_PIN_MASK
        #define RS485_DE_PIN_MASK  GPIO_Pin_6  // 位掩码（FwLib GPIO_Pin_x 枚举，=0x40，GPIO 配置用）
    #endif

    // 使用 fwlib 标准 SBIT 宏定义，SDCC/Keil 双兼容
    // 硬件配置集中在本文件：改端口 = 改上面 PORT_NUM/PIN_NUM/PIN_MASK + 新增下面分支
    #if RS485_DE_PORT_NUM == 3 && RS485_DE_PIN_NUM == 6
        SBIT(RS485_DE, 0xB0, 6); // P3地址0xB0，bit6，对应P3.6
        #define RS485_DE_SET_PP_MODE()   GPIO_P3_SetMode(RS485_DE_PIN_MASK, GPIO_Mode_Output_PP)
    #elif RS485_DE_PORT_NUM == 4 && RS485_DE_PIN_NUM == 6
        SBIT(RS485_DE, 0xC0, 6); // P4地址0xC0，bit6（示例分支，地址以手册为准）
        #define RS485_DE_SET_PP_MODE()   GPIO_P4_SetMode(RS485_DE_PIN_MASK, GPIO_Mode_Output_PP)
    #else
        #error "Please add RS485_DE port/pin combination in config.h"
    #endif

    static inline void RS485_SetTxMode(void) { RS485_DE = 1; }
    static inline void RS485_SetRxMode(void) { RS485_DE = 0; }
#else
    #define RS485_SetTxMode()
    #define RS485_SetRxMode()
#endif


// 命令总线保护延时(ms)：主机命令帧发完后需要时间把自身 DE 切回接收态。
// 从机收齐命令行后、解析执行前先等待该窗口，再即时应答，避免应答首字节被方向切换吞掉；
// 仅作用于命令入口（同步即时应答受保护），全局错峰 / 周期完成应答不经此延时。
#define RS485_REPLY_GUARD_MS   4

// RS485/UART 波特率（bps）
// 需与上位机（如 Klipper、Marlin）配置的波特率严格一致
#define SERIAL_BAUD  115200

// RS485地址范围
// 合法地址范围 (Modbus/RS485 惯例是到 247, 248-254 保留, 255 用于调试后门)
#define RS485_ADDR_MIN        0
#define RS485_ADDR_MAX        63
#define RS485_ADDR_DEFAULT    99  // 出厂默认地址 (避开常用的地址, 降低多机未改址时的冲突概率)
#define RS485_ADDR_BACKDOOR   255 // 调试后门或者说是广播地址: UART直连无地址前缀时解析器返回此值, 无条件放行
//#define COMM_MODE_RS485 0 // 严格校验地址 (产线总线) 没必要区分了，硬件上直接接uart，直接用uart模式就行了
//#define COMM_MODE_UART  1 // 忽略地址校验 (单机直连调试)



/* ========================================================================
 *  飞达机械与舵机参数配置
 * ========================================================================
 ** ========================================================================
 *  飞达反馈引脚配置
 * ======================================================================== */

// 启用反馈线检测功能（微动开关检测料带张力）
//  如果硬件上没有接微动开关，请注释掉下面这行
// 【历史 2026-10-14】曾屏蔽：P3.4 行程开关未接，悬空抖动会误触发"限位到位"
//   → 自动进料 → 舵机猛转 → LVR 复位（M610 S1 重启根因）。
// 【2026-10-16】开关已接（P3.4、NO接P3.4/COM接GND、按下拉低），恢复本行；
//   防悬空：Feedback_Pin_Init 已开内部上拉（松开=HIGH 才判未到位，抖动免疫）。
#define HAS_FEEDBACKLINES

#ifdef HAS_FEEDBACKLINES
    // 反馈微动开关引脚：P3.4
    // 硬件接法：NO端接P3.4，COM端接GND
    // 按下=LOW(正常)，松开=HIGH(故障，依靠内部上拉)
    #define FEEDER_FEEDBACK_PORT  GPIO_Port_3
    #define FEEDER_FEEDBACK_PIN   GPIO_Pin_4
#endif

/* ========================================================================
 *  独立功能按键引脚配置 (P3.5)
 * ======================================================================== */
// 按键接法：一端接 P3.5，一端接 GND；内部上拉，按下=LOW（低有效）
// 功能：短按=手动进料一次(忽略反馈)；双击=显示循环(地址/固件年份/错误)；
//       长按5s进入待确认(地址闪烁3s倒计时)，期间再短按=恢复出厂，超时取消
#define FEEDER_KEY_PORT    GPIO_Port_3
#define FEEDER_KEY_PIN     GPIO_Pin_5

/* ========================================================================
 *  电源开关 SGM2593AD 引脚配置（舵机 + N20 共用一路）
 * ======================================================================== */
// SGM2593AD 是配电开关（非 DCDC）：EN 高=导通，集成可编程电流限制、过温保护。
// nFAULT：N-MOSFET 开漏输出，过流（内部 15ms 去抖）/过温时拉低，故障恢复自动拉高。
// 接法：nFAULT 经上拉接 P3.2（输入，准双向内部弱上拉）；EN 接 P3.7（推挽输出）。
// 故障处理：nFAULT 低 → EN=0 断电保护 + 数码管 E3 + M602 error:4；
//           nFAULT 回高且稳定 1s → EN=1 恢复供电，自动清除。
// 未焊接该芯片时注释掉 HAS_PWR_FAULT 即可（零代码开销）。
// 【X41】打开：SGM2593AD 未焊时 P3.2(nFAULT) 悬空，power.c 已加 4K 内部强上拉（悬空读高=无故障），
//   不误报 E3；焊芯片后 nFAULT 开漏拉低仍能覆盖检测。10-14 屏蔽理由（P3.2 悬空弱上拉误报）已消除。
#define HAS_PWR_FAULT
#ifdef HAS_PWR_FAULT
    #define PWR_FAULT_PORT   GPIO_Port_3
    #define PWR_FAULT_PIN    GPIO_Pin_2   // P3.2 nFAULT 输入（低=故障）
    #define PWR_EN_PORT      GPIO_Port_3
    #define PWR_EN_PIN       GPIO_Pin_7   // P3.7 EN 输出（高=导通）
    SBIT(PWR_EN, 0xB0, 7);               // P3 地址 0xB0，bit7 → P3.7
    // 仅控制 N20 支路（舵机直连电源不受控）：上电默认断电，
    // 进料排程成功 Power_EnableNow() 通电、周期收尾 Power_DisableNow() 断电（按需供电）
#endif

/* ========================================================================
 *  舵机 PWM 消声开关（X48）
 * ======================================================================== */
// 进料周期完成后关闭舵机 PWM 输出（舵机断电），消除 SG92R 模拟舵机
// 保持力矩时的"吱吱"声。前提：机构有棘轮止逆，料带不靠舵机力矩保持；
// 若机构无自锁、料带会回退，将此宏改为 0 = 持续输出（保持力矩、有轻微吱声）。
#define SERVO_OFF_AFTER_MOVE 1

/* ========================================================================
 *  舵机 PWM 引脚配置
 * ======================================================================== */
// 同样建议从 feeder.c 移到此处集中管理
#define SERVO_PWM_PORT       GPIO_Port_3
#define SERVO_PWM_PIN        GPIO_Pin_3      // P3.3 → TIM2_CH7
#define SERVO_PWM_TIM        TIM2
#define SERVO_PWM_CHANNEL    TIM_Channel_7


 /*  曲柄滑块机构角度计算参考：https://de.wikipedia.org/wiki/Schubkurbel
 *
 *  设置以下参数前，请确保 MIN/MAX 脉宽值与实际舵机的物理行程对应：
 *    - MIN_PULSEWIDTH 对应舵机 0° 位置（拨杆完全收回）
 *    - MAX_PULSEWITH  对应舵机 180° 位置（拨杆完全推出）
 *
 *  正确校准后：
 *    - 拨杆在 ~90° 时处于最远推送位置
 *    - 拨杆在 ~20° 时处于完全收回位置
 *
 *  【收回状态】俯视示意图：
 *    .
 *   /_\
 *    |            ← 料带前进方向
 *    |
 *  -x-----        ← 推进拨杆，x 落入料带孔中推动料带
 *  -------
 *  ----
 *  ----
 *  ----
 *  ----
 *  -------
 *  -------
 *  -------
 *  -\\o\\-         ← 连杆 / Pleuelstange
 *    \\\\\
 *     \\\\\
 *      \\\\\
 *       \\\\\
 *     O====o\\
 *     ^   \\\\\
 *     \_ 舵机位于 FEEDER_DEFAULT_RETRACT_ANGLE（约 20°）
 *
 *  【完全推出状态】俯视示意图：
 *    .
 *   /_\
 *    |            ← 料带前进方向
 *    |
 *  -x-----        ← 推进拨杆，x 落入料带孔中推动料带
 *  -------
 *  ----
 *  ----
 *  ----
 *  ----
 *  -------
 *  -------
 *  -------
 *  -||o||-         ← 连杆 / Pleuelstange
 *   |||||
 *   |||||
 *   |||||
 *   |||||
 *   |||||
 *   ||o||
 *     :
 *     :            ← 舵机摇臂完全伸出
 *     :
 *     :
 *     0            ← 舵机位于 FEEDER_DEFAULT_FULL_ADVANCED_ANGLE（约 90°）
 */

// 拨杆完全推出时的舵机角度 [°]
// 通常设为 90°，对应料带前进一个完整齿距的位置
#define FEEDER_DEFAULT_FULL_ADVANCED_ANGLE  90

// 拨杆半推时的舵机角度 [°]
// 用于 0401 等小封装元件的半步进料
// 理论计算值为 43.85°，实际需根据机械间隙微调
#define FEEDER_DEFAULT_HALF_ADVANCED_ANGLE  44

// 拨杆完全收回时的舵机角度 [°]
// 理论值为 20°，此处设为 15° 作为安全余量，防止舵机堵转或过流
#define FEEDER_DEFAULT_RETRACT_ANGLE        15

// 单次机械进料的固定行程 [mm]
// 由曲柄滑块机构的几何尺寸决定，硬件设计固定为 4mm
// ?? 此宏必须在 FEEDER_DEFAULT_FEED_LENGTH 之前定义，否则预处理器展开为 0
#define FEEDER_MECHANICAL_ADVANCE_LENGTH    4

// 默认进料长度 [mm]
// 当上位机发送 M600 指令未指定长度时，使用此默认值
// 默认等于单次机械行程，即每次 M600 触发一次完整的推-收动作
#define FEEDER_DEFAULT_FEED_LENGTH          FEEDER_MECHANICAL_ADVANCE_LENGTH

// 舵机运动稳定等待时间 [ms]
// 从完全推出回到完全收回所需的时间，包含运动时间和机械回弹稳定时间
// ?? 原注释标注为 uint8_t（最大 255ms），此处建议调用方使用 uint16_t 存储
// 若实测 200ms 不够导致料带未到位就返回 OK，请适当增大此值
#define FEEDER_DEFAULT_TIME_TO_SETTLE       240

// N20 到位等待窗口默认值 [ms]
// 进料周期（舵机动作序列）完成后，限位开关最迟需在此窗口内到位（pin=0）
// 多步进料（如 16mm = 4 次舵机动作）时 N20 拉带可能滞后于舵机，留出窗口避免误报；
// 窗口内到位 → ok；窗口超时仍未到位 → E1 + error（N20 堵转/卡料/料带用完）
// 存于 FeederSettings_t.n20_timeout_ms（EEPROM，M620 T 可调）；0 = 周期完成立即检查（等效无窗口）
// 仅 HAS_FEEDBACKLINES 编译
#define FEEDER_DEFAULT_N20_TIMEOUT_MS      800

/* ========================================================================
 *  舵机 PWM 脉宽参数
 * ========================================================================
 *
 *  SG90 舵机典型脉宽范围：
 *    0°   → ~544 ?s（最小脉宽）
 *    90°  → ~1500 ?s（中位脉宽）
 *    180° → ~2400 ?s（最大脉宽）
 *
 *  等效 Arduino 调用：SERVO.attach(PIN, 544, 2400);
 *  以下参数需根据实际舵机规格书或实测微调
 */

// 舵机 0° 位置对应的 PWM 脉宽 [?s]
// 应使拨杆处于完全收回状态，过小会导致舵机异响或抖动
#define FEEDER_DEFAULT_MOTOR_MIN_PULSEWIDTH  550

// 舵机 180° 位置对应的 PWM 脉宽 [?s]
// 应使拨杆处于完全推出状态，过大会导致舵机堵转过热
#define FEEDER_DEFAULT_MOTOR_MAX_PULSEWIDTH   2400



// 反馈信号检查开关
// 0 = 每次进料前检查反馈信号，异常时报错（推荐生产环境使用）
// 1 = 跳过反馈检查，始终返回 OK（仅用于调试或无传感器的简易飞达）
#define FEEDER_DEFAULT_IGNORE_FEEDBACK       0






/* ========================================================================
 *  ADC 模拟量采集配置
 * ======================================================================== */

// ADC 轮询周期 [ms]
// 定时器按此间隔读取 ADC 原始值并缓存，上位机查询时直接返回缓存值
// 避免在串口响应中阻塞等待 ADC 转换完成
#define ADC_READ_EVERY_MS  20

/* ------------------------------------------------------------------------
 *  ADC 通道缩放因子与偏移量
 *  换算公式：物理量 = (ADC原始值 / 1023.0) * SCALING_FACTOR + OFFSET
 *  ?? 所有浮点常量添加 f 后缀，避免 SDCC 隐式 double→float 转换警告
 *  ?? OFFSET 负数加括号，防止宏展开时运算符优先级错误
 * ------------------------------------------------------------------------ */

// A0 通道：NXP 真空传感器
// 气压 [kPa] = (ADC/1023 - 0.92) / 0.007652 ≈ ADC/1023 * 0.1277 - 120.23
/*
#define ANALOG_A0_SCALING_FACTOR  0.1277f
#define ANALOG_A0_OFFSET          (-120.23f)

// A1 通道：NXP 真空传感器（与 A0 相同传感器型号）
#define ANALOG_A1_SCALING_FACTOR  0.1277f
#define ANALOG_A1_OFFSET          (-120.23f)

// A2~A7 通道：默认直通（缩放=1，偏移=0），按需修改
#define ANALOG_A2_SCALING_FACTOR  1.0f
#define ANALOG_A2_OFFSET          0.0f

#define ANALOG_A3_SCALING_FACTOR  1.0f
#define ANALOG_A3_OFFSET          0.0f

#define ANALOG_A4_SCALING_FACTOR  1.0f
#define ANALOG_A4_OFFSET          0.0f

#define ANALOG_A5_SCALING_FACTOR  1.0f
#define ANALOG_A5_OFFSET          0.0f

#define ANALOG_A6_SCALING_FACTOR  1.0f
#define ANALOG_A6_OFFSET          0.0f

#define ANALOG_A7_SCALING_FACTOR  1.0f
#define ANALOG_A7_OFFSET          0.0f
*/
/* ========================================================================
 *  ? 以下为内部定义区，请勿随意修改
 * ======================================================================== */

// EEPROM 布局（Bootloader IAP：STC-ISP EEPROM=28K，IAP 地址=物理地址）：
//   0x6A00~0x6BFF：IAP 升级标志（1 扇区，Bootloader/App 共用）
//   0x6C00~0x6DFF：FeederSettings_t 配置（M799 只擦此扇区）
//   0x6E00~0x6FFF：持久化兜底 ChipID（M799 不擦除）
//   程序区：Bootloader 0x0000~0x0FFF；App 0x1000~0x69FF；
//   注：App 链接必须 --code-loc 0x1000 --code-size 23040 -Wl-bHOME=0x1000。
#define EEPROM_FEEDER_SETTINGS_ADDRESS_OFFSET  0x6C00
#define EEPROM_FALLBACK_ID_OFFSET               0x6E00   // 兜底 ChipID 持久化（扇区首地址）

/* ========================================================================
 *  IAP_CONTR 软复位控制字（X62，整字写入，不走"读-改-写"）
 *  bit7 IAPEN | bit6 SWBS(1=复位后进ISP区) | bit5 SWRST(1=触发软复位)
 *  FwLib 的 IAP_SetRestartFrom()+IAP_SoftReset() 是两个"读-改-写"宏
 *  （SDCC 生成 anl a,_IAP_CONTR / orl _IAP_CONTR,#0x20）：只要 IAP_CONTR
 *  的 SWBS 位在某批次/型号上读回为 0，第二次 orl 就会把 SWBS 冲成 0，
 *  结果"复位回用户程序区"而不是进 ISP。整字写入没有这个歧义。
 *    M788 进 ISP：0xE0（IAPEN=1, SWBS=1, SWRST=1）
 *                 若实测 0xE0 仍不进 ISP，把本宏改 0x60（不置 IAPEN）再试
 *    M799 回用户区：0xA0（IAPEN=1, SWBS=0, SWRST=1）
 * ======================================================================== */
#define IAP_CONTR_SWRST_TO_ISP   0xE0
#define IAP_CONTR_SWRST_TO_USER  0xA0

// 串口命令接收缓冲区最大长度（字节）
// 单条 M-code 指令不得超过此长度，超出部分将被截断丢弃
#define MAX_BUFFFER_MCODE_LINE  64

/* ========================================================================
 *  SDCC + STC8H1K08 平台补充定义
 * ======================================================================== */

// ENABLED / DISABLED 兼容宏
// 原 Arduino 框架内置定义，SDCC 环境中需手动补充
// gcode.c 等模块中使用 ENABLED(x) 进行条件判断
#ifndef ENABLED
  #define ENABLED(x)   ((x) != 0)
#endif
#ifndef DISABLED
  #define DISABLED(x)  ((x) == 0)
#endif


uint16_t millis(void);

/* ========================================================================
 *  CharliePlex 数码管引脚映射
 * ========================================================================
 //转移到 seg.h 统一管理
*/
#endif // _CONFIG_H