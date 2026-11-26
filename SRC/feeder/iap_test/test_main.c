/* H1K28 IAP / EEPROM / Bootloader 最小验证
 *
 * 同一份代码编两次：
 *   -DTEST_BOOT  -> 0x0000, 擦写 0x6A00/0x6C00，校验后跳 0x1000
 *   -DTEST_APP   -> 0x1000, 打印 APP OK，并用 MOVC 读回 0x6A00
 *
 * 前提：STC-ISP 把 EEPROM 设为 28K（整个 28K 仍可运行程序）。
 * 测试只碰 0x6A00/0x6C00，不碰 0x0000-0x0FFF 和 0x1000-0x69FF。
 */
#include "fw_hal.h"
#include "../src/config.h"

#define TEST_IAP_ADDR    0x6A00U
#define TEST_EEPROM_ADDR 0x6C00U
#define TEST_LEN         8U

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

static void de_init(void)
{
    RS485_DE_SET_PP_MODE();
    RS485_SetRxMode();
}

static void iap_enable(void)
{
    IAP_SetEnabled(1);
    IAP_TPS = 24;          /* 24MHz */
}

static uint8_t iap_read_byte(uint16_t addr)
{
    IAP_CmdRead(addr);
    return IAP_ReadData();
}

static void iap_write_byte(uint16_t addr, uint8_t v)
{
    IAP_WriteData(v);
    IAP_CmdWrite(addr);
}

static void eeprom_erase_write(uint16_t addr, const uint8_t *buf, uint8_t len)
{
    uint8_t i;
    IAP_CmdErase(addr & 0xFE00U);
    for (i = 0; i < len; i++) iap_write_byte((uint16_t)(addr + i), buf[i]);
}

static uint8_t test_pattern(uint8_t i)
{
    return (uint8_t)(0xA5U ^ (uint8_t)(i * 0x11U));
}

#ifdef TEST_BOOT

static void boot_jump_app(void)
{
    EA = 0;
    SP = 0x07;
    __asm
        ljmp 0x1800
    __endasm;
    while (1);
}

void main(void)
{
    uint8_t i;
    uint8_t ok1 = 1, ok2 = 1;

    EA = 0;
    de_init();
    uart_init();
    uart_puts("\r\nBOOT TEST\r\n");

    iap_enable();

    /* 1) IAP 擦写 0x6A00，验证程序 Flash 可被 IAP 写 */
    IAP_CmdErase(TEST_IAP_ADDR);
    for (i = 0; i < TEST_LEN; i++) iap_write_byte((uint16_t)(TEST_IAP_ADDR + i), test_pattern(i));
    for (i = 0; i < TEST_LEN; i++) {
        if (iap_read_byte((uint16_t)(TEST_IAP_ADDR + i)) != test_pattern(i)) ok1 = 0;
    }
    uart_puts(ok1 ? "IAP 0x6A00 OK\r\n" : "IAP 0x6A00 FAIL\r\n");

    /* 2) EEPROM 在 0x6C00，验证 28K EEPROM 下可访问 */
    eeprom_erase_write(TEST_EEPROM_ADDR, (const uint8_t *)"IAPTEST", 8);
    for (i = 0; i < 8; i++) {
        if (iap_read_byte((uint16_t)(TEST_EEPROM_ADDR + i)) != (uint8_t)"IAPTEST"[i]) ok2 = 0;
    }
    uart_puts(ok2 ? "EEPROM 0x6C00 OK\r\n" : "EEPROM 0x6C00 FAIL\r\n");

    uart_puts("JUMP APP\r\n");
    boot_jump_app();
}

#elif defined(TEST_APP)

void main(void)
{
    uint8_t i;
    uint8_t ok = 1;

    EA = 0;
    de_init();
    uart_init();
    uart_puts("\r\nAPP OK @0x1800\r\n");

    /* MOVC 读回 Boot 写的 0x6A00，验证 IAP 写的是程序 Flash */
    for (i = 0; i < TEST_LEN; i++) {
        uint8_t v = *(const uint8_t __code *)(TEST_IAP_ADDR + i);
        if (v != test_pattern(i)) ok = 0;
    }
    uart_puts(ok ? "APP READ 0x6A00 OK\r\n" : "APP READ 0x6A00 FAIL\r\n");

    while (1) { }
}

#else
#error "define TEST_BOOT or TEST_APP"
#endif


