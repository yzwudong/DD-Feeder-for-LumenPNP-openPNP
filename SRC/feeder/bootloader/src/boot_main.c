#include "fw_hal.h"
#include "../src/config.h"
#include "boot_flag.h"
#include "boot_iap.h"
#include "boot_proto.h"
#include "boot_uart.h"
#include "../src/version.h"

__code const char boot_ver[] = "DDFBL:" FW_YYMM_STR "." FW_DD_BN_STR;

static void boot_de_init(void)
{
    RS485_DE_SET_PP_MODE();
    RS485_SetRxMode();
}

static void boot_jump_app(void)
{
    boot_timer_release();
    ES = 0;
    EA = 0;
    SP = 0x07;
    __asm
        ljmp 0x1800
    __endasm;
    while (1);
}

/* App 复位向量必须是 02 18 xx（LJMP 0x18xx）。 */
static uint8_t boot_app_valid(void)
{
    const uint8_t __code *p = (const uint8_t __code *)0x1800;
    return (p[0] == 0x02 && p[1] == 0x18) ? 1 : 0;
}

/* 广播一次 BOOT 身份；无论 flag 是否有效，只要 App 不完整都要让主机看见。 */
static void boot_announce(void)
{
    uint8_t cid[3];
    if (!boot_flag_chipid(cid)) return;
    static const char hx[] = "0123456789ABCDEF";
    uint8_t b[23];
    uint8_t i;
    const char *pre = "ok boot ChipID:";
    for (i = 0; i < 15; i++) b[i] = (uint8_t)pre[i];
    b[15] = (uint8_t)hx[cid[0] >> 4]; b[16] = (uint8_t)hx[cid[0] & 15];
    b[17] = (uint8_t)hx[cid[1] >> 4]; b[18] = (uint8_t)hx[cid[1] & 15];
    b[19] = (uint8_t)hx[cid[2] >> 4]; b[20] = (uint8_t)hx[cid[2] & 15];
    b[21] = '\r'; b[22] = '\n';
    boot_uart_send(b, 23);
}

void main(void)
{
    uint8_t need_iap;

    EA = 0;
    WDT_CONTR = 0x00;
    boot_de_init();
    boot_uart_init();
    IAP_SetEnabled(1);
    IAP_TPS = 24;

    /* 上电按住 P3.5：只清 0x6A00 升级 flag，不碰配置/0x6E00 ChipID。 */
    GPIO_P3_SetMode(GPIO_Pin_5, GPIO_Mode_Input_HIP);
    GPIO_SetPullUp(GPIO_Port_3, GPIO_Pin_5, 1);
    boot_delay_ms(2);
    if (!(P3 & GPIO_Pin_5)) boot_flag_clear();

    need_iap = boot_flag_need_iap();
    if (need_iap) boot_announce();

    /* 只有 REQUEST/WRITING 才开 IAP 窗口；正常上电立即回 App。 */
    if (need_iap) {
        for (;;) {
            if (boot_proto_run() == 0) boot_jump_app();
            /* 超时/失败绝不清 flag：0x1800 只有 2 字节有效也可能是半写 App。
             * flag 只由 END 校验通过后清除，保证下次上电仍广播 ok boot。 */
        }
    }

    if (boot_app_valid()) boot_jump_app();

    /* flag 丢失/半写但 App 也不完整：必须广播，否则主机完全发现不了。 */
    boot_announce();
    for (;;) {
        if (boot_proto_run() == 0) boot_jump_app();
    }
}








