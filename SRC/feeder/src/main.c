/**
 * @file main.c
 * @brief STC8H1K08 OpenPnP 飞达控制器 (RS485 通信 + N20 反馈检测)
 * @note  从 Arduino (mgrl v0.3) 移植到 STC8H/SDCC 平台。
 *        1. 用非阻塞状态机替换了阻塞式 delay()，确保串口不丢包。
 *        2. 加入了 RS485 DE/RE 硬件流向控制。
 *        3. 实现了基于 N20 反馈引脚的进料校验逻辑。
 */
#include "fw_hal.h"
#include "config.h"
#include "uart.h"     // 带环形缓冲和 RS485 控制的 UART 驱动
#include "adc.h"      // ADC 缩放与采集驱动
#include "feeder.h"   // 非阻塞舵机/N20 状态机驱动
#include "gcode.h"    // 纯 C 语言 G-code 解析器
//#include "eeprom.h"   // STC8H IAP EEPROM 读写封装
#include <string.h>
//#include <stdio.h>
#include <stdarg.h>
#include <my_sprintf.h>
#include "seg.h"
#include "key.h"     // P3.5 独立功能按键（短按进料/双击显示循环/长按恢复出厂）
#include "power.h"   // 电源开关 SGM2593AD（nFAULT 过流检测 + EN 控制）

/* ========================================================================
 *  固件版本静态标识
 *  version.h 由 gen_fw_ver.ps1 自动生成：数值宏 FW_YYMM/FW_DD_BN 供 M602 运行时
 *  上报；字符串副本 FW_YYMM_STR/FW_DD_BN_STR 由脚本直接产出（#x 会把数值后缀
 *  U 一并字符串化且无法剥离，故不在本单元做字符串化）。
 *  启动行前缀 "DDFW:" 固定 5 字节：上位机解析 hex 镜像时扫描该前缀即可取
 *  待刷包版本（yymm.ddbn），无需先让飞达上线；运行时板上版本仍走 M602 FW 字段。
 * ======================================================================== */
#define FW_VER_STR  FW_YYMM_STR "." FW_DD_BN_STR

/* ========================================================================
 *  全局变量定义 (gcode.h 中声明的外部变量，供解析器访问)
 * ======================================================================== */
// 飞达实例数组 (管理所有飞达的状态、引脚、配置)
__xdata Feeder_t feeders[NUMBER_OF_FEEDER];

/*
// 存储在 EEPROM 中的全局公共配置结构体
typedef struct {
  char version[4]; // 固件版本号，用于检测是否需要恢复出厂设置
#ifdef HAS_ANALOG_IN
  float adc_scaling_values[2][2]; // 8个ADC通道的缩放系数 [斜率, 偏移量]
#endif
} sCommonSettings;
sCommonSettings commonSettings; // 运行时使用的全局配置实例
// 恢复出厂设置时使用的默认配置 (存储在 ROM/Code 区)
static const sCommonSettings commonSettings_default = {
  {CONFIG_VERSION},
#ifdef HAS_ANALOG_IN
  {
    {ANALOG_A0_SCALING_FACTOR, ANALOG_A0_OFFSET},
    {ANALOG_A1_SCALING_FACTOR, ANALOG_A1_OFFSET},
    {ANALOG_A2_SCALING_FACTOR, ANALOG_A2_OFFSET},
    {ANALOG_A3_SCALING_FACTOR, ANALOG_A3_OFFSET},
    //{ANALOG_A4_SCALING_FACTOR, ANALOG_A4_OFFSET},
    //{ANALOG_A5_SCALING_FACTOR, ANALOG_A5_OFFSET},
    //{ANALOG_A6_SCALING_FACTOR, ANALOG_A6_OFFSET},
    //{ANALOG_A7_SCALING_FACTOR, ANALOG_A7_OFFSET},
  },
#endif
};
#ifdef HAS_ANALOG_IN
static uint32_t lastTimeADCread = 0; // 记录上一次 ADC 采样的时间戳
#endif
*/

AsyncReply_t g_asyncReply;
/* 注：原 lastTimeADCread 节拍变量已随 8 通道采样循环删除（堵转检测已取消） */



/* ========================================================================
 *  系统滴答定时器 (提供 1ms 精度的非阻塞时间基准)
 * ======================================================================== */
volatile uint16_t sysTick_ms = 0;
// 获取系统运行时间 (类似 Arduino 的 millis())
uint16_t millis(void) {
  uint16_t t;
 // uint16_t 16bit读取是原子操作，无需关中断
  t = sysTick_ms;
  return t;
}
// Timer0 中断服务函数 (每 1ms 触发一次)
void TIM0_ISR_Handler(void) __interrupt 1 {
  // 【关键】软件重载：Timer0 为 16 位模式(模式1)，溢出后回 0 不自动重载，
  // 必须在此重新写入初值，否则周期变成 65536 ticks = 32.77ms @24MHz（慢 33 倍）
  TH0 = 0xF8;   // 2000 ticks @12T/24MHz = 1ms
  TL0 = 0x30;
  sysTick_ms++;
  
  // 【关键】将 1ms 滴答喂给飞达状态机，用于内部精确计时
  // (如 N20 电机超时检测、舵机 PWM 脉宽计算)
  Feeder_SysTick_Handler(); 
//==== SEG MODIFY ====
  Seg_Refresh(); // X9恢复：1ms中断内执行CharliePlex数码管扫描刷新（X1实验曾禁用定位）
//==== SEG MODIFY END ====
}

/* ========================================================================
 *  UART1 中断服务函数【X12根因修复：必须定义在含 main() 的模块！】
 *
 *  SDCC 4.2.0 的 mcs51 后端：HOME 中断向量表只由"含 main() 的编译单元"
 *  生成，且只包含该单元内声明的中断函数。此前 ISR 定义在 uart.c
 *  （跨编译单元），0x0023（UART1 向量）处没有生成跳转指令，
 *  ES=1 时收到任意字节 → 中断触发 → CPU 跳到 0x0023 执行启动代码
 *  （垃圾指令）→ 跑飞死机。X11 轮询版（ES=0 关中断）不死与此吻合。
 *  修复：ISR 移入 main.c，向量表即包含 0x0023 → ljmp UART1_ISR_Handler。
 *  缓冲变量 rx_buf/rx_head/rx_tail 已在 uart.h 全局化（extern）。
 * ======================================================================== */
void UART1_ISR_Handler(void) __interrupt 4
{
    /* 只处理 RI 接收（发送已由 UART_WriteRaw 内 ES=0 隔离，TI 由发送函数清除） */
    if (RI) {
        RI = 0;
        uint8_t next_head = rx_head + 1;
        if(next_head >= UART_RX_BUF_SIZE) next_head = 0;
        if (next_head != rx_tail) {
            rx_buf[rx_head] = SBUF;
            rx_head = next_head;
        }
    }
}
// 初始化 Timer0 为 1ms 中断（直接寄存器操作，SDCC兼容，移除HAL结构体）
static void Timer0_Init(void)
{
  TMOD |= 0x01;   // Timer0: 16bit模式
  TH0 = 0xF8;
  TL0 = 0x30;
  ET0 = 1;       // 使能Timer0中断（X9恢复：X2实验曾禁用定位，UART修复后恢复）
  TR0 = 1;        // 启动Timer0
  EA = 1;         // 开启全局中断
}
/* ========================================================================
 *  飞达指令分发器 (用于对所有飞达执行统一操作，目前固件用于单飞达控制器)
 * ======================================================================== */
void executeCommandOnAllFeeder(FeederCmd_t cmd)
{
  uint8_t i;
  for(i = 0; i < NUMBER_OF_FEEDER; i++)
  {
    /*
     * Use an if/else chain instead of switch here.  Some SDCC builds
     * assign the same numeric value to command aliases, which makes a
     * switch generate duplicate case-label errors.
     */
    if (cmd == cmdSetup) {
      Feeder_Setup(&feeders[i]);
    } else if (cmd == cmdUpdate) {
      Feeder_Update(&feeders[i]);
    } else if (cmd == cmdEnable) {
      Feeder_Enable(&feeders[i]);
    } else if (cmd == cmdDisable) {
      Feeder_Disable(&feeders[i]);
    } else if (cmd == cmdInitializeWithId) {
      Feeder_Init(&feeders[i], i);
    } else if (cmd == cmdFactoryReset) {
      Feeder_FactoryReset(&feeders[i]);
    }
  }
}
/* ========================================================================
 *  硬件基础初始化
 * ======================================================================== */
static void System_Init(void) {
  // 系统时钟 24MHz：依赖 STC-ISP 下载时校准（选项区写入 23.995MHz，误差 -0.02%）
  // 不再调用 SYS_SetClock()——它会重写 IRCBAND/IRTRIM（IRTRIM=0）覆盖 ISP 校准值，
  // 实测覆盖后 24M 档频率偏离，导致 UART 115200 乱码（26.10.13 验证）
  // 约束：必须用 STC-ISP 官方工具下载（"输入用户程序运行时的IRBC频率=24.000MHz"），
  //       不能用 stc8isp.py 等第三方脚本（不写选项区，芯片会回出厂默认 11.0592MHz）
  IAP_SetEnabled(1);      // 使能 IAP 模块 (对应 IAP_CONTR.7 = 1)
  IAP_SetWaitTime();      // 根据 __CONF_FOSC 自动计算并设置等待时间
  
  // TODO: 在此处初始化飞达总电源控制引脚 (FEEDER_ENABLE_PIN)
  // 以及各路电源输出引脚，默认全部拉低 (关闭电源)
  //初始化数码管gpio脚
  Seg_GPIO_Init();
  Key_Init();   // 初始化 P3.5 功能按键
}
/* ========================================================================
 *  安全启动序列 (严格复刻 mgrl v0.3 的防抖与防失控逻辑)
 * ======================================================================== */
/* 静态版本标识：只保留在 hex 中供上位机扫描包版本，不再运行时打印 DDFW。
 * 运行时打印会和上电 ok ready 抢总线，多台同时上电时更容易撞车。 */
static const __code volatile char g_fwTag[] = "DDFW:" FW_VER_STR;
static volatile __data uint8_t g_fwTagKeep;

static void Setup_Sequence(void) {
  g_fwTagKeep = (uint8_t)g_fwTag[0];   /* volatile 读取, 防止链接器丢掉静态 DDFW */
  // 清空 G-Code 接收缓冲区
  setupGCodeProc();
  // 【步骤 1】：为所有飞达分配 ID。这里为飞达分配的feederno是 0~(因为固件是给单个飞达的所以都是0，NUMBER_OF_FEEDER-1)。
  // 必须在读取 EEPROM 之前执行，因为 EEPROM 的地址偏移量依赖于飞达 ID。
  executeCommandOnAllFeeder(cmdInitializeWithId);
  // 【步骤 2】：从 EEPROM 加载全局公共配置
  //EEPROM_ReadBlock(EEPROM_COMMON_SETTINGS_ADDRESS_OFFSET,
  //                   (uint8_t *)&commonSettings, sizeof(sCommonSettings));
  Feeder_LoadSettings(&feeders[0]); // 读取配置，单飞达控制器，也就不存在公共配置，而且原公共配置只有版本号和ADC缩放参数，所以直接读取第一个飞达的配置即可
  // 【步骤 3】：配置版本号校验 (防止结构体变更导致读取乱码)
  if (Config_CheckNeedInit(&feeders[0])) {
    // 触发所有飞达恢复出厂默认配置，这里是不是需要等待用户确认？
    executeCommandOnAllFeeder(cmdFactoryReset);
    
  }
  // 【步骤 4】：打印当前的 ADC 缩放配置 (方便用户备份)
  // 注：原 commonSettings 公共配置结构体已废弃（定义被注释），缩放参数由 adc.c 内部管理，
  //     此处打印块引用已删除的结构体，一并移除。
  // 【步骤 5】：舵机安全上电序列 (极其关键！)
  // 原理：单片机刚上电时 PWM 引脚电平不确定。如果直接给舵机供电，
  // 舵机会因为收到错误的脉宽而疯狂打齿甚至烧毁。
  // 正确做法：先上电 -> 发送正确归位脉宽 -> 等待舵机物理到位 -> 断电。
  
  // TODO: digitalWrite(FEEDER_ENABLE_PIN, HIGH); // 开启飞达总电源
  
  // 让所有飞达执行 Setup，输出安全的初始 PWM 脉宽
  // （舵机直连电源：上电无脉冲不动作；PWM 初始化先设中位脉宽再启动，安全）
  executeCommandOnAllFeeder(cmdSetup);
  // 【非阻塞延时】：等待 1000ms 让舵机物理归位。
  // 注意：这里不能用死等，必须在此期间持续调用 listenToSerialStream(),
  // 否则如果上位机在这 1 秒内发送了握手包，会导致串口缓冲区溢出丢包。
  uint16_t startTime = millis();
  while ((millis() - startTime) < 1000) {
    listenToSerialStream(); 
  }
  // 归位完成，将飞达状态机设为 Disable (停止输出 PWM)
  executeCommandOnAllFeeder(cmdDisable);
  // TODO: digitalWrite(FEEDER_ENABLE_PIN, LOW); // 关闭飞达总电源，等待上位机 M1 S1 唤醒
  // 【步骤 6】：初始化 ADC 模块
#ifdef HAS_ANALOG_IN
  ADC_Init();
#endif
  // 【X56】上电自动上报：ready. 后延迟(与广播应答相同双因子公式)广播
  //   ok ready addr:<a> ChipID:<HHHHHH> —— 新飞达插电即上报，上位机秒级发现；
  //   同地址(重复地址)靠 chip 散列微调错开 → 上位机收到"一个addr两个chipid"直接发现冲突。
  //   边延时边监听，不丢上位机握手包（与步骤5归位延时同模式）。
  {
    uint16_t addrPart = (feeders[0].settings.rs485_addr < 99)
        ? (uint16_t)(feeders[0].settings.rs485_addr * 5) : 495U;
    uint16_t repDelay = addrPart + ((feeders[0].chip_id[0] ^ feeders[0].chip_id[1]
                                     ^ feeders[0].chip_id[2]) & 0x0F) * 5;
    uint16_t repStart = millis();
    while ((millis() - repStart) < repDelay) {
      listenToSerialStream();
    }
    my_printf("ok ready addr:%d ChipID:%X%X%X\r\n",
        feeders[0].settings.rs485_addr,
        (unsigned int)feeders[0].chip_id[0],
        (unsigned int)feeders[0].chip_id[1],
        (unsigned int)feeders[0].chip_id[2]);
  }
//==== SEG MODIFY ====
  // 【X45】上电默认 disable：与 M610 S0 显示一致（地址慢闪 1s），提示飞达禁用中；
  //   原为常亮（与命令禁用行为不一致，用户反馈）。
  Seg_SetDisplay(DISP_ADDR, feeders[0].settings.rs485_addr, 0xFFFF, 1000);
//==== SEG MODIFY END ====
}
/* ========================================================================
 *  主函数入口
 * ======================================================================== */
void main(void) {
  // 1. 基础硬件与时钟初始化
  System_Init();
  Timer0_Init();
  
  // 2. 初始化 RS485 串口 (内部会自动配置 DE 引脚并拉低进入接收模式)
  //    提前到自检之前：自检需要串口打印段名，供对照反推 segMap 接线映射
  UART_Init();  
  
//==== SEG MODIFY ====
#if SEG_ONLY_A1
  // 调试：只显示 a1 段常亮，报告实际亮的位置后置 config.h SEG_ONLY_A1=0 恢复正常
  Seg_OnlyA1();
#elif SEG_SELF_TEST
  // 上电自检：14 段逐段常亮 800ms（约 11s，阻塞），每段串口打印段名，
  // 肉眼对照"打印段名 vs 实际亮的位置"反推 segMap 与 PCB 接线的真实映射
  Seg_SelfTest_Run();
#endif
  // 自检结束（或跳过）后进入正常显示：初始化状态 闪烁 --，持续1000ms
  Seg_SetDisplay(DISP_INIT, 0, 1000, 0);
//==== SEG MODIFY END ====
  
  // 2.5 初始化电源开关 SGM2593AD（引脚配置，N20 默认断电 EN=0；
  //     仅控制 N20 支路，舵机直连电源不受控；N20 在进料排程时由
  //     Power_EnableNow() 按需通电、周期收尾 Power_DisableNow() 断电）
  Power_Init();
  
  // 3. 执行安全启动序列 (读 EEPROM、舵机安全归位)
  Setup_Sequence();
  // ================= 核心主循环 =================
  while (1) {
//===== 异步错峰应答任务轮询 =====
if(g_asyncReply.pending == 1)
{
    uint16_t now = millis();
    // 支持uint16溢出回绕，判断是否到达触发时间
    if( (now - g_asyncReply.triggerTick) < 32768U )
    {
        sendAnswerFmt(0, "addr:%d error:%d EN:%d VER:%d ChipID:%X%X%X",
            g_asyncReply.localAddr,
            (int)g_asyncReply.err,
            (int)g_asyncReply.enabled,
            g_asyncReply.config_ver,
            (unsigned int)g_asyncReply.chipid[0],
            (unsigned int)g_asyncReply.chipid[1],
            (unsigned int)g_asyncReply.chipid[2]);
        g_asyncReply.pending = 0; //清除任务
    }
}
    // 【任务 1】：监听 RS485 串口数据流 (非阻塞)
    // 内部自动完成：读环形缓冲区 -> 去除注释 -> Trim -> 解析 G/M 代码 -> 执行动作
    listenToSerialStream();
    // 【任务 2】：轮询更新所有飞达的状态机 (非阻塞)
    // 这是整个系统的"心脏"。舵机的 PWM 生成、N20 电机的运转、
    // 以及 N20 反馈引脚的电平检测，全部依赖这个函数的高频轮询。
    // 如果不调用此函数，电机和舵机将完全罢工。
    executeCommandOnAllFeeder(cmdUpdate);
    // 【任务 3】：电源故障轮询（SGM2593AD nFAULT：过流/过温断电保护 + 自动恢复）
    Power_Update();
    // 【任务 4】：数码管闪烁处理
    // 【X44】tick 用真实毫秒差：原固定传 1ms，空载主循环(<1ms/次)下闪烁计时被放大数倍
    //   （设计 1000ms 实际可能只有 100-500ms）→ "亮1s灭1s"看起来太快。改后计时准确。
    static uint16_t _lastBlinkMs = 0;
    uint16_t _blinkNow = millis();
    Seg_Blink_Process((uint16_t)(_blinkNow - _lastBlinkMs));
    _lastBlinkMs = _blinkNow;

    // 【X47】620 设置快闪(250ms/3s)结束后按当前状态恢复：disabled→1s慢闪 / enabled→常亮
    if (g_dispRestorePending && (uint16_t)(millis() - g_dispRestoreAt) >= 3000U) {
        g_dispRestorePending = 0;
        if (feeders[0].state == FEEDER_STATE_DISABLED)
            Seg_SetDisplay(DISP_ADDR, feeders[0].settings.rs485_addr, 0xFFFF, 1000);
        else
            Seg_SetDisplay(DISP_ADDR, feeders[0].settings.rs485_addr, 0, 0);
    }
    // 【任务 5】：P3.5 功能按键轮询（短按进料/双击显示循环/长按恢复出厂）
    // 【X41】恢复：Key_Read 已修（读 P3 真实电平）+ Key_Init 4K 内部上拉，悬空读高不误触；
    //   10-14 屏蔽时的"P3.5 误触→LVR"归因有误（真因是 GPIO_Port_3 端口号常量当寄存器读，X39 已修）
    Key_Update();
//==== SEG MODIFY END ====
  }
}
