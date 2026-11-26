/*
 * intel_hex.h - Intel HEX 文件解析（供 ISP 在线升级使用）
 *
 * 输入：SDCC 生成的 .hex 文本（如 build/Debug/ddfeedermcu.hex）
 * 输出：连续二进制固件 + 起始/结束地址，可直接传给 stc_isp_download()
 *
 * 覆盖记录类型：00(数据)、01(EOF)、04(扩展线性地址)
 */
#ifndef INTEL_HEX_H
#define INTEL_HEX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 固件最大长度：STC8H1K28 程序区 27K（27648B，X57 EEPROM 划 1K 后），留 512B 冗余 */
#define HEX_MAX_SIZE  32768

typedef struct {
    uint8_t  data[HEX_MAX_SIZE];
    uint32_t base;        /* 固件起始绝对地址（通常 0x0000） */
    uint32_t end;         /* 固件结束绝对地址（不含） */
    uint32_t size;        /* 固件长度 = end - base */
    int      valid;       /* 解析成功标志 */
} HexImage_t;

/**
 * @brief 解析 Intel HEX 文本
 * @param text      HEX 文本（可含空白/换行）
 * @param text_len  文本字节数
 * @param out       输出固件镜像
 * @return 0=成功，-1=格式错误，-2=地址越界，-3=校验和错误
 */
int intel_hex_parse(const char *text, uint32_t text_len, HexImage_t *out);

/* ---- 固件版本（hex 镜像静态标识 DDFW:<yymm>.<ddbn>；与总线 M602 应答 FW: 同口径） ---- */

/* 版本语义：yymm=年(2位)月(2位)；ddbn=日(2位)当日构建号(2位，00~99)。
   比较先比 yymm，相等再比 ddbn，纯数值序，禁止按字符串字典序比较。 */
typedef struct {
    uint16_t yymm;   /* 如 2610 */
    uint16_t ddbn;  /* 如 2402 */
    int      valid;
} HexFwVer_t;

/**
 * @brief 在已解析镜像中扫描 "DDFW:" 前缀，解析版本号
 * @param img  已解析固件镜像（intel_hex_parse 成功后调用）
 * @param out  版本输出
 * @return 0=找到且格式合法；-1=未找到标识（旧格式包，应拒绝升级）；-2=格式/范围非法
 * @note  扫描窗口为 img.base ~ img.end；标识由固件启动行 "DDFW:<yymm>.<ddbn> Xnn"
 *        以明文落在 code 段，前缀固定 5 字节，版本号后遇空格/换行/NUL 停止。
 */
int intel_hex_read_fw_ver(const HexImage_t *img, HexFwVer_t *out);

/**
 * @brief 解析文本中的版本号 yymm.ddbn（供 hex 镜像与 M602 总线应答 FW: 字段复用）
 * @param p    文本指针（调用方已定位到版本号起始处）
 * @param len  可用长度
 * @param out  版本输出
 * @return 0=合法；-2=格式/范围非法
 */
int intel_hex_parse_ver_text(const uint8_t *p, uint32_t len, HexFwVer_t *out);

/**
 * @brief 版本比较（a 包版本 vs b 板上版本）
 * @return 1=a 更新；0=相等；-1=a 更旧
 */
int intel_hex_ver_cmp(const HexFwVer_t *a, const HexFwVer_t *b);

#ifdef __cplusplus
}
#endif

#endif /* INTEL_HEX_H */
