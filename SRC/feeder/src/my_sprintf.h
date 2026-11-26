#ifndef __MY_Smy_printf_H__
#define __MY_Smy_printf_H__
#include "config.h"
#include <stdarg.h>

/**
 * @brief 简易smy_printf，接口和标准一致
 * @param str 输出字符缓冲区
 * @param fmt 格式化字符串（const）
 * @return 字符串长度
 * @note 支持 %d %u %x %s %%，支持 %02d %04u 补零；不支持%f浮点数、小数精度
 */
int my_sprintf(char *str, const char *fmt, ...);

/**
 * @brief 简易my_printf，直接输出到UART（带 RS485 DE 控制）
 */
void my_printf(const char *fmt, ...);

int my_vsprintf(char *str, const char *fmt, va_list ap);

#endif


