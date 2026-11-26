#include <Arduino.h>
#include <LittleFS.h>
#include <nvs_flash.h>
#include "config.h"
#include "factory_reset.h"
#include "buzzer.h"
#include "log_util.h"

#if FACTORY_RESET_PIN >= 0
static bool          g_pressed    = false;
static unsigned long g_holdStart  = 0;
static bool          g_keyHealthy = false;  /* 开机必须读到过高电平; 否则按键异常(恒低) -> 本次禁用 */
#endif

/* M907 / Web 触发的"稍后执行": 先让 ok 回复发出, 再清配置重启 */
static bool          g_pending   = false;
static unsigned long g_pendingAt = 0;
static const uint32_t FACTORY_RESET_REPLY_MS = 400;  /* 给回复/响应留的发送窗口 */

void factoryResetInit() {
#if FACTORY_RESET_PIN >= 0
    pinMode(FACTORY_RESET_PIN, INPUT);
    /* 防恢复出厂死循环: 开机瞬间若该脚为低(上拉失效/按键卡死/接线错), 本次启动禁用按键。
     * 否则上电即被读成"长按" -> 清配置 -> 重启 -> 再触发 ... 无限循环恢复出厂。 */
    g_keyHealthy = (digitalRead(FACTORY_RESET_PIN) == HIGH);
    if (!g_keyHealthy) {
        LOGD("[FactoryReset] key abnormal: GPIO%d LOW at boot (check 10k pullup/key), hw key disabled\n",
             FACTORY_RESET_PIN);
    } else {
        LOGD("[FactoryReset] key GPIO%d, hold %dms for factory reset\n",
             FACTORY_RESET_PIN, (int)FACTORY_RESET_HOLD_MS);
    }
#else
    LOGD("[FactoryReset] hw key disabled (FACTORY_RESET_PIN=-1)\n");
#endif
}

void doFactoryReset() {
    LOGD("[FactoryReset] factory reset: erase NVS + rm /apmode -> reboot\n");
    buzzerBeep(BEEP_OK, 1);              /* 提示一声(尽量先发出) */
    buzzerPrefsEnd();                    /* 关闭常驻 NVS handle, 否则 nvs_flash_erase 可能失败 */
    if (LittleFS.begin()) LittleFS.remove("/apmode");  /* 清除持久 AP 标志(回出厂默认 AP 配网) */
    nvs_flash_erase();                   /* 清空全部 NVS: 密码/网络/蜂鸣器/电源/事件日志等 */
    nvs_flash_init();                    /* 重新初始化, 首次启动回写默认值 */
    delay(400);
    ESP.restart();
}

void factoryResetRequest() {
    g_pending   = true;
    g_pendingAt = millis();
}

void factoryResetLoop() {
    /* M907 / Web 延迟执行 */
    if (g_pending) {
        if ((long)(millis() - g_pendingAt) >= (long)FACTORY_RESET_REPLY_MS) {
            g_pending = false;
            doFactoryReset();            /* 不返回 */
        }
        return;
    }

#if FACTORY_RESET_PIN >= 0
    if (!g_keyHealthy) return;   /* 开机即低的异常态: 不响应按键, 防无限恢复出厂循环 */
    /* 硬件长按检测: 低电平持续 FACTORY_RESET_HOLD_MS -> 立即恢复出厂 */
    bool low = (digitalRead(FACTORY_RESET_PIN) == LOW);
    if (low) {
        if (!g_pressed) { g_pressed = true; g_holdStart = millis(); }
        else if ((long)(millis() - g_holdStart) >= (long)FACTORY_RESET_HOLD_MS) {
            doFactoryReset();            /* 不返回 */
        }
    } else {
        g_pressed = false;
    }
#endif
}
