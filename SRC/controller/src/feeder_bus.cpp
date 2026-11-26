#include "feeder_bus.h"
#include "config.h"
#include "log_ring.h"

void FeederBus::begin(uint8_t txPin, uint8_t rxPin, uint8_t dePin, uint32_t baud) {
    _dePin = dePin;
    pinMode(_dePin, OUTPUT);
    digitalWrite(_dePin, LOW);          // 默认接收
    Serial2.begin(baud, SERIAL_8N1, rxPin, txPin);   // 485 总线在 UART2
    _ser = &Serial2;
    _txGuardUs = BUS_TX_GUARD_MS * 1000UL;
    _cmdGuardUs = BUS_CMD_GUARD_MS * 1000UL;
    _rxIdleUs = BUS_COLLECT_IDLE_MS * 1000UL;
}

void FeederBus::setDE(bool transmit) {
    digitalWrite(_dePin, transmit ? HIGH : LOW);
}

void FeederBus::loop() {
    if (!_ser || _exclusive) return;

    // 读入可用字节
    while (_ser->available()) {
        char c = (char)_ser->read();
        _rxCount++;
        _lastRxAt = micros();
        _lastActMs = millis();
        if (_state == IDLE && _unsolCb) {
            /* 总线主动消息(X56 ready 上报): 空闲时缓冲, 行尾/静默后回调 */
            if (_unsolBuf.length() == 0) _unsolLastRx = micros();
            _unsolBuf += c;
            if (c == '\n' || _unsolBuf.length() >= 256) _flushUnsolicited();
        } else if (_state == LISTEN) {
            _rx += c;
            if (c == '\n') { _lineSeen = true; _lineDoneUs = micros(); }
            // 普通模式完成判定(收集模式: 继续收直到静默超时):
            //   - 特征(ok/M620/error)命中只代表"应答中段已到达", 整行余帧可能还在路上:
            //     从机 X59 整帧发送(115200 下 100B ≈ 9ms), 特征字节与行尾之间仍有间隙,
            //     必须进尾窗等续帧, 到期仍无续帧才完成(如 M600 纯 ok / 单行短应答)
            //   - 不能见 M620/error 立即完成: 整行("ok error:3 M620 N99 A90...")未收完就
            //     finish, 余帧(N99 A90...)会被当 unsolicited 丢弃, 配置解析缺失
            if (!_collect && _expectOK) {
                bool hasOk  = _rx.indexOf("ok") >= 0 || _rx.indexOf("OK") >= 0;
                bool hasCfg = _rx.indexOf("M620") >= 0 || _rx.indexOf("addr:") >= 0 ||
                              _rx.indexOf("error ") >= 0 || _rx.indexOf("Error ") >= 0;
                if (hasCfg || hasOk) {
                    if (!_okTailWait) {
                        _okTailWait = true;
                        _okTailDeadline = micros() + BUS_REPLY_TAIL_MS * 1000UL;
                    } else if ((long)(micros() - _okTailDeadline) >= 0) {
                        _okTailWait = false;
                        _finish();
                    }
                }
            }
        }
    }

    switch (_state) {
        case IDLE:
            /* 主动消息行尾兜底: 缓冲非空但长时间无新数据 -> 当作一行回调 */
            if (_unsolBuf.length() && (long)(micros() - _unsolLastRx) >= 2000000UL) {
                _flushUnsolicited();
            }
            break;

        case SEND: {
            // 发送前加 DE 高 + 命令间隔保护
            if ((unsigned long)(micros() - _lastIdleAt) < _cmdGuardUs) return;
            setDE(true);
            delayMicroseconds(500);          /* DE 提前量: 首字节 M 不再被吃掉 */
            _ser->write((const uint8_t*)_tx.c_str(), _tx.length());
            _ser->flush();
            _txCount++;
            _t0 = micros();
            _state = RX_GUARD;
            break;
        }

        case RX_GUARD: {
            if ((unsigned long)(micros() - _t0) >= _txGuardUs) {
                setDE(false);           // 切接收
                /* 丢弃 DE 高(发送)期间回环/残渣字节, 避免污染应答开头(此前前缀被吞/乱码) */
                while (_ser->available()) _ser->read();
                _t0 = micros();
                _state = LISTEN;
            }
            break;
        }

        case LISTEN: {
            if (!_collect && _lineSeen) {
                if ((unsigned long)(micros() - _lineDoneUs) >= BUS_LINE_GUARD_MS * 1000UL) _finish();
            } else if (_collect) {
                // 多帧收集: 有数据后静默 _rxIdleUs 判定结束; 总超时兜底
                if (_rx.length() == 0) {
                    if ((unsigned long)(micros() - _t0) >= _cmdTimeoutMs * 1000UL) _finish();
                } else if ((unsigned long)(micros() - _lastRxAt) >= _rxIdleUs) {
                    _finish();
                }
            } else if ((unsigned long)(micros() - _t0) >= _cmdTimeoutMs * 1000UL) {
                _finish();
            }
            break;
        }
    }
}

void FeederBus::_finish() {
    String r = _rx;
    r.trim();
    if (r.length()) logPush('R', r);      // 485 -> 网关
    _state = IDLE;
    _lastIdleAt = micros();
    _lastActMs = millis();
    _collect = false;
    _okTailWait = false;
    _lineSeen = false;
    _rx = "";                         /* 防止应答尾字节污染下一条主动上报 */
    auto cb = _cb;
    _cb = nullptr;
    if (cb) cb(r);
}

/* 主动消息行收尾: 去空白后回调(只有注册了 _unsolCb 才可能非空) */
void FeederBus::_flushUnsolicited() {
    String line = _unsolBuf;
    _unsolBuf = "";
    line.trim();
    if (line.length() && _unsolCb) {
        logPush('R', line);               // 485 -> 网关(日志可见)
        _unsolCb(line);
    }
}

bool FeederBus::send(const String& cmd, uint32_t timeoutMs,
                     std::function<void(const String&)> cb) {
    if (_exclusive || busy()) return false;
    String c = cmd;
    c.trim();
    if (c.length()) logPush('T', c);     // 网关 -> 485
    _tx = cmd;
    _rx = "";
    _unsolBuf = "";          /* 启动上报/主动消息不能混进本次命令应答 */
    _unsolLastRx = 0;
    _cb = cb;
    _cmdTimeoutMs = timeoutMs;
    _expectOK = true;
    _collect = false;
    _lineSeen = false;
    _state = SEND;
    _lastActMs = millis();
    return true;
}

bool FeederBus::sendCollect(const String& cmd, uint32_t timeoutMs,
                            std::function<void(const String&)> cb) {
    if (_exclusive || busy()) return false;
    String c = cmd;
    c.trim();
    if (c.length()) logPush('T', c);     // 网关 -> 485
    _tx = cmd;
    _rx = "";
    _unsolBuf = "";          /* 启动上报/主动消息不能混进本次命令应答 */
    _unsolLastRx = 0;
    _cb = cb;
    _cmdTimeoutMs = timeoutMs;
    _expectOK = true;
    _collect = true;
    _lineSeen = false;
    _state = SEND;
    _lastActMs = millis();
    return true;
}

void FeederBus::beginExclusive() {
    _exclusive = true;
    setDE(false);
    if (_ser) {
        while (_ser->available()) _ser->read();
    }
    _state = IDLE;
    _cb = nullptr;
    _tx = "";
    _rx = "";
    _unsolBuf = "";
    _lastIdleAt = micros();
    _lastActMs = millis();
}

void FeederBus::endExclusive() {
    _exclusive = false;
    _state = IDLE;
    _cb = nullptr;
    _lastIdleAt = micros();
    _lastActMs = millis();
}

void FeederBus::writeRaw(const uint8_t* data, size_t len) {
    if (!_ser) return;
    /* 升级事务开始前清掉普通轮询/其他飞达留下的残字节 */
    while (_ser->available()) _ser->read();
    _lastActMs = millis();
    setDE(true);
    delayMicroseconds(500);
    _ser->write(data, len);
    _ser->flush();
    /* flush 只保证 FIFO/移位寄存器写入，不保证最后一个停止位已出线；
     * 立即拉低 DE 会截断 M788 末尾的 CRLF。 */
    delayMicroseconds(100);
    setDE(false);
}







