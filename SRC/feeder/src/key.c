/**
 * @file key.c
 * @brief 独立功能按键 (P3.5)：短按进料 / 双击显示循环 / 长按连续拉带(装料)
 * @note  按键低有效（一端接 GND，按下=LOW，内部上拉）。
 *        时序：短按 <800ms；双击窗口 350ms；按住 ≥1s = 连续拉带（N20 通电，松开停）。
 *        【X46】删除长按 5s 恢复出厂：已有 M 命令(MCODE_FACTORY_RESET)可远程复位，
 *        且"拉带中 5s 停电机等确认"易误触（用户发现电机停会顺手按两下→误重置）。
 *        （反馈线为 P3.4，与此 P3.5 按键互不冲突）
 */
#include "key.h"
#include "fw_hal.h"
#include "feeder.h"
#include "gcode.h"   // extern __xdata Feeder_t feeders[]
#include "seg.h"
#include "power.h"   // Power_EnableNow / Power_DisableNow 原型（消除隐式声明警告）
#include "version.h"

extern uint16_t millis(void);

#define KEY_DEBOUNCE_MS     10       // 消抖采样节拍(ms)
#define KEY_SHORT_MAX_MS    800      // 短按最大时长(ms)，超过视为长按路径
#define KEY_DOUBLE_WIN_MS   350      // 双击判定窗口(ms)
#define KEY_PULL_HOLD_MS    1000     // 按住≥1s进入连续拉带（装料模式：N20持续通电，松开停）
#define KEY_CYCLE_SHOW_MS   2000     // 显示循环非地址项停留(ms)

typedef enum {
    KEY_IDLE = 0,       // 空闲（未按下）
    KEY_PRESSED,        // 按下中（判定短按/连续拉带）
    KEY_RELEASE_WAIT    // 松开后等待双击窗口(350ms)：窗口内按下=双击(切显示)，
                        //   超时=单次短按(进料)——双击不再误推料，代价=短按进料延迟350ms
} KeyState_t;

static __data uint8_t  g_keyState;
static __data uint16_t g_keyLastCheck;    // 消抖节拍时间戳
static __data uint16_t g_keyPressTick;    // 按下起始时间戳
static __data uint16_t g_keyLastRelease;  // 上次短按松开时间戳（双击窗口计时）
static __data uint8_t  g_keyDispIndex;    // 显示循环项 0=地址 1=错误状态
static __data uint16_t g_keyDispTime;     // 显示循环切换时间戳
static __data uint8_t  g_keyPendingDouble;// 1=本次按下是双击第二下（松开时不进料）
static __data uint8_t  g_keyPullActive;   // 1=连续拉带中（N20 通电，装料模式）

// 【X45】连续拉带：N20 持续通电直接拉带（装料模式）——不走进料状态机/500ms 到位窗口，
//   长距离上料按住即拉、到位松手即停；SGM2593AD 限流+过温兜底（堵转→nFAULT→断电保护）
static void Key_StartPull(void)
{
    Power_EnableNow();
    g_keyPullActive = 1;
}
static void Key_StopPull(void)
{
    Power_DisableNow();
    g_keyPullActive = 0;
}

void Key_Init(void)
{
    // P3.5 高阻输入 + 内部上拉，按下接 GND → LOW
    GPIO_P3_SetMode(FEEDER_KEY_PIN, GPIO_Mode_Input_HIP);
    GPIO_SetPullUp(FEEDER_KEY_PORT, FEEDER_KEY_PIN, 1);
}

// 读按键：1=释放(高)，0=按下(低)
// 注意：GPIO_Port_3 是端口号常量(=3U)，不能当 P3 寄存器读！(3 & 0x20) 恒 0 曾致按键恒"按下"
static uint8_t Key_Read(void)
{
    return (P3 & FEEDER_KEY_PIN) ? 1U : 0U;
}

// 恢复默认显示：地址常亮
static void Key_ShowAddr(void)
{
    Seg_SetDisplay(DISP_ADDR, feeders[0].settings.rs485_addr, 0, 0);
}

// 应用当前显示循环项
static void Key_ApplyDisplay(void)
{
    uint8_t addr = feeders[0].settings.rs485_addr;
    switch (g_keyDispIndex) {
        case 0:  // 地址
            Key_ShowAddr();
            break;
        default: // 【X46】错误状态（删固件年份项——显示"26"对用户无意义）：
                 //   E1(N20超时)或地址（无错）
#ifdef HAS_FEEDBACKLINES
            if (feeders[0].n20TimeoutActive)
                Seg_SetDisplay(DISP_ERR_N20_TOUT, 0, 0, 0);
            else
#endif
                Key_ShowAddr();
            break;
    }
}

// 双击：切到下一显示项（地址 ↔ 错误状态）
static void Key_CycleDisplay(void)
{
    g_keyDispIndex++;
    if (g_keyDispIndex > 1)
        g_keyDispIndex = 0;
    Key_ApplyDisplay();
    g_keyDispTime = millis();
}

// 短按：
//   disabled -> 只做 EN，不进料；地址常亮，再短按才进料
//   IDLE     -> 手动进料一次（忽略反馈状态，override=true）
static void Key_ShortFeed(void)
{
    Feeder_t *f = &feeders[0];

    if (f->state == FEEDER_STATE_DISABLED) {
        Feeder_Enable(f);
        f->restoreDisabled = 0;      // 本地 EN 是保持使能，不是临时进料
        g_keyDispIndex = 0;          // 回到地址显示
        Seg_SetDisplay(DISP_ADDR, f->settings.rs485_addr, 0, 0);
        /* 短按本地使能后主动上报；只带 ChipID，不带 N，N99 也能准确定位。 */
        sendAnswerFmt(0, "EN:1 CHIP:%X%X%X",
            f->chip_id[0], f->chip_id[1], f->chip_id[2]);
        return;
    }

    Feeder_Advance(f, f->settings.feed_length, true);
}

// 【X46】删除 Key_DoFactoryReset：恢复出厂已由 M 命令(MCODE_FACTORY_RESET)承担

void Key_Update(void)
{
    uint16_t now = millis();

    // 显示循环非地址项停留 KEY_CYCLE_SHOW_MS 后自动回地址
    if (g_keyDispIndex != 0 && (uint16_t)(now - g_keyDispTime) >= KEY_CYCLE_SHOW_MS) {
        g_keyDispIndex = 0;
        Key_ShowAddr();
    }

    // 消抖节拍
    if ((uint16_t)(now - g_keyLastCheck) < KEY_DEBOUNCE_MS)
        return;
    g_keyLastCheck = now;

    uint8_t pressed = (Key_Read() == 0);   // 低有效

    switch (g_keyState) {
        case KEY_IDLE:
            if (pressed) {
                g_keyState = KEY_PRESSED;
                g_keyPressTick = now;
            }
            break;

        case KEY_PRESSED:
            if (pressed) {
                // 【X46】按住≥1s：连续拉带（N20 通电，装料拉长距离，松开停）。
                //   不再查 5s 恢复出厂——装料长按不会误触重置（M 命令恢复出厂）
                if (!g_keyPullActive && (uint16_t)(now - g_keyPressTick) >= KEY_PULL_HOLD_MS) {
                    Key_StartPull();
                }
            } else {
                // 松开
                uint16_t holdMs = (uint16_t)(now - g_keyPressTick);
                if (g_keyPullActive) {
                    // 连续拉带中松开：停拉带，不进料不等双击窗口
                    Key_StopPull();
                    g_keyState = KEY_IDLE;
                } else if (holdMs >= KEY_SHORT_MAX_MS) {
                    // 长按（<1s 未进拉带）松开：取消，回空闲
                    g_keyState = KEY_IDLE;
                } else if (g_keyPendingDouble) {
                    // 双击第二下：松开只清标志回空闲，不再进料（显示已在按下时切换）
                    g_keyPendingDouble = 0;
                    g_keyState = KEY_IDLE;
                } else {
                    // 第一下短按松开：进入双击等待窗口(350ms)，
                    //   窗口内按下=双击(切显示)，窗口超时=单次短按(进料)
                    g_keyLastRelease = now;
                    g_keyState = KEY_RELEASE_WAIT;
                }
            }
            break;

        case KEY_RELEASE_WAIT:
            if (pressed) {
                // 双击窗口内按下：判定双击 → 立即切显示循环；松开时（KEY_PRESSED）不再进料
                Key_CycleDisplay();
                g_keyPendingDouble = 1;
                g_keyPressTick = now;
                g_keyState = KEY_PRESSED;
            } else if ((uint16_t)(now - g_keyLastRelease) >= KEY_DOUBLE_WIN_MS) {
                // 窗口超时：判定为单次短按 → 进料一次（延迟 350ms）
                g_keyState = KEY_IDLE;
                Key_ShortFeed();
            }
            break;

        // 【X46】删除 KEY_ARM_WAIT / KEY_PRESSED_ARM（5s 恢复出厂路径，防误触）
    }
}
