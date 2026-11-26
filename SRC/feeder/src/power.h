#ifndef _POWER_H
#define _POWER_H

#include <stdbool.h>
#include <stdint.h>     // uint16_t

/* ========================================================================
 *  电源管理：SGM2593AD 配电开关（ N20 用）
 * ========================================================================
 *  - nFAULT (P3.2)：开漏输出，过流/过温拉低，故障恢复自动拉高
 *  - EN      (P3.7)：MCU 控制，高=导通；故障时断电保护
 *  - 故障检测：软件 10ms 轮询 + 连续 2 次确认（芯片内已有 15ms deglitch）
 *  - 故障恢复：nFAULT 回高并稳定 1s 后重新供电（EN=1），自动清除故障
 * ======================================================================== */

void Power_Init(void);      // 初始化引脚，N20 默认断电（EN=0）
void Power_Update(void);    // 主循环轮询（10ms 节拍）：检测 N20 故障/断电保护/解除清除
void Power_EnableNow(void); // N20 通电（进料排程成功时调用）
void Power_DisableNow(void);// N20 断电（周期收尾 Feeder_CycleEnd 时调用）
bool Power_IsFault(void);   // 返回当前是否处于电源故障（已断电）

#endif // _POWER_H
