#include "feeder.h"
#include "fw_hal.h"
#include <string.h>
//#include <stdio.h>
#include "gcode.h" 
#include <stdarg.h>
#include <my_sprintf.h>
#include "seg.h"     // N20 超时报警显示（E1）
#include "power.h"   // 电源故障（SGM2593AD nFAULT）

// ========== // 引用 main.c 中的全局时基变量 ==========
extern volatile uint16_t sysTick_ms;
// ========================================

#define SERVO_PERIOD_US 20000U

/* ========================================================================
 *  平台适配层：舵机 PWM 控制 (P3.3 = PWMB_PWM7)
 * ======================================================================== */
static void Servo_PWM_On(void);   // 前向声明：定义在 Servo_WritePulse 之后，消除隐式声明警告
static void Servo_PWM_Off(void);

static void Servo_PWM_Init(void)
{
    // P3.3 推挽输出
    GPIO_P3_SetMode(SERVO_PWM_PIN, GPIO_Mode_Output_PP);
    
    // PWMB_PWM7 映射到 P3.3，SDCC不能直接对SFRX做移位运算！
    SFRX_ON();
    uint8_t temp_ps = PWMB_PS;   // 【修复】先读到临时变量
    temp_ps &= ~(0x03U << 4);
    temp_ps |=  (0x01U << 4);
    PWMB_PS = temp_ps;
    SFRX_OFF();
    
    // 停止 PWMB 以便配置
    PWMB_SetCounterState(0);
    
    // 边沿对齐，向上计数
    PWMB_SetEdgeAlignment(PWM_EdgeAlignment_Side);
    PWMB_SetCounterDirection(PWM_CounterDirection_Up);
    
    // 预分频: 24MHz / (11+1) = 2MHz → 0.5?s/tick
    PWMB_SetPrescaler(11);
    
    // 周期: 20ms = 20000?s / 0.5?s - 1 = 39999
    // 【X31修复】STC8H 16位寄存器官方规则：必须先写高字节再写低字节！
    //  （手册：写高字节时影子寄存器更新被禁止，直到低字节写完。X30误改先低后高→撤销）
    //  EA=0 关中断保证两次写原子性（防任何中断插入破坏影子机制）
    {
        uint8_t arrh = (uint8_t)(39999U >> 8);
        uint8_t arrl = (uint8_t)(39999U & 0xFF);
        EA = 0;
        SFRX_ON();
        PWMB_ARRH = arrh;
        PWMB_ARRL = arrl;
        SFRX_OFF();
        EA = 1;
    }
    
    // 配置 PWMB.3 (PWM7) 为 PWM 模式1 (CNT<CCR 时高电平)
    PWMB_PWM3_ConfigOutputMode(PWM_OutputMode_PWM_HighIfLess);
    PWMB_PWM3_SetComparePreload(0);  // 输出关闭后再写 CCR；关闭预装载，避免首脉冲仍是旧角度
    PWMB_PWM3_SetPortPolar(0);       // 高电平有效
    PWMB_PWM3_SetPortState(1);       // 使能输出
    // 【X26修复】主输出使能 MOE（PWMB_BKR bit7）：STC8H 高级 PWM 与 STM32 同架构，
    // 复位 MOE=0 时 PWM 输出被整体封锁（X26 实测 BKR=80 生效）。
    PWMB_SetOverallState(1);
    // 【X27修复】ENO7P 引脚输出使能——必须包在 SFRX_ON()（EAXFR=1）内写！
    //  X26 直写漏了 SFRX_ON → EAXFR=0 时扩展 SFR 写入无效（读回 00），
    //  而 PWMB_SetOverallState 等宏自带 SFRX_ON/OFF 所以 BKR/PS 都写进去了。
    SFRX_ON();
    PWMB_ENO = 0x10U;
    SFRX_OFF();
    
    // 使能 ARR 预装载
    PWMB_SetAutoReloadPreload(1);
    
    // 初始脉宽1500?s中位：1500 /0.5 = 3000，【修复】纯整数，去掉浮点数
    // 【X31修复】同上：先高后低 + EA关中断原子性
    {
        uint8_t ccrh = (uint8_t)(3000U >> 8);
        uint8_t ccrl = (uint8_t)(3000U & 0xFF);
        EA = 0;
        SFRX_ON();
        PWMB_CCR7H = ccrh;
        PWMB_CCR7L = ccrl;
        SFRX_OFF();
        EA = 1;
    }
    
    // 启动 PWMB
    PWMB_SetCounterState(1);
}


static void Servo_WritePulse(uint16_t pulseWidthUs)
{
    /* 先关输出再写 CCR：否则会先输出上一周期的旧脉宽，
     * 表现为回退时先冲大角度，或前进时先反向一下。 */
    Servo_PWM_Off();
    uint16_t ccr;
    if (pulseWidthUs < 400U)  pulseWidthUs = 400U;
    if (pulseWidthUs > 2500U) pulseWidthUs = 2500U;
    
    // 原：ccr = (uint16_t)(pulseWidthUs / 0.5f);
    // 修复：除以0.5等价乘以2，纯整数运算，无浮点
    ccr = pulseWidthUs * 2U;
    // 【X31修复】STC8H 16位寄存器官方规则：必须先写高字节再写低字节！
    //  （手册§22：写高字节时影子更新被禁止直到低字节写完。X30误改先低后高→撤销）
    //  EA=0 关中断保证两次写原子性；显式高/低字节变量防SDCC优化混淆
    {
        uint8_t ccrh = (uint8_t)(ccr >> 8);
        uint8_t ccrl = (uint8_t)(ccr & 0xFF);
        EA = 0;
        SFRX_ON();
        PWMB_CCR7H = ccrh;
        PWMB_CCR7L = ccrl;
        SFRX_OFF();
        EA = 1;
    }
    Servo_PWM_On();   // 新 CCR 写完后才使能输出
}

static void Servo_WriteAngle(uint16_t minPw, uint16_t maxPw, uint8_t angle)
{
    uint16_t pulse;
    uint16_t span;
    if (angle > 180U) angle = 180U;
    // 【X34压缩】避免32位乘除库(_divulong/_mullong 占~500B)：改16位"先除后乘"
    //  span=1985 → 1985/180=11（整除）→ 全行程误差 ≤5µs(≈0.4°) 舵机无感；
    //  若直接用 (max-min)*angle 则 16位溢出（X32根因）。span%180 余数仅5µs，忽略。
    span = maxPw - minPw;
    pulse = minPw + (uint16_t)((span / 180U) * angle);
    Servo_WritePulse(pulse);
    
}

/* 【X48】舵机 PWM 输出使能/关闭（消除运动结束后的"吱吱"保持力矩声）
 *  On:  ENO7P=0x10 输出使能；Off: ENO=0 输出关闭（舵机断电，无保持力矩）
 *  关 PWM 前提：机构有棘轮止逆，料带不靠舵机力矩保持。
 *  写 ENO 为扩展 SFR，必须 SFRX_ON()（EAXFR=1）内写（X27 教训）。
 */
static void Servo_PWM_On(void)
{
    SFRX_ON();
    PWMB_ENO = 0x10U;
    SFRX_OFF();
}

static void Servo_PWM_Off(void)
{
    SFRX_ON();
    PWMB_ENO = 0x00U;
    SFRX_OFF();
    P33 = 0;          /* 关闭 PWM 后把 P3.3 拉低，避免信号线浮空 */
}

// 导出接口（gcode.c 620 在 disabled 场景使用）：1=开 PWM，0=关 PWM
void Feeder_Servo_SetPower(uint8_t on)
{
    if (on) Servo_PWM_On();
    else    Servo_PWM_Off();
}


//调用示例 Servo_WriteAngle(500U,2500U,90U); //90度，中位1500us

/* ========================================================================
 *  平台适配层：IAP EEPROM 读写 (修正版)
 * ======================================================================== */

/* ========================================================================
 *  IAP EEPROM 读写 (完全适配 IOsetting fw_iap.h)
 * ======================================================================== */
static void IAP_ReadBlock(uint16_t addr, void *buf, uint16_t len)
{
    uint8_t *p = (uint8_t *)buf;
    uint16_t i;
    
    // 【注意】IAP_CmdRead 宏内部已包含 EA=0/1 和触发序列
    // 无需手动 SFRX_ON/OFF，也无需手动写 IAP_TRIG
    for (i = 0; i < len; i++) {
        IAP_CmdRead(addr + i);
        p[i] = IAP_ReadData();
    }
}

static void IAP_WriteBlock(uint16_t addr, const void *buf, uint16_t len)
{
    uint16_t sectorAddr = addr & 0xFE00U;  // 512字节扇区对齐
    const uint8_t *p = (const uint8_t *)buf;
    uint16_t i;

    // 1. 擦除整个扇区 (必须先擦后写)
    IAP_CmdErase(sectorAddr);
    
    // 2. 逐字节写入
    for (i = 0; i < len; i++) {
        IAP_WriteData(p[i]);       // 先写数据到 IAP_DATA 寄存器
        IAP_CmdWrite(addr + i);    // 再执行写命令宏
    }
}

extern uint16_t millis(void);

/* --------------------------------------------------------------------
 * 兜底 ChipID 持久化（EEPROM 扇区1，偏移 EEPROM_FALLBACK_ID_OFFSET）
 * 布局 6 字节：magic0(0xC1) magic1(0x4D) id0 id1 id2 checksum(异或)
 * 仅在寄存器/ROM拷贝均失效（全FF）时生成并写入；独立于配置扇区，M799 不擦除。
 * ------------------------------------------------------------------ */
#define FALLBACK_ID_MAGIC0  0xC1
#define FALLBACK_ID_MAGIC1  0x4D

// 三字节 ID 有效判定：非全 FF、非全 00（寄存器 / ROM 拷贝 / 兜底记录三级通用）
static uint8_t ChipID_IsValid(const uint8_t id[CHIP_ID_LEN])
{
    return !((id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF) ||
             (id[0] == 0x00 && id[1] == 0x00 && id[2] == 0x00));
}

static uint8_t FallbackID_Load(uint8_t out[CHIP_ID_LEN])
{
    uint8_t buf[6];
    uint8_t i;
    IAP_ReadBlock(EEPROM_FALLBACK_ID_OFFSET, buf, 6);
    if (buf[0] != FALLBACK_ID_MAGIC0 || buf[1] != FALLBACK_ID_MAGIC1)
        return 0;
    // 兜底 ID 自身全 FF/全 00 视为无效（防御性校验）
    if (!ChipID_IsValid(&buf[2]))
        return 0;
    if ((buf[0] ^ buf[1] ^ buf[2] ^ buf[3] ^ buf[4]) != buf[5])
        return 0;
    for (i = 0; i < CHIP_ID_LEN; i++)
        out[i] = buf[2 + i];
    return 1;
}

static void FallbackID_Save(const uint8_t id[CHIP_ID_LEN])
{
    uint8_t buf[6];
    buf[0] = FALLBACK_ID_MAGIC0;
    buf[1] = FALLBACK_ID_MAGIC1;
    buf[2] = id[0];
    buf[3] = id[1];
    buf[4] = id[2];
    buf[5] = buf[0] ^ buf[1] ^ buf[2] ^ buf[3] ^ buf[4];
    IAP_WriteBlock(EEPROM_FALLBACK_ID_OFFSET, buf, 6);
}

// 随机兜底 ID：STC8H 无硬件 RNG，熵 = millis() 滚动值 ^ xdata 尾部上电残差，LFSR 迭代 3 字节。
// 3 字节 1677 万空间，64 台规模碰撞概率极低；仅作总线区分，出厂前上位机核查无重复即可。
static void ChipID_RandomGen(uint8_t out[CHIP_ID_LEN])
{
    uint16_t m = millis();
    uint8_t ent = (uint8_t)m ^ (uint8_t)(m >> 8);
    ent ^= *(uint8_t volatile __xdata *)0x3F8;  // xdata 尾部未清零上电残差（XDATA 实际占用 ≤0x1D3）
    ent ^= *(uint8_t volatile __xdata *)0x3F9;
    uint8_t i;
    for (i = 0; i < CHIP_ID_LEN; i++) {
        ent = (uint8_t)((ent << 1) ^ ((ent & 0x80) ? 0x1D : 0x00));
        out[i] = ent;
    }
    if (!ChipID_IsValid(out))
        out[0] = 0xA0;
}

/* ========================================================================
 *  平台适配层：反馈引脚 GPIO 操作
 * ======================================================================== */
#ifdef HAS_FEEDBACKLINES


static void Feedback_Pin_Init(void)
{
    // 外部已有上拉（2026-10-16 确认），高阻输入即可，无需内部上拉。
    // 直写 P3M1:P3M0=10（高阻输入），省 FwLib 宏展开代码（flash 余量紧张）。
    // 历史教训(2026-10-14)悬空抖动：外部上拉已接，松开=HIGH 稳定，不会误判到位。
    P3M1 |= 0x10;    // P3M1.4=1 → 高阻输入
    P3M0 &= ~0x10;   // P3M0.4=0
}


static uint8_t Feedback_Pin_Read(void)
{
    // P3.4：按下=LOW(到位)，松开=HIGH(未到位)
    // 注意：GPIO_Port_3 是端口号常量(=3U)，不能当 P3 寄存器读！(3 & 0x10) 恒 0 曾致"永远到位"
    return (P3 & FEEDER_FEEDBACK_PIN) ? 1U : 0U;
}

#endif // HAS_FEEDBACKLINES

/* ========================================================================
 *  飞达核心逻辑实现
 * ======================================================================== */
void Feeder_Init(Feeder_t *f, uint8_t feederNo)
{
    memset(f, 0, sizeof(Feeder_t));
    f->feederNo = (int8_t)feederNo;
    f->state    = FEEDER_STATE_DISABLED;
    f->position = POS_RETRACT;
    // 填充芯片唯一ID：M602上报 / ChipID匹配 / M620回显均依赖此字段
    // 原代码漏调 Load_ChipID，导致 chip_id 恒为0，已修复
    {
        uint8_t i;
        uint8_t hw0;
        uint8_t valid = 0;
        #ifdef DEBUG
        // 【X59·诊断】落级编号 + 各级原始快照：两次刷机上电各抓一行，即可定位首次失效层级
        uint8_t src = 0;   // 落级：1=寄存器 2=ROM拷贝 3=EEPROM兜底 4=随机生成
        uint8_t rawReg[CHIP_ID_LEN] = {0};
        uint8_t rawRom[CHIP_ID_LEN] = {0};
        uint8_t rawFb[CHIP_ID_LEN]  = {0};
        #endif
        // 【X57】ChipID 四级来源优先级（任一级读出无效 → 自动落到下一级）：
        //  ① CHIPID 只读寄存器(xdata 0xFDE0，仅手册 §9.4 名单内型号具备；首字节=0xF7 型号码)——OTP 值，与擦除状态无关；
        //     STC8H1K28 不在 §9.4 寄存器型号名单内：MOVX 空闲页回读 0xFF ≠ 0xF7，自动落空级（自适应校验，非故障）；
        //  ② ROM 拷贝区(0x6FF9 起 7 字节，手册 §9.3 表 1K28=6FF9H~6FFFH)——仅 STC-ISP 勾选"程序区结束处添加重要测试参数"才会烧入；
        //  ③ EEPROM 扇区1 已持久化的兜底 ID（独立扇区不随 M799 丢失）；
        //  ④ 三者均无效 → 随机生成 3 字节兜底 ID 并写回扇区1 持久化。
        SFRX_ON();
        hw0 = *(uint8_t volatile __xdata *)0xFDE0;
        SFRX_OFF();

        if (hw0 == 0xF7) {
            // ① 名单内型号：CHIPID 寄存器读（OTP 出厂值，ID0/1 为型号码 1K28=F724、1K17=F737）；读出全 FF/00 则落级
            SFRX_ON();
            for (i = 0; i < CHIP_ID_LEN; i++)
                f->chip_id[i] = *(uint8_t volatile __xdata *)(0xFDE0 + i);
            SFRX_OFF();
            valid = ChipID_IsValid(f->chip_id);
            #ifdef DEBUG
            if (valid) src = 1;
            for (i = 0; i < CHIP_ID_LEN; i++) rawReg[i] = f->chip_id[i];
            #endif
        }
        if (!valid && *(uint8_t __code *)CHIP_ID_ADDR != 0xFF) {
            // ② ROM 拷贝区可用：三字节不全 FF/00 才采用
            for (i = 0; i < CHIP_ID_LEN; i++)
                f->chip_id[i] = *(uint8_t __code *)(CHIP_ID_ADDR + i);
            valid = ChipID_IsValid(f->chip_id);
            #ifdef DEBUG
            if (valid) src = 2;
            #endif
        }
        #ifdef DEBUG
        // ②被首字节 FF 条件跳过也照读三字节，供诊断对照（未命中时 rawRom=ROM 原始值）
        if (!valid) {
            for (i = 0; i < CHIP_ID_LEN; i++)
                rawRom[i] = *(uint8_t __code *)(CHIP_ID_ADDR + i);
        } else {
            for (i = 0; i < CHIP_ID_LEN; i++) rawRom[i] = f->chip_id[i];
        }
        #endif
        if (!valid && FallbackID_Load(f->chip_id)) {
            // ③ EEPROM 兜底 ID（magic + checksum 校验由 Load 完成）
            valid = 1;
            #ifdef DEBUG
            src = 3;
            for (i = 0; i < CHIP_ID_LEN; i++) rawFb[i] = f->chip_id[i];
            #endif
        }
        if (!valid) {
            // ④ 全部失效：生成随机兜底 ID 并持久化到扇区1
            ChipID_RandomGen(f->chip_id);
            FallbackID_Save(f->chip_id);
            #ifdef DEBUG
            src = 4;
            #endif
        } else {
            /* BL 广播只读 0x6E00；即使 ID 来自 ROM/寄存器，也保证兜底区一致 */
            uint8_t fbc[CHIP_ID_LEN];
            uint8_t need = 0;
            uint8_t k;
            if (!FallbackID_Load(fbc)) need = 1;
            else {
                for (k = 0; k < CHIP_ID_LEN; k++)
                    if (fbc[k] != f->chip_id[k]) { need = 1; break; }
            }
            if (need) FallbackID_Save(f->chip_id);
        }
        #ifdef DEBUG
        // 诊断行：src 落级；hw0=0xFDE0 首字节；REG/ROM/FB=各级原始三字节；FINAL=最终采用值
        my_printf("CID src:%d hw0:%X REG:%X%X%X ROM:%X%X%X FB:%X%X%X FINAL:%X%X%X\r\n",
            src, hw0,
            rawReg[0], rawReg[1], rawReg[2],
            rawRom[0], rawRom[1], rawRom[2],
            rawFb[0],  rawFb[1],  rawFb[2],
            f->chip_id[0], f->chip_id[1], f->chip_id[2]);
        #endif
    }
}

bool Feeder_IsInitialized(const Feeder_t *f)
{
    return (f->feederNo >= 0);
}

void Feeder_Setup(Feeder_t *f)
{
    Feeder_LoadSettings(f);
    Servo_PWM_Init();
    
#ifdef HAS_FEEDBACKLINES
    Feedback_Pin_Init();
#endif
    
    Servo_WriteAngle(
        f->settings.motor_min_pulsewidth,
        f->settings.motor_max_pulsewidth,
        f->settings.retract_angle
    );
    f->position = POS_RETRACT;
    /* 上电默认禁用: 等上位机 M610 S1、短按使能或 M600 前显式使能。
     * 总电源刚打开时总览/详情先显示 dis，避免一上电就是 en。 */
    f->state    = FEEDER_STATE_DISABLED;
}

void Feeder_GetSettings(const Feeder_t *f, FeederSettings_t *out)
{
    memcpy(out, &f->settings, sizeof(FeederSettings_t));
}

void Feeder_SetSettings(Feeder_t *f, const FeederSettings_t *newSet)
{
    memcpy(&f->settings, newSet, sizeof(FeederSettings_t));
#ifdef DEBUG
    my_printf("updated feeder settings\r\n");
    Feeder_OutputCurrentSettings(f);
#endif
}

void Feeder_LoadSettings(Feeder_t *f)
{
   // uint16_t addr = EEPROM_FEEDER_SETTINGS_ADDRESS_OFFSET 
   //               + (uint16_t)f->feederNo * sizeof(FeederSettings_t);
   uint16_t addr = EEPROM_FEEDER_SETTINGS_ADDRESS_OFFSET;

    IAP_ReadBlock(addr, &f->settings, sizeof(FeederSettings_t));
    
#ifdef DEBUG
    my_printf("loaded settings from eeprom:\r\n");
    Feeder_OutputCurrentSettings(f);
#endif
}

void Feeder_SaveSettings(Feeder_t *f)
{
    //uint16_t addr = EEPROM_FEEDER_SETTINGS_ADDRESS_OFFSET 
    //              + (uint16_t)f->feederNo * sizeof(FeederSettings_t);
    uint16_t addr = EEPROM_FEEDER_SETTINGS_ADDRESS_OFFSET;

    IAP_WriteBlock(addr, &f->settings, sizeof(FeederSettings_t));
    
#ifdef DEBUG
    my_printf("stored settings to eeprom:\r\n");
    Feeder_OutputCurrentSettings(f);
#endif
}

void Feeder_FactoryReset(Feeder_t *f)
{
    f->settings.full_advanced_angle  = FEEDER_DEFAULT_FULL_ADVANCED_ANGLE;
    f->settings.half_advanced_angle  = FEEDER_DEFAULT_HALF_ADVANCED_ANGLE;
    f->settings.retract_angle        = FEEDER_DEFAULT_RETRACT_ANGLE;
    f->settings.feed_length          = FEEDER_DEFAULT_FEED_LENGTH;
    f->settings.time_to_settle       = FEEDER_DEFAULT_TIME_TO_SETTLE;
    f->settings.motor_min_pulsewidth = FEEDER_DEFAULT_MOTOR_MIN_PULSEWIDTH;
    f->settings.motor_max_pulsewidth = FEEDER_DEFAULT_MOTOR_MAX_PULSEWIDTH;
    f->settings.n20_timeout_ms       = FEEDER_DEFAULT_N20_TIMEOUT_MS;
    f->settings.ignore_feedback      = FEEDER_DEFAULT_IGNORE_FEEDBACK;
    f->settings.rs485_addr           = RS485_ADDR_DEFAULT ;
    f->settings.config_ver           = CONFIG_VERSION;   // 标记：出厂默认
    Feeder_SaveSettings(f);
}

void Feeder_OutputCurrentSettings(const Feeder_t *f)
{
     my_printf("Feeder %d Config Ver:%d\r\n", f->settings.rs485_addr, f->settings.config_ver);
    my_printf("M%d N%d A%d B%d C%d F%d U%d V%d W%d",
           MCODE_UPDATE_FEEDER_CONFIG,
           //f->feederNo, 这里没用了，改用飞达485地址
           f->settings.rs485_addr,
           
           f->settings.full_advanced_angle,
           f->settings.half_advanced_angle,
           f->settings.retract_angle,
           f->settings.feed_length,
           f->settings.time_to_settle,
           f->settings.motor_min_pulsewidth,
           f->settings.motor_max_pulsewidth);
#ifdef HAS_FEEDBACKLINES
    my_printf(" X%d T%d", f->settings.ignore_feedback, f->settings.n20_timeout_ms);
#endif
    my_printf("\r\n");
   
    
}
void Feeder_GotoPostPick(Feeder_t *f)
{
    if(f->position == POS_FULL_ADVANCED)
    {
        Feeder_GotoRetract(f);
#ifdef DEBUG
        my_printf("gotoPostPickPosition retracted feeder\r\n");
#endif
    }
    else
    {
#ifdef DEBUG
        my_printf("gotoPostPickPosition didn't need to retract feeder\r\n");
#endif
    }
}

void Feeder_GotoRetract(Feeder_t *f)
{
    Servo_WriteAngle(f->settings.motor_min_pulsewidth,
                     f->settings.motor_max_pulsewidth,
                     f->settings.retract_angle);
    f->position = POS_RETRACT;
    f->state    = FEEDER_STATE_MOVING;
}

void Feeder_GotoHalfAdv(Feeder_t *f)
{
    Servo_WriteAngle(f->settings.motor_min_pulsewidth,
                     f->settings.motor_max_pulsewidth,
                     f->settings.half_advanced_angle);
    f->position = POS_HALF_ADVANCED;
    f->state    = FEEDER_STATE_MOVING;
}

void Feeder_GotoFullAdv(Feeder_t *f)
{
    Servo_WriteAngle(f->settings.motor_min_pulsewidth,
                     f->settings.motor_max_pulsewidth,
                     f->settings.full_advanced_angle);
    f->position = POS_FULL_ADVANCED;
    f->state    = FEEDER_STATE_MOVING;
}

void Feeder_GotoAngle(Feeder_t *f, uint8_t angle)
{
    Servo_WriteAngle(f->settings.motor_min_pulsewidth,
                     f->settings.motor_max_pulsewidth,
                     angle);
#ifdef DEBUG
    my_printf("going to %d deg\r\n", angle);
#endif
}

bool Feeder_Advance(Feeder_t *f, uint8_t feedLength, bool overrideErr)
{
#ifdef DEBUG
    my_printf("advance triggered\r\n");
#endif
    
    if (!Feeder_IsOk(f)) {
        if (!overrideErr) return false;
#ifdef DEBUG
        my_printf("overridden error temporarily\r\n");
#endif
    }
    
    if (feedLength == 0) {
#ifdef DEBUG
        my_printf("advance ignored, 0 feedlength\r\n");
#endif
        return true;
    }
    
    if (f->state != FEEDER_STATE_IDLE) {
#ifdef DEBUG
        my_printf("advance ignored, feeder busy (state=%d)\r\n", f->state);
#endif
        return false;
    }
    
    f->remainingFeedLen = feedLength;
#ifdef HAS_FEEDBACKLINES
    f->n20WaitStart = 0;    // 新进料：重置 N20 到位等待窗口
#endif
#ifdef HAS_PWR_FAULT
    Power_EnableNow();      // 进料排程成功：N20 通电拉带（按需供电）
#endif
    return true;
}

bool Feeder_IsOk(const Feeder_t *f)
{
    return (Feeder_GetErrorState(f) != ERR_FAULT);
}

FeederErrorState_t Feeder_GetErrorState(const Feeder_t *f)
{
    // 电源故障（SGM2593AD nFAULT）优先级最高：过流/过温已断电保护
    if (Power_IsFault()) return ERR_PWR_FAULT;
#ifdef HAS_FEEDBACKLINES
    uint8_t pinLevel = Feedback_Pin_Read();
    
    if (pinLevel == 0) {
        return ERR_OK;
    } else {
        if (f->settings.ignore_feedback) return ERR_IGNORED;
        return ERR_FAULT;
    }
#else
    (void)f;
    return ERR_OK_NO_FEEDBACK;
#endif
}

void Feeder_Enable(Feeder_t *f)  { f->state = FEEDER_STATE_IDLE; }
void Feeder_Disable(Feeder_t *f) { f->state = FEEDER_STATE_DISABLED; }
// 进料周期收尾：按键短按临时使能（restoreDisabled 置位）时恢复禁用态，
// 否则回 IDLE。被 M601 上位机进料与按键手动进料共用。
static void Feeder_CycleEnd(Feeder_t *f)
{
#ifdef HAS_PWR_FAULT
    Power_DisableNow();     // 周期收尾：N20 断电（到位机械已断/超时/故障时真断电保护）
#endif
#ifdef SERVO_OFF_AFTER_MOVE
    /* 只有回到 RETRACT 才关 PWM；停 FULL 时必须保持力矩，取料刚性才成立。 */
    if (f->position == POS_RETRACT) Servo_PWM_Off();
#endif
    if (f->restoreDisabled) {
        f->restoreDisabled = 0;
        f->state = FEEDER_STATE_DISABLED;
    } else {
        f->state = FEEDER_STATE_IDLE;
    }
}

void Feeder_Update(Feeder_t *f)
{
    uint16_t now = millis();
    
#ifdef HAS_FEEDBACKLINES
    // N20 超时报警恢复：限位开关到位（pin=0）后自动清除 E1，恢复地址显示
    if (f->n20TimeoutActive && Feedback_Pin_Read() == 0) {
        f->n20TimeoutActive = 0;
        Seg_SetDisplay(DISP_ADDR, f->settings.rs485_addr, 0, 0);
    }
    if (f->state == FEEDER_STATE_IDLE) {
        if ((now - f->lastFbCheckTime) >= 10UL) {
            uint8_t btnState = Feedback_Pin_Read();
            f->lastFbCheckTime = now;
            
            if (btnState != f->lastBtnState && btnState == 0) {
                f->lastBtnState  = btnState;
                f->fbTickCounter = 1;
            } else if (btnState != f->lastBtnState) {
                f->lastBtnState = btnState;
            }
            
            if (f->fbTickCounter > 0) {
                f->fbTickCounter++;
                if (f->fbTickCounter > 5 && btnState == 1) {
                    Feeder_Advance(f, f->settings.feed_length, true);
                    f->fbTickCounter = 0;
                }
                if (f->fbTickCounter > 50) {
                    f->fbTickCounter = 0;
                }
            }
        }
    } else {
        f->fbTickCounter = 0;
    }
#endif
    
    if (f->lastPosition != f->position) {
        f->lastPosChangeTime = now;
        f->lastPosition      = f->position;
    }
    
    if ((now - f->lastPosChangeTime) >= f->settings.time_to_settle) {
        if (f->state == FEEDER_STATE_ADV_CYCLE_COMPLETED) {
            /* 原版语义：最后一段完成停 FULL，ok；回退由 M601 负责。 */
#ifdef HAS_FEEDBACKLINES
            // N20 到位等待窗口：
            // 周期完成（舵机动作序列走完）后，不立即判死，先给 N20 拉带留出到位时间
            // （多步进料如 16mm=4 次舵机动作，N20 可能滞后于舵机，避免"速度快了"误报）
            if (!f->settings.ignore_feedback) {
                if (Feedback_Pin_Read() == 0) {
                    // 限位已到位 → 周期正常完成，回复 ok（对应 Arduino 原版
                    // Serial.println("ok, advancing cycle completed")）
                    f->n20WaitStart = 0;
                    sendAnswer(0, "advancing done");
                    Feeder_CycleEnd(f);
                } else if (f->n20WaitStart == 0) {
                    // 限位未到位：进入等待窗口（最长 settings.n20_timeout_ms，M620 T 可调）
                    f->n20WaitStart = now;
                } else if (Power_IsFault()) {
                    // 电源过流/过温（E3 已由 power.c 显示并断电）：立即结束周期，
                    // 报 error 但不显示 E1——真实原因是电源故障，E3 优先
                    f->n20WaitStart = 0;
                    sendAnswer(1, "pwr fault");
                    Feeder_CycleEnd(f);
                } else if ((uint16_t)(now - f->n20WaitStart) >= f->settings.n20_timeout_ms) {
                    // 窗口超时仍未到位：N20 堵转/卡料/料带用完 → E1 + error
                    f->n20TimeoutActive = 1;
                    f->n20WaitStart = 0;
#ifdef DEBUG
                    my_printf("N20 feed timeout (tape not seated)\r\n");
#endif
                    Seg_SetDisplay(DISP_ERR_N20_TOUT, 0, 0, 0);   // 数码管 E1
                    sendAnswer(1, "N20 feed timeout");
                    Feeder_CycleEnd(f);
                }
                // 未到位且在窗口内：保持 ADV_CYCLE_COMPLETED，下一轮继续检查
            } else
#endif
            {
                // 无反馈线/忽略反馈：周期完成直接 ok
                sendAnswer(0, "advancing done");
                Feeder_CycleEnd(f);
            }
        }
        
        if (f->remainingFeedLen == 0) {
            /* 不再强制回退；最后一段保持 FULL，等 M601 做 post-pick。 */
            if (f->state == FEEDER_STATE_MOVING) {
                f->state = FEEDER_STATE_ADV_CYCLE_COMPLETED;
            } else if (f->state != FEEDER_STATE_DISABLED &&
                       f->state != FEEDER_STATE_ADV_CYCLE_COMPLETED) {
                f->state = FEEDER_STATE_IDLE;
            }
            return;
        }
        
        f->state = FEEDER_STATE_MOVING;
        
        switch (f->position) {
            case POS_RETRACT:
                if (f->remainingFeedLen >= FEEDER_MECHANICAL_ADVANCE_LENGTH) {
                    Feeder_GotoFullAdv(f);
                    f->remainingFeedLen -= FEEDER_MECHANICAL_ADVANCE_LENGTH;
                } else if (f->remainingFeedLen >= FEEDER_MECHANICAL_ADVANCE_LENGTH / 2) {
                    Feeder_GotoHalfAdv(f);
                    f->remainingFeedLen -= FEEDER_MECHANICAL_ADVANCE_LENGTH / 2;
                }
                break;
            case POS_HALF_ADVANCED:
                if (f->remainingFeedLen >= FEEDER_MECHANICAL_ADVANCE_LENGTH / 2) {
                    Feeder_GotoFullAdv(f);
                    f->remainingFeedLen -= FEEDER_MECHANICAL_ADVANCE_LENGTH / 2;
                }
                break;
            case POS_FULL_ADVANCED:
                Feeder_GotoRetract(f);
                break;
            default:
                break;
        }
        
        if (f->remainingFeedLen == 0) {
            f->state = FEEDER_STATE_ADV_CYCLE_COMPLETED;
        }
    }
}
/**
 * @brief 系统滴答1ms回调
 * @note  如果你的定时器 ISR 已经在 main.c 中直接做了 sysTick_ms++，
 *        则此函数可以留空或仅作为扩展钩子。
 *        保留它只是为了满足链接器对符号的定义要求。
 */
void Feeder_SysTick_Handler(void)
{
    // sysTick_ms 已在 main.c 的定时器 ISR 中递增
    // 此处可作为飞达专属的高优先级时基钩子（当前无需额外操作）
}


/* ========================================================================
 *  RS485通信配置接口函数
 * ======================================================================== */


    /**
 * @brief 读取并缓存 STC8H 唯一芯片 ID，二进制形式存储在 Feeder_t 结构体中
 * @note  在系统初始化最早期调用一次，后续直接使用 结构体中的 g_chip_id
 */

/// @brief 比对二进制ID与十六进制字符串是否匹配
/// @param binId  本机3字节二进制chipid
/// @param hexStr 上位机输入的十六进制字符串，例如 "AABBCC"
/// @return 1=匹配成功，0=格式错误/不匹配

uint8_t Comm_MatchChipID(uint8_t *binId, char *hexStr)
{
    uint8_t i;
    uint8_t len = CHIP_ID_LEN; //固定3字节，对应6个hex字符
    if(hexStr == NULL) return 0;
    for(i = 0; i < len; i++)
    {
        char c1 = hexStr[i*2];
        char c2 = hexStr[i*2 + 1];
        if(c1 == '\0' || c2 == '\0') return 0; // 字符串提前截断，长度不足
        
        uint8_t val = 0;
        // 高4位
        if(c1 >= '0' && c1 <= '9') val |= (c1 - '0') << 4;
        else if(c1 >= 'A' && c1 <= 'F') val |= (c1 - 'A' + 10) << 4;
        else if(c1 >= 'a' && c1 <= 'f') val |= (c1 - 'a' + 10) << 4;
        else return 0; //非法字符
        // 低4位
        if(c2 >= '0' && c2 <= '9') val |= (c2 - '0');
        else if(c2 >= 'A' && c2 <= 'F') val |= (c2 - 'A' + 10);
        else if(c2 >= 'a' && c2 <= 'f') val |= (c2 - 'a' + 10);
        else return 0; //非法字符
        if(val != binId[i])
            return 0;
    }
    // 6位hex之后只能是字符串结束 或者 空格（G代码下一个参数分隔）
    char endCh = hexStr[len*2];
    if(endCh != '\0' && endCh != ' ')
    {
        return 0;
    }
    return 1;
}





/**
 * @brief 校验配置版本，判断EEPROM配置是否需要初始化/恢复出厂
 * @retval 0: 正常，版本匹配，无需操作
 * @retval 1: 需要恢复出厂（版本不匹配/非法）
 */
bool Config_CheckNeedInit(Feeder_t *f)
{
    // g_feeder_cfg.config_ver 是EEPROM读取出来的值
    if(f->settings.config_ver == 0U)
    {
        // 出厂默认配置，合法，不需要恢复出厂
        return 0;
    }
    if(f->settings.config_ver != CONFIG_VERSION)
    {
        // 用户配置，但版本不匹配，必须恢复出厂
        return 1;
    }
    // 用户配置，版本匹配
    return 0;
}
