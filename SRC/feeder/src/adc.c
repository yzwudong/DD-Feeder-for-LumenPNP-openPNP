#include "adc.h"      // IOsetting fwlib_stc8 ADC 驱动头文件
#include "config.h"   // 包含项目级宏定义（如通道数）

// ============================================================================
// 宏定义与全局变量
// ============================================================================

// 默认 ADC 通道数 (STC8H 最多支持 15 个外部通道 + 1 个内部参考)
#ifndef NUMBER_OF_ADC_CHANNELS
#define NUMBER_OF_ADC_CHANNELS 8
#endif

/* 注：原 adc_scaling 浮点缩放数组与 ADC_ReadScaled/ADC_SetScaling 已删除。
 *     缩放/浮点运算会拉入 SDCC 浮点库（约 2~4KB ROM），而当前固件
 *     仅堵转检测需要整数 ADC 原始值（ADC_ReadRaw），故移除以压缩代码。
 *     若将来需要 M11/M12 浮点缩放，再恢复。 */

// ============================================================================
// 函数实现
// ============================================================================

/**
 * @brief 初始化 ADC 外设
 * @note  使用 IOsetting fwlib 的纯宏 API 进行配置
 *        注意：本库没有结构体初始化方式，需逐个调用配置宏
 */
void ADC_Init(void)
{
    // 设置 ADC 时钟分频器: ADC_CLK = SYSclk / 2 / (Prescaler + 1)
    // 0x0F = 15 → ADC_CLK = SYSclk / 2 / 16
    // 请根据实际系统主频调整，确保 ADC_CLK 在芯片规格书允许范围内
    ADC_SetClockPrescaler(0x0F);

    // 设置采样保持时间 (单位: ADC 时钟周期)
    // 0x0A = 10 → 采样时间为 11 个 ADC 时钟周期
    // 规格书要求最小值为 10，阻抗较高时建议增大此值
    ADC_SetSampleTime(0x0A);

    // 设置转换结果右对齐 (12-bit 有效数据占据低 12 位)
    // STC8H 系列为 12-bit ADC，右对齐可直接作为 uint16_t 使用
    ADC_SetResultAlignmentRight();

    // 开启 ADC 模块电源
    // 如果需要在低功耗模式下关闭 ADC，可注释此行并在需要时动态开启
    ADC_SetPowerState(ENABLE);
}

/**
 * @brief 读取指定通道的 ADC 原始值 (12-bit)
 * @param channel ADC 通道号 (0 ~ NUMBER_OF_ADC_CHANNELS-1)
 * @return 12-bit 转换结果 (0~4095)，通道非法时返回 0
 * @note  该函数为阻塞式读取，内部自动完成: 选通道→启动→等待→读结果
 */
uint16_t ADC_ReadRaw(uint8_t channel)
{
    // 边界检查：防止越界访问
    if (channel >= NUMBER_OF_ADC_CHANNELS) {
        return 0;
    }

    // 选择 ADC 输入通道 (宏内部完成寄存器读写)
    ADC_SetChannel(channel);

    // 【关键】IOsetting fwlib 中 ADC_ConvertHP() 已封装完整的转换流程:
    //   1. 启动 ADC 转换
    //   2. 轮询等待 EOC (转换完成标志)
    //   3. 清除中断标志
    //   4. 拼接 ADC_RES + ADC_RESL 并返回 16-bit 结果
    // ⚠️ 不要手动调用 ADC_Start() + ADC_SamplingFinished()，否则会重复触发转换
    return ADC_ConvertHP();
}
