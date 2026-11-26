#pragma once
/* 蜂鸣器: 无源蜂鸣器 + LEDC PWM, 非阻塞状态机, 可编程音调/音量
 * 事件音定义见 docs/蜂鸣器说明.md
 */

typedef enum {
    BEEP_NONE = 0,
    BEEP_OK,           /* 成功提示(扫描完成/操作成功): 短促 1 次 */
    BEEP_CMD_ERR,      /* M 命令执行失败: 短鸣 2 次 */
    BEEP_FEEDER_ERR,   /* 飞达错误(lastErr 状态变化): 短鸣 N 次(N=出错台数, 上限 3) */
    BEEP_BUS_OFFLINE,  /* 飞达离线增多: 长鸣 2 次 */
    BEEP_OTA_FAIL,     /* OTA 失败: 急促连鸣 5 次 */
    BEEP_OTA_OK,       /* OTA 成功: 上扬双音 */
    BEEP_POWER_FAULT,  /* 总线电源故障(nFAULT): 中长鸣 3 次 */
    BEEP_POWER_LOCK,   /* 电源自动重试被终止(保持关断): 低频长鸣 2 次 */
} BeepKind;

void buzzerInit();              /* setup 调用: 读偏好 + 配置引脚 */
void buzzerLoop();              /* loop 调用: 驱动蜂鸣状态机(非阻塞) */
void buzzerBeep(BeepKind k, int count);
void buzzerPrefsEnd();          /* 恢复出厂用: 关闭 NVS handle(见 factory_reset.cpp) */
bool buzzerIsOn();
void buzzerSetOn(bool on);
int  buzzerVol();
void buzzerSetVol(int vol);     /* 0~100 */
