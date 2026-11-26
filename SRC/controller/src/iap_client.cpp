#include "iap_client.h"
#include "feeder_bus.h"

/* 帧：A5 5A | CMD | SEQ | LEN(2) | ADDR(2) | DATA | CRC16(2) */
#define IAP_SOF0 0xA5
#define IAP_SOF1 0x5A
#define IAP_MAX_DATA 128
#define IAP_ACK_HELLO 0x81
#define IAP_ACK_ERASE 0x82
#define IAP_ACK_DATA 0x83
#define IAP_ACK_END 0x84
#define IAP_ACK_REBOOT 0x85
#define IAP_APP_ADDR 0x1800UL
#define IAP_APP_END  0x69FFUL

static uint16_t crc16_add(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)b << 8;
    for (int i = 0; i < 8; i++) {
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                             : (uint16_t)(crc << 1);
    }
    return crc;
}

uint16_t IapClient::crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) crc = crc16_add(crc, data[i]);
    return crc;
}

uint32_t IapClient::crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

bool IapClient::begin(FeederBus* bus) {
    _bus = bus;
    _seq = 0;
    _err = "";
    return _bus != nullptr;
}

bool IapClient::sendFrame(uint8_t cmd, uint8_t seq, uint16_t addr,
                          const uint8_t* data, uint16_t len) {
    if (!_bus || len > IAP_MAX_DATA) return false;
    uint8_t buf[6 + IAP_MAX_DATA + 2];
    buf[0] = IAP_SOF0;
    buf[1] = IAP_SOF1;
    buf[2] = cmd;
    buf[3] = seq;
    buf[4] = (uint8_t)(len >> 8);
    buf[5] = (uint8_t)(len & 0xFF);
    buf[6] = (uint8_t)(addr >> 8);
    buf[7] = (uint8_t)(addr & 0xFF);
    for (uint16_t i = 0; i < len; i++) buf[8 + i] = data[i];
    uint16_t crc = crc16(&buf[2], (size_t)(6 + len));
    buf[8 + len] = (uint8_t)(crc >> 8);
    buf[9 + len] = (uint8_t)(crc & 0xFF);

    for (int i = 0; i < 256 && Serial2.available(); i++) Serial2.read();
    delay(5);                 // 对端 ACK 后仍有 DE 释放/接收切换窗口
    _bus->setDE(true);
    delayMicroseconds(50);
    Serial2.write(buf, (size_t)(10 + len));
    Serial2.flush();
    delayMicroseconds(50);
    _bus->setDE(false);
    return true;
}

static int readByte(uint32_t deadline) {
    while ((long)(millis() - deadline) < 0) {
        if (Serial2.available()) return Serial2.read();
        delay(1);
    }
    return -1;
}

bool IapClient::recvFrame(uint8_t& cmd, uint8_t& seq, uint16_t& addr,
                          uint8_t* data, uint16_t& len, uint32_t timeoutMs) {
    if (!_bus) return false;
    uint32_t deadline = millis() + timeoutMs;

    /* 找 A5 5A */
    int c = -1;
    while (true) {
        c = readByte(deadline);
        if (c < 0) return false;
        if (c != IAP_SOF0) continue;
        c = readByte(deadline);
        if (c < 0) return false;
        if (c == IAP_SOF1) break;
    }

    int v;
    v = readByte(deadline); if (v < 0) return false; cmd = (uint8_t)v;
    v = readByte(deadline); if (v < 0) return false; seq = (uint8_t)v;
    int lh = readByte(deadline); if (lh < 0) return false;
    int ll = readByte(deadline); if (ll < 0) return false;
    int ah = readByte(deadline); if (ah < 0) return false;
    int al = readByte(deadline); if (al < 0) return false;
    uint16_t n = (uint16_t)((lh << 8) | ll);
    if (n > IAP_MAX_DATA) return false;
    addr = (uint16_t)((ah << 8) | al);

    uint8_t tmp[IAP_MAX_DATA];
    for (uint16_t i = 0; i < n; i++) {
        v = readByte(deadline);
        if (v < 0) return false;
        tmp[i] = (uint8_t)v;
    }
    int ch = readByte(deadline); if (ch < 0) return false;
    int cl = readByte(deadline); if (cl < 0) return false;
    uint16_t crcRx = (uint16_t)((ch << 8) | cl);

    uint8_t hdr[6] = {cmd, seq, (uint8_t)lh, (uint8_t)ll, (uint8_t)ah, (uint8_t)al};
    uint16_t crc = crc16(hdr, sizeof(hdr));
    for (uint16_t i = 0; i < n; i++) crc = crc16_add(crc, tmp[i]);
    if (crc != crcRx) return false;

    len = n;
    if (data) for (uint16_t i = 0; i < n; i++) data[i] = tmp[i];
    return true;
}

bool IapClient::hello(uint32_t timeoutMs) {
    /* App 收到 M788 后还要写 flag 再复位；Bootloader 可能在几 ms 后才起来。
     * 周期重发 HELLO，直到超时。先清掉 M788 应答残留，避免误解析。 */
    for (int i = 0; i < 256 && Serial2.available(); i++) Serial2.read();
    uint32_t deadline = millis() + timeoutMs;
    while ((long)(millis() - deadline) < 0) {
        if (!sendFrame(0x01, 0, 0, nullptr, 0)) {
            _err = "IAP hello send failed";
            return false;
        }
        uint8_t cmd = 0, seq = 0;
        uint16_t addr = 0, len = 0;
        uint8_t data[IAP_MAX_DATA];
        if (recvFrame(cmd, seq, addr, data, len, 200) && cmd == IAP_ACK_HELLO) {
            _err = "";
            return true;
        }
        delay(100);
    }
    _err = "IAP hello timeout";
    return false;
}

bool IapClient::erase(uint32_t appSize, uint32_t timeoutMs) {
    (void)appSize;
    if (!sendFrame(0x02, 0, 0, nullptr, 0)) {
        _err = "IAP erase send failed";
        return false;
    }
    uint8_t cmd = 0, seq = 0;
    uint16_t addr = 0, len = 0;
    uint8_t data[IAP_MAX_DATA];
    if (!recvFrame(cmd, seq, addr, data, len, timeoutMs) || cmd != IAP_ACK_ERASE) {
        _err = "IAP erase timeout/bad ack";
        return false;
    }
    _seq = 0;
    _err = "";
    return true;
}

bool IapClient::write(const uint8_t* data, uint32_t size,
                      std::function<void(int)> onProgress) {
    if (!data || !size) {
        _err = "IAP write empty";
        return false;
    }
    uint32_t off = 0;
    while (off < size) {
        uint32_t n = size - off;
        if (n > IAP_MAX_DATA) n = IAP_MAX_DATA;
        uint32_t addr = IAP_APP_ADDR + off;
        if (addr + n > IAP_APP_END + 1) {
            _err = "IAP image exceeds app region";
            return false;
        }
        if (!sendFrame(0x03, _seq, (uint16_t)addr, data + off, (uint16_t)n)) {
            _err = "IAP data send failed";
            return false;
        }
        uint8_t cmd = 0, seq = 0;
        uint16_t raddr = 0, rlen = 0;
        uint8_t rdata[IAP_MAX_DATA];
        if (!recvFrame(cmd, seq, raddr, rdata, rlen, 8000) ||
            cmd != IAP_ACK_DATA || seq != _seq) {
            _err = "IAP data ack failed @" + String(off);
            return false;
        }
        _seq++;
        off += n;
        if (onProgress) onProgress((int)((uint64_t)off * 100 / size));
    }
    _err = "";
    return true;
}

bool IapClient::end(uint32_t totalSize, uint32_t crc32v, uint32_t timeoutMs) {
    uint8_t d[8];
    d[0] = (uint8_t)(totalSize >> 24);
    d[1] = (uint8_t)(totalSize >> 16);
    d[2] = (uint8_t)(totalSize >> 8);
    d[3] = (uint8_t)totalSize;
    d[4] = (uint8_t)(crc32v >> 24);
    d[5] = (uint8_t)(crc32v >> 16);
    d[6] = (uint8_t)(crc32v >> 8);
    d[7] = (uint8_t)crc32v;
    if (!sendFrame(0x04, 0, 0, d, sizeof(d))) {
        _err = "IAP end send failed";
        return false;
    }
    uint8_t cmd = 0, seq = 0;
    uint16_t addr = 0, len = 0;
    uint8_t rdata[IAP_MAX_DATA];
    for (int attempt = 0; attempt < 2; attempt++) {
        if (attempt) {
            delay(50);
            if (!sendFrame(0x04, 0, 0, d, sizeof(d))) continue;
        }
        if (recvFrame(cmd, seq, addr, rdata, len, timeoutMs) &&
            cmd == IAP_ACK_END && len >= 1 && rdata[0] == 1) {
            _err = "";
            return true;
        }
    }
    _err = "IAP end failed reason=" + String(len > 1 ? rdata[1] : 255);
    return false;
}

bool IapClient::reboot(uint32_t timeoutMs) {
    if (!sendFrame(0x05, 0, 0, nullptr, 0)) {
        _err = "IAP reboot send failed";
        return false;
    }
    uint8_t cmd = 0, seq = 0;
    uint16_t addr = 0, len = 0;
    uint8_t data[IAP_MAX_DATA];
    if (!recvFrame(cmd, seq, addr, data, len, timeoutMs) || cmd != IAP_ACK_REBOOT) {
        _err = "IAP reboot ack failed";
        return false;
    }
    _err = "";
    return true;
}




