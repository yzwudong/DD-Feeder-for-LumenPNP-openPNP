/* 最小 UART 测试：0x0000 启动，只初始化 UART1/DE，循环打印。
 * 用于排查“下载后串口完全没反应”。
 */
#include "fw_hal.h"
#include "../src/config.h"

static void uart_init(void)
{
    UART1_SwitchPort(UART1_AlterPort_P30_P31);
    SM0 = 0;
    SM1 = 1;
    UART1_SetBaudSource(UART1_BaudSource_Timer2);
    TIM_Timer2_Set1TMode(ENABLE);
    TIM_Timer2_SetInitValue(0xFFCC >> 8, 0xFFCC & 0xFF);
    TIM_Timer2_SetRunState(HAL_State_ON);
    UART1_SetRxState(ENABLE);
}

static void uart_putc(char c)
{
    SBUF = c;
    while (!TI) { }
    TI = 0;
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

void main(void)
{
    unsigned int i;

    EA = 0;
    RS485_DE_SET_PP_MODE();
    RS485_SetRxMode();          /* DE=0，接收；发送时由 uart 手动静默 */
    uart_init();

    while (1) {
        RS485_SetTxMode();
        uart_puts("UART OK 115200 24MHz\r\n");
        RS485_SetRxMode();
        for (i = 0; i < 30000; i++) { }   /* 约几百 ms */
    }
}
