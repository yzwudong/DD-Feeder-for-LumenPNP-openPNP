/*
 * intel_hex.c - Intel HEX 解析实现
 */
#include "intel_hex.h"
#include <string.h>

static int hex_val(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static int hex_byte(const char *p)
{
    int hi = hex_val(p[0]);
    int lo = hex_val(p[1]);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

int intel_hex_parse(const char *text, uint32_t text_len, HexImage_t *out)
{
    uint32_t i = 0;
    uint32_t base_upper = 0;    /* 04 记录：线性地址高 16 位 */
    uint32_t min_addr = 0xFFFFFFFF;
    uint32_t max_addr = 0;
    int have_data = 0;

    if (!text || !out) return -1;
    memset(out->data, 0xFF, sizeof(out->data));
    out->base = 0;
    out->end = 0;
    out->size = 0;
    out->valid = 0;

    while (i < text_len) {
        /* 跳过空白 */
        while (i < text_len && (text[i] == '\r' || text[i] == '\n' || text[i] == ' ')) i++;
        if (i >= text_len) break;

        /* 每行必须以 ':' 开头 */
        if (text[i] != ':') return -1;
        i++;

        /* 读取 LL AAAA TT */
        int len = 0, aahi = 0, aalo = 0, type = 0;
        if (i + 10 > text_len) return -1;
        len  = hex_byte(text + i);       i += 2;
        aahi = hex_byte(text + i);       i += 2;
        aalo = hex_byte(text + i);       i += 2;
        type = hex_byte(text + i);       i += 2;
        if (len < 0 || aahi < 0 || aalo < 0 || type < 0) return -1;
        if (len > 255) return -1;   /* Intel HEX 记录最大 255 字节 */

        /* 数据 + 校验和 */
        if (i + len * 2 + 2 > text_len) return -1;
        {
            uint8_t data[256];
            int cc, j, sum;
            for (j = 0; j < len; j++) {
                int b = hex_byte(text + i);
                if (b < 0) return -1;
                data[j] = (uint8_t)b;
                i += 2;
            }
            cc = hex_byte(text + i);
            i += 2;
            if (cc < 0) return -1;

            /* 校验和：所有字节（含 CC）之和 & 0xFF == 0 */
            sum = len + aahi + aalo + type + cc;
            for (j = 0; j < len; j++) sum += data[j];
            if ((sum & 0xFF) != 0) return -3;

            switch (type) {
                case 0x00: { /* 数据记录 */
                    uint32_t addr = (base_upper << 16) | ((uint32_t)aahi << 8) | aalo;
                    int j;
                    if (addr + (uint32_t)len > HEX_MAX_SIZE) return -2;
                    for (j = 0; j < len; j++) out->data[addr + j] = data[j];
                    if (addr < min_addr) min_addr = addr;
                    if (addr + len > max_addr) max_addr = addr + len;
                    have_data = 1;
                    break;
                }
                case 0x01: /* EOF */
                    i = text_len; /* 结束解析 */
                    break;
                case 0x04: /* 扩展线性地址 */
                    if (len != 2) return -1;
                    base_upper = ((uint32_t)data[0] << 8) | data[1];
                    break;
                default:
                    break; /* 02/03 等其他类型忽略 */
            }
        }
    }

    if (!have_data) return -1;
    out->base = min_addr;
    out->end  = max_addr;
    out->size = max_addr - min_addr;
    out->valid = 1;
    return 0;
}

/* ------------------------------------------------------------------------
 * 静态固件版本扫描：定位固件启动行明文前缀 "DDFW:"，解析 yymm.ddbn
 * 前缀后只允许数字与一个 '.'；遇空格 / NUL / 其他字符停止。
 * 范围校验：yymm 101~9912（yy*100+mm，月 01~12）；ddbn 100~3199（dd*100+bn，日 01~31、构建号 00~99）。
 * ------------------------------------------------------------------------ */
static int parse_ver_field(const uint8_t *p, uint32_t len, HexFwVer_t *out)
{
    uint32_t i = 0;
    int dot = 0;
    uint32_t a = 0, b = 0;
    int a_digit = 0, b_digit = 0;

    while (i < len) {
        uint8_t c = p[i++];
        if (c >= '0' && c <= '9') {
            uint32_t d = (uint32_t)(c - '0');
            if (dot == 0) { a = a * 10 + d; a_digit = 1; }
            else          { b = b * 10 + d; b_digit = 1; }
        } else if (c == '.' && dot == 0) {
            dot = 1;
        } else {
            break; /* 空格 / NUL / 其他字符：版本段结束 */
        }
    }

    if (!a_digit || !dot || !b_digit) return -2;
    if (a < 101 || a > 9912 || b < 100 || b > 3199) return -2;

    out->yymm   = (uint16_t)a;
    out->ddbn   = (uint16_t)b;
    out->valid  = 1;
    return 0;
}

int intel_hex_parse_ver_text(const uint8_t *p, uint32_t len, HexFwVer_t *out)
{
    if (!p || !out) return -2;
    out->valid = 0;
    return parse_ver_field(p, len, out);
}

int intel_hex_read_fw_ver(const HexImage_t *img, HexFwVer_t *out)
{
    static const uint8_t tag[5] = { 'D', 'D', 'F', 'W', ':' };
    uint32_t i;

    if (!img || !out || !img->valid) return -1;
    out->valid = 0;

    for (i = img->base; i + 5 <= img->end; i++) {
        uint32_t j;
        for (j = 0; j < 5; j++) {
            if (img->data[i + j] != tag[j]) break;
        }
        if (j == 5) {
            int rc = parse_ver_field(img->data + i + 5, img->end - i - 5, out);
            if (rc == 0) return 0;
            return -2; /* 前缀唯一：找到但格式非法，不再继续扫描 */
        }
    }
    return -1;
}

int intel_hex_ver_cmp(const HexFwVer_t *a, const HexFwVer_t *b)
{
    if (!a->valid || !b->valid) return 0;
    if (a->yymm != b->yymm)  return (a->yymm > b->yymm) ? 1 : -1;
    if (a->ddbn != b->ddbn)  return (a->ddbn > b->ddbn) ? 1 : -1;
    return 0;
}
