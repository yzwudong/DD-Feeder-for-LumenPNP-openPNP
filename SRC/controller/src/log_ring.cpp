#include "log_ring.h"
#include "time_util.h"
#include "config.h"

struct LogEntry {
    uint32_t seq;          /* 0 = 空槽 */
    uint32_t t;            /* millis() 时间戳 */
    char     dir;
    String   data;
};

static LogEntry  s_log[LOG_RING_CAP];
static uint32_t  s_next = 0;      /* 下一个写入槽位(环形) */
static uint32_t  s_seq  = 0;      /* 全局递增序号 */

static String _esc(const String& s) {
    String o;
    for (unsigned i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if ((uint8_t)c < 0x20) {
            /* JSON 字符串禁止裸控制符(485 噪声/二进制字节会让 r.json() 直接失败,
             * 前端表现为 /api/log 有响应但日志窗永远空白) */
            char b[7];
            snprintf(b, sizeof(b), "\\u%04x", (unsigned)(uint8_t)c);
            o += b;
        }
        else o += c;
    }
    return o;
}

void logPush(char dir, const String& data) {
    uint32_t idx = s_next % LOG_RING_CAP;
    s_log[idx].seq  = ++s_seq;
    s_log[idx].t    = millis();
    s_log[idx].dir  = dir;
    s_log[idx].data = data;
    s_next++;
    /* UART0 调试口镜像(总线 TX/RX + 调试行): 与 Web 终端同内容。
     * 屏幕复用 UART0 时跳过, 避免裸文本混入屏幕指令流。
     * 非阻塞: 对端(串口助手)忙/不读时丢弃, 绝不拖死 loopTask。 */
#if !(SCREEN_ENABLED && SCREEN_BACKEND == SCREEN_BACKEND_UART0)
    if (Serial.availableForWrite() >= (int)data.length() + 1) {
        Serial.print(data);
        Serial.print('\n');
    }
#endif
}

uint32_t logNextSeq() { return s_seq; }

void logClear() {
    memset(s_log, 0, sizeof(s_log));
    s_next = 0;
    s_seq = 0;
}

String logJson(uint32_t sinceSeq, uint32_t& latestSeq) {
    latestSeq = s_seq;
    String out = "[";
    bool first = true;
    uint32_t count = 0;
    const uint32_t MAX_ITEMS = 30;   /* ???? 30 ????? String ??? */
    /* 环形从最旧到最新遍历 */
    for (uint32_t k = 0; k < LOG_RING_CAP; k++) {
        const LogEntry& e = s_log[(s_next + k) % LOG_RING_CAP];
        if (e.seq == 0 || e.seq <= sinceSeq) continue;
        if (count++ >= MAX_ITEMS) break;
        latestSeq = e.seq;
        if (!first) out += ',';
        first = false;
        out += "{\"seq\":" + String(e.seq) +
               ",\"t\":" + String(e.t) +
               ",\"ts\":\"" + wallClockOrUptime() +
               "\",\"dir\":\"" + String(e.dir) +
               "\",\"d\":\"" + _esc(e.data) + "\"}";
    }
    out += "]";
    return out;
}


