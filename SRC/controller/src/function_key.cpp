#include "function_key.h"
#include "config.h"
#include "bus_power.h"
#include "buzzer.h"
#include "evlog.h"
#include "calib.h"
#include "ota_manager.h"
#include "feeder_upgrade.h"
#include "feeder_manager.h"
#include "feeder_bus.h"

#if FUNCTION_KEY_PIN >= 0

extern FeederManager g_mgr;
extern FeederBus     g_bus;

namespace {
bool     g_down = false;
bool     g_long = false;
bool     g_pending = false;
bool     g_second = false;
uint32_t g_downAt = 0;
uint32_t g_pendingAt = 0;

bool _allowed() {
    if (g_mgr.productionLocked()) return false;
    if (calibActive()) return false;
    if (otaBusy()) return false;
    if (fupBusy()) return false;
    if (g_bus.busy()) return false;
    return true;
}

void _beepOk()  { buzzerBeep(BEEP_OK, 1); }
void _beepErr() { buzzerBeep(BEEP_CMD_ERR, 1); }

void _shortAction() {
    if (!_allowed()) {
        _beepErr();
        evlogAdd("KEY", "bus toggle blocked (busy/production/OTA/calib)");
        return;
    }
    bool on = !busPowerIsOn();
    if (!busPowerSet(on)) {
        _beepErr();
        evlogAdd("KEY", "bus power %s failed", on ? "ON" : "OFF");
        return;
    }
    _beepOk();
    evlogAdd("KEY", "bus power %s", on ? "ON" : "OFF");
    Serial.printf("[KEY] bus power -> %s\n", on ? "ON" : "OFF");
}

void _doubleAction() {
    if (!_allowed()) {
        _beepErr();
        evlogAdd("KEY", "rescan blocked (busy/production/OTA/calib)");
        return;
    }
    if (!g_mgr.requestScan()) {
        _beepErr();
        evlogAdd("KEY", "rescan rejected");
        return;
    }
    _beepOk();
    evlogAdd("KEY", "rescan requested");
    Serial.printf("[KEY] rescan requested\n");
}
} // namespace

void functionKeyInit() {
    pinMode(FUNCTION_KEY_PIN, FUNCTION_KEY_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
    Serial.printf("[KEY] function key GPIO%d, short=bus power, double=scan\n", FUNCTION_KEY_PIN);
}

void functionKeyLoop() {
    bool low = (digitalRead(FUNCTION_KEY_PIN) == LOW);
    uint32_t now = millis();

    if (low && !g_down) {
        g_down = true;
        g_downAt = now;
        g_long = false;
        /* 第二次按下: 取消待执行的短按，改为双击 */
        if (g_pending) {
            g_pending = false;
            g_second = true;
        }
    }

    if (low && g_down && !g_long &&
        (uint32_t)(now - g_downAt) > FUNCTION_KEY_SHORT_MAX_MS) {
        g_long = true;          /* 长按: 不动作，避免误触 */
    }

    if (!low && g_down) {
        g_down = false;
        uint32_t dur = now - g_downAt;

        if (!g_long && dur >= FUNCTION_KEY_DEBOUNCE_MS &&
            dur <= FUNCTION_KEY_SHORT_MAX_MS) {
            if (g_second) {
                g_second = false;
                _doubleAction();
            } else {
                g_pending = true;
                g_pendingAt = now;
            }
        } else {
            g_second = false;
        }
    }

    /* 短按延时执行，给第二次点击留出双击窗口 */
    if (g_pending &&
        (uint32_t)(now - g_pendingAt) >= FUNCTION_KEY_DOUBLE_MS) {
        g_pending = false;
        _shortAction();
    }
}

#else

void functionKeyInit() {}
void functionKeyLoop() {}

#endif
