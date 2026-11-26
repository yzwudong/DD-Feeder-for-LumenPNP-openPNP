#include "boot_flag.h"
#include "fw_hal.h"
#include "../src/iap_flag.h"
#include "../src/config.h"

/* BL 侧最小实现：只读 magic/state，不校验 CRC，控制 6KB 体积。 */
uint8_t boot_flag_need_iap(void)
{
    IapFlag_t f;
    uint8_t *p = (uint8_t *)&f;
    uint16_t i;

    IAP_SetEnabled(1);
    IAP_TPS = 24;
    for (i = 0; i < sizeof(IapFlag_t); i++) {
        IAP_CmdRead((uint16_t)(IAP_FLAG_ADDR + i));
        p[i] = IAP_ReadData();
    }
    if (f.magic != IAP_FLAG_MAGIC) return 0;
    return (f.state == IAP_STATE_REQUEST ||
            f.state == IAP_STATE_WRITING) ? 1 : 0;
}

/* 从 EEPROM 兜底区 0x6E00 读 ChipID：magic + xor 校验。 */
uint8_t boot_flag_chipid(uint8_t out[3])
{
    uint8_t b[6];
    uint8_t i;

    IAP_SetEnabled(1);
    IAP_TPS = 24;
    for (i = 0; i < 6; i++) {
        IAP_CmdRead((uint16_t)(EEPROM_FALLBACK_ID_OFFSET + i));
        b[i] = IAP_ReadData();
    }
    if (b[0] != 0xC1 || b[1] != 0x4D) return 0;
    if ((b[0] ^ b[1] ^ b[2] ^ b[3] ^ b[4]) != b[5]) return 0;
    out[0] = b[2]; out[1] = b[3]; out[2] = b[4];
    return 1;
}

/* END 通过后擦掉 flag，避免下次上电又进 IAP。 */
void boot_flag_clear(void)
{
    EA = 0;
    IAP_SetEnabled(1);
    IAP_TPS = 24;
    IAP_CmdErase((uint16_t)IAP_FLAG_ADDR);
    EA = 1;
}

