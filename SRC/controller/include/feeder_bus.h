#pragma once
#include <Arduino.h>
#include <functional>

/* ============================================================
 * 485 半双工总线驱动 (MAX485 + UART2)
 * 非阻塞状态机: IDLE -> SEND -> LISTEN -> DONE
 * 逐条命令排队发送，发送后自动切接收，收满/超时后回调。
 * 收发均写入 log_ring 供 Web 实时日志显示。
 * 供 MCODE 轮询与 STC8 ISP 共用（ISP 走 write/read 原始字节）。
 * ============================================================ */
class FeederBus {
public:
    void begin(uint8_t txPin, uint8_t rxPin, uint8_t dePin, uint32_t baud);
    void loop();                 // 高频调用
    void setDE(bool transmit);   // 手动控制收发方向

    /* 非阻塞发送一条命令(以换行结尾)。busy 时返回 false。
     * 完成(收到 "ok" / 超时)后调用 cb(reply)。reply 为收集到的原始文本。 */
    bool send(const String& cmd, uint32_t timeoutMs,
              std::function<void(const String&)> cb);

    /* 多帧收集模式(M602 N255 全局查询用):
     * 不因首个 "ok" 结束，持续接收所有设备的错峰回复，
     * 直到静默 BUS_COLLECT_IDLE_MS 或总超时 timeoutMs。 */
    bool sendCollect(const String& cmd, uint32_t timeoutMs,
                     std::function<void(const String&)> cb);

    /* 原始字节发送(ISP 升级用)，立即发送，不进入状态机。 */
    void writeRaw(const uint8_t* data, size_t len);

    /* 升级独占：暂停状态机、拒绝普通 send、清空 RX 残留。
     * 只允许 IapClient / writeRaw 直接访问 485。 */
    void beginExclusive();
    void endExclusive();
    bool exclusive() const { return _exclusive; }

    /* 总线主动消息监听(X56 上电上报/设备自发消息):
     * IDLE 空闲时收到的非请求数据行(以 \n 或静默 50ms 结束)回调。
     * 正常命令收发期间(SEND/RX_GUARD/LISTEN)不触发, 避免与命令回复重复。 */
    void onUnsolicited(std::function<void(const String&)> cb) { _unsolCb = cb; }

    bool busy() const { return _state != IDLE; }
    uint32_t getTxCount() const { return _txCount; }
    uint32_t getRxCount() const { return _rxCount; }
    uint32_t getErrCount() const { return _errCount; }
    void resetCounters() { _txCount = _rxCount = _errCount = 0; }
    /* 总线最近一次活动(收/发/DE 切换)时刻(ms) —— 供"空闲窗口"判定,
     * 广播扫描只在连续空闲 ≥ SCAN_IDLE_MS 时启动, 避免与生产指令争抢 485。 */
    unsigned long lastActivityMs() const { return _lastActMs; }

private:
    enum State { IDLE, SEND, RX_GUARD, LISTEN };

    HardwareSerial* _ser = nullptr;
    uint8_t  _dePin = 0;
    uint32_t _cmdTimeoutMs = 100;
    uint32_t _txGuardUs = 2000;   // 发送->接收 防抖
    uint32_t _cmdGuardUs = 2000;  // 命令间隔保护
    uint32_t _rxIdleUs = 200000;  // 收集模式: 无新字节静默判定(200ms)

    State _state = IDLE;
    bool _exclusive = false;
    unsigned long _t0 = 0;
    unsigned long _lastRxAt = 0;  // 最近一次收到字节的时刻
    String _tx;
    String _rx;
    std::function<void(const String&)> _cb;
    bool _expectOK = false;      // 是否等 "ok" 即结束
    bool _collect = false;       // 多帧收集模式
    bool _okTailWait = false;    // 普通模式: 已见 "ok" 但配置续帧未到(拆段应答), 等短窗口
    bool _lineSeen = false;      // LISTEN ??????
    unsigned long _lineDoneUs = 0;
    unsigned long _okTailDeadline = 0;  // 拆段等待截止(micros)
    unsigned long _lastIdleAt = 0;
    unsigned long _lastActMs = 0;   // 最近收发活动(ms)
    std::function<void(const String&)> _unsolCb;   // 主动消息回调
    String _unsolBuf;                               // 主动消息行缓冲
    unsigned long _unsolLastRx = 0;                 // 主动消息最近字节时刻

    uint32_t _txCount = 0, _rxCount = 0, _errCount = 0;

    void _finish();                 // 结束当前收发并触发回调
    void _flushUnsolicited();       // 主动消息行收尾(去空白后回调)
};
