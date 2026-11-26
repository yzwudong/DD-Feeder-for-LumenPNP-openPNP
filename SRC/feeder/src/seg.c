/**
 * @file    seg.c
 * @brief   CharliePlex 2位共阳极数码管驱动 (FwLib SDCC版, QFN20 5节点)
 * @note    定时器中断中调用 Seg_Refresh()，主循环调用 Seg_Blink_Process()
 *          上电初始化：闪烁 --
 *          正常运行：稳定显示RS485地址
 *          故障：E1 / E2 / E3
 *          新增：Seg_SetDisplay第3参数直接传入闪烁总时长(ms)，内部自动倒计时，到期停止闪烁
 *          驱动原理：5节点CharliePlex，按阳极节点分组扫描（5组各1ms，组内该阳极
 *                    所有应亮段同时点亮），5ms一轮=200Hz无闪烁，占空比≈1/5
 */
#include "config.h"
#include "seg.h"
/* ================================================================
 * 静态/全局变量
 * ================================================================ */
extern uint16_t millis(void);   // main.c 定义的系统毫秒计数
DisplayState_t g_dispState = DISP_INIT;   // 当前显示状态，上电默认初始化态
#if SEG_SELF_TEST
uint8_t g_selfTestIdx = 0xFF;   // 0xFF=不在自检；0~13=当前自检段（上电自检用）
#endif
uint8_t        segBuf[2] = {10, 10};      // 显示缓冲: [十位, 个位]
uint8_t        cpScanIdx = 0;             // CharliePlex 扫描索引: 0~13 = a1..g2
static uint8_t g_blinkEn;                 // 闪烁使能：1=闪烁，0=常亮
static uint16_t g_blinkTick;              // 闪烁周期计时，主循环更新
uint16_t g_blinkRemainMs = 0;             // 闪烁剩余总时间 ms
static uint8_t blinkShow;                 // 闪烁当前画面：1=显示segBuf内容，0=黑屏
#define BLINK_PERIOD 500                  // 默认闪烁周期 500ms 亮/灭切换（告警快闪）
static uint16_t g_blinkPeriod = BLINK_PERIOD; // 当前闪烁周期（Seg_SetDisplay第4参设置，0=默认）

/* ================================================================
 * 共阳段码表 a b c d e f g → bit0~bit6
 * index 0~9:数字 |10:'-' |11:'F' |12:'E'
 * 注意：最大索引12，不要传入>=13的值，防止数组越界
 * 位形只描述"哪些段亮"，点亮极性由驱动层处理（共阳：阳高阴低）
 * ================================================================ */
const uint8_t segCode[] =
{
    0x3F, // 0  abcdef
    0x06, // 1  bc
    0x5B, // 2  abdeg
    0x4F, // 3  abcdg
    0x66, // 4  bcfg
    0x6D, // 5  acdfg
    0x7D, // 6  acdefg
    0x07, // 7  abc
    0x7F, // 8  abcdefg
    0x6F, // 9  abcdfg
    0x40, //10 '-' 仅g段
    0x71, //11 'F' aefg
    0x79  //12 'E' abcd eg
};

/* ================================================================
 * 5节点CharliePlex段映射表
 * 索引0~13 = a1 b1 c1 d1 e1 f1 g1 a2 b2 c2 d2 e2 f2 g2
 * 每段两个节点：{阳极节点, 阴极节点}（共阳：阳高阴低点亮）
 * 节点编号→引脚：Node1=P1.4 Node2=P1.3 Node3=P1.2 Node4=P1.1 Node5=P1.0
 * ================================================================ */
const uint8_t segMap[14][2] =
{
    {2,1}, // a1
    {3,1}, // b1
    {4,1}, // c1
    {5,1}, // d1
    {1,2}, // e1
    {3,2}, // f1
    {4,2}, // g1
    {5,2}, // a2  用户看图确认：5-2（阳5阴2）
    {1,3}, // b2
    {2,3}, // c2
    {4,3}, // d2
    {5,3}, // e2  用户看图确认：5-3（阳5阴3）
    {1,4}, // f2
    {2,4}  // g2
};

/* ================================================================
 * 公共 API
 * ================================================================ */
/**
 * @brief  设置数码管显示内容，设置闪烁总时长
 * @param  state 目标显示状态
 * @param  addr  RS485地址，仅DISP_ADDR有效，0~99
 * @param  blinkMs 闪烁持续毫秒；0=常亮不闪烁；>0开启闪烁并设置倒计时；0xFFFF=永久闪烁
 * @param  periodMs 闪烁周期ms；0=默认500ms（500ms快闪=告警，1000ms慢闪=禁用）
 * @note   会填充segBuf，重置闪烁计时器
 */
void Seg_SetDisplay(DisplayState_t state, uint8_t addr, uint16_t blinkMs, uint16_t periodMs)
{
    g_dispState = state;
    g_blinkPeriod = (periodMs == 0) ? BLINK_PERIOD : periodMs;
    if(blinkMs > 0)
    {
        g_blinkEn = 1;
        g_blinkRemainMs = blinkMs;
    }
    else
    {
        g_blinkEn = 0;
        g_blinkRemainMs = 0;
    }
    g_blinkTick = 0;
    blinkShow = 1; // 默认先点亮画面

    switch(state)
    {
        case DISP_INIT:
            segBuf[0] = 10;
            segBuf[1] = 10;
            break;
        case DISP_ADDR:
            segBuf[0] = addr / 10;
            segBuf[1] = addr % 10;
            break;
        case DISP_ERR_COMM:
            segBuf[0] = 12; // E
            segBuf[1] = 0;
            break;
        case DISP_ERR_N20_TOUT:
            segBuf[0] = 12; // E
            segBuf[1] = 1;
            break;
        case DISP_ERR_SERVO_TOUT:
            segBuf[0] = 12; // E
            segBuf[1] = 2;
            break;
        case DISP_ERR_PWR:
            segBuf[0] = 12; // E
            segBuf[1] = 3;
            break;
        default:
            segBuf[0] = 0xFF;
            segBuf[1] = 0xFF;
            break;
    }
}

/**
 * @brief  主循环调用，处理闪烁计时+倒计时递减
 * @param  tickMs 系统tick，每次传入1ms增量
 * @note   ?不要放到定时器中断 Seg_Refresh，中断只做硬件扫描输出
 *         倒计时到自动关闭闪烁
 */
void Seg_Blink_Process(uint16_t tickMs)
{
    if(!g_blinkEn)
    {
        g_blinkTick = 0;
        return;
    }

    // 总闪烁倒计时减少(g_blinkRemainMs != 
    if(g_blinkRemainMs != 0xFFFF)
    {
        if(g_blinkRemainMs >= tickMs)
        {
            g_blinkRemainMs -= tickMs;
        }
        else
        {
            g_blinkRemainMs = 0;
        }
    }


    // 倒计时结束，关闭闪烁
    if(g_blinkRemainMs == 0)
    {
        g_blinkEn = 0;
        g_blinkTick = 0;
        blinkShow = 1;
        return;
    }

    // 【X43】恢复对称翻转（用户指定"灭一秒亮一秒"）：禁用=1000/1000，告警=500/500。
    // X42 不对称（灭125ms）视觉上≈常亮，禁用提示消失，弃用。
    g_blinkTick += tickMs;
    if(g_blinkTick >= g_blinkPeriod)
    {
        g_blinkTick = 0;
        blinkShow = !blinkShow; // 翻转：显示 <-> 黑屏
    }
}

/**
 * @brief  数码管 GPIO 初始化 (上电调用一次)
 * @note   P1.0~P1.4 推挽输出，默认全低（共阳：节点等电位=无段点亮）
 */
void Seg_GPIO_Init(void)
{
    // 5 个节点全部推挽输出
    GPIO_P1_SetMode(GPIO_Pin_0, GPIO_Mode_Output_PP);
    GPIO_P1_SetMode(GPIO_Pin_1, GPIO_Mode_Output_PP);
    GPIO_P1_SetMode(GPIO_Pin_2, GPIO_Mode_Output_PP);
    GPIO_P1_SetMode(GPIO_Pin_3, GPIO_Mode_Output_PP);
    GPIO_P1_SetMode(GPIO_Pin_4, GPIO_Mode_Output_PP);
    // 默认全低，不点亮任何段
    P10 = 0;
    P11 = 0;
    P12 = 0;
    P13 = 0;
    P14 = 0;
}

/* ================================================================
 * 内部辅助函数
 * ================================================================ */
/**
 * @brief  将 5 个节点全部设为高阻输入 (消隐)
 * @note   CharliePlex 切换段前必须先消隐，否则切换瞬间会误亮其他段
 */
static void _cp_blank_all(void)
{
    GPIO_P1_SetMode(GPIO_Pin_0, GPIO_Mode_Input_HIP);
    GPIO_P1_SetMode(GPIO_Pin_1, GPIO_Mode_Input_HIP);
    GPIO_P1_SetMode(GPIO_Pin_2, GPIO_Mode_Input_HIP);
    GPIO_P1_SetMode(GPIO_Pin_3, GPIO_Mode_Input_HIP);
    GPIO_P1_SetMode(GPIO_Pin_4, GPIO_Mode_Input_HIP);
}

/**
 * @brief  将指定节点设为推挽输出并输出电平
 * @param  node  节点编号 1~5（1=P1.4 2=P1.3 3=P1.2 4=P1.1 5=P1.0）
 * @param  level 0=低 1=高
 */
static void _cp_node_set(uint8_t node, uint8_t level)
{
    switch(node)
    {
        case 1: GPIO_P1_SetMode(GPIO_Pin_4, GPIO_Mode_Output_PP); P14 = level; break;
        case 2: GPIO_P1_SetMode(GPIO_Pin_3, GPIO_Mode_Output_PP); P13 = level; break;
        case 3: GPIO_P1_SetMode(GPIO_Pin_2, GPIO_Mode_Output_PP); P12 = level; break;
        case 4: GPIO_P1_SetMode(GPIO_Pin_1, GPIO_Mode_Output_PP); P11 = level; break;
        case 5: GPIO_P1_SetMode(GPIO_Pin_0, GPIO_Mode_Output_PP); P10 = level; break;
        default: break;
    }
}

/**
 * @brief  点亮一段（极性由 SEG_POLARITY_INVERT 决定）
 * @param  segIdx 段索引 0~13（a1..g2）
 * @param  on     1=点亮 0=忽略
 * @note   正常：阳极节点高、阴极节点低；反转（硬件LED反向时）：阳极节点低、阴极节点高
 */
static void _cp_seg_paint(uint8_t segIdx, uint8_t on)
{
    if(!on) return;
#if SEG_POLARITY_INVERT
    _cp_node_set(segMap[segIdx][0], 0);   // 反转：阳极节点 → 低
    _cp_node_set(segMap[segIdx][1], 1);   // 反转：阴极节点 → 高
#else
    _cp_node_set(segMap[segIdx][0], 1);   // 正常：阳极节点 → 高
    _cp_node_set(segMap[segIdx][1], 0);   // 正常：阴极节点 → 低
#endif
}

/**
 * @brief  CharliePlex 扫描刷新 (定时器中断中周期性调用)
 * @note   按"阳极节点"分组扫描：5 个阳极各占 1ms，组内该阳极的所有应亮段同时点亮
 *         一轮 5ms（200Hz 无闪烁），每段占空比 1/5≈20%（逐段扫描 1/14≈7%，提亮约 2.8 倍）
 *         消隐：全部高阻，避免切换鬼影
 *         闪烁逻辑：闪烁黑屏阶段使用非法索引，直接消隐
 *         自检模式（SEG_SELF_TEST=1 时）：只点亮 g_selfTestIdx 段，常亮不分组
 */
void Seg_Refresh(void)
{
#if SEG_DISABLE
    /* 调试：屏蔽数码管驱动——全部节点高阻消隐，无任何段点亮（零扫描电流） */
    _cp_blank_all();
    return;
#endif
#if SEG_SELF_TEST
    /* ========== 自检模式：当前自检段 100% 占空比常亮 ========== */
    if(g_selfTestIdx <= 13)
    {
        _cp_blank_all();
        _cp_seg_paint(g_selfTestIdx, 1);
        return;   // 不推进 cpScanIdx，保持该段常亮
    }
#endif
    /* ========== 正常扫描路径（局部变量 static xdata：ISR 栈最小化、不占 data 段，防栈溢出） ========== */
    static __xdata uint8_t dispBuf[2];     // static xdata：不占栈
    static __xdata uint8_t anodeNode;      // static xdata
    static __xdata uint8_t i;              // static xdata
    // ========== 闪烁逻辑 ==========
    if(g_blinkEn && !blinkShow)
    {
        // 闪烁黑屏阶段：填充非法索引，驱动自动消隐
        dispBuf[0] = 0xFF;
        dispBuf[1] = 0xFF;
    }
    else
    {
        // 正常显示，使用segBuf缓存
        dispBuf[0] = segBuf[0];
        dispBuf[1] = segBuf[1];
    }
    // ==============================

    /* Step1: 消隐 —— 5 个节点全部高阻，消除段切换鬼影 */
    _cp_blank_all();

    /* Step2: 确定当前阳极组 (cpScanIdx 0~4 = 阳极 Node1~Node5) */
    anodeNode = cpScanIdx + 1;
    /* 阳极节点 → 推挽高 */
    _cp_node_set(anodeNode, 1);

    /* Step3: 遍历 14 段，点亮所有"阳极==本组"且段码应亮的段（对应阴极拉低） */
    uint8_t segCodeCnt = sizeof(segCode)/sizeof(segCode[0]);
    for(i = 0; i < 14; i++)
    {
        if(segMap[i][0] != anodeNode) continue;   // 只处理本阳极组的段
        uint8_t digit  = i / 7;      // 0=十位, 1=个位
        uint8_t segBit = i % 7;      // 0=a ~ 6=g
        uint8_t codeIdx = dispBuf[digit];
        if(codeIdx >= segCodeCnt) continue;       // 非法索引跳过（保持消隐）
        if(segCode[codeIdx] & (1U << segBit))     // 该段需要点亮
        {
            _cp_seg_paint(i, 1);
        }
    }
    /* Step4: 切换到下一阳极组 */
    cpScanIdx++;
    if(cpScanIdx >= 5) cpScanIdx = 0;
}

#if SEG_SELF_TEST
#if SEG_ONLY_A1
/**
 * @brief  调试：只显示 a1 段常亮（阻塞死循环）
 * @note   主循环停在此处，TIM0 ISR 的 Seg_Refresh 因 g_selfTestIdx=0 持续画 a1。
 *         用户观察并报告"实际亮的段位"，用于反推 segMap 映射。
 */
void Seg_OnlyA1(void)
{
    g_selfTestIdx = 0;      // 自检段索引 0 = a1
    while(1) { }            // 死循环，ISR 持续画 a1
}
#endif

/**
 * @brief  上电自检：14 段逐段常亮 800ms（阻塞，约 11s），每段串口打印段名
 * @note   由 main() 在 UART_Init() 之后调用一次；期间 TIM0 ISR 的 Seg_Refresh
 *         只画当前自检段（100% 占空比）。
 *         用途：肉眼对照"串口打印的段名 vs 数码管实际亮的位置"，
 *               14 条记录反推 segMap 与 PCB 走线的真实映射（乱序/错位时）。
 *         裸发送（不依赖 uart.c）：ES=0 防丢字节（X8 结论），轮询 TI。
 */
static const char _segNames[] = "a1b1c1d1e1f1g1a2b2c2d2e2f2g2";
static void _selftest_putc(char c)
{
    ES = 0;
    SBUF = c;
    while(!TI);
    TI = 0;
    ES = 1;
}
static void _selftest_print(const char *s)
{
    while(*s) _selftest_putc(*s++);
    _selftest_putc('\r');
    _selftest_putc('\n');
}
void Seg_SelfTest_Run(void)
{
    uint8_t idx;
    g_selfTestIdx = 0;
    for(idx = 0; idx < 14; idx++)
    {
        g_selfTestIdx = idx;            // 切换到当前自检段（中断扫描画它）
        _selftest_print(_segNames + idx*2);   // 打印当前段名（如 a1）
        uint16_t t0 = millis();
        while((millis() - t0) < 800);   // 每段常亮 800ms
    }
    g_selfTestIdx = 0xFF;   // 自检结束，恢复正常扫描
}
#endif
