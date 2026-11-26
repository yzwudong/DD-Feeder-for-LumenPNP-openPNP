#pragma once
#include <Arduino.h>
#include <functional>

class FeederBus;

/* IAP Bootloader 客户端骨架。
 *
 * 协议：A5 5A | CMD | SEQ | LEN(2) | ADDR(2) | DATA | CRC16(2)
 * 当前只提供接口/CRC/帧结构，方法返回 false，不接入 feeder_upgrade。
 * 启用前需实现 sendFrame/recvFrame 和重试。
 */
class IapClient {
public:
    bool begin(FeederBus* bus);

    /* HELLO：等待 Bootloader 回应，返回 Boot 版本/页大小。 */
    bool hello(uint32_t timeoutMs = 3000);

    /* ERASE：擦除 App 区。 */
    bool erase(uint32_t appSize, uint32_t timeoutMs = 15000);

    /* DATA：按 128B 分片写入；onProgress 0..100。 */
    bool write(const uint8_t* data, uint32_t size,
               std::function<void(int)> onProgress = nullptr);

    /* END：整包长度 + CRC32。 */
    bool end(uint32_t totalSize, uint32_t crc32, uint32_t timeoutMs = 3000);

    /* REBOOT：跳 App。 */
    bool reboot(uint32_t timeoutMs = 1000);

    const String& lastError() const { return _err; }

    static uint16_t crc16(const uint8_t* data, size_t len);
    static uint32_t crc32(const uint8_t* data, size_t len);

private:
    FeederBus* _bus = nullptr;
    uint8_t    _seq = 0;
    String     _err;

    bool sendFrame(uint8_t cmd, uint8_t seq, uint16_t addr,
                   const uint8_t* data, uint16_t len);
    bool recvFrame(uint8_t& cmd, uint8_t& seq, uint16_t& addr,
                   uint8_t* data, uint16_t& len, uint32_t timeoutMs);
};

