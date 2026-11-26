#include "boot_uart.h"
#include "../src/config.h"
#include "fw_hal.h"

#define BOOT_UART_TMR2_INIT  0xFFCC
#define BOOT_T0_10US         (65536U - 240U)   /* 24MHz 1T: 240 counts = 10us */

void boot_uart_init(void)
{
    UART1_SwitchPort(UART1_AlterPort_P30_P31);
    GPIO_P3_SetMode(GPIO_Pin_0, GPIO_Mode_Input_HIP);
    SM0 = 0;
    SM1 = 1;
    SM2 = 0;

    UART1_SetBaudSource(UART1_BaudSource_Timer2);
    TIM_Timer2_SetRunState(HAL_State_OFF);
    TIM_Timer2_Set1TMode(ENABLE);
    TIM_Timer2_SetInitValue(BOOT_UART_TMR2_INIT >> 8, BOOT_UART_TMR2_INIT & 0xFF);
    TIM_Timer2_SetRunState(HAL_State_ON);

    /* Timer0: 1T，16 位，10us 节拍；仅 BL 使用，App 启动后自行配置 */
    TIM_Timer0_SetRunState(HAL_State_OFF);
    TIM_Timer0_Set1TMode(ENABLE);
    TIM_Timer0_SetMode(0);
    TIM_Timer0_SetInitValue(BOOT_T0_10US >> 8, BOOT_T0_10US & 0xFF);

    UART1_SetRxState(ENABLE);
}

void boot_delay_10us(void)
{
    TH0 = (uint8_t)(BOOT_T0_10US >> 8);
    TL0 = (uint8_t)BOOT_T0_10US;
    TF0 = 0;
    TR0 = 1;
    while (!TF0) { }
    TR0 = 0;
    TF0 = 0;
}

void boot_delay_ms(uint16_t ms)
{
    while (ms--) {
        uint8_t i;
        for (i = 0; i < 100; i++) boot_delay_10us();
    }
}

void boot_timer_release(void)
{
    TIM_Timer0_SetRunState(HAL_State_OFF);
    TIM_Timer0_Set1TMode(DISABLE);
    TMOD &= 0xFC;
    TF0 = 0;
}

int boot_uart_getc(void)
{
    if (RI) {
        RI = 0;
        return (int)SBUF;
    }
    return -1;
}

void boot_uart_putc(uint8_t c)
{
    SBUF = c;
    while (!TI) { }
    TI = 0;
}

void boot_uart_send(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    if (!data || !len) return;
    RS485_SetTxMode();
    boot_delay_ms(1);
    for (i = 0; i < len; i++) boot_uart_putc(data[i]);
    boot_delay_ms(1);
    RS485_SetRxMode();
}

