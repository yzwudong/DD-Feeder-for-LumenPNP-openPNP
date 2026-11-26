#pragma once
#include <Arduino.h>
#include <functional>
#include "config.h"
#include "feeder_bus.h"
#include "mcode.h"

/* 单个飞达的运行状态（对应 485 上一个地址，1..64） */
struct FeederState {
    uint8_t  slot   = 0;        /* 0..63: row=slot/32, col=slot%32 */
    uint8_t  addr   = 0;
    bool     present = false;   /* 近期有回复 = 在线 */
    bool     conflict = false;  /* 该地址存在多台不同 ChipID(地址冲突, 需人工处理) */
    bool     boot    = false;   /* BL 广播 ok boot ChipID: 升级中断电, 等待重刷 */
    bool     enabled = false;   /* feederEnabled:1/0 使能状态(全局/单点应答) */
    bool     hasEnable = false; /* 是否收到过 enable 字段(未收到视为未知) */
    bool     busy    = false;   /* M602 应答无忙字段，新协议下保持默认 */
    uint32_t chipid  = 0;       /* M602 CHIP: 6位十六进制 */
    uint16_t fwVer   = 0;       /* M602 VER: 配置版本(config_ver) */
    /* M602 单点应答回显的配置参数 (-1=未知) */
    int cfgA = -1, cfgB = -1, cfgC = -1, cfgF = -1;   /* A全进料 B半进料 C回退 F进料长度 */
    int cfgU = -1, cfgV = -1, cfgW = -1, cfgX = -1;   /* U稳定 V最小脉宽 W最大脉宽 X忽略反馈 */
    int cfgT = -1;              /* T N20 超时参数(仅 HAS_FEEDBACKLINES, 0~5000, 默认800) */
    uint32_t cfgSeq = 0;        /* 每次收到定向 M602 配置应答 +1: 前端据此判断"这次查询已回" */
    uint32_t fw = 0;            /* FW 下位机固件版本 major*10000+minor (2610.2001 -> 26102001), 升级判断用 */
    int      speed   = -1;      /* 旧协议字段，新协议无数据源 */
    int      pitch   = -1;
    uint32_t lastSeenMs = 0;
    uint32_t lastErr   = 0;
    String   lastReply;
};

/* 手动命令的回调: (addr, reply) */
using FeederCmdCb = std::function<void(uint8_t addr, const String& reply)>;

/* ============================================================
 * 飞达阵列管理器：持有 64 个飞达状态，
 *   - 后台按 POLL_INTERVAL 全量轮询(M602)，错峰逐地址
 *   - 提供手动命令队列(web 按钮触发)，优先于轮询
 * 命令寻址: N<id> 定向 / 无N隐式广播 / N255 显式广播 / P<chipid> 按ID
 * ============================================================ */
class FeederManager {
public:
    void begin(FeederBus* bus);
    void loop();                       // 驱动调度，高频调用

    FeederState& at(uint8_t slot) { return _feeders[slot]; }
    const FeederState* state(uint8_t slot) const { return &_feeders[slot]; }
    uint16_t count() const { return FEEDER_COUNT; }

    /* 下发手动命令(定向或广播)。bus 忙时进入普通队列，稍后发送。 */
    bool command(const MCmdMsg& m, FeederCmdCb cb = nullptr);
    /* UART1 生产命令(M600/M601)：最高优先级，生产锁期间不被普通命令打断。 */
    bool commandProduction(const MCmdMsg& m, FeederCmdCb cb = nullptr);
    /* 生产准备(M610 S1)：高优先级，但不启动生产锁。 */
    bool commandPriority(const MCmdMsg& m, FeederCmdCb cb = nullptr);
    bool productionLocked() const { return _prodActive && !_manualReleased; }
    /* 停止 Web 自动送料: 清掉普通队列中尚未发出的 M600，不打断已在总线上的 M600。 */
    uint8_t cancelNormalFeed();
    bool manualReleased() const { return _manualReleased; }
    void releaseProduction();          /* 手动解除，保持到下一条 UART1 M600 */
    unsigned long productionIdleMs() const { return _prodLastAt; }

    /* 请求立即全量扫描一次(返回 true 表示已排队) */
    bool requestScan();
    void clearRuntime();                /* 总线断电/上电: 清在线表/en/临时表 */

    /* X56 上电主动上报("ok ready addr:.. ChipID:..")/总线自发消息:
     * 复用回复处理(在线表/冲突检测/临时表), 新飞达插电秒级可见 */
    void onBusMessage(const String& line);

    /* 定向心跳轮询(P1, 保活+冲突检测): 周期可设置(M908 / NVS "hb"),
     * 只在总线空闲窗口逐台单播 M602 N<addr>, 不广播、不打扰生产 */
    uint32_t heartbeatMs() const { return _hbPeriodMs; }
    void setHeartbeatMs(uint32_t ms);   /* 写 NVS, 立即生效 */

    uint32_t onlineCount() const;
    int addrForChip(uint32_t chipid) const;     /* chipid 定位: 返回当前地址 0~63, -1 未识别 */
    /* M620 R 改址成功后: 把 chipid 的状态从旧地址/临时表迁移到新地址,
     * 否则 addrForChip 仍返回旧槽位, 详情页/查询会继续发 M602 N<旧地址>。 */
    void moveAddress(uint32_t chipid, int fromAddr, uint8_t toAddr);
    /* M799 恢复出厂后地址回 99: 把 chipid 从旧槽位迁到临时表, 否则 addrForChip 仍返回旧地址 */
    void moveToTemp(uint32_t chipid, int fromAddr);
    /* M610 回 ok 后同步缓存使能状态, 否则详情页启用后总览仍显示禁用(要等下次 M602) */
    void setEnabled(int addr, uint32_t chipid, bool en);
    /* IAP 失败/断线: 立即把该 ChipID 标记离线，避免总览继续显示旧在线状态 */
    void invalidateChip(uint32_t chipid);
    bool isScanning() const { return _scanning; }
    uint32_t lastScanAt() const { return _lastScanAt; }

    /* 未初始化(addr 越界如 99)/地址冲突 诊断信息 */
    uint16_t uninitCount() const { return _uninitCount; }
    uint32_t uninitChip() const { return _uninitChip; }
    uint16_t conflictCount() const { return _conflictCount; }
    uint16_t conflictAddr(int i) const { return _conflictAddr[i]; }

    /* 临时飞达: addr=99 未配置 或 地址冲突(相同地址多台) 的设备
     * 按 ChipID 去重保存, 最多 TEMP_FEEDER_MAX 台; 总览单独一栏显示, 可进详情页手动/校准。
     * 配置地址成功后自动归入正式槽位(收到新地址应答即 promote, 无需等下次扫描)。 */
    uint8_t  tempCount() const { return _tempCount; }
    const FeederState* tempAt(uint8_t i) const { return (i < _tempCount) ? &_temp[i] : nullptr; }
    int tempIdxForChip(uint32_t chipid) const;   /* 返回索引, -1 未识别 */

    /* 推料计数: 每条 M600 进料命令(无论 F 长度/舵机动作次数)计一次。
     * 计数与 ChipID 绑定(地址可改, ChipID 才是设备身份), 最多记 64 台。 */
    uint32_t feedCountAt(uint8_t slot) const;   /* 按地址查(经该地址当前 ChipID 关联) */
    uint32_t feedCountFor(uint32_t chipid) const; /* 按 ChipID 查 */
    uint64_t feedTotal() const;                 /* 全部之和 */
    void clearFeed();                           /* 清零全部 */
    void clearFeed(uint32_t chipid);            /* 清零指定 ChipID */
    void saveFeedCount();                       /* 落盘 LittleFS /feedcnt.bin(512B) */
    void loadFeedCount();

private:
    struct Job {
        MCmdMsg m;
        FeederCmdCb cb;
        bool production = false;      /* 来自 UART1 的 M600/M601 */
        unsigned long expireAt = 0;   /* 普通命令 TTL(0=不过期) */
    };
    static const uint8_t JOB_QUEUE_LEN = 8;

    FeederBus* _bus = nullptr;
    FeederState _feeders[FEEDER_COUNT];

    unsigned long _nextPollAt = 0;
    bool     _scanning = false;
    unsigned long _lastScanAt = 0;
    bool     _bootScanDone = false;      /* 启动广播只做一次 */
    unsigned long _bootScanAt = 0;
    bool     _scanPending = false;       /* 广播已受理, 等总线空闲窗口后发出 */
    unsigned long _scanWaitAt = 0;       /* 空闲等待截止(ms), 超时自动取消 */

    /* 定向心跳: 周期 + 轮询游标 + 连续 miss 计数(判离线) */
    uint32_t _hbPeriodMs = 0;            /* 0=由 begin 读 NVS 初始化 */
    unsigned long _hbNextAt = 0;
    uint8_t  _hbIdx = 0;                 /* 本轮查到哪个地址 */
    uint8_t  _hbMiss[FEEDER_COUNT] = {0}; /* 连续无回复次数 */

    uint16_t _uninitCount = 0;           /* 未初始化(addr 越界)台数 */
    uint32_t _uninitChip = 0;            /* 最近一台未初始化设备的 ChipID */
    uint16_t _conflictCount = 0;         /* 冲突地址数(去重) */
    uint16_t _conflictAddr[32] = {0};    /* 冲突地址列表 */

    /* 临时飞达(addr=99, 未配置/冲突): 按 ChipID 去重 */
    static const uint8_t TEMP_FEEDER_MAX = 32;
    FeederState _temp[TEMP_FEEDER_MAX] = {};
    uint8_t _tempCount = 0;
    void _tempAdd(uint32_t chipid, uint8_t addr, const String& reply); /* 加入/更新临时表 */
    void _tempRemove(uint32_t chipid);                                  /* promote 后移除 */

    /* 按 ChipID 的推料计数表(地址可改, 数据跟设备走) */
    struct FeedStat { uint32_t chipid = 0; uint32_t count = 0; };
    FeedStat _feedStats[FEEDER_COUNT] = {};
    void _countFeed(uint32_t chipid);          /* 内部: 该 ChipID 计数+1 */
    bool     _feedDirty = false;               /* 有变化待落盘 */
    unsigned long _feedSaveAt = 0;             /* 周期落盘时刻(30s) */

    Job _prodQ[JOB_QUEUE_LEN];
    Job _normQ[JOB_QUEUE_LEN];
    uint8_t _prodHead = 0, _prodTail = 0;
    uint8_t _normHead = 0, _normTail = 0;
    bool _prodActive = false;          /* UART1 M600 已开始生产 */
    bool _manualReleased = false;      /* 手动解除，保持到下一条 UART1 M600 */
    unsigned long _prodLastAt = 0;     /* 最近生产命令时刻，用于 10s 空闲释放 */
    unsigned long _feedGapUntil = 0;   /* 普通 M600 应答后的最小间隔，防止 Web 连发撞总线 */

    uint8_t cancelNormalQueries();   /* 扫描前排空普通 M602，避免 N3 抢 N255 */
    bool _enqueue(const Job& j, bool priority);
    bool _nextJob(Job& out);           /* 取最高优先级可执行任务 */
    bool _sendJob(const Job& j);       /* 真正调用 _bus->send */
    void _onReply(uint8_t addr, const String& reply);
    void _onGlobalReply(const String& reply);   /* M602 N255 多帧回复拆分 */
    void _pushScanAll();             /* 触发一次 M602 N255 快速扫描 */
};

