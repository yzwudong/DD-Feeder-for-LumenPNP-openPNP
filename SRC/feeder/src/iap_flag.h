#ifndef __IAP_FLAG_H__
#define __IAP_FLAG_H__

#include <stdint.h>

/* IAP Bootloader 升级标志
 *
 * 默认 0：保持现有 M788 -> ROM ISP 行为，避免在 Bootloader 未完成时
 *         把飞达切到不存在的升级流程。
 * 改为 1：M788 写标志 + IAP_CONTR=0xA0 复位到 Bootloader。
 *         前提：Bootloader 已烧录、App 已按 0x1000/0x5A00 重新链接。
 */
#ifndef USE_IAP_BOOTLOADER
#define USE_IAP_BOOTLOADER 1
#endif

/* 0x6A00 扇区（512B）；不能与 App 区 0x1000-0x69FF 重叠 */
#define IAP_FLAG_ADDR       0x6A00UL
#define IAP_FLAG_MAGIC      0x49415055UL   /* "IAPU" */

#define IAP_STATE_ERASED    0xFFFFFFFFUL
#define IAP_STATE_REQUEST   0x5A5A0001UL
#define IAP_STATE_WRITING   0x5A5A0002UL
#define IAP_STATE_VALID     0x5A5A0003UL

typedef struct {
    uint32_t magic;
    uint32_t state;
    uint32_t app_size;
    uint32_t app_crc32;
    uint16_t app_ver_yymm;
    uint16_t app_ver_ddbn;
    uint16_t retry;
    uint16_t reserved;
    uint32_t flag_crc;
} IapFlag_t;

/* 读取 0x6A00 标志；成功返回 0，magic 不匹配返回 -1 */
int iap_flag_read(IapFlag_t *out);

/* App 请求升级：写 IAP_STATE_REQUEST。
 * 骨架阶段返回 -2（TODO: IAP 擦写未实现），默认不调用。 */
int iap_flag_write_request(uint32_t app_size, uint32_t app_crc32,
                           uint16_t ver_yymm, uint16_t ver_ddbn);

/* BL 在 END 校验通过后置 VALID。 */
int iap_flag_set_valid(void);

#endif
