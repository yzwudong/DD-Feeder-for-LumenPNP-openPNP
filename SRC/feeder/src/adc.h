#ifndef __ADC_H__
#define __ADC_H__

#include "fw_hal.h"
#include <stdint.h>

// ADC 通道数量（默认 8；adc.c 中也有 #ifndef 兜底定义）
#ifndef NUMBER_OF_ADC_CHANNELS
#define NUMBER_OF_ADC_CHANNELS  8
#endif

// 初始化 ADC 模块 (12位精度)
void ADC_Init(void);

// 读取指定通道的 ADC 原始值 (0~4095)
uint16_t ADC_ReadRaw(uint8_t channel);

// 注：浮点缩放函数 ADC_ReadScaled / ADC_SetScaling 已移除（省 ROM，见 adc.c 注释）
#endif