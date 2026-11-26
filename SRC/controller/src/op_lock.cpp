#include "op_lock.h"

static OpOwner  s_owner[64];
static uint32_t s_at[64];

void opLockInit() {
    for (int i = 0; i < 64; i++) { s_owner[i] = OP_FREE; s_at[i] = 0; }
}

OpOwner opLockOwner(uint8_t addr) {
    if (addr >= 64) return OP_FREE;
    if (s_owner[addr] == OP_FREE) return OP_FREE;
    if ((long)(millis() - s_at[addr]) >= (long)OP_LOCK_MS) {
        s_owner[addr] = OP_FREE;      /* 窗口到期, 自动释放 */
        return OP_FREE;
    }
    return s_owner[addr];
}

bool opLockTake(uint8_t addr, OpOwner owner) {
    if (addr >= 64) return true;      /* 非单台操作(广播等)不加锁 */
    OpOwner cur = opLockOwner(addr);
    if (cur != OP_FREE && cur != owner) return false;
    s_owner[addr] = owner;
    s_at[addr] = millis();
    return true;
}

void opLockRelease(uint8_t addr, OpOwner owner) {
    if (addr >= 64) return;
    if (s_owner[addr] == owner) s_owner[addr] = OP_FREE;
}

const char* opOwnerName(OpOwner o) {
    switch (o) {
    case OP_SCREEN: return "屏幕";
    case OP_WEB:    return "网页";
    case OP_HOST:   return "主机";
    default:        return "";
    }
}
