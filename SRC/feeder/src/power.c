#include "power.h"
#include "fw_hal.h"
#include "config.h"
#include "seg.h"
#include "gcode.h"      // extern __xdata Feeder_t feeders[]（恢复时显示地址）

#ifdef HAS_PWR_FAULT

// 去抖参数
#define PWR_DEBOUNCE_CNT  2       // 连续采样次数（10ms/次）确认故障
#define PWR_POLL_MS       10      // 轮询周期

// 状态变量 volatile：防止 SDCC 优化器做跨调用值推断删掉恢复逻辑。
// 注意：SDCC 4.2.0 --opt-code-size 下对"互斥分支写+后续读"的状态变量
// 会产生 warning 110/126 误报（static/volatile/xdata/全局均复现），
// 已用汇编验证生成代码完整（setb/clr PWR_EN、Seg_SetDisplay 均保留），属编译器误报。
static volatile uint8_t  pwr_fault;         // 1=电源故障（已断电保护）
static volatile uint8_t  pwr_debounce;      // 故障确认计数
static volatile uint16_t pwr_lastChk;       // 上次轮询时间戳 [ms]

// ========================================================================
//  N20 电源控制（SGM2593AD，仅 N20 支路；舵机直连电源不受控）
//
//  设计：N20 只在进料周期通电（按需供电）——
//    Power_EnableNow()  进料排程成功时（Feeder_Advance）：N20 通电拉带
//    Power_DisableNow() 周期收尾时（Feeder_CycleEnd）：N20 断电
//  上电默认断电（EN=0）：调测/装料带时 N20 不转（不用手压压板）；
//  几十个飞达同时接电也无 N20 拉带浪涌（舵机无脉冲不动作，电流很小）。
// ========================================================================
void Power_Init(void)
{
    GPIO_P3_SetMode(PWR_EN_PIN, GPIO_Mode_Output_PP);
    // nFAULT 开漏输出：准双向（内部弱上拉）读电平，低=故障
    GPIO_P3_SetMode(PWR_FAULT_PIN, GPIO_Mode_InOut_QBD);
    // 【X41】SGM2593AD 未焊时 P3.2 悬空：准双向弱上拉会被噪声拉低→误报 E3。
    // 加 4K 内部强上拉：悬空稳定读高（无故障）；焊芯片后 nFAULT 开漏拉低仍能覆盖检测。
    GPIO_SetPullUp(PWR_FAULT_PORT, PWR_FAULT_PIN, 1);
    PWR_EN = 0;   // N20 默认断电
}

void Power_EnableNow(void)
{
    PWR_EN = 1;   // N20 通电（进料拉带）
}

void Power_DisableNow(void)
{
    PWR_EN = 0;   // N20 断电（周期结束/调测）
}

void Power_Update(void)
{
    uint16_t now = millis();
    if ((uint16_t)(now - pwr_lastChk) < PWR_POLL_MS)
        return;
    pwr_lastChk = now;

    // nFAULT 拉低 = 故障（过流/过温，芯片内 15ms 去抖后输出）
    // 注意：GPIO_Port_3 是端口号常量(=3U)，不能当 P3 寄存器读！(3 & mask) 恒 0 曾致恒判"故障"
    if ((P3 & PWR_FAULT_PIN) == 0) {
        if (pwr_debounce < PWR_DEBOUNCE_CNT) { pwr_debounce++; return; }
        if (!pwr_fault) {
            pwr_fault = 1;
            PWR_EN = 0;                     // 断电保护（N20）
            Seg_SetDisplay(DISP_ERR_PWR, 0, 0, 0);   // 数码管 E3
        }
        return;
    }

    // nFAULT 高 = 无故障（EN=0 时输出关闭、过流消失，电平稳定）
    pwr_debounce = 0;
    if (pwr_fault) {
        // 故障已解除：清标志、回地址显示。
        // EN 保持现状（周期外为 0），由下次进料 Power_EnableNow() 重试；
        // 若短路仍在，nFAULT 将再次拉低 → E3（进料时自动重试清除）。
        pwr_fault = 0;
        Seg_SetDisplay(DISP_ADDR, feeders[0].settings.rs485_addr, 0, 0);
    }
}

bool Power_IsFault(void)
{
    return pwr_fault;
}

#else
/* ========================================================================
 *  未焊接 SGM2593AD（HAS_PWR_FAULT 未定义）：空桩实现
 *  避免 main.c/feeder.c 的无条件调用导致链接错误。
 *  调试期用；硬件装芯片/上拉后恢复 config.h 的 HAS_PWR_FAULT 即启用真实逻辑。
 * ======================================================================== */
void Power_Init(void) {}
void Power_EnableNow(void) {}
void Power_DisableNow(void) {}
void Power_Update(void) {}
bool Power_IsFault(void) { return false; }

#endif // HAS_PWR_FAULT
