#ifndef __BOOT_IAP_H__
#define __BOOT_IAP_H__

#include "boot_config.h"

/* 擦除包含 addr 的 512B 扇区并写入 data（len <= 512）。
 * TODO: 用 FwLib IAP_CmdErase/IAP_CmdWrite 实现。 */
int boot_iap_erase_write(uint32_t addr, const uint8_t *data, uint16_t len);

/* 写 128B 页到 App 区；首次访问该 512B 扇区时自动擦除。 */
void boot_iap_reset_erase_map(void);

/* 写 128B 页到 App 区；addr 必须 128B 对齐，且范围在 App 区。 */
int boot_iap_write_page(uint32_t addr, const uint8_t *data, uint16_t len);

#endif
