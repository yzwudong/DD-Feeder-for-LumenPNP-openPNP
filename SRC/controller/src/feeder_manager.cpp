#include "feeder_manager.h"
#include "config.h"
#include "mcode.h"
#include "calib.h"
#include "bus_power.h"
#include "log_util.h"
#include "buzzer.h"
#include <LittleFS.h>
#include <Preferences.h>

void FeederManager::begin(FeederBus* bus) {
    _bus = bus;
    for (int i = 0; i < FEEDER_COUNT; i++) {
        _feeders[i].slot = i;
        _feeders[i].addr = i;              // 飞达地址 0~63, 与槽位一致
    }
    _bootScanAt = millis() + BOOT_SCAN_DELAY_MS;   // 启动延时广播一次, 等飞达初始化
    _feedSaveAt = millis() + 300000UL;   /* 计数落盘 5 分钟，降低 Flash 写次数 */
    Preferences p;
    if (p.begin("hb", true)) {
        _hbPeriodMs = p.getULong("period", HEARTBEAT_DEFAULT_MS);
        p.end();
    } else {
        _hbPeriodMs = HEARTBEAT_DEFAULT_MS;
    }
    if (_hbPeriodMs < 1000 || _hbPeriodMs > 3600000UL) _hbPeriodMs = HEARTBEAT_DEFAULT_MS;
    loadFeedCount();                       // 恢复推料计数
}

void FeederManager::setHeartbeatMs(uint32_t ms) {
    _hbPeriodMs = ms;
    Preferences p;
    if (p.begin("hb", false)) {
        p.putULong("period", ms);
        p.end();
    }
}

void FeederManager::loop() {
    if (!_bus) return;


    unsigned long now = millis();

    /* 生产锁 10s 空闲自动释放；手动解除状态不自动重新加锁。 */
    if (_prodActive && !_manualReleased && _prodHead == _prodTail &&
        (long)(now - _prodLastAt) >= 30000L) {
        _prodActive = false;
    }

    /* N255 扫描优先于普通队列；先清普通 M602，避免 N3 抢总线。 */
    if (!calibActive() && !productionLocked() && _scanPending &&
        !_bus->busy() &&
        (long)(now - _bus->lastActivityMs()) >= (long)SCAN_IDLE_MS) {
        _scanPending = false;
        _pushScanAll();
        return;
    }

    /* 生产队列优先，其次普通队列；都要求总线空闲窗口。 */
    if (!_bus->busy() &&
        (long)(now - _bus->lastActivityMs()) >= (long)HB_IDLE_MS) {
        Job out;
        if (_nextJob(out)) {
            _sendJob(out);
            return;
        }
    }

    // 校准期间暂停心跳/扫描，避免 M602 与校准 M603 抢总线
    if (!calibActive()) {
    if (productionLocked()) _scanPending = false;
    // 广播(启动扫描/手动 scan)受理后: 等总线连续空闲窗口再发出
    if (_scanPending) {
        if (!_bus->busy() &&
            (long)(millis() - _bus->lastActivityMs()) >= (long)SCAN_IDLE_MS) {
            _scanPending = false;
            _pushScanAll();                 // 空闲窗口: 立即发 M602 N255
        } else if ((long)(millis() - _scanWaitAt) >= 0) {
            _scanPending = false;           // 生产持续占用, 等待超时 -> 取消
        }
    }

    // 定向心跳(P1): 总线空闲窗口内, 逐台单播已知地址(保活+冲突检测), 不广播
    if (!_bus->busy() && _hbPeriodMs > 0 && !_scanning && !_scanPending &&
        _prodHead == _prodTail && _normHead == _normTail &&
        (long)(millis() - _bus->lastActivityMs()) >= (long)HB_IDLE_MS &&
        (long)(millis() - _hbNextAt) >= 0) {
        _hbNextAt = millis() + _hbPeriodMs;   // 至少每周期扫一轮
        for (int n = 0; n < FEEDER_COUNT; n++) {
            int i = _hbIdx;
            _hbIdx = (_hbIdx + 1) % FEEDER_COUNT;
            FeederState& f = _feeders[i];
            /* 心跳只保活在线飞达。离线飞达不再每 30s 发 N3；
             * 重新插入靠 X56 ready 上报或手动 N255 扫描恢复。 */
            if (!f.present) continue;
            int a = f.addr;
            _bus->send(mcodeQuery(MCMD_QUERY, a), BUS_QUERY_TIMEOUT,
                       [this, a](const String& r){
                if (r.length() == 0) {
                    if (_hbMiss[a] < 255) _hbMiss[a]++;
                    if (_hbMiss[a] >= HB_MAX_MISS) _feeders[a].present = false;  // 判离线
                } else {
                    _hbMiss[a] = 0;
                }
                _onReply(a, r);              // lastSeen/冲突检测/临时表 复用
            });
            return;                          // 每轮只发一条, 下轮再发下一条
        }
    }

    // 固件启动后延时广播一次(M602 N255): 等飞达上电初始化; 只在总线空闲时执行
    if (!_bootScanDone && !_scanning && !_scanPending &&
        (long)(millis() - _bootScanAt) >= 0) {
        _bootScanDone = true;               // 只受理一次(成败不再重试)
        _scanPending = true;
        _scanWaitAt = millis() + SCAN_ABORT_MS;
    }

    }
    // 推料计数周期落盘(dirty 后 5 分钟写一次, 减少 flash 磨损)
    if (_feedDirty && (long)(millis() - _feedSaveAt) >= 0) {
        _feedSaveAt = millis() + 300000UL;   /* 计数落盘 5 分钟，降低 Flash 写次数 */
        saveFeedCount();
    }
    // 注: 无周期自动扫描。广播查询会与飞达正常动作争抢 485 总线,
    // 只在启动时一次 + 用户手动触发(requestScan), 且均需总线空闲窗口(SCAN_IDLE_MS)。
}

void FeederManager::_pushScanAll() {
    if (_bus->busy()) return;
    cancelNormalQueries();       /* N255 前清掉排队 N3，避免抢总线 */
    _scanning = true;
    _lastScanAt = millis();
    // 扫描开始前全部置离线: N255 只收集在线回复, 未回复者视为无飞达(灰化)
    for (int i = 0; i < FEEDER_COUNT; i++) { _feeders[i].present = false; _feeders[i].conflict = false; }
    for (int i = 0; i < TEMP_FEEDER_MAX; i++) _temp[i].present = false;   // 临时飞达同样按回复刷新
    _conflictCount = 0;
    for (int i = 0; i < 32; i++) _conflictAddr[i] = 0;
    // 注: _uninitCount/_tempCount 不在此清零 —— 广播 1s 收集窗口内保留旧值,
    //     避免 Web 轮询读到空表导致临时卡片闪没; 应答按 isNew 增量, 广播结束统一清理未应答项。

    MCmdMsg m;
    m.code = MCMD_QUERY;
    m.explicitBroadcast = true;              // M602 N255 全局查询
    String cmd = mcodeBuildMsg(m);
    if (!_bus->sendCollect(cmd, BUS_COLLECT_TIMEOUT_MS,
                           [this](const String& r){ _onGlobalReply(r); })) {
        _scanning = false;
    }
}

/* M602 N255 多帧回复拆分:
 * "ok addr:5 error:0 VER:3 ChipID:112233\r\nok addr:8 ...\r\n"
 * 全局应答不含配置参数(A/B/C/F/U/V/W/X)，只更新在线/chipid/VER。 */
void FeederManager::_onGlobalReply(const String& reply) {
    int pos = 0;
    bool scanBeepNew = false;   /* 本轮扫描新增未配址设备 -> 蜂鸣一次(防多台连续响) */
    while (pos < (int)reply.length()) {
        int s = reply.indexOf("ok ", pos);
        if (s < 0) break;
        int e = reply.indexOf("ok ", s + 3);
        if (e < 0) e = (int)reply.length();
        String one = reply.substring(s, e);

        FeederCfg cfg;
        if (mcodeParseConfig(one, cfg)) {
            if (cfg.addr < FEEDER_ADDR_MIN || cfg.addr > FEEDER_ADDR_MAX) {
                /* 未初始化/非法地址(如出厂默认 addr=99): 记入"临时飞达"栏(按 ChipID 去重) */
                if (cfg.hasChip) {
                    _uninitChip = cfg.chipid;
                    bool isNew = (tempIdxForChip(cfg.chipid) < 0);
                    _tempAdd(cfg.chipid, 99, one);   /* addr=99 未配置 */
                    if (isNew) { _uninitCount++; scanBeepNew = true; }
                }
                pos = s + 3;
                continue;
            }
            FeederState& f = _feeders[cfg.addr];   // 地址即槽位(0~63)
            bool conflicted = false;
            if (cfg.hasChip) {
                /* 地址冲突: 本次扫描内该地址已有别的 ChipID, 或槽位本就处于冲突态。
                 * 冲突的 ALL 设备都进临时表(先应答者 + 本次应答者), 槽位标记冲突红卡;
                 * 这样无论下次广播谁先答, 临时表都能完整保留每一台冲突设备。 */
                if (f.conflict || (f.present && f.chipid != 0 && cfg.chipid != f.chipid)) {
                    conflicted = true;
                    f.conflict = true;
                    if (f.present && f.chipid != 0 && f.chipid != cfg.chipid)
                        _tempAdd(f.chipid, (uint8_t)cfg.addr, f.lastReply);
                    _tempAdd(cfg.chipid, (uint8_t)cfg.addr, one);
                    /* 冲突地址去重记录(蜂鸣/诊断用) */
                    bool logged = false;
                    for (int k = 0; k < (int)_conflictCount; k++)
                        if (_conflictAddr[k] == (uint16_t)cfg.addr) { logged = true; break; }
                    if (!logged && _conflictCount < 32) _conflictAddr[_conflictCount++] = cfg.addr;
                }
            }
            f.present = true;
            f.lastSeenMs = millis();
            f.lastReply = one;
            if (cfg.hasChip) {
                f.chipid = cfg.chipid;
                if (!conflicted) _tempRemove(cfg.chipid);   /* 配置完成(地址合法) -> 自动归入正式槽位 */
            }
            if (cfg.ver)     f.fwVer = cfg.ver;
            if (cfg.hasEnable) { f.enabled = cfg.enabled; f.hasEnable = true; }
            f.lastErr = (cfg.err > 0) ? (uint32_t)cfg.err : 0;
        }
        pos = s + 3;
    }
    if (scanBeepNew) buzzerBeep(BEEP_FEEDER_ERR, 3);  /* 新增未配址 -> 立即提示, 不依赖 Web 轮询 */
    /* 广播结束: 本轮未应答(已拔下/掉线)的临时飞达清理, 未初始化计数同步回落 */
    for (int i = _tempCount - 1; i >= 0; i--) {
        if (!_temp[i].present && _temp[i].chipid) _tempRemove(_temp[i].chipid);
    }
    _scanning = false;
}

uint8_t FeederManager::cancelNormalQueries() {
    Job tmp[JOB_QUEUE_LEN];
    uint8_t n = 0, removed = 0;
    while (_normHead != _normTail) {
        Job j = _normQ[_normHead];
        _normHead = (_normHead + 1) % JOB_QUEUE_LEN;
        if (j.m.code == MCMD_QUERY) { removed++; continue; }
        tmp[n++] = j;
    }
    for (uint8_t i = 0; i < n; i++) _normQ[i] = tmp[i];
    _normHead = 0; _normTail = n;
    return removed;
}

bool FeederManager::_enqueue(const Job& j, bool priority) {
    uint8_t head, tail;
    Job* q;
    if (priority) { head = _prodHead; tail = _prodTail; q = _prodQ; }
    else          { head = _normHead; tail = _normTail; q = _normQ; }

    uint8_t next = (tail + 1) % JOB_QUEUE_LEN;
    if (next == head) return false;               /* 该优先级队列满 */
    q[tail] = j;
    if (priority) _prodTail = next; else _normTail = next;
    return true;
}

bool FeederManager::_nextJob(Job& out) {
    unsigned long now = millis();

    if (_prodHead != _prodTail) {
        out = _prodQ[_prodHead];
        _prodHead = (_prodHead + 1) % JOB_QUEUE_LEN;
        return true;
    }

    /* 生产锁期间普通队列暂停；手动解除后放行。 */
    if (_prodActive && !_manualReleased) return false;

    while (_normHead != _normTail) {
        Job& j = _normQ[_normHead];
        if (j.expireAt && (long)(now - j.expireAt) >= 0) {
            _normHead = (_normHead + 1) % JOB_QUEUE_LEN;
            continue;                                             /* 5s TTL */
        }
        /* 扫描期间暂停普通 M602，避免 N3 与 N255 抢总线 */
        if ((_scanning || _scanPending) && j.m.code == MCMD_QUERY) return false;
        /* 普通 M600 应答后至少隔 700ms，避免 Web 自动送料把节拍压到 1.7s */
        if (j.m.code == MCMD_ADVANCE && (long)(now - _feedGapUntil) < 0) return false;
        _normHead = (_normHead + 1) % JOB_QUEUE_LEN;
        out = j;
        return true;
    }
    return false;
}

bool FeederManager::command(const MCmdMsg& m, FeederCmdCb cb) {
    if (!busPowerIsOn()) return false;

    Job j; j.m = m; j.cb = cb; j.production = false;
    j.expireAt = millis() + 5000UL;               /* 普通命令 5s TTL */

    /* M602 同目标归一：不重复排队，保留最新。 */
    if (m.code == MCMD_QUERY) {
        uint8_t head = _normHead, tail = _normTail;
        while (head != tail) {
            Job& q = _normQ[head];
            bool same = false;
            if (m.hasP && q.m.hasP && m.chipid && q.m.chipid == m.chipid) same = true;
            if (m.hasN && q.m.hasN && q.m.addr == m.addr) same = true;
            if (same) { q = j; return true; }
            head = (head + 1) % JOB_QUEUE_LEN;
        }
    }
    return _enqueue(j, false);
}

bool FeederManager::commandPriority(const MCmdMsg& m, FeederCmdCb cb) {
    if (!busPowerIsOn()) return false;
    Job j; j.m = m; j.cb = cb; j.production = false; j.expireAt = 0;
    return _enqueue(j, true);
}

bool FeederManager::commandProduction(const MCmdMsg& m, FeederCmdCb cb) {
    if (!busPowerIsOn()) return false;
    Job j; j.m = m; j.cb = cb; j.production = true; j.expireAt = 0;

    if (m.code == MCMD_ADVANCE) {
        /* 第一条 UART1 M600 开始生产锁；同时取消手动解除，并停掉 Web 自动送料剩余 M600。 */
        _prodActive = true;
        _manualReleased = false;
        _prodLastAt = millis();
        cancelNormalFeed();
    } else if (m.code == MCMD_RETRACT) {
        /* M601 只刷新空闲计时，不作为解锁条件。 */
        if (_prodActive) _prodLastAt = millis();
    }
    return _enqueue(j, true);
}

uint8_t FeederManager::cancelNormalFeed() {
    Job tmp[JOB_QUEUE_LEN];
    uint8_t n = 0, removed = 0;
    while (_normHead != _normTail) {
        Job j = _normQ[_normHead];
        _normHead = (_normHead + 1) % JOB_QUEUE_LEN;
        if (j.m.code == MCMD_ADVANCE) { removed++; continue; }
        tmp[n++] = j;
    }
    for (uint8_t i = 0; i < n; i++) _normQ[i] = tmp[i];
    _normHead = 0; _normTail = n;
    return removed;
}

void FeederManager::releaseProduction() {
    _manualReleased = true;
    _prodActive = false;
    _prodHead = _prodTail = 0;   /* 生产队列里的过期任务不再发 */
}

void FeederManager::clearRuntime() {
    for (int i = 0; i < FEEDER_COUNT; i++) {
        _feeders[i] = FeederState();
        _feeders[i].slot = i;
        _feeders[i].addr = i;
    }
    for (int i = 0; i < TEMP_FEEDER_MAX; i++) _temp[i] = FeederState();
    _tempCount = 0;
    _uninitCount = 0;
    _conflictCount = 0;
    _scanning = false;
    _scanPending = false;
    _prodHead = _prodTail = 0;
    _normHead = _normTail = 0;
    _prodActive = false;
    _manualReleased = false;
    _prodLastAt = 0;
    _feedGapUntil = 0;
}

bool FeederManager::requestScan() {
    if (productionLocked()) return false;
    if (_scanning || _scanPending) return false;
    if (_bus->busy() ||
        (long)(millis() - _bus->lastActivityMs()) < (long)SCAN_IDLE_MS) {
        _scanPending = true;                 // 总线忙/有活动: 挂起等空闲窗口
        _scanWaitAt = millis() + SCAN_ABORT_MS;   // 超时自动取消(生产优先)
    } else {
        _pushScanAll();                      // 立即发 M602 N255
    }
    return true;
}

/* X56 上电主动上报("ok ready addr:<a> ChipID:<HHHHHH>"):
 * 直接复用 _onReply —— 更新 lastSeen/present/ChipID、冲突检测、addr=99 进临时表。 */
void FeederManager::onBusMessage(const String& line) {
    FeederCfg cfg;
    if (!mcodeParseConfig(line, cfg) || (!cfg.hasChip && !cfg.hasEnable)) return;
    LOGD("[Mgr] unsolicited addr=%d chip=%06X en=%d\n", cfg.addr, cfg.chipid, cfg.hasEnable ? (int)cfg.enabled : -1);
    if (cfg.hasChip) {
        int a = addrForChip(cfg.chipid);
        if (a >= 0) {
            _onReply((uint8_t)a, line);
            return;
        }
        /* 未配置 N99: 按 ChipID 进临时表，再更新 en。 */
        int ti = tempIdxForChip(cfg.chipid);
        if (ti < 0) { _tempAdd(cfg.chipid, 99, line); ti = tempIdxForChip(cfg.chipid); }
        if (ti >= 0 && cfg.hasEnable) {
            _temp[ti].enabled = cfg.enabled;
            _temp[ti].hasEnable = true;
            _temp[ti].lastSeenMs = millis();
            _temp[ti].present = true;
        }
        return;
    }
    if (cfg.addr >= 0) _onReply((uint8_t)cfg.addr, line);
}

bool FeederManager::_sendJob(const Job& j) {
    /* 超时按命令类型区分:
     * - M602 查询: 单点应答含 M620/A..T/VER/FW/CHIP 约 100B, 从机处理+分帧常 >80ms,
     *   统一用 300ms(与 feeder_upgrade.cpp 的板上版本查询一致); 80ms 会导致
     *   cfgA..cfgT 永远解析不到 -> 详情页状态参数全"未知"。
     * - 其它写命令: 只回 "ok ready", 80ms 足够, 保持快速失败。 */
    uint32_t tmo = BUS_CMD_TIMEOUT;
    if (j.m.code == MCMD_QUERY) tmo = BUS_QUERY_TIMEOUT;
    else if (j.m.code == MCMD_ADVANCE) tmo = 10000;   /* F24 最坏约 3.9s + 余量 */
    else if (j.m.code == MCMD_RETRACT) tmo = 5000;
    else if (j.m.code == MCMD_ISP_RESET) tmo = 300;
    String cmd = mcodeBuildMsg(j.m);

    if (j.production) _prodLastAt = millis();   /* 生产命令真正发出时刷新空闲计时 */

    if (_bus->busy() ||
        (long)(millis() - _bus->lastActivityMs()) < (long)HB_IDLE_MS) return false;
    bool ok = _bus->send(cmd, tmo, [this, j](const String& r){
        _onReply(j.m.addr, r);
        if (j.m.code == MCMD_ADVANCE) _feedGapUntil = millis() + 700UL;
        if (j.cb) j.cb(j.m.addr, r);
    });
    // M600 进料: 每次命令(F2/F4/F8... 无论舵机动作几次)计一次, 按 ChipID 绑定
    if (ok && j.m.code == MCMD_ADVANCE) {
        if (!j.m.explicitBroadcast) {
            /* Web 统一按 P<chipid>，N99 的 addr=99 不能用来索引 _feeders[64]。
             * 优先用命令携带的 ChipID；只有 N 命令才回落到地址槽位。 */
            uint32_t cid = 0;
            if (j.m.hasP && j.m.chipid) cid = j.m.chipid;
            else if (j.m.hasN && j.m.addr < FEEDER_COUNT) cid = _feeders[j.m.addr].chipid;
            if (cid) _countFeed(cid);
        } else {
            for (int i = 0; i < FEEDER_COUNT; i++)
                if (_feeders[i].chipid) _countFeed(_feeders[i].chipid);
        }
        _feedDirty = true;
    }
    return ok;
}

void FeederManager::_onReply(uint8_t addr, const String& reply) {
    /* 临时飞达(P<chipid> 定向命令 / X56 ready 主动上报): 应答带 addr:99 -> 更新临时飞达表 */
    FeederCfg tcfg;
    if (mcodeParseConfig(reply, tcfg) && tcfg.hasChip &&
        (tcfg.addr < FEEDER_ADDR_MIN || tcfg.addr > FEEDER_ADDR_MAX)) {
        bool isNew = (tempIdxForChip(tcfg.chipid) < 0);
        _tempAdd(tcfg.chipid, 99, reply);
        if (isNew) { _uninitCount++; _uninitChip = tcfg.chipid; buzzerBeep(BEEP_FEEDER_ERR, 3); } /* 未配址新增 -> 立即蜂鸣(不依赖 Web 轮询) */
        return;
    }
    /* 有些应答(尤其升级后/残留缓存)命令行地址与应答里的 N/addr 不一致；
     * 以设备自报地址为准，避免把 addr1 的数据写进 addr0。 */
    FeederCfg pre;
    if (mcodeParseConfig(reply, pre) && pre.hasChip &&
        pre.addr >= FEEDER_ADDR_MIN && pre.addr <= FEEDER_ADDR_MAX)
        addr = (uint8_t)pre.addr;
    if (addr < FEEDER_ADDR_MIN || addr > FEEDER_ADDR_MAX) return;
    FeederState& f = _feeders[addr];      // 地址 0~63 即槽位索引(注意: 不是 addr-1!)
    f.lastReply = reply;

    if (reply.length() == 0) {   // 超时无回复: 不刷新 lastSeen, present 由离线超时判定
        f.lastErr = 1;
        return;
    }

    f.lastSeenMs = millis();
    f.present = true;            // 有应答(ok 或 error)即在线

    int rt = mcodeReplyType(reply);
    if (rt == 1) {               // "error ..." 应答：在线但状态未知
        f.lastErr = 1;
        return;
    }
    if (rt != 0) return;         // 无法识别的应答，保持现状

    // "ok ..." 应答：先按 M602 配置格式解析(新协议)
    FeederCfg cfg;
    if (mcodeParseConfig(reply, cfg)) {
        /* 只有带配置字段的应答才推进 cfgSeq; 全局扫描/错误应答不含 A..T,
         * 若也 +1 会让前端误以为"这次查询已回"而提前返回旧值。 */
        bool hasCfg = (cfg.a >= 0 || cfg.b >= 0 || cfg.c >= 0 || cfg.f >= 0 ||
                       cfg.u >= 0 || cfg.v >= 0 || cfg.w >= 0 || cfg.x >= 0 || cfg.t >= 0);
        if (hasCfg) f.cfgSeq++;
        f.lastErr = (cfg.err > 0) ? (uint32_t)cfg.err : 0;   // 记录错误码, 不累加(避免详情查询后返回总览误鸣)
        bool conflicted = false;
        if (cfg.hasChip) {
            /* 地址冲突检测: 槽位已有别的 ChipID, 或槽位本就处于冲突态。
             * 冲突 ALL 设备进临时表 + 槽位标记冲突, 任何冲突态应答都不 promote。 */
            if (f.conflict || (f.chipid != 0 && cfg.chipid != f.chipid)) {
                conflicted = true;
                f.conflict = true;
                if (f.chipid != 0 && f.chipid != cfg.chipid)
                    _tempAdd(f.chipid, addr, f.lastReply);
                _tempAdd(cfg.chipid, addr, reply);
                bool logged = false;
                for (int k = 0; k < (int)_conflictCount; k++)
                    if (_conflictAddr[k] == (uint16_t)addr) { logged = true; break; }
                if (!logged && _conflictCount < 32) _conflictAddr[_conflictCount++] = addr;
            }
            f.chipid = cfg.chipid;
            /* 同一 ChipID 只能占一个正式槽位：升级/改址后旧槽位可能残留，
             * 这里清掉其它槽位，避免总览出现 addr0/addr1 两台相同 ChipID。 */
            for (int i = 0; i < FEEDER_COUNT; i++) {
                if (i == (int)addr) continue;
                if (_feeders[i].chipid == cfg.chipid) {
                    _feeders[i].chipid = 0;
                    _feeders[i].present = false;
                    _feeders[i].conflict = false;
                    _feeders[i].hasEnable = false;
                    _feeders[i].enabled = false;
                }
            }
            if (!conflicted) _tempRemove(cfg.chipid);   /* 配置完成(地址合法) -> 自动归入正式槽位 */
        }
        if (cfg.ver)     f.fwVer = cfg.ver;
        if (cfg.hasEnable) { f.enabled = cfg.enabled; f.hasEnable = true; }
        if (cfg.a >= 0) f.cfgA = cfg.a;
        if (cfg.b >= 0) f.cfgB = cfg.b;
        if (cfg.c >= 0) f.cfgC = cfg.c;
        if (cfg.f >= 0) f.cfgF = cfg.f;
        if (cfg.u >= 0) f.cfgU = cfg.u;
        if (cfg.v >= 0) f.cfgV = cfg.v;
        if (cfg.w >= 0) f.cfgW = cfg.w;
        if (cfg.x >= 0) f.cfgX = cfg.x;
        if (cfg.t >= 0) f.cfgT = cfg.t;          // T N20 超时参数
        if (cfg.fw > 0) f.fw = (uint32_t)cfg.fw; // FW 下位机固件版本(2610.2001 -> 26102001)
    } else {
        // 兼容旧格式: "I12345678 V100" 与 "E1 B0 S120 P8"
        uint32_t cid; uint16_t ver;
        if (mcodeParseInfo(reply, cid, ver)) {
            if (cid) f.chipid = cid;
            if (ver) f.fwVer = ver;
        }
        bool enabled = false, busy = false; int sp = -1, pt = -1;
        if (mcodeParseStatus(reply, enabled, busy, sp, pt)) {
            f.enabled = enabled;
            f.busy = busy;
            if (sp >= 0) f.speed = sp;
            if (pt >= 0) f.pitch = pt;
        }
    }
}

uint32_t FeederManager::onlineCount() const {
    uint32_t n = 0;
    unsigned long now = millis();
    /* 在线窗口必须覆盖"心跳周期 + 余量": 心跳默认 30s, 而固定 6s 会把刚查过又等
     * 下次心跳的飞达误判离线, 造成 online 数周期性下降 -> 蜂鸣器每隔一段响 2 声。 */
    unsigned long off = FEEDER_OFFLINE_MS;
    unsigned long hb = _hbPeriodMs ? _hbPeriodMs : HEARTBEAT_DEFAULT_MS;
    if (hb + 10000UL > off) off = hb + 10000UL;
    for (int i = 0; i < FEEDER_COUNT; i++) {
        if (_feeders[i].present &&
            (long)(now - _feeders[i].lastSeenMs) < (long)off) n++;
    }
    return n;
}

int FeederManager::addrForChip(uint32_t chipid) const {
    if (!chipid) return -1;
    int best = -1, fallback = -1;
    unsigned long bestSeen = 0, fallbackSeen = 0;
    for (int i = 0; i < FEEDER_COUNT; i++) {
        if (_feeders[i].chipid != chipid) continue;
        /* 同一 ChipID 可能因升级/改址残留到旧槽位：在多个槽位中选最近应答的，
         * 不能简单取最小下标，否则详情页会按 addr0 去查，把残留槽位重新标在线。 */
        if (_feeders[i].present) {
            if (best < 0 || (long)(_feeders[i].lastSeenMs - bestSeen) > 0) {
                best = i; bestSeen = _feeders[i].lastSeenMs;
            }
        } else if (fallback < 0 || (long)(_feeders[i].lastSeenMs - fallbackSeen) > 0) {
            fallback = i; fallbackSeen = _feeders[i].lastSeenMs;
        }
    }
    return (best >= 0) ? best : fallback;
}

int FeederManager::tempIdxForChip(uint32_t chipid) const {
    if (!chipid) return -1;
    for (int i = 0; i < _tempCount; i++)
        if (_temp[i].chipid == chipid) return i;
    return -1;
}

/* IAP 失败/断电: 立即失效该 ChipID 的在线缓存，避免总览继续显示旧状态。 */
void FeederManager::invalidateChip(uint32_t chipid) {
    if (!chipid) return;
    for (int i = 0; i < FEEDER_COUNT; i++) {
        if (_feeders[i].chipid == chipid) {
            _feeders[i].present = false;
            _feeders[i].boot = false;
            _feeders[i].lastReply = "";
        }
    }
    int ti = tempIdxForChip(chipid);
    if (ti >= 0) {
        _temp[ti].present = false;
        _temp[ti].boot = false;
        _temp[ti].lastReply = "";
    }
}

/* M620 R 改址成功回调: 把 chipid 的状态迁到新地址。
 * fromAddr=-1 表示目标在临时表(addr=99); 迁移后从临时表移除。
 * 不清旧槽位会导致 addrForChip 仍返回旧地址 -> 后续 M602 N<旧> 超时。 */
void FeederManager::moveAddress(uint32_t chipid, int fromAddr, uint8_t toAddr) {
    if (!chipid || toAddr >= FEEDER_COUNT) return;
    FeederState src;
    bool have = false;
    if (fromAddr >= 0 && fromAddr < FEEDER_COUNT &&
        _feeders[fromAddr].chipid == chipid) {
        src = _feeders[fromAddr];
        have = true;
    } else {
        int ti = tempIdxForChip(chipid);
        if (ti >= 0) { src = _temp[ti]; have = true; }
    }
    FeederState& dst = _feeders[toAddr];
    if (have) {
        dst = src;                         /* 带过去 cfg / fw / enabled / cfgSeq */
    } else {
        if (dst.chipid == chipid && dst.addr == toAddr) {  /* 已迁移(立即迁移+回调重复调用) */
            if (fromAddr >= 0 && fromAddr < FEEDER_COUNT && fromAddr != (int)toAddr &&
                _feeders[fromAddr].chipid == 0) {
                _feeders[fromAddr] = FeederState();
                _feeders[fromAddr].slot = (uint8_t)fromAddr;
                _feeders[fromAddr].addr = (uint8_t)fromAddr;
            }
            return;
        }
        dst = FeederState();               /* 只有 chipid 映射, 等下次 M602 填参数 */
        dst.chipid = chipid;
    }
    dst.slot = toAddr;
    dst.addr = toAddr;
    dst.present = true;
    dst.conflict = false;
    dst.lastSeenMs = millis();
    if (fromAddr >= 0 && fromAddr < FEEDER_COUNT && fromAddr != (int)toAddr) {
        _feeders[fromAddr] = FeederState();
        _feeders[fromAddr].slot = (uint8_t)fromAddr;
        _feeders[fromAddr].addr = (uint8_t)fromAddr;
    }
    int ti = tempIdxForChip(chipid);
    if (ti >= 0) _tempRemove(chipid);      /* 临时飞达改址后归入正式槽位 */
    _uninitCount = 0;
    for (int i = 0; i < _tempCount; i++)
        if (_temp[i].present) _uninitCount++;
}

/* M799 恢复出厂: 设备地址回到 99。把旧槽位的 chipid/参数迁到临时表并清空旧槽位,
 * 否则 addrForChip 仍返回旧地址, 详情页会继续 M602 N<旧>, 永远查不到 99 设备。 */
void FeederManager::moveToTemp(uint32_t chipid, int fromAddr) {
    if (!chipid) return;
    FeederState src;
    bool have = false;
    if (fromAddr >= 0 && fromAddr < FEEDER_COUNT &&
        _feeders[fromAddr].chipid == chipid) {
        src = _feeders[fromAddr];
        have = true;
    } else {
        int ti0 = tempIdxForChip(chipid);
        if (ti0 >= 0 && _temp[ti0].addr == 99) {
            /* 已迁移: 回调时清掉旧槽位因 M799 "ok ready" 被重新标 present 的幽灵在线 */
            if (fromAddr >= 0 && fromAddr < FEEDER_COUNT && _feeders[fromAddr].chipid == 0) {
                _feeders[fromAddr] = FeederState();
                _feeders[fromAddr].slot = (uint8_t)fromAddr;
                _feeders[fromAddr].addr = (uint8_t)fromAddr;
            }
            return;
        }
        if (ti0 >= 0) { src = _temp[ti0]; have = true; }
    }
    int ti = tempIdxForChip(chipid);
    if (ti < 0) {
        if (_tempCount >= TEMP_FEEDER_MAX) return;
        ti = _tempCount++;
    }
    FeederState& tf = _temp[ti];
    if (have) tf = src;
    else tf = FeederState();
    tf.chipid = chipid;
    tf.slot = 99;
    tf.addr = 99;
    tf.present = true;
    tf.conflict = false;
    tf.lastSeenMs = millis();
    if (fromAddr >= 0 && fromAddr < FEEDER_COUNT) {
        _feeders[fromAddr] = FeederState();
        _feeders[fromAddr].slot = (uint8_t)fromAddr;
        _feeders[fromAddr].addr = (uint8_t)fromAddr;
    }
    _uninitCount = 0;
    for (int i = 0; i < _tempCount; i++)
        if (_temp[i].present) _uninitCount++;
}

/* M610 回 ok 后同步使能缓存: addr 0~63 直接更新; addr=99/无效时按 chipid 找临时表;
 * 都没有(chipid=0)视为广播, 更新所有正式槽位和临时飞达。 */
void FeederManager::setEnabled(int addr, uint32_t chipid, bool en) {
    if (addr >= 0 && addr < FEEDER_COUNT) {
        _feeders[addr].enabled = en;
        _feeders[addr].hasEnable = true;
        return;
    }
    if (chipid) {
        int a = addrForChip(chipid);
        if (a >= 0) { _feeders[a].enabled = en; _feeders[a].hasEnable = true; return; }
        int ti = tempIdxForChip(chipid);
        if (ti >= 0) { _temp[ti].enabled = en; _temp[ti].hasEnable = true; }
        return;
    }
    for (int i = 0; i < FEEDER_COUNT; i++) {
        if (_feeders[i].present) { _feeders[i].enabled = en; _feeders[i].hasEnable = true; }
    }
    for (int i = 0; i < _tempCount; i++) {
        if (_temp[i].present) { _temp[i].enabled = en; _temp[i].hasEnable = true; }
    }
}

/* 加入/更新临时飞达(按 ChipID 去重; addr=99 未配置 或 冲突地址) */
void FeederManager::_tempAdd(uint32_t chipid, uint8_t addr, const String& reply) {
    if (!chipid) return;
    int ti = tempIdxForChip(chipid);
    if (ti < 0) {
        if (_tempCount >= TEMP_FEEDER_MAX) return;   /* 表满: 仅统计, 不显示 */
        ti = _tempCount++;
    }
    FeederState& tf = _temp[ti];
    FeederCfg cfg;
    if (mcodeParseConfig(reply, cfg)) {
        tf.addr = (cfg.addr >= 0) ? (uint8_t)cfg.addr : addr;   /* ok boot 无 addr 时保留 99 */
        if (cfg.hasEnable) { tf.enabled = cfg.enabled; tf.hasEnable = true; }
        if (cfg.a >= 0) tf.cfgA = cfg.a;
        if (cfg.b >= 0) tf.cfgB = cfg.b;
        if (cfg.c >= 0) tf.cfgC = cfg.c;
        if (cfg.f >= 0) tf.cfgF = cfg.f;
        if (cfg.u >= 0) tf.cfgU = cfg.u;
        if (cfg.v >= 0) tf.cfgV = cfg.v;
        if (cfg.w >= 0) tf.cfgW = cfg.w;
        if (cfg.x >= 0) tf.cfgX = cfg.x;
        if (cfg.t >= 0) tf.cfgT = cfg.t;
        if (cfg.fw > 0) tf.fw = (uint32_t)cfg.fw;
        if (cfg.ver) tf.fwVer = cfg.ver;
        /* 只有带配置字段的应答才推进 seq(全局扫描不含 A..T, 不能算一次配置更新) */
        if (cfg.a >= 0 || cfg.b >= 0 || cfg.c >= 0 || cfg.f >= 0 ||
            cfg.u >= 0 || cfg.v >= 0 || cfg.w >= 0 || cfg.x >= 0 || cfg.t >= 0) tf.cfgSeq++;
    } else {
        tf.addr = addr;
    }
    tf.slot = 99;                     /* 临时飞达标记 */
    tf.boot = cfg.boot;               /* ok boot ChipID: 升级中断电 */
    tf.chipid = chipid;
    tf.present = true;
    tf.lastSeenMs = millis();
    tf.lastReply = reply;
}

/* 配置完成(地址合法)后从临时表移除(自动归入正式槽位) */
void FeederManager::_tempRemove(uint32_t chipid) {
    int ti = tempIdxForChip(chipid);
    if (ti < 0) return;
    if (_temp[ti].addr == 99 && _uninitCount > 0) _uninitCount--;  /* 配址完成, 未初始化计数回落 */
    for (int i = ti; i < _tempCount - 1; i++) _temp[i] = _temp[i + 1];
    _tempCount--;
    _temp[_tempCount] = FeederState();   /* 清尾 */
}

/* ---------------- 推料计数(ChipID 绑定) ---------------- */
void FeederManager::_countFeed(uint32_t chipid) {
    for (int i = 0; i < FEEDER_COUNT; i++) {
        if (_feedStats[i].chipid == chipid) { _feedStats[i].count++; return; }
        if (_feedStats[i].chipid == 0)     { _feedStats[i].chipid = chipid; _feedStats[i].count = 1; return; }
    }
}

uint32_t FeederManager::feedCountAt(uint8_t slot) const {
    return feedCountFor(_feeders[slot].chipid);   // 经该地址当前 ChipID 关联
}

uint32_t FeederManager::feedCountFor(uint32_t chipid) const {
    if (!chipid) return 0;
    for (int i = 0; i < FEEDER_COUNT; i++)
        if (_feedStats[i].chipid == chipid) return _feedStats[i].count;
    return 0;
}

uint64_t FeederManager::feedTotal() const {
    uint64_t t = 0;
    for (int i = 0; i < FEEDER_COUNT; i++) t += _feedStats[i].count;
    return t;
}

void FeederManager::clearFeed() {
    for (int i = 0; i < FEEDER_COUNT; i++) _feedStats[i] = FeedStat();
    _feedDirty = true;
    saveFeedCount();
}

void FeederManager::clearFeed(uint32_t chipid) {
    for (int i = 0; i < FEEDER_COUNT; i++)
        if (_feedStats[i].chipid == chipid) { _feedStats[i] = FeedStat(); break; }
    _feedDirty = true;
    saveFeedCount();
}

/* 推料计数存 NVS，不存 LittleFS：升级页面 FS 会整块替换 LittleFS，
 * 放 /feedcnt.bin 会被一起清掉。NVS 分区不受 FS OTA 影响。 */
void FeederManager::saveFeedCount() {
    Preferences p;
    if (!p.begin("feedcnt", false)) return;
    p.putBytes("data", _feedStats, sizeof(_feedStats));
    p.end();
    _feedDirty = false;
}

void FeederManager::loadFeedCount() {
    Preferences p;
    if (p.begin("feedcnt", true)) {
        size_t len = p.getBytesLength("data");
        if (len == sizeof(_feedStats)) {
            p.getBytes("data", _feedStats, len);
            p.end();
            return;
        }
        p.end();
    }

    /* 一次性迁移旧 LittleFS /feedcnt.bin，然后删除旧文件 */
    File f = LittleFS.open("/feedcnt.bin", "r");
    if (!f) return;                    // 首次启动无文件 -> 全 0
    bool got = false;
    for (int i = 0; i < FEEDER_COUNT && f.available() >= 8; i++) {
        f.readBytes((char*)&_feedStats[i].chipid, sizeof(uint32_t));
        f.readBytes((char*)&_feedStats[i].count,  sizeof(uint32_t));
        got = true;
    }
    f.close();
    if (got) {
        saveFeedCount();
        LittleFS.remove("/feedcnt.bin");
    }
}






