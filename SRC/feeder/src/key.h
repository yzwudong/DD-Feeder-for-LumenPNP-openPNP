#ifndef _KEY_H
#define _KEY_H

/**
 * @brief  初始化按键 GPIO（内部上拉，低有效）
 * @note   上电调用一次
 */
void Key_Init(void);

/**
 * @brief  主循环轮询按键状态机（非阻塞）
 * @note   ⚠️ 必须在 main() while(1) 中持续调用
 *         内部完成消抖与三态识别：
 *         短按(<800ms) = 手动进料一次（忽略反馈，override）；
 *         双击(300ms内两次) = 显示循环切换（地址/固件年份/错误状态，2s自动回地址）；
 *         长按(5s) = 进入恢复出厂待确认（地址闪烁3s倒计时），
 *                    期间再短按 = 恢复出厂；超时/长按 = 取消恢复默认显示
 */
void Key_Update(void);

#endif // _KEY_H
