#ifndef __SEG_H__
#define __SEG_H__
#include <fw_hal.h>
#include "config.h"   // SEG_SELF_TEST / SEG_POLARITY_INVERT 宏
/* ========================================================================
 *  CharliePlex 数码管引脚映射（QFN20，5 节点，共阳极）
 * ========================================================================
 *
 *  硬件：2 位共阳数码管，14 段经 249Ω 交叉接到 5 个节点（P1.0~P1.4）
 *  驱动：按阳极节点分组扫描——5 组各占 1ms，组内该阳极的所有应亮段同时点亮
 *        （阳极节点输出高、阴极节点输出低，其余节点高阻），5ms 一轮=200Hz 无闪烁，
 *        每段占空比 ≈1/5（逐段扫描的 2.8 倍亮度）
 *
 *  节点编号 → 引脚：
 *      Node1 = P1.4   Node2 = P1.3   Node3 = P1.2   Node4 = P1.1   Node5 = P1.0
 *
 *  段映射（格式：阳极节点-阴极节点）：
 *      a1:2-1  b1:3-1  c1:4-1  d1:5-1  e1:1-2  f1:3-2  g1:4-2
 *      a2:5-2  b2:1-3  c2:2-3  d2:4-3  e2:5-3  f2:1-4  g2:2-4
 *
 *  注：数码管只占 P1.0~P1.4，不碰 P3 组（UART/nFAULT/舵机/反馈/按键/DE/EN 全部释放）
 */

//显示状态枚举
typedef enum {
    DISP_INIT = 0,        // 上电初始化中，显示 --
    DISP_ADDR,            // 正常显示 RS485 地址
    DISP_RUNNING,         // 运行中 (显示 --)
    DISP_ERR_COMM,        // 通信错误 (显示 E0)
    DISP_ERR_N20_TOUT,    // N20电机超时 (显示 E1)
    DISP_ERR_SERVO_TOUT,  // 舵机超时 (显示 E2)
    DISP_ERR_PWR          // 电源过流/过温 (显示 E3)
} DisplayState_t;

// 闪烁倒计时全局变量，驱动内部维护（保留extern，如果外部代码还需要读取；不需要可删掉）
extern uint16_t g_blinkRemainMs;

/**
 * @brief  设置数码管显示内容
 * @param  state 目标显示状态
 * @param  addr  RS485地址（仅DISP_ADDR时有效，范围0~99，超出自动钳位）
 * @param  blinkMs 闪烁时长，单位ms；0=不闪烁，>0开启闪烁并自动倒计时，0xFFFF=永久闪烁
 * @param  periodMs 闪烁周期（亮/灭各一半），单位ms；0=使用默认500ms
 *                  约定：500ms快闪=告警(M611)，1000ms慢闪=禁用(M610 S0)
 * @note   仅更新显示缓冲segBuf[]，实际刷新由定时器中断中的Seg_Refresh()完成
 */
void Seg_SetDisplay(DisplayState_t state, uint8_t addr, uint16_t blinkMs, uint16_t periodMs);

/**
 * @brief  主循环调用，处理闪烁计时+闪烁倒计时递减
 * @param  tickMs 系统tick，每次传入1ms增量
 * @note   ❗不要放到定时器中断 Seg_Refresh，中断只做硬件扫描输出
 *         倒计时到自动关闭闪烁
 */
void Seg_Blink_Process(uint16_t tickMs);

/**
 * @brief  数码管GPIO初始化（上电调用一次）
 * @note   将 P1.0~P1.4 全部配置为推挽输出，默认全低（共阳：节点等电位=不亮）
 */
void Seg_GPIO_Init(void);

/**
 * @brief  CharliePlex扫描刷新函数
 * @note   ⚡ 必须在定时器中断中周期性调用（1ms/次）
 *         按阳极节点分组扫描（5 组 1ms/组），靠视觉暂留实现 2 位显示
 */
void Seg_Refresh(void);

#if SEG_SELF_TEST
/**
 * @brief  上电自检：14 段逐段常亮 300ms（阻塞约 4.2s）
 * @note   必须在 Timer0 启动后、正常显示前调用一次；期间中断扫描只画自检段
 */
void Seg_SelfTest_Run(void);
#endif

#if SEG_ONLY_A1
/**
 * @brief  调试：只显示 a1 段常亮（阻塞死循环，ISR 持续画 a1）
 * @note   用于反推 segMap 与 PCB 实际接线的映射：观察并报告"实际亮的段位"
 */
void Seg_OnlyA1(void);
#endif

#endif // __SEG_H__
