#ifndef _FEEDER_H
#define _FEEDER_H

#include "config.h"
#include "version.h"



// STC8H 唯一芯片 ID 定义
// 复用 FwLib 的 ID_ROMADDR 宏，其根据 __CONF_MCU_MODEL 自动匹配各型号：
//   STC8H1K17 => __CID_ADDR(0x43E7)+18 = 0x43F9（17K ROM 末 7 字节）
//   STC8H1K28 => __CID_ADDR(0x6FE7)+18 = 0x6FF9（28K ROM 末 7 字节）【X51】
// 原硬编码 0xFDE4 是 64K 系列地址，本型号读取会出错，已修复。
#define CHIP_ID_ADDR        ID_ROMADDR
#define CHIP_ID_LEN         3       // ID 长度， 3字节足够，7字节太长了，浪费空间

// 错峰上报参数
#define STAGGER_SLOTS       256     // 64节点×4倍安全系数，向上取2的幂
#define STAGGER_SLOT_MS     10      // 10ms/槽，最大窗口=2.56s
#define REPORT_SLOT_MS      10      // 10ms/槽，最大窗口=2.56s
#define REPORT_MAX_RETRY    1       // 64节点规模1次重传足够
#define RETRY_BACKOFF_MS    30      // 重传退避固定30ms)

/* ========================================================================
 *  飞达状态与错误枚举定义
 * ======================================================================== */

// 飞达运行状态机
typedef enum {
    FEEDER_STATE_DISABLED = 0,          // 禁用状态（M610 S0 触发）
    FEEDER_STATE_IDLE,                  // 空闲状态，等待进料指令或手动触发
    FEEDER_STATE_MOVING,                // 舵机正在运动中，等待稳定时间
    FEEDER_STATE_ADV_CYCLE_COMPLETED    // 进料周期完成，等待上位机确认
} FeederState_t;

// 飞达当前位置标记
typedef enum {
    POS_UNKNOWN = 0,            // 未知位置
    POS_RETRACT ,               // 完全收回位置
    POS_HALF_ADVANCED,          // 半推位置（用于0401等小封装半步进料）
    POS_FULL_ADVANCED           // 完全推出位置
} FeederPosition_t;

// 飞达错误状态
typedef enum {
    ERR_OK_NO_FEEDBACK = 0,     // 无反馈线，默认视为正常
    ERR_OK,                     // 有反馈线且信号正常
    ERR_IGNORED,                // 有错误但配置为忽略
    ERR_FAULT,                  // 反馈线检测到故障
    ERR_PWR_FAULT               // 电源过流/过温（SGM2593AD nFAULT，已断电保护）
} FeederErrorState_t;

// 飞达操作指令
typedef enum
{
    cmdSetup,
    cmdUpdate,
    cmdEnable,
    cmdDisable,
    cmdInitializeWithId,
    cmdFactoryReset
}FeederCmd_t;

/* 异步错峰应答：用于M602全局查询，非阻塞延时发送 */
typedef struct
{
    uint8_t pending;
    uint16_t triggerTick;
    uint8_t localAddr;
    uint8_t config_ver;
    FeederErrorState_t err;
    uint8_t enabled;                 /* 1=使能 0=禁用，全局 M602 也带 */
    uint8_t chipid[3];
} AsyncReply_t;


/* ========================================================================
 *  飞达设置结构体
 * ========================================================================
 * ⚠️ 此结构体直接映射到 EEPROM 存储格式
 * 修改成员顺序或类型后必须更新 CONFIG_VERSION
 * SDCC 不支持结构体按值传递/返回，所有接口均使用指针
 */
//config.h中的 #define CONFIG_VERSION 1 //飞达结构体变更时，版本号需要加1，用于判断飞达中的参数是否可用，触发恢复出厂设置，对应于结构体中的 config_ver 字段
// 配置结构体：飞达全部参数 + RS485地址 + 通信模式
// 单飞达，主副本/备份副本都使用这个结构体
typedef struct {

    //uint8_t  comm_mode;              // 通信模式：0=RS485，1=UART调试  取消，没必要了，硬件上直接从uart接线
    uint8_t  rs485_addr;             // RS485总线地址
    uint8_t  ignore_feedback;        // 是否忽略进料反馈 0:检测 1:忽略
    uint8_t  config_ver;            // 飞达配置参数版本，0表示出厂默认，>=1表示用户修改过配置
    uint8_t  full_advanced_angle;    // 完全进料角度
    uint8_t  half_advanced_angle;    // 半进料角度
    uint8_t  retract_angle;          // 舵机回退角度
    uint8_t  feed_length;            // 进料长度参数
    uint16_t time_to_settle;         // 运动稳定等待时间(ms)
    uint16_t motor_min_pulsewidth;   // 舵机最小脉宽 us
    uint16_t motor_max_pulsewidth;   // 舵机最大脉宽 us
    uint16_t n20_timeout_ms;         // N20 到位等待窗口(ms)，0=周期完成立即检查（M620 T 可调）

} FeederSettings_t;






/* ========================================================================
 *  飞达实例结构体（替代原 C++ 类）
 * ========================================================================
 * STC8H1K08 RAM 仅 1K，每个实例约 28 字节
 * 多飞达场景建议用数组管理，避免动态内存分配
 */
typedef struct {
    int8_t           feederNo;              // 飞达编号 (-1=未初始化)  这个实际可以跟485地址绑定，单飞达就没必要了
    uint8_t          chip_id[CHIP_ID_LEN];     // 【新增】芯片唯一ID运行时缓存
    
    FeederSettings_t settings;              // 当前运行参数
    FeederState_t    state;                 // 运行状态
    FeederPosition_t position;              // 当前物理位置
    FeederPosition_t lastPosition;          // 上次位置（用于检测位置变化）
    uint8_t          remainingFeedLen;      // 剩余待进料长度 [mm]
    uint16_t         lastPosChangeTime;     // 上次位置变化的时间戳 [ms]
    uint8_t          restoreDisabled;       // 按键短按临时使能标志：进料周期结束恢复禁用态
#ifdef HAS_FEEDBACKLINES
    uint8_t          lastBtnState;          // 上次按钮电平（消抖用）
    uint8_t          fbTickCounter;         // 反馈线消抖计数器
    uint16_t         lastFbCheckTime;       // 上次反馈线检查时间 [ms]
    uint8_t          n20TimeoutActive;      // N20 进料超时报警标志（等待窗口超时且限位未到位，显示 E1）
    uint16_t         n20WaitStart;          // N20 到位等待窗口起始时间戳 [ms]（0=未在等待）
#endif
} Feeder_t;

/* ========================================================================
 *  公共 API 函数声明
 * ========================================================================
 * 所有函数第一个参数均为 Feeder_t* 实例指针（模拟 C++ this 指针）
 */
// 系统滴答1ms回调，定时器中断调用
void Feeder_SysTick_Handler(void);
// 获取当前飞达编号
int8_t Feeder_GetFeederNo(const Feeder_t *f);
// 获取当前飞达设置版本号
uint8_t Feeder_GetConfigVersion(void);



// 获取当前错误状态枚举
FeederErrorState_t Feeder_GetErrorState(const Feeder_t *f);

// 初始化飞达实例，绑定编号
void Feeder_Init(Feeder_t *f, uint8_t feederNo);

// 检查飞达是否已初始化
bool Feeder_IsInitialized(const Feeder_t *f);

// 首次上电调用：加载EEPROM配置、初始化舵机PWM、归位到收回位置
void Feeder_Setup(Feeder_t *f);

// 获取当前设置（通过输出指针返回，SDCC不允许返回结构体）
void Feeder_GetSettings(const Feeder_t *f, FeederSettings_t *out);

// 更新运行时设置（不自动保存EEPROM，需显式调用Save）
void Feeder_SetSettings(Feeder_t *f, const FeederSettings_t *newSet);

// 从 EEPROM 加载设置到 RAM
void Feeder_LoadSettings(Feeder_t *f);

// 将 RAM 中的设置写入 EEPROM
void Feeder_SaveSettings(Feeder_t *f);

// 恢复出厂设置（将默认值写入EEPROM）
void Feeder_FactoryReset(Feeder_t *f);

// 通过串口输出当前配置（M620格式）
void Feeder_OutputCurrentSettings(const Feeder_t *f);

void Feeder_GotoPostPick(Feeder_t *f);

void Feeder_GotoRetract(Feeder_t *f);

//控制飞达的舵机运动到“回退/复位”位置，并更新内部状态机
void Feeder_GotoPostPick(Feeder_t *f);
//半推0402用
void Feeder_GotoHalfAdv(Feeder_t *f);
//全推
void Feeder_GotoFullAdv(Feeder_t *f);

// 触发进料动作，feedLength=0 则忽略；overrideErr=true 跳过故障检查
bool Feeder_Advance(Feeder_t *f, uint8_t feedLength, bool overrideErr);

// 检查飞达是否处于正常工作状态
bool Feeder_IsOk(const Feeder_t *f);

// 获取详细错误状态枚举
FeederErrorState_t Feeder_GetErrorState(const Feeder_t *f);

// 启用飞达（进入IDLE状态）
void Feeder_Enable(Feeder_t *f);

// 禁用飞达
void Feeder_Disable(Feeder_t *f);

// 主循环调用：处理状态机推进、手动进料消抖、舵机时序控制
// ⚠️ 必须在 main() while(1) 中周期性调用，建议每 1~5ms 一次
void Feeder_Update(Feeder_t *f);

// 手动设置舵机角度（调试用）
void Feeder_GotoAngle(Feeder_t *f, uint8_t angle);

// 【X48】舵机 PWM 输出使能控制（1=开 0=关）：620 在 disabled 状态设置后
//   Feeder_Setup 会重新输出 retract 脉宽并保持力矩，此时断电消声
void Feeder_Servo_SetPower(uint8_t on);



//通过 RS485 总线上报本机芯片ID与当前地址
bool RS485_Send_ID_Report(const uint8_t *chip_id, uint8_t addr);
// 设置 RS485 地址 (自动切回 RS485 模式并持久化)
bool Comm_Set_RS485_Addr(Feeder_t *f, uint8_t new_addr);
// 强制设置通信模式 (持久化到 EEPROM)
bool Comm_Set_Mode(Feeder_t *f, uint8_t mode);
// 检查命令是否应由当前飞达处理（根据地址匹配）
bool Comm_ShouldProcessCommand(Feeder_t *f, uint8_t target_addr_in_cmd);
//比较飞达的chipid和上位机输入的十六进制字符串是否匹配
uint8_t Comm_MatchChipID(uint8_t *binId, char *hexStr);
// 校验配置版本，判断EEPROM配置是否需要初始化/恢复出厂
bool Config_CheckNeedInit(Feeder_t *f);




#endif // _FEEDER_H


