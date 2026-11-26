#include "boot_iap.h"
#include <string.h>
#include "fw_hal.h"

/* H1K28 + EEPROM=28K：IAP 地址=物理地址。
 * 只允许写 App 0x1000-0x69FF 和标志 0x6A00-0x6BFF；
 * 绝不擦 0x0000-0x0FFF（Bootloader）和 0x6C00-0x6FFF（配置/兜底 ID）。
 */
static int allow_write(uint32_t addr, uint16_t len)
{
    if (!len) return 0;
    if (addr >= BOOT_APP_ADDR && addr + len <= BOOT_APP_END + 1) return 1;
    if (addr >= BOOT_FLAG_SECTOR && addr + len <= BOOT_FLAG_SECTOR + BOOT_SECTOR_SIZE) return 1;
    return 0;
}

static __xdata uint8_t erased_map[6];

void boot_iap_reset_erase_map(void)
{
    memset(erased_map, 0, sizeof(erased_map));
}

int boot_iap_erase_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint16_t sector;

    if (!data || !len || len > BOOT_SECTOR_SIZE) return -1;
    if (!allow_write(addr, len)) return -1;

    uint8_t ea = EA;
    EA = 0;
    IAP_SetEnabled(1);
    IAP_TPS = 24;
    sector = (uint16_t)(addr & 0xFE00U);
    IAP_CmdErase(sector);
    for (i = 0; i < len; i++) {
        IAP_WriteData(data[i]);
        IAP_CmdWrite((uint16_t)(addr + i));
    }
    EA = ea;
    return 0;
}

int boot_iap_write_page(uint32_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint8_t idx, bit;
    if (!data || !len || len > BOOT_PAGE_SIZE) return -1;
    if (addr < BOOT_APP_ADDR || addr + len > BOOT_APP_END + 1) return -1;
    if (addr % BOOT_PAGE_SIZE) return -1;

    idx = (uint8_t)((addr - BOOT_APP_ADDR) / BOOT_SECTOR_SIZE);
    bit = (uint8_t)(1U << (idx & 7));
    if (!(erased_map[idx >> 3] & bit)) {
        uint8_t ea = EA;
        EA = 0;
        IAP_SetEnabled(1); IAP_TPS = 24;
        IAP_CmdErase((uint16_t)(addr & 0xFE00U));
        erased_map[idx >> 3] |= bit;
        EA = ea;
    }

    {
        uint8_t ea = EA;
        EA = 0;
        IAP_SetEnabled(1); IAP_TPS = 24;
        for (i = 0; i < len; i++) {
            IAP_WriteData(data[i]);
            IAP_CmdWrite((uint16_t)(addr + i));
        }
        EA = ea;
    }
    for (i = 0; i < len; i++) {
        if (*(const uint8_t __code *)(addr + i) != data[i]) return -1;
    }
    return 0;
}



