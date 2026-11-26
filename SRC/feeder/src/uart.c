#include "uart.h"
#include "config.h"
#include "fw_hal.h"
/* ========================================================================
 *  环形接收缓冲区（全局，见 uart.h）
 * ======================================================================== */
__xdata volatile char rx_buf[UART_RX_BUF_SIZE];
volatile uint8_t rx_head = 0;
volatile uint8_t rx_tail = 0;
// 发送完成标志：由 UART1 ISR 在 TI 置位时置 1，主循环轮询它代替轮询 TI。
// 【原因】ISR 必须清 TI，否则 TI 置位后中断立即重入形成风暴；
//        而主循环不能直接 while(!TI)（TI 可能已被 ISR 清掉导致死等）。
static volatile uint8_t tx_done = 0;
// 固定波特率初值：sysclk=24000000，Timer2，1T，115200
/*
计算公式：
Timer2_InitValue = 0xFFFF - (sysclk / (4 * baudrate))
对于 24000000 / 115200 = 208.33 → 取 52（重载周期）
value = 52
init = 0xFFFF - 52 + 1 = 0xFFCC
实际波特率 = 24000000 / (4 * 52) = 115384（误差 +0.16%，UART 容差 ±2% 内）
*/
#define UART1_TMR2_INIT_VAL  0xFFCC
/* ========================================================================
 *  UART1 初始化（直接展开，移除动态计算与多余helper函数）
 * ======================================================================== */
void UART_Init(void)
{
#ifdef HAS_RS485_DE_PIN
    // DE引脚推挽输出：硬件配置(端口/引脚/位掩码)全部集中在 config.h，
    // 改端口只需改 config.h 的 RS485_DE_PORT_NUM/PIN_NUM/PIN_MASK + 对应 #if 分支，本文件不用动。
    // 用 FwLib 宏 GPIO_Px_SetMode，传位掩码(RS485_DE_PIN_MASK)而非引脚号，避免位错位。
    RS485_DE_SET_PP_MODE();
    RS485_SetRxMode();  // 默认接收模式
#endif
    // UART1 切换到 P3.0/P3.1
    UART1_SwitchPort(UART1_AlterPort_P30_P31);
    // 8bit UART: SM0=0 SM1=1
    SM0 = 0;
    SM1 = 1;
    UART1_SetBaudSource(UART1_BaudSource_Timer2);
    // Timer2配置 1T模式 + 加载初值 + 启动
    TIM_Timer2_Set1TMode(ENABLE);
    TIM_Timer2_SetInitValue(UART1_TMR2_INIT_VAL >> 8, UART1_TMR2_INIT_VAL & 0xFF);
    TIM_Timer2_SetRunState(HAL_State_ON);
    UART1_SetRxState(ENABLE);
    
    // 【X12根因修复】恢复串口接收中断。ISR 已移到 main.c
    // （SDCC 4.2.0 的 HOME 向量表只由含 main 的编译单元生成且只含该单元
    //  中断；跨单元中断函数不生成向量 → 中断触发跳垃圾代码死机）
    ES = 1;  // 串口1中断使能（接收用）
    EA = 1;  // 全局中断
    // 【X18】串口中断优先级 1 > Timer0(0)：UART1 ISR 可抢占 TIM0 ISR。
    // TIM0 ISR 内含 Seg_Refresh（数码管 CharliePlex 扫描，实测/估算 55~80µs），
    // 同优先级不抢占时命令字节若恰逢 ISR 执行可能被延迟/覆盖丢失（快速连发偶发
    // unknown cmd / Invalid parameters 的疑似根因）。UART ISR 本体仅 ~2µs，抢占无副作用。
    PS = 1;
}
/* ========================================================================
 *  接收接口（X12：纯中断接收，ISR 见 main.c）
 * ======================================================================== */
bool UART_Available(void)
{
    return (rx_head != rx_tail);
}
char UART_Read(void)
{
    if (rx_head == rx_tail) return -1;
    char c = rx_buf[rx_tail];
    rx_tail = rx_tail + 1;
    if(rx_tail >= UART_RX_BUF_SIZE) rx_tail = 0;
    return c;
}
/* ========================================================================
 *  发送接口 (含 RS485 流向控制)
 * ======================================================================== */
// 底层原始发送，不操作DE
static void UART_WriteRaw(char c)
{
    // 【X8修复】发送期间临时关 ES：ES=1 时固定丢第4字节，ES=0 轮询不丢。
    // 每字节在无中断环境发送，发完恢复接收中断。
    ES = 0;
    UART1_WriteBuffer(c);
    while(!TI);
    UART1_ClearTxInterrupt();
    ES = 1;
}
// 单字节发送
void UART_Write(char c)
{
    RS485_SetTxMode();
    UART_WriteRaw(c);
    SYS_DelayUs(100);
    RS485_SetRxMode();
}
// 字符串发送：整包一次切换DE
void UART_WriteString(const char *str)
{
    if (!str) return;
    RS485_SetTxMode();
    while (*str)
    {
        UART_WriteRaw(*str++);
    }
    SYS_DelayUs(100);
    // 【X16】删除发送后 RI=0：理由同 gcode.c sendAnswer（误清命令首字节）
    RS485_SetRxMode();
}
// SDCC my_printf 重定向
int putchar(int c)
{
    if (c == '\n') {
        UART_WriteRaw('\r');
        UART_WriteRaw('\n');
    } else {
        UART_WriteRaw((char)c);
    }
    return c;
}
