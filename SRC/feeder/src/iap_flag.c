#include "iap_flag.h"
#include "fw_hal.h"
#include <stddef.h>

static uint32_t flag_crc32(const IapFlag_t *f)
{
    const uint8_t *p = (const uint8_t *)f;
    uint16_t n = (uint16_t)offsetof(IapFlag_t, flag_crc);
    uint32_t crc = 0xFFFFFFFFUL;
    uint16_t i;
    uint8_t b;

    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (b = 0; b < 8; b++) {
            if (crc & 1UL) crc = (crc >> 1) ^ 0xEDB88320UL;
            else           crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

static void flag_read_raw(IapFlag_t *out)
{
    uint8_t *dst = (uint8_t *)out;
    uint16_t i;

    IAP_SetEnabled(1);
    IAP_TPS = 24;
    for (i = 0; i < sizeof(IapFlag_t); i++) {
        IAP_CmdRead((uint16_t)(IAP_FLAG_ADDR + i));
        dst[i] = IAP_ReadData();
    }
}

/* 关中断擦写 + 回读校验；失败返回 -1 */
static int flag_write_raw(IapFlag_t *f)
{
    uint8_t *p = (uint8_t *)f;
    IapFlag_t rb;
    uint8_t *q = (uint8_t *)&rb;
    uint16_t i;

    f->flag_crc = flag_crc32(f);

    EA = 0;
    IAP_SetEnabled(1);
    IAP_TPS = 24;
    IAP_CmdErase((uint16_t)IAP_FLAG_ADDR);
    for (i = 0; i < sizeof(IapFlag_t); i++) {
        IAP_WriteData(p[i]);
        IAP_CmdWrite((uint16_t)(IAP_FLAG_ADDR + i));
    }

    for (i = 0; i < sizeof(IapFlag_t); i++) {
        IAP_CmdRead((uint16_t)(IAP_FLAG_ADDR + i));
        q[i] = IAP_ReadData();
    }
    EA = 1;

    if (rb.magic != f->magic || rb.state != f->state ||
        rb.flag_crc != f->flag_crc) return -1;
    return 0;
}

int iap_flag_read(IapFlag_t *out)
{
    if (!out) return -1;
    flag_read_raw(out);
    if (out->magic != IAP_FLAG_MAGIC) return -1;
    if (out->flag_crc != flag_crc32(out)) return -2;
    return 0;
}

/* App 请求升级：擦 0x6A00 扇区并写入 REQUEST。 */
int iap_flag_write_request(uint32_t app_size, uint32_t app_crc32,
                           uint16_t ver_yymm, uint16_t ver_ddbn)
{
    IapFlag_t f;

    f.magic = IAP_FLAG_MAGIC;
    f.state = IAP_STATE_REQUEST;
    f.app_size = app_size;
    f.app_crc32 = app_crc32;
    f.app_ver_yymm = ver_yymm;
    f.app_ver_ddbn = ver_ddbn;
    f.retry = 0;
    f.reserved = 0;
    f.flag_crc = 0;
    return flag_write_raw(&f);
}

/* BL 在 END 校验通过后置 VALID，保留 App 信息。 */
int iap_flag_set_valid(void)
{
    IapFlag_t f;

    if (iap_flag_read(&f) != 0) {
        f.magic = IAP_FLAG_MAGIC;
        f.app_size = 0;
        f.app_crc32 = 0;
        f.app_ver_yymm = 0;
        f.app_ver_ddbn = 0;
        f.retry = 0;
        f.reserved = 0;
    }
    f.state = IAP_STATE_VALID;
    return flag_write_raw(&f);
}
