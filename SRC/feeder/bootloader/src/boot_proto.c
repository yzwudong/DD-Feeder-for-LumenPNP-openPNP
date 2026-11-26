#include "boot_proto.h"
#include "boot_flag.h"
#include "../src/config.h"
#include "boot_uart.h"
#include "boot_iap.h"
#include "boot_config.h"
#include "fw_hal.h"

#define BOOT_FRAME_MAX  128
#define BOOT_TX_MAX     16
#define BOOT_TIMEOUT_10US  200000UL   /* 2s 名义；Timer0 实测偏慢，实际约 10s */

static uint16_t crc16_add(uint16_t crc, uint8_t b)
{
    uint8_t i;
    crc ^= (uint16_t)b << 8;
    for (i = 0; i < 8; i++) {
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                             : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint16_t crc16(const uint8_t *d, uint16_t n)
{
    uint16_t crc = 0xFFFF;
    uint16_t i;
    for (i = 0; i < n; i++) crc = crc16_add(crc, d[i]);
    return crc;
}

static uint32_t crc32_add(uint32_t crc, uint8_t b)
{
    uint8_t i;
    crc ^= b;
    for (i = 0; i < 8; i++) {
        crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }
    return crc;
}

static int wait_byte(uint32_t *elapsed, uint32_t limit)
{
    int c;
    while (*elapsed < limit) {
        c = boot_uart_getc();
        if (c >= 0) return c;
        boot_delay_10us();
        (*elapsed)++;
    }
    return -1;
}

static void send_ack(uint8_t cmd, uint8_t seq, uint16_t addr,
                     const uint8_t *data, uint8_t len)
{
    static __xdata uint8_t tx[6 + BOOT_TX_MAX + 2];
    uint16_t crc;
    uint8_t i;

    if (len > BOOT_TX_MAX) return;
    tx[0] = BOOT_SOF0;
    tx[1] = BOOT_SOF1;
    tx[2] = cmd;
    tx[3] = seq;
    tx[4] = 0;
    tx[5] = len;
    tx[6] = (uint8_t)(addr >> 8);
    tx[7] = (uint8_t)addr;
    for (i = 0; i < len; i++) tx[8 + i] = data[i];
    crc = crc16(&tx[2], (uint16_t)(6 + len));
    tx[8 + len] = (uint8_t)(crc >> 8);
    tx[9 + len] = (uint8_t)crc;

    /* 统一 turnaround：等主机 485 DE 释放 */
    boot_delay_ms(3);
    boot_uart_send(tx, (uint16_t)(10 + len));
}

static int recv_frame(uint8_t *cmd, uint8_t *seq, uint16_t *addr,
                      uint8_t *data, uint16_t *len)
{
    uint32_t elapsed = 0;
    uint8_t h[6];
    uint16_t n, crc_rx, crc_calc;
    uint16_t i;
    int c;

    RS485_SetRxMode();
    for (;;) {
        c = wait_byte(&elapsed, BOOT_TIMEOUT_10US);
        if (c < 0) return -1;
        if (c != BOOT_SOF0) continue;
        c = wait_byte(&elapsed, BOOT_TIMEOUT_10US);
        if (c < 0) return -1;
        if (c == BOOT_SOF1) break;
    }
    for (i = 0; i < 6; i++) {
        c = wait_byte(&elapsed, BOOT_TIMEOUT_10US);
        if (c < 0) return -1;
        h[i] = (uint8_t)c;
    }
    *cmd = h[0];
    *seq = h[1];
    n = (uint16_t)((h[2] << 8) | h[3]);
    *addr = (uint16_t)((h[4] << 8) | h[5]);
    if (n > BOOT_FRAME_MAX) return -1;

    for (i = 0; i < n; i++) {
        c = wait_byte(&elapsed, BOOT_TIMEOUT_10US);
        if (c < 0) return -1;
        data[i] = (uint8_t)c;
    }
    c = wait_byte(&elapsed, BOOT_TIMEOUT_10US);
    if (c < 0) return -1;
    crc_rx = (uint16_t)c << 8;
    c = wait_byte(&elapsed, BOOT_TIMEOUT_10US);
    if (c < 0) return -1;
    crc_rx |= (uint16_t)c;

    crc_calc = crc16(h, 6);
    for (i = 0; i < n; i++) crc_calc = crc16_add(crc_calc, data[i]);
    if (crc_calc != crc_rx) return -1;

    *len = n;
    return 0;
}

int boot_proto_run(void)
{
    uint8_t cmd, seq;
    uint16_t addr, len;
    static __xdata uint8_t data[BOOT_FRAME_MAX];
    uint32_t total = 0;
    uint32_t crc = 0xFFFFFFFFUL;
    uint8_t ack[2];

    for (;;) {
        if (recv_frame(&cmd, &seq, &addr, data, &len) != 0) return -1;

        if (cmd == BOOT_CMD_HELLO) {
            ack[0] = 0x01;
            ack[1] = BOOT_PAGE_SIZE;
            send_ack(BOOT_ACK_HELLO, seq, 0, ack, 2);
        } else if (cmd == BOOT_CMD_ERASE) {
            total = 0;
            crc = 0xFFFFFFFFUL;
            boot_iap_reset_erase_map();
            ack[0] = 0;
            send_ack(BOOT_ACK_ERASE, seq, 0, ack, 0);
        } else if (cmd == BOOT_CMD_DATA) {
            uint16_t i;
            if (len == 0 || boot_iap_write_page(addr, data, len) != 0) {
                ack[0] = 0;
                send_ack(0xFF, seq, addr, ack, 0);
            } else {
                for (i = 0; i < len; i++) crc = crc32_add(crc, data[i]);
                total += len;
                ack[0] = 1;
                send_ack(BOOT_ACK_DATA, seq, addr, ack, 0);
            }
        } else if (cmd == BOOT_CMD_END) {
            uint32_t size, crc_in;
            if (len < 8) {
                ack[0] = 0;
                send_ack(BOOT_ACK_END, seq, 0, ack, 1);
                continue;
            }
            size = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                   ((uint32_t)data[2] << 8) | data[3];
            crc_in = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                     ((uint32_t)data[6] << 8) | data[7];
            {
                const uint8_t __code *v = (const uint8_t __code *)0x1800;
                uint8_t app_ok = (v[0] == 0x02 && v[1] == 0x18) ? 1 : 0;
                uint8_t reason = 0;
                if (size != total) reason = 1;
                else if (crc_in != (crc ^ 0xFFFFFFFFUL)) reason = 2;
                else if (!app_ok) reason = 3;
                else { boot_flag_clear(); }
                ack[0] = reason ? 0 : 1;
                ack[1] = reason;
            }
            send_ack(BOOT_ACK_END, seq, 0, ack, 2);
        } else if (cmd == BOOT_CMD_REBOOT) {
            ack[0] = 0;
            send_ack(BOOT_ACK_REBOOT, seq, 0, ack, 0);
            return 0;
        } else {
            ack[0] = 0;
            send_ack(0xFF, seq, addr, ack, 0);
        }
    }
}






