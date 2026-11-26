/* STC8G/8H ISP 下载 —— 按 wagiminator/stc8isp.py (MIT) 的 STC8H 协议实现。
 *
 * 与 stcflash 的 0x8F/0x8E 不同，STC8H 实际使用:
 *   2400 8E1, 0x7F 连接 -> 0x50 info
 *   0x01 设 115200 -> 0x05 校验
 *   0x03 擦除 -> 0x22 首帧 / 0x02 后续 128B -> 0xFF 退出
 * 现场 STC-ISP 抓包中的 0x05/0x03/0x02/0x04 与这套协议一致。
 */
#include "stc8_isp.h"
#include "config.h"
#include "log_util.h"
#include "driver/uart.h"

static FeederBus* g_ispBus = nullptr;
static bool  g_uartReady = false;
static uint32_t g_fosc = 0;
static uint32_t g_fisp = 0;
static uint8_t  g_chipver = 0;
static uint32_t g_chipid = 0;
static uint32_t g_connBaud = 2400;

#define ISP_BAUD_START 2400
#define ISP_BAUD_HIGH  115200
#define ISP_FRAME_HDR_HOST 0x6A
#define ISP_FRAME_HDR_CHIP 0x68
#define ISP_FRAME_TAIL     0x16
#define ISP_MAX_RECV       64
#define ISP_PROBE_ONLY     1   /* 1=只连接/读 info, 不设波特率/不擦/不写 */

static void ispSendRaw(const uint8_t* d, size_t n) {
    if (!g_ispBus || !n) return;
    g_ispBus->setDE(true);
    Serial2.write(d, n);
    Serial2.flush();
    g_ispBus->setDE(false);
}

static int ispReadRaw(uint8_t* d, size_t n, uint32_t timeoutMs) {
    size_t got = 0;
    unsigned long t0 = millis();
    while (got < n && (long)(millis() - t0) < (long)timeoutMs) {
        if (Serial2.available()) d[got++] = (uint8_t)Serial2.read();
        else delay(1);
    }
    return (int)got;
}

static void ispSetBaud(uint32_t baud, uint32_t cfg = SERIAL_8E1) {
    if (!g_uartReady) {
        Serial2.begin(baud, cfg, BUS_RX_PIN, BUS_TX_PIN);
        g_uartReady = true;
        return;
    }
    uart_set_baudrate(UART_NUM_2, baud);
    uart_set_parity(UART_NUM_2, (cfg == SERIAL_8E1) ? UART_PARITY_EVEN : UART_PARITY_DISABLE);
    delay(1);
}

static void ispRestoreBus() {
    Serial2.begin(BUS_BAUD, SERIAL_8N1, BUS_RX_PIN, BUS_TX_PIN);
    g_uartReady = false;
}

static void ispFlushRx() {
    while (Serial2.available()) Serial2.read();
}

/* 发送一帧: 46 B9 6A | N(2B) | payload(cmd+dat) | chksum(2B) | 16 */
static void ispSendFrame(const uint8_t* payload, size_t plen) {
    uint8_t frame[192];
    if (plen + 8 > sizeof(frame)) return;
    uint16_t n = (uint16_t)(plen + 6);
    frame[0] = 0x46; frame[1] = 0xB9; frame[2] = ISP_FRAME_HDR_HOST;
    frame[3] = (uint8_t)(n >> 8); frame[4] = (uint8_t)(n & 0xFF);
    uint16_t sum = ISP_FRAME_HDR_HOST + frame[3] + frame[4];
    for (size_t i = 0; i < plen; i++) { frame[5 + i] = payload[i]; sum += payload[i]; }
    frame[5 + plen]     = (uint8_t)(sum >> 8);
    frame[5 + plen + 1] = (uint8_t)(sum & 0xFF);
    frame[5 + plen + 2] = ISP_FRAME_TAIL;
    ispSendRaw(frame, plen + 8);
}

/* 接收一帧(芯片->主机): 46 B9 68 | N(2B) | cmd | dat | chksum(2B) | 16
 * 兼容 0x7F 首次应答只有 68 + len 的形式。 */
static bool ispRecvFrame(uint8_t* cmd, uint8_t* dat, uint16_t* datLen, uint32_t timeoutMs) {
    unsigned long t0 = millis();
    while ((long)(millis() - t0) < (long)timeoutMs) {
        uint8_t c;
        if (ispReadRaw(&c, 1, 50) != 1) continue;
        uint8_t lb[2];
        if (c == 0x46) {
            uint8_t h[2];
            if (ispReadRaw(h, 2, 50) != 2) continue;
            if (h[0] != 0xB9 || h[1] != ISP_FRAME_HDR_CHIP) continue;
            if (ispReadRaw(lb, 2, 50) != 2) continue;
        } else if (c == ISP_FRAME_HDR_CHIP) {
            if (ispReadRaw(lb, 2, 50) != 2) continue;
        } else {
            continue;
        }

        uint16_t n = (uint16_t)((lb[0] << 8) | lb[1]);
        if (n < 4 || n > ISP_MAX_RECV) continue;

        uint8_t body[ISP_MAX_RECV];
        uint16_t blen = (uint16_t)(n - 3);
        if (blen > sizeof(body) || ispReadRaw(body, blen, 500) != (int)blen) continue;
        if (body[blen - 1] != ISP_FRAME_TAIL) continue;

        uint16_t sum = ISP_FRAME_HDR_CHIP + lb[0] + lb[1];
        for (uint16_t i = 0; i < (uint16_t)(blen - 3); i++) sum += body[i];
        if (body[blen - 2] != (uint8_t)(sum & 0xFF)) continue;
        if (body[blen - 3] != (uint8_t)(sum >> 8)) continue;

        if (cmd) *cmd = body[0];
        uint16_t dl = (uint16_t)(blen - 4);   /* 去掉 cmd + chk2 + tail */
        if (dat && datLen) {
            if (dl > *datLen) dl = *datLen;
            for (uint16_t i = 0; i < dl; i++) dat[i] = body[1 + i];
        }
        if (datLen) *datLen = dl;
        return true;
    }
    return false;
}

static bool ispCmd(uint8_t cmd, const uint8_t* dat, uint16_t dlen, uint8_t expect, uint32_t tmo) {
    uint8_t p[160];
    if (dlen + 1 > sizeof(p)) return false;
    p[0] = cmd;
    if (dlen) memcpy(p + 1, dat, dlen);
    ispSendFrame(p, (size_t)dlen + 1);
    uint8_t rcmd = 0;
    if (!ispRecvFrame(&rcmd, nullptr, nullptr, tmo)) return false;
    return rcmd == expect;
}

/* 枚举常见波特率/校验位, 每个组合发 0x7F, 记录收到的 cmd/dl;
 * 只有收到 0x50 info 才认为连接成功。之前 0x8F/0x01 失败可能把芯片
 * 留在 115200 或其他波特率, 所以 115200 先试。 */
static bool ispConnect() {
    struct Mode { uint32_t baud; uint32_t cfg; };
    static const Mode modes[] = {
        {115200, SERIAL_8E1},
        {115200, SERIAL_8N1},
        {57600,  SERIAL_8E1},
        {38400,  SERIAL_8E1},
        {19200,  SERIAL_8E1},
        {9600,   SERIAL_8N1},
        {9600,   SERIAL_8E1},
        {4800,   SERIAL_8E1},
        {2400,   SERIAL_8E1},
    };
    const int nmodes = (int)(sizeof(modes) / sizeof(modes[0]));

    for (int m = 0; m < nmodes; m++) {
        g_connBaud = modes[m].baud;
        ispSetBaud(modes[m].baud, modes[m].cfg);
        ispFlushRx();
        for (int i = 0; i < 8; i++) {
            uint8_t sync = 0x7F;
            ispSendRaw(&sync, 1);
            uint8_t cmd = 0;
            uint8_t dat[64]; uint16_t dl = sizeof(dat);
            if (ispRecvFrame(&cmd, dat, &dl, 200)) {
                LOGD("[ISP] probe %u %s: cmd=%02X dl=%u\n",
                     (unsigned)g_connBaud,
                     (modes[m].cfg == SERIAL_8N1) ? "8N1" : "8E1",
                     cmd, dl);
                if (cmd == 0x50 && dl >= 40) {
                    g_fosc = ((uint32_t)dat[0] << 24) | ((uint32_t)dat[1] << 16) |
                             ((uint32_t)dat[2] << 8) | dat[3];
                    g_fisp = (((uint16_t)dat[12] << 8) | dat[13]) * g_connBaud;
                    g_chipver = dat[16];
                    g_chipid = ((uint16_t)dat[19] << 8) | dat[20];
                    LOGD("[ISP] connect OK at %u %s\n", (unsigned)g_connBaud,
                         (modes[m].cfg == SERIAL_8N1) ? "8N1" : "8E1");
                    return true;
                }
            }
        }
    }
    return false;
}

static bool ispSetBaudHigh() {
    if (!g_fisp) return false;
    uint32_t count = 65536UL - (g_fisp / (4UL * ISP_BAUD_HIGH));
    uint8_t blk[7] = {(uint8_t)(g_fosc & 0xFF), 0x40,
                      (uint8_t)(count >> 8), (uint8_t)(count & 0xFF),
                      0x00, 0x00, 0x97};
    LOGD("[ISP] fosc=%u fisp=%u chipid=%06X ver=%02X count=%04X\n",
         (unsigned)g_fosc, (unsigned)g_fisp, (unsigned)g_chipid, g_chipver, (unsigned)count);
    if (!ispCmd(0x01, blk, sizeof(blk), 0x01, 1000)) return false;
    delay(10);
    ispSetBaud(ISP_BAUD_HIGH, SERIAL_8E1);

    uint8_t chk[4] = {0, 0, 0x5A, 0xA5};
    if (g_chipver >= 0x72) {
        if (!ispCmd(0x05, chk, sizeof(chk), 0x05, 1000)) return false;
    } else {
        if (!ispCmd(0x05, nullptr, 0, 0x05, 1000)) return false;
    }
    return true;
}

static bool ispErase() {
    uint8_t e[4] = {0, 0, 0x5A, 0xA5};
    return ispCmd(0x03, e, sizeof(e), 0x03, 5000);
}

static bool ispWrite(const uint8_t* code, uint32_t size, std::function<void(int)> onProgress) {
    uint8_t blk[5 + 128];
    uint32_t off = 0;
    bool first = true;
    while (off < size) {
        uint32_t n = size - off;
        if (n > 128) n = 128;
        blk[0] = first ? 0x22 : 0x02;
        blk[1] = (uint8_t)(off >> 8);
        blk[2] = (uint8_t)(off & 0xFF);
        blk[3] = 0x5A; blk[4] = 0xA5;
        memcpy(blk + 5, code + off, n);
        if (!ispCmd(blk[0], blk + 1, 4 + n, 0x02, 1500)) {
            LOGD("[ISP] page 0x%04X failed\n", (unsigned)off);
            return false;
        }
        first = false;
        off += n;
        if (onProgress) onProgress(10 + (int)((uint64_t)off * 85 / size));
    }
    return true;
}

bool stc8Flash(FeederBus* bus, const uint8_t* code, uint32_t size,
               uint32_t startBaud, uint32_t dlBaud,
               std::function<void(int)> onProgress) {
    (void)startBaud; (void)dlBaud;
    if (!bus || !code || size == 0) return false;
    if (size > 0x8000) size = 0x8000;
    g_ispBus = bus;
    g_uartReady = false;
    if (onProgress) onProgress(5);

    if (!ispConnect()) {
        LOGD("[ISP] connect 2400 8E1 failed\n");
        ispRestoreBus();
        return false;
    }
    LOGD("[ISP] connect OK chipid=%06X ver=%02X fosc=%u\n",
         (unsigned)g_chipid, g_chipver, (unsigned)g_fosc);

#if ISP_PROBE_ONLY
    LOGD("[ISP] PROBE ONLY: no 0x01/0x05/0x03/write sent\n");
    ispRestoreBus();
    return false;
#endif

    if (!ispSetBaudHigh()) {
        LOGD("[ISP] set baud/check failed\n");
        ispRestoreBus();
        return false;
    }
    LOGD("[ISP] baud 115200 OK\n");

    if (!ispErase()) {
        LOGD("[ISP] erase failed\n");
        ispRestoreBus();
        return false;
    }
    LOGD("[ISP] erase OK\n");
    if (onProgress) onProgress(10);

    if (!ispWrite(code, size, onProgress)) {
        LOGD("[ISP] write failed\n");
        ispRestoreBus();
        return false;
    }

    uint8_t fin = 0xFF;
    ispSendFrame(&fin, 1);
    delay(200);
    if (onProgress) onProgress(100);
    LOGD("[ISP] done\n");
    ispRestoreBus();
    return true;
}

