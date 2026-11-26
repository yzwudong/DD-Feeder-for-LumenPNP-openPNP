/*
 * stc_isp.c - STC8 系列 ISP 在线下载实现（上位机侧）
 *
 * 协议移植自 tools/stcflash.py（STC8 分支），已按 STC8H1K17 实测参数固定：
 *   - 握手波特率 9600，偶校验
 *   - 协商目标 115200，Timer1 参数按 24MHz IRC 公式计算（与 stcflash.py 一致）
 *   - 擦除等待 twait=0x80（对应 stcflash.py 在 STC8H1K17 上实测成功的取值）
 *
 * 芯片进入 ISP 引导区后，本模块负责剩下的握手→协商→擦除→写入→结束。
 */
#include "stc_isp.h"

/* ---- 协议常量 ---- */
#define ISP_FRAME_HDR_HOST    0x6A   /* 主机→芯片 帧头第3字节 */
#define ISP_FRAME_HDR_CHIP    0x68   /* 芯片→主机 帧头第3字节 */
#define ISP_FRAME_TAIL        0x16
#define ISP_CHKMODE           2      /* STC8：2 字节校验和 */
#define ISP_MAX_RECV_FRAME    64     /* 应答帧 n 上限 */

#define ISP_BAUD_START        9600
#define ISP_BAUD_HIGH         115200

/* 波特率协商常量（115200，24MHz IRC）：Timer1=0xFFCC → [FF CC 00 68 A0] + twait 0x80 */
static const uint8_t ISP_BAUDSTR_HIGH[] = { 0xFF, 0xCC, 0x00, 0x68, 0xA0 };
#define ISP_TWAIT            0x80

/* ---- 内部状态 ---- */
typedef struct {
    const StcIspPort_t *port;
    int chkmode;          /* 校验和字节数（STC8=2） */
} IspCtx_t;

/* ---- 底层帧收发 ---- */

static int isp_read_exact(IspCtx_t *c, uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
    uint32_t got = 0;
    uint32_t deadline = 0; /* 简化：每段读都带独立超时，由平台 read 保证 */
    (void)deadline;
    while (got < len) {
        int r = c->port->read(buf + got, len - got, timeout_ms);
        if (r <= 0) return STC_ISP_ERR_TIMEOUT;
        got += (uint32_t)r;
    }
    return STC_ISP_OK;
}

/* 发送一帧：46 B9 6A | N(2B) | cmd | dat | chksum(2B) | 16 */
static void isp_send(IspCtx_t *c, uint8_t cmd, const uint8_t *dat, uint16_t dat_len)
{
    uint8_t buf[160];
    uint32_t chksum = 0;
    uint16_t n = (uint16_t)(1 + 2 + 1 + dat_len + ISP_CHKMODE + 1); /* 不含帧头3字节 */
    uint16_t i;

    buf[0] = 0x46; buf[1] = 0xB9; buf[2] = ISP_FRAME_HDR_HOST;
    buf[3] = (uint8_t)(n >> 8); buf[4] = (uint8_t)(n & 0xFF);
    buf[5] = cmd;
    for (i = 0; i < dat_len; i++) buf[6 + i] = dat[i];

    chksum = ISP_FRAME_HDR_HOST + buf[3] + buf[4] + cmd;
    for (i = 0; i < dat_len; i++) chksum += dat[i];

    buf[6 + dat_len]     = (uint8_t)(chksum >> 8);
    buf[6 + dat_len + 1] = (uint8_t)(chksum & 0xFF);
    buf[6 + dat_len + 2] = ISP_FRAME_TAIL;

    c->port->write(buf, 6 + dat_len + 3);
}

/* 接收一帧：46 B9 68 | N(2B) | cmd | dat | chksum(2B) | 16；返回 cmd，dat 存入 out */
static int isp_recv(IspCtx_t *c, uint8_t *out_dat, uint16_t *out_dat_len,
                    uint8_t *out_cmd, uint32_t timeout_ms)
{
    uint8_t hdr[3], lenb[2], body[64];
    uint32_t chksum;
    uint16_t n, i;

    if (isp_read_exact(c, hdr, 3, timeout_ms) != STC_ISP_OK) return STC_ISP_ERR_TIMEOUT;
    if (hdr[0] != 0x46 || hdr[1] != 0xB9 || hdr[2] != ISP_FRAME_HDR_CHIP) return STC_ISP_ERR_TIMEOUT;

    if (isp_read_exact(c, lenb, 2, timeout_ms) != STC_ISP_OK) return STC_ISP_ERR_TIMEOUT;
    n = (uint16_t)((lenb[0] << 8) | lenb[1]);
    if (n > ISP_MAX_RECV_FRAME || n < 4) return STC_ISP_ERR_TIMEOUT;

    if (isp_read_exact(c, body, n - 3, timeout_ms) != STC_ISP_OK) return STC_ISP_ERR_TIMEOUT;
    if (body[n - 4] != ISP_FRAME_TAIL) return STC_ISP_ERR_TIMEOUT;

    /* 校验和：0x68 + 长度2字节 + body 去掉末尾 (chksum 2B + 0x16) */
    chksum = ISP_FRAME_HDR_CHIP + lenb[0] + lenb[1];
    for (i = 0; i < n - 3 - (1 + ISP_CHKMODE); i++) chksum += body[i];
    if ((chksum & 0xFF) != body[n - 3 - 1]) return STC_ISP_ERR_TIMEOUT;
    if (ISP_CHKMODE > 1 && ((chksum >> 8) & 0xFF) != body[n - 3 - 2]) return STC_ISP_ERR_TIMEOUT;

    if (out_cmd) *out_cmd = body[0];
    if (out_dat && out_dat_len) {
        uint16_t dl = n - 3 - 1 - ISP_CHKMODE - 1; /* 去掉 cmd + chksum + 0x16 */
        if (dl > 0) {
            for (i = 0; i < dl; i++) out_dat[i] = body[1 + i];
        }
        *out_dat_len = dl;
    }
    return STC_ISP_OK;
}

/* ---- 流程：握手 ---- */

static int isp_detect(IspCtx_t *c)
{
    uint8_t dat[64];
    uint16_t dat_len = 0, i;
    int rc;

    c->port->set_baud(ISP_BAUD_START);
    c->port->set_parity(1); /* EVEN */

    for (i = 0; i < 300; i++) {
        uint8_t sync = 0x7F;
        c->port->write(&sync, 1);
        rc = isp_recv(c, dat, &dat_len, NULL, 30);
        if (rc == STC_ISP_OK) {
            return STC_ISP_OK; /* 应答帧已收（dat[16:] 为 info，本模块不解析） */
        }
        c->port->delay_ms(10);
    }
    return STC_ISP_ERR_TIMEOUT;
}

/* ---- 流程：波特率协商（0x8F 测试 + 0x8E 确认，切 115200） ---- */

static int isp_handshake(IspCtx_t *c)
{
    uint8_t dat[64];
    uint16_t dat_len = 0;
    uint8_t baudstr[6];
    int rc;
    uint8_t i;

    for (i = 0; i < 5; i++) baudstr[i] = ISP_BAUDSTR_HIGH[i];
    baudstr[5] = ISP_TWAIT;

    /* 1) 测试：0x8F，仍 9600 */
    isp_send(c, 0x8F, baudstr, 6);
    rc = isp_recv(c, dat, &dat_len, NULL, 200);
    if (rc != STC_ISP_OK) return STC_ISP_ERR_BAUD;

    /* 2) 切 115200 后再收一次（stcflash 实测流程） */
    c->port->set_baud(ISP_BAUD_HIGH);
    rc = isp_recv(c, dat, &dat_len, NULL, 200);
    c->port->set_baud(ISP_BAUD_START); /* 回 9600 准备发确认帧 */
    if (rc != STC_ISP_OK) return STC_ISP_ERR_BAUD;

    /* 3) 确认：0x8E，然后切 115200 并收应答 */
    isp_send(c, 0x8E, baudstr, 5);
    c->port->set_baud(ISP_BAUD_HIGH);
    rc = isp_recv(c, dat, &dat_len, NULL, 200);
    if (rc != STC_ISP_OK) return STC_ISP_ERR_BAUD;

    return STC_ISP_OK;
}

/* ---- 流程：擦除 ---- */

static int isp_erase(IspCtx_t *c)
{
    uint8_t dat[64];
    uint16_t dat_len = 0;
    uint8_t cmd = 0;
    int rc;
    static const uint8_t magic[4] = { 0x00, 0x00, 0x5A, 0xA5 };

    /* 读序列号（可选，照 stcflash 流程保留） */
    isp_send(c, 0x05, magic, 4);
    rc = isp_recv(c, dat, &dat_len, &cmd, 1000);
    if (rc != STC_ISP_OK) return STC_ISP_ERR_ERASE;

    /* 擦除 */
    isp_send(c, 0x03, magic, 4);
    rc = isp_recv(c, dat, &dat_len, &cmd, 3000);
    if (rc != STC_ISP_OK) return STC_ISP_ERR_ERASE;

    return STC_ISP_OK;
}

/* ---- 流程：写入（128 字节/帧，首帧 0x22，后续 0x02） ---- */

static int isp_flash(IspCtx_t *c, const uint8_t *bin, uint32_t len,
                     StcIspProgress_t progress)
{
    uint32_t total = (len + 127) & ~127U; /* 128 对齐 */
    uint32_t i = 0;
    uint8_t dat[160];
    int rc;
    uint8_t first = 1;

    for (i = 0; i < total; i += 128) {
        uint8_t addr[4] = { (uint8_t)(i >> 8), (uint8_t)(i & 0xFF), 0x5A, 0xA5 };
        uint32_t j, chunk = 128;
        uint16_t dat_len;

        if (i + chunk > len) {
            /* 最后一块不足 128：补 0xFF */
            chunk = len - i;
        }
        for (j = 0; j < 4; j++) dat[j] = addr[j];
        for (j = 0; j < chunk; j++) dat[4 + j] = bin[i + j];
        for (j = chunk; j < 128; j++) dat[4 + j] = 0xFF;
        dat_len = 4 + 128;

        isp_send(c, first ? 0x22 : 0x02, dat, dat_len);
        first = 0;

        rc = isp_recv(c, NULL, NULL, NULL, 500);
        if (rc != STC_ISP_OK) return STC_ISP_ERR_WRITE;

        if (progress) {
            if (progress(i + 128 > len ? len : i + 128, len)) return STC_ISP_ERR_WRITE;
        }
    }
    return STC_ISP_OK;
}

/* ---- 流程：结束 ---- */

static void isp_terminate(IspCtx_t *c)
{
    isp_send(c, 0xFF, NULL, 0);
    c->port->delay_ms(200);
}

/* ---- 总入口 ---- */

int stc_isp_download(StcIspPort_t *port,
                     const uint8_t *bin, uint32_t len,
                     StcIspProgress_t progress)
{
    IspCtx_t ctx;
    int rc;

    if (!port || !bin || len == 0 || len > 27648) return STC_ISP_ERR_PARAM;  // STC8H1K28 程序区 27K（X57）

    ctx.port = port;
    ctx.chkmode = ISP_CHKMODE;

    rc = isp_detect(&ctx);
    if (rc != STC_ISP_OK) return rc;

    rc = isp_handshake(&ctx);
    if (rc != STC_ISP_OK) return rc;

    rc = isp_erase(&ctx);
    if (rc != STC_ISP_OK) return rc;

    rc = isp_flash(&ctx, bin, len, progress);
    if (rc != STC_ISP_OK) return rc;

    isp_terminate(&ctx);
    return STC_ISP_OK;
}
