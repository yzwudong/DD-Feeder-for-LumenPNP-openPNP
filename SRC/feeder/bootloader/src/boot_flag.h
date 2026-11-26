#ifndef __BOOT_FLAG_H__
#define __BOOT_FLAG_H__

#include <stdint.h>

/* BL 侧最小 flag 判断：只读 magic/state；END 后擦除。 */
uint8_t boot_flag_need_iap(void);
void    boot_flag_clear(void);
uint8_t boot_flag_chipid(uint8_t out[3]);

#endif

