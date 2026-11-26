#include "buzzer.h"
#include "config.h"
#include <Preferences.h>

#if BUZZER_PIN >= 0

static bool g_on = true;
static int  g_vol = 50;

/* 当前蜂鸣状态机 */
static BeepKind      g_kind = BEEP_NONE;
static int           g_count = 0;      /* 剩余鸣叫次数 */
static int           g_freq = 0;
static int           g_onMs = 0, g_offMs = 0;
static bool          g_phase = false;  /* true=正在响 */
static unsigned long g_tick = 0;

static Preferences g_pref;

/* 音调表: 各事件 -> 频率/时长(次数由调用方传入) */
static void _toneOf(BeepKind k, int &freq, int &onMs, int &offMs) {
    switch (k) {
        case BEEP_OK:          freq = 1200; onMs =  80; offMs = 120; break;
        case BEEP_CMD_ERR:     freq =  700; onMs = 120; offMs = 120; break;
        case BEEP_FEEDER_ERR:  freq =  880; onMs = 150; offMs = 200; break;
        case BEEP_BUS_OFFLINE: freq =  660; onMs = 400; offMs = 250; break;
        case BEEP_OTA_FAIL:    freq = 1320; onMs =  70; offMs =  90; break;
        case BEEP_OTA_OK:      freq = 1000; onMs = 100; offMs = 100; break;
        case BEEP_POWER_FAULT: freq = 1000; onMs = 200; offMs = 150; break;
        case BEEP_POWER_LOCK:  freq =  500; onMs = 300; offMs = 200; break;
        default:               freq = 1000; onMs = 100; offMs = 150; break;
    }
}

static void _startTone() {
    /* 引脚已在 init 常驻 attach(只打一次日志), 响时只改频率+占空比, 不再重复 attach/detach
     * (旧版每次蜂鸣都 attach -> esp32-hal-ledc 每次都打印 ledcAttachChannel 日志刷屏)。
     * 无源蜂鸣器: 音量=方波摆幅, 占空比 50%(127) 最响, 100%(255) 恒高无振荡不响.
     * 故音量映射到 0~50% 占空比区间(255*vol/100/2). */
    ledcChangeFrequency((uint8_t)BUZZER_PIN, (uint32_t)g_freq, 8);
    ledcWrite((uint8_t)BUZZER_PIN, (uint32_t)(255UL * g_vol / 100 / 2));
}

static void _stopTone() {
    ledcWrite((uint8_t)BUZZER_PIN, 0);
}

void buzzerInit() {
    g_pref.begin("buzzer", false);
    g_on  = g_pref.getBool("on",  BUZZER_DEF_ON);
    g_vol = g_pref.getUChar("vol", BUZZER_DEF_VOL);
    if (g_vol < 0)  g_vol = 0;
    if (g_vol > 100) g_vol = 100;
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    ledcAttach((uint8_t)BUZZER_PIN, 1000, 8);   /* 常驻 attach, 仅初始化打印一次 */
}

void buzzerPrefsEnd() {          /* 恢复出厂: 关闭常驻 NVS handle */
    g_pref.end();
}

void buzzerLoop() {
    if (!g_count || !g_on) return;
    unsigned long now = millis();
    if (g_phase && (now - g_tick) >= (unsigned long)g_onMs) {
        _stopTone();
        g_phase = false; g_tick = now;
    } else if (!g_phase && (now - g_tick) >= (unsigned long)g_offMs) {
        g_count--;
        if (g_count > 0) {
            g_phase = true; g_tick = now;
            _startTone();
        }
    }
}

void buzzerBeep(BeepKind k, int count) {
    if (!g_on || k == BEEP_NONE || BUZZER_PIN < 0) return;
    if (count <= 0) count = 1;
    if (count > 9)  count = 9;
    _toneOf(k, g_freq, g_onMs, g_offMs);
    g_kind = k; g_count = count;
    g_phase = true; g_tick = millis();
    _startTone();
}

bool buzzerIsOn() { return g_on; }

void buzzerSetOn(bool on) {
    if (on == g_on) return;
    g_on = on;
    g_pref.putBool("on", g_on);
    if (!g_on) { g_count = 0; _stopTone(); }
}

int buzzerVol() { return g_vol; }

void buzzerSetVol(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    g_vol = vol;
    g_pref.putUChar("vol", (uint8_t)g_vol);
    /* 若正在响, 立即应用新音量 */
    if (g_phase && g_count) _startTone();
}

#else  /* BUZZER_PIN < 0: 禁用 */

void buzzerInit() {}
void buzzerLoop() {}
void buzzerBeep(BeepKind k, int count) { (void)k; (void)count; }
bool buzzerIsOn() { return false; }
void buzzerSetOn(bool on) { (void)on; }
int  buzzerVol() { return 0; }
void buzzerSetVol(int vol) { (void)vol; }

#endif
