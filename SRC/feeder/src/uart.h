#ifndef __UART_H__
#define __UART_H__

#include "fw_hal.h"
#include <stdint.h>
#include <stdbool.h>

// ========================================================================
// 环形接收缓冲区（全局化：UART1 ISR 必须定义在 main.c，
// SDCC 4.2.0 的 HOME 向量表只由含 main() 的编译单元生成且只含该单元中断，
// 跨单元的中断函数不生成向量 → 中断触发跳垃圾代码死机。见 main.c X12 注释）
// ========================================================================
#define UART_RX_BUF_SIZE 64
extern __xdata volatile char rx_buf[UART_RX_BUF_SIZE];
extern volatile uint8_t rx_head;
extern volatile uint8_t rx_tail;

// 初始化 UART1 (默认 P3.0/P3.1, 115200波特率)
void UART_Init(void);

// 检查接收缓冲区是否有数据
bool UART_Available(void);

// 从缓冲区读取一个字符 (非阻塞)
char UART_Read(void);

// 发送单个字符 (阻塞等待发送完成)
void UART_Write(char c);

// 发送字符串
void UART_WriteString(const char *str);

// SDCC 标准库 printf 重定向必须的底层函数
int putchar(int c);

#endif