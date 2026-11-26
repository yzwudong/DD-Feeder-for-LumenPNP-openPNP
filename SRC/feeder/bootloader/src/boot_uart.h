#ifndef __BOOT_UART_H__
#define __BOOT_UART_H__

#include <stdint.h>

void boot_uart_init(void);
void boot_uart_putc(uint8_t c);
int  boot_uart_getc(void);                 /* -1 = 无数据 */
void boot_uart_send(const uint8_t *data, uint16_t len);  /* 自动 DE 切换 */
void boot_delay_10us(void);
void boot_delay_ms(uint16_t ms);
void boot_timer_release(void);

#endif

