#include "fw_hal.h"
#include "gcode.h"
#include "uart.h"
#include "adc.h"
#include "feeder.h"
#include "config.h"
#include <string.h>
//#include <stdio.h>
//#include <stdlib.h>  // atof删掉，不需要了
#include <stdarg.h>
#include <my_sprintf.h>
#include "seg.h"
#include "iap_flag.h"

// 外部全局变量声明
extern AsyncReply_t g_asyncReply;
static char* my_strchr(char *s, char ch)
{
    // 【防御】有界扫描：即使 inputBuffer 被越界写破坏、丢失 '\0' 也不会死循环
    uint8_t i = 0;
    while(*s && i < MAX_BUFFFER_MCODE_LINE)
    {
        if(*s == ch) return s;
        s++;
        i++;
    }
    return NULL;
}

/* ========================================================================
 *  接收缓冲区 (替代 Arduino 的 String inputBuffer)
 * ======================================================================== */
static __xdata char inputBuffer[MAX_BUFFFER_MCODE_LINE];
static uint8_t bufLen = 0;

// 【X47】620 设置快闪(250ms/3s)结束后的显示恢复（main 循环按当前状态执行）
uint8_t  g_dispRestorePending;
uint16_t g_dispRestoreAt;

/* ========================================================================
 *  参数解析：从 inputBuffer 中提取指定字母后的数值
 *  替代原 Arduino parseParameter(char code, float defaultVal)
 *  修改：仅支持整数，返回int16_t，不再支持浮点数，移除atof，节省Flash
 * ======================================================================== */
int16_t parseParameter(char code, int16_t defaultVal)
{
    // 1. 在缓冲区中查找目标字符 (如 'M', 'N', 'F')
    char *p = my_strchr(inputBuffer, code);
    if (p == NULL) {
        return defaultVal; // 未找到，返回默认值
    }
    // 2. 跳过字母本身，指向数字起始位置
    p++;
    // 3. 检查后面是否还有有效字符
    if (*p == '\0' || *p == ' ') {
        return defaultVal;
    }
    // 4. 纯整数解析，支持负号，遇到非数字字符停止
    int16_t val = 0;
    uint8_t neg = 0;
    if(*p == '-')
    {
        neg = 1;
        p++;
    }
    while(*p >= '0' && *p <= '9')
    {
        val = (val << 3) + (val << 1) + (*p - '0');
        p++;
    }
    if(neg) val = -val;
    return val;
}

// 在inputBuffer查找code，返回参数字符串起始指针；找不到返回NULL，用户P后面的值，不再解析为整数，直接返回字符串指针
char* parseParameterStr(char code)
{
    char *p = my_strchr(inputBuffer, code);
    if(p == NULL)
        return NULL;
    p++;
    // 跳过等号，支持 P112233 或者 P=112233
    if(*p == '=')
        p++;
    // 空格/结束 → 空字符串
    if(*p == '\0' || *p == ' ')
        return NULL;
    return p;
}



/* ========================================================================
 *  初始化 GCode 处理器
 * ======================================================================== */
void setupGCodeProc(void)
{
    bufLen = 0;
    // memset(inputBuffer, 0, sizeof(inputBuffer));
    // 只在上电初始化时调用一次memset，串口接收循环不再重复清零大xdata缓冲区
}

/* ========================================================================
 *  应答发送 (替代 Arduino sendAnswer)
 *  SDCC 下直接使用 my_printf，由 uart.c 中的 putchar 接管底层发送
 * ======================================================================== */
void sendAnswer(uint8_t error, const char *message)
{
    // 【减栈】不再走 my_printf→my_vsprintf→num2str（命令链 7 层逼近 SSEG 207B 上限，
    // 疑似栈溢出跑飞）。改为 putchar 直发，砍掉 3 层栈帧。
    RS485_SetTxMode();
    if (error == 0) {
        putchar('o'); putchar('k'); putchar(' ');
    } else {
        putchar('e'); putchar('r'); putchar('r'); putchar('o'); putchar('r'); putchar(' ');
    }
    while (*message) putchar(*message++);
    putchar('\n');   // putchar 自动补 \r
    SYS_DelayUs(100);
    // 【X16】删除发送后 RI=0：X15 该行会误清"紧贴应答到达"的下一条命令首字节，
    //        导致快速连发偶发 unknown cmd / Invalid parameters（X14 无 RI=0 时正常）。
    //        实测 DE=1 时 485 芯片 RO 高阻、发送无回环，无需清除。
    RS485_SetRxMode();
}
//带格式化的应答输出  sendAnswerFmt(0,"addr:%d",5); → ok addr:5\r\n
void sendAnswerFmt(uint8_t error, const char *fmt, ...)
{
    // 【X17修复·评审§3-7】buf 128→136：前缀 "ok "/"error "（3~6字节）写在前，
    // 再交给按 128 上限写的 my_vsprintf → 理论越界 6 字节（X15 同类问题）。扩到 136 消除。
    static __xdata char buf[136];  // static，扩展ram数据段，不占用栈
    va_list ap;
    char *p = buf;

    // 写入协议前缀 ok / error
    if(error == 0)
    {
        const char *pre = "ok ";
        while(*pre)
        {
            *p++ = *pre++;
        }
    }
    else
    {
        const char *pre = "error ";
        while(*pre)
        {
            *p++ = *pre++;
        }
    }

    va_start(ap, fmt);
    // 从p的位置继续格式化，va只使用一次
    my_vsprintf(p, fmt, ap);
    va_end(ap);

    // RS485整包发送
    RS485_SetTxMode();
    uint8_t i = 0;
    while(buf[i])
    {
        putchar(buf[i++]);
    }
    SYS_DelayUs(100);
    // 【X16】删除发送后 RI=0：理由同 sendAnswer（误清命令首字节 → 快速连发异常）
    RS485_SetRxMode();
}


/* ========================================================================
 *  飞达编号校验
 * ======================================================================== 
bool validFeederNo(int8_t signedFeederNo, uint8_t feederNoMandatory)
{
    if (signedFeederNo == -1 && feederNoMandatory >= 1) {
        return false; // 必须提供编号但未提供
    }
    if (signedFeederNo < 0 || signedFeederNo > (NUMBER_OF_FEEDER - 1)) {
        return false; // 编号越界
    }
    return true;
}
*/

/**
 * @brief 通用寻址判断：N地址 + P ChipID，支持单独/同时指定，相与判断
 * @param nRaw 解析出来的N参数，-1表示N不存在
 * @param chipidStr P参数字符串，NULL表示P不存在
 * @retval 1:本机需要执行指令；0:跳过
 */
uint8_t CheckTargetFeeder(int16_t nRaw, char *chipidStr)
{
    uint8_t addrMatch = 0;
    uint8_t chipMatch = 0;
    uint8_t localAddr = feeders[0].settings.rs485_addr;

    //地址判断
    if(nRaw != -1)
    {
        //N参数存在，先校验地址合法性
        if(!validFeederAddr(nRaw))
        {
            return 0; //地址非法，直接不命中
        }
        if(nRaw == 255)
        {
            addrMatch = 1;
        }
        else
        {
            if(nRaw == localAddr)
            {
                addrMatch = 1;
            }
        }
    }
    else
    {
        //N不存在，地址条件自动成立
        addrMatch = 1;
    }

    //ChipID判断
    if(chipidStr != NULL)
    {
        if(Comm_MatchChipID(feeders[0].chip_id, chipidStr))
        {
            chipMatch = 1;
        }
    }
    else
    {
        //P不存在，chip条件自动成立
        chipMatch = 1;
    }

    return (addrMatch && chipMatch);
}

/* 【X52】广播(N=255)与单点(P)互斥检查：
 *  N255 表示总线广播，P 表示 ChipID 单点，语义冲突——并存视为非法命令，拒绝执行
 *  （避免"想广播误带P → 静默变单点漏动作"或"想单点误写255"的隐患）。
 *  返回 1=有冲突（已应答 error），0=无冲突。 */
static uint8_t CheckBroadcastConflict(int16_t nRaw, char *chipidStr)
{
    if(nRaw == 255 && chipidStr != NULL)
    {
        sendAnswer(1, "busy");
        return 1;
    }
    return 0;
}

/**
 * @brief 校验RS485地址合法性，包含后门地址判断
 * @param addr 待校验地址
 * @return true合法，false非法
 * @note 合法范围：
 *        1. RS485_ADDR_MIN ~ RS485_ADDR_MAX
 *        2. 255 用于后门地址，允许用于总线扫描和救砖
 */
// 指令寻址校验：N参数合法判断（允许255广播查询、99出厂、0~63），需要提前校验addr是int8_t类型
bool validFeederAddr(uint8_t addr)
{
    return (addr == 255) || (addr == 99) || (addr <= RS485_ADDR_MAX);
}

// M620 写入新地址校验：禁止写入99，只能0~63
bool validNewFeederAddr(uint8_t addr)
{
    if(addr == RS485_ADDR_DEFAULT)
    {
        return false;
    }
    return (addr <= RS485_ADDR_MAX);
}

/* ========================================================================
 *  命令处理核心 (状态机)
 * ======================================================================== */
void processCommand(void)
{
    // 解析 M 代码，默认 -1 表示无命令
    int cmd = parseParameter('M', -1);
#ifdef DEBUG
    my_printf("command found: M%d\r\n", cmd);
#endif
    switch (cmd) {
        /* ----------------- 飞达控制指令 ----------------- */
        
            case MCODE_SET_FEEDER_ENABLE: {
                int16_t nRaw = parseParameter('N', -1);
                char *chipidStr = parseParameterStr('P');   // 【X52】P 单点寻址（ChipID 唯一）
                int16_t sRaw = parseParameter('S', -1);

                // S只能是0或1，其他全部非法
                if(sRaw != 0 && sRaw != 1)
                {
                    sendAnswer(1, "Invalid parameters");
                    break;
                }
                // N不为空时校验地址合法性（0~63/99/255 均合法，255=广播）
                if(nRaw != -1 && !validFeederAddr((uint8_t)nRaw))
                {
                    sendAnswer(1, "addr invalid");
                    break;
                }
                // 【X52】广播(N255)与单点(P)互斥
                if(CheckBroadcastConflict(nRaw, chipidStr)) break;
                // 【X52】统一寻址语义（CheckTargetFeeder）：
                //   N缺省=自动成立；N=255=广播；N=0~63/99=地址匹配；P=ChipID匹配；N+P=双锁
                if(!CheckTargetFeeder(nRaw, chipidStr))
                {
                    // 不是目标飞达，直接退出，不动作、不应答
                    break;
                }

                // 执行使能/禁用（单飞达：两级使能已合并，统一由 feeders[0].state 表达）
                if(sRaw == 1)
                {
                    executeCommandOnAllFeeder(cmdEnable);
                    // 恢复常亮显示本机地址（清除禁用慢闪/告警快闪）
                    Seg_SetDisplay(DISP_ADDR, feeders[0].settings.rs485_addr, 0, 0);
                    sendAnswer(0, "feeder enabled");
                }
                else
                {
                    executeCommandOnAllFeeder(cmdDisable);
                    // 禁用：地址永久慢闪(1s)，与M611告警快闪(500ms)区分（见 seg.h）
                    Seg_SetDisplay(DISP_ADDR, feeders[0].settings.rs485_addr, 0xFFFF, 1000);
                    sendAnswer(0, "feeder disabled");
                }
                break;
            }

            
        case MCODE_ALARM_FLASH_ADDR: {
            // address conflict / unconfigured(addr=99) alarm: blink addr continuously
            // Host sends M611 to this board when M602 reports addr=99 or duplicate addr
            int16_t nRaw = parseParameter('N', -1);
            char *chipidStr = parseParameterStr('P');
            if(nRaw != -1 && !validFeederAddr((uint8_t)nRaw))
            {
                sendAnswer(1, "addr invalid");
                break;
            }
            // 【X52】广播(N255)与单点(P)互斥
            if(CheckBroadcastConflict(nRaw, chipidStr)) break;
            if(!CheckTargetFeeder(nRaw, chipidStr))
            {
                break;
            }
            // blinkMs=0xFFFF means infinite blink (see seg.c)；periodMs=0 → 默认500ms快闪
            Seg_SetDisplay(DISP_ADDR, feeders[0].settings.rs485_addr, 0xFFFF, 0);
            sendAnswer(0, "alarm on");
            break;
        }

        case MCODE_CLEAR_ALARM: {
            // clear address alarm: steady display of local addr
            int16_t nRaw = parseParameter('N', -1);
            char *chipidStr = parseParameterStr('P');
            if(nRaw != -1 && !validFeederAddr((uint8_t)nRaw))
            {
                sendAnswer(1, "addr invalid");
                break;
            }
            // 【X52】广播(N255)与单点(P)互斥
            if(CheckBroadcastConflict(nRaw, chipidStr)) break;
            if(!CheckTargetFeeder(nRaw, chipidStr))
            {
                break;
            }
            Seg_SetDisplay(DISP_ADDR, feeders[0].settings.rs485_addr, 0, 0);
            sendAnswer(0, "alarm off");
            break;
        }

           case MCODE_ADVANCE: {
                // 进料前必须确保系统已使能
                if (feeders[0].state == FEEDER_STATE_DISABLED) {
                    // 改为统一应答接口
                    sendAnswerFmt(1, "enable first M%d S1", MCODE_SET_FEEDER_ENABLE);
                    break;
                }
                int16_t addrRaw = parseParameter('N', -1);
                char *chipidStr = parseParameterStr('P');   // 【X52】P 单点寻址
                int16_t overrideErrorRaw = parseParameter('X', -1);
                bool overrideError = (overrideErrorRaw >= 1); // X1 表示忽略反馈错误
                // N参数原始校验（【X52】不再强制 N 必填：P 或无寻址可自动成立）
                if(addrRaw != -1 && !validFeederAddr((uint8_t)addrRaw))
                {
                    sendAnswer(1, "addr invalid");
                    break;
                }
                // 【X52】广播(N255)与单点(P)互斥
                if(CheckBroadcastConflict(addrRaw, chipidStr)) break;
                // 获取进料长度，若未指定则使用飞达默认配置
                uint8_t feedLength = (uint8_t)parseParameter('F', feeders[0].settings.feed_length);
                // 校验：必须是 2 的倍数，且最大 24mm
                if ((feedLength % 2) != 0 || feedLength > 24) {
                    sendAnswer(1, "invalid F");
                    break;
                }
            #ifdef DEBUG
                // 调试日志，保留my_printf，不属于协议应答
                my_printf("Determined feedLength %d\r\n", feedLength);
            #endif
                // 触发进料动作
                // 【X22】拆文案：忙（上一条进料未完成，重试即可）与真故障（反馈线/错误状态）区分，
                // 上位机不再把"重试即可"误判为"报警停机"。
                if (feeders[0].state != FEEDER_STATE_IDLE) {
                    sendAnswer(1, "feeder busy");
                    break;
                }
                // 【X52】统一寻址：N(0~63/99)/P/N255/双锁，不命中跳过不应答（M600 为异步命令）
                if(!CheckTargetFeeder(addrRaw, chipidStr)) break;
                bool triggerFeedOK = Feeder_Advance(&feeders[0], feedLength, overrideError);
                if (!triggerFeedOK) {
                    sendAnswer(1, "feedback fault");
                }
                // 注意：成功时不立即回复 ok，等进料完成后由 feeder.c 的状态机回复
                break;
            }

        case MCODE_RETRACT_POST_PICK: {
            if (feeders[0].state == FEEDER_STATE_DISABLED) {
                sendAnswer(1, "enable first!");
                break;
            }
            int16_t addrRaw = parseParameter('N', -1);
            char *chipidStr = parseParameterStr('P');   // 【X52】P 单点寻址
            // 【X52】不再强制 N 必填：P 或无寻址可自动成立
            if(addrRaw != -1 && !validFeederAddr((uint8_t)addrRaw))
            {
                sendAnswer(1, "addr invalid");
                break;
            }
            // 【X52】广播(N255)与单点(P)互斥
            if(CheckBroadcastConflict(addrRaw, chipidStr)) break;
            // 【X52】统一寻址
            if(!CheckTargetFeeder(addrRaw, chipidStr)) break;
            // TODO: 调用飞达的 Post-Pick 退回函数
            Feeder_GotoPostPick(&feeders[0]);
            sendAnswer(0, "retract done");
            break;
        }
        case MCODE_FEEDER_IS_OK: {
            int16_t addrRaw = parseParameter('N', -1);
            char *chipidStr = parseParameterStr('P');
            uint8_t localAddr = feeders[0].settings.rs485_addr;
            uint8_t match = 0;
            uint8_t isGlobalQuery = 0;

            // 第一步：N 参数原始校验（带 P 时允许不带 N，P 已唯一锁定目标）
            if(addrRaw != -1)
            {
                if(addrRaw < 0 || !validFeederAddr((uint8_t)addrRaw))
                {
                    sendAnswer(1, "addr invalid");
                    break;
                }
            }
            // 【X52】广播(N255)与单点(P)互斥（原"P优先单点"静默降级改显式拒绝）
            if(CheckBroadcastConflict(addrRaw, chipidStr)) break;
            // 第二步：匹配。P 优先（chipid 唯一寻址），无 P 时保持原 N 逻辑
            if(chipidStr != NULL)
            {
                // 带 P：chipid 必须匹配；同时带 N（非255）还需地址相与
                if(!Comm_MatchChipID(feeders[0].chip_id, chipidStr))
                {
                    break; // ID不匹配，跳过不应答
                }
                if(addrRaw != -1 && addrRaw != 255)
                {
                    if(localAddr != (uint8_t)addrRaw)
                    {
                        break; // ID匹配但地址不匹配，跳过
                    }
                }
                match = 1; // 带P一律走单点应答（即使N=255，P已唯一锁定）
            }
            else
            {
                // 不带P：原 N 寻址逻辑（255全局 / N==本机地址 / 缺N报错）
                if(addrRaw < 0)
                {
                    sendAnswer(1, "addr missing");
                    break;
                }
                uint8_t signedFeederAddr = (uint8_t)addrRaw;
                if(signedFeederAddr == 255)
                {
                    match = 1;
                    isGlobalQuery = 1;
                }
                else
                {
                    if(localAddr == signedFeederAddr)
                    {
                        match = 1;
                    }
                }
            }
            if(!match)
            {
                break;
            }

            FeederErrorState_t err = Feeder_GetErrorState(&feeders[0]);
            if(isGlobalQuery)
            {
                // 全局查询：双因子错峰发送（X55）
                // delay = addr_part + chip_part：
                //   addr_part = 配址0~63 addr*5ms（0~315ms）/ N99 固定 495ms —— 主错峰（不同地址拉开大间隔）
                //   chip_part = 3字节散列&0x0F ×5ms（0~75ms）—— 微调（同地址/重复地址也错开，
                //              上位机收到"一个addr两个chipid"→ 直接发现地址冲突）
                // 上位机收包窗口 600ms，按应答 addr:+ChipID 二元收集；异常槽位（乱码/半截/无干净行）→ P 点名确认
                uint16_t addrPart = (localAddr < 99) ? (uint16_t)(localAddr * 5) : 495U;
                uint16_t delayMs = addrPart + ((feeders[0].chip_id[0] ^ feeders[0].chip_id[1]
                                                ^ feeders[0].chip_id[2]) & 0x0F) * 5;
                g_asyncReply.pending = 1;
                g_asyncReply.triggerTick = millis() + delayMs;
                g_asyncReply.localAddr = localAddr;
                g_asyncReply.err = err;
                g_asyncReply.enabled = (feeders[0].state != FEEDER_STATE_DISABLED) ? 1 : 0;
                g_asyncReply.chipid[0] = feeders[0].chip_id[0];
                g_asyncReply.chipid[1] = feeders[0].chip_id[1];
                g_asyncReply.chipid[2] = feeders[0].chip_id[2];
                g_asyncReply.config_ver = feeders[0].settings.config_ver;
            }
            else
            {
                // ===== 单点查询：整帧先组好，一次切 DE 发出（X59）=====
                // 原三段 my_printf：每个 my_printf 内部各自 SetTxMode→DelayUs100→SetRxMode，
                // 段间 DE 回落把整行切成 3 个方向周期，主机 485 方向翻转后第二、三段被当碎片。
                // 现改为单缓冲顺序拼接三段，整行一次 SetTxMode 循环 putchar，发完统一切回接收。
                // SDCC varargs 上限≈15：三段各自 my_sprintf，单段实参不超限；buf 136B 容纳整行。
                static __xdata char buf[136];
                uint16_t off = 0;
                off += (uint16_t)my_sprintf(buf + off,
                    "ok error:%d M%d N%d A%d B%d C%d F%d U%d V%d W%d",
                    (int)err,
                    MCODE_UPDATE_FEEDER_CONFIG,
                    localAddr,
                    feeders[0].settings.full_advanced_angle,
                    feeders[0].settings.half_advanced_angle,
                    feeders[0].settings.retract_angle,
                    feeders[0].settings.feed_length,
                    feeders[0].settings.time_to_settle,
                    feeders[0].settings.motor_min_pulsewidth,
                    feeders[0].settings.motor_max_pulsewidth);
        #ifdef HAS_FEEDBACKLINES
                off += (uint16_t)my_sprintf(buf + off, " X%d T%d",
                    feeders[0].settings.ignore_feedback,
                    feeders[0].settings.n20_timeout_ms);
        #endif
                /* 单点 M602 也带 EN：短按使能/ESP32 重启后详情页也能拿到 en。
                 * 手写 5 字节，避免 my_sprintf 拉入浮点/长整型支持导致 ROM 溢出。 */
                buf[off++] = ' '; buf[off++] = 'E'; buf[off++] = 'N';
                buf[off++] = ':';
                buf[off++] = (feeders[0].state != FEEDER_STATE_DISABLED) ? '1' : '0';
                // FW 的 DD_BN 固定 4 位：202 -> 0202；my_sprintf 不支持 %04d，手动补前导零
                off += (uint16_t)my_sprintf(buf + off, " VER:%d FW:%d.",
                    feeders[0].settings.config_ver,
                    FW_YYMM);
                if (FW_DD_BN < 1000U) buf[off++] = '0';
                off += (uint16_t)my_sprintf(buf + off, "%d CHIP:%X%X%X\r\n",
                    FW_DD_BN,
                    feeders[0].chip_id[0],
                    feeders[0].chip_id[1],
                    feeders[0].chip_id[2]);

                RS485_SetTxMode();
                uint8_t i = 0;
                while (buf[i])
                {
                    putchar(buf[i++]);
                }
                SYS_DelayUs(100);
                RS485_SetRxMode();
            }
            break;
        }





        case MCODE_SERVO_SET_ANGLE: {
            if (feeders[0].state == FEEDER_STATE_DISABLED) {
                sendAnswer(1, "enable first!");
                break;
            }
           int16_t addrRaw = parseParameter('N', -1);
            char *chipidStr = parseParameterStr('P');   // 【X52】P 单点寻址
            uint8_t angle = (uint8_t)parseParameter('A', 90);
            // 原始参数校验（【X52】不再强制 N 必填）
            if(addrRaw != -1 && !validFeederAddr((uint8_t)addrRaw))
            {
                sendAnswer(1, "addr invalid");
                break;
            }
            // 【X52】广播(N255)与单点(P)互斥
            if(CheckBroadcastConflict(addrRaw, chipidStr)) break;
            if (angle > 180) {
                sendAnswer(1, "illegal angle");
                break;
            }
            // 【X52】统一寻址
            if(!CheckTargetFeeder(addrRaw, chipidStr)) break;
            Feeder_GotoAngle(&feeders[0], angle);
            sendAnswer(0, "angle set");
            break;
        }

        case MCODE_UPDATE_FEEDER_CONFIG: {
            // N=源地址，R=新地址，P=ChipID字符串
            int16_t srcAddrRaw = parseParameter('N', -1);
            int16_t newRs485AddrRaw = parseParameter('R', -1);
            char *chipidStr = parseParameterStr('P');
            uint8_t localAddr = feeders[0].settings.rs485_addr;
            uint8_t pass = 0;

            // srcAddrRaw是int16_t，先判断负数非法
            int8_t srcAddr = (int8_t)srcAddrRaw;
            if(srcAddrRaw != -1)
            {
                if(srcAddrRaw < 0 || !validFeederAddr((uint8_t)srcAddrRaw))
                {
                    sendAnswer(1, "srcAddr invalid");
                    break;
                }
            }
            // 【X52】广播(N255源地址)与单点(P)互斥
            if(CheckBroadcastConflict(srcAddrRaw, chipidStr)) break;

            if(chipidStr != NULL)
            {
                // 带P，校验ChipID
                if(Comm_MatchChipID(feeders[0].chip_id, chipidStr))
                {
                    if(srcAddrRaw == -1)
                    {
                    // P存在，无N：只要ID匹配，允许改配置
                        pass = 1;
                    }
                    else
                    {
                    // P+N都存在：ID匹配 AND 本机地址等于源地址
                        if(localAddr == (uint8_t)srcAddrRaw)
                        {
                            pass = 1;
                        }
                    }
                }
            }
            else
            {
                // 不带P，必须提供N，本机地址匹配N
                if(srcAddrRaw != -1 && localAddr == (uint8_t)srcAddrRaw)
                {
                    pass = 1;
                }
            }

            if(!pass)
            {
                sendAnswer(1, "Access denied");
                break;
            }

            FeederSettings_t oldSet;
            Feeder_GetSettings(&feeders[0], &oldSet);
            FeederSettings_t newSet;
            memcpy(&newSet, &oldSet, sizeof(FeederSettings_t));
            // 【X48】620 前后保持原使能状态：Feeder_Setup 内部强制 state=IDLE，
            //   disabled 状态设置后应保持 disabled（显示恢复慢闪、供电策略都不变）
            uint8_t prevState = feeders[0].state;

            if(newRs485AddrRaw != -1)
            {
                uint8_t newAddr = (uint8_t)newRs485AddrRaw;
                if(!validNewFeederAddr(newAddr))
                {
                    sendAnswer(1, "New R addr invalid");
                    break;
                }
                newSet.rs485_addr = newAddr;
            }

            // 其余参数
            newSet.full_advanced_angle  = (uint8_t)parseParameter('A', newSet.full_advanced_angle);
            newSet.half_advanced_angle  = (uint8_t)parseParameter('B', newSet.half_advanced_angle);
            newSet.retract_angle        = (uint8_t)parseParameter('C', newSet.retract_angle);
            newSet.feed_length          = (uint8_t)parseParameter('F', newSet.feed_length);
            newSet.time_to_settle       = (uint16_t)parseParameter('U', newSet.time_to_settle);
            newSet.motor_min_pulsewidth = (uint16_t)parseParameter('V', newSet.motor_min_pulsewidth);
            newSet.motor_max_pulsewidth = (uint16_t)parseParameter('W', newSet.motor_max_pulsewidth);
        #ifdef HAS_FEEDBACKLINES
            newSet.ignore_feedback      = (uint8_t)parseParameter('X', newSet.ignore_feedback);
            newSet.n20_timeout_ms       = (uint16_t)parseParameter('T', newSet.n20_timeout_ms);
        #endif

            Feeder_SetSettings(&feeders[0], &newSet);
            Feeder_SaveSettings(&feeders[0]);
            Feeder_Setup(&feeders[0]);
            feeders[0].state = prevState;   // 【X48】恢复原使能状态（Setup 强制 IDLE 被还原）
#ifdef SERVO_OFF_AFTER_MOVE
            if (prevState == FEEDER_STATE_DISABLED)
                Feeder_Servo_SetPower(0);   // 【X48】dis：舵机已在 retract（重写脉宽基本不动），断电消声
#endif
            // 【X47】设置反馈：快闪周期 250ms（与禁用 1s 慢闪明显区分，不混淆）；
            //   3s 后由 main 循环按【当前状态】恢复显示（disabled→1s慢闪 / enabled→常亮）
            g_dispRestorePending = 1;
            g_dispRestoreAt = millis();
            Seg_SetDisplay(DISP_ADDR, feeders[0].settings.rs485_addr, 3000, 250);

            sendAnswer(0, "config saved");
            break;
        }


        /* ----------------- ADC 模拟量采集指令 ----------------- */
        /* ----------------- ADC 模拟量采集指令 ----------------- */
#ifdef HAS_ANALOG_IN
        case MCODE_GET_ADC_RAW: {
            int16_t nRaw = parseParameter('N', -1);
            char *chipidStr = parseParameterStr('P');
            // N不为空时校验地址合法性
            if(nRaw != -1 && !validFeederAddr(nRaw))
            {
                sendAnswer(1, "addr invalid");
                break;
            }
            uint8_t runThisCmd = CheckTargetFeeder(nRaw, chipidStr);
            if(!runThisCmd)
            {
                break;
            }

            int8_t channel = (int8_t)parseParameter('A', -1);
            if (channel >= 0 && channel < NUMBER_OF_ADC_CHANNELS) {
                uint16_t raw = ADC_ReadRaw((uint8_t)channel);
                // OpenPnP 解析要求第一行必须是 "value:xxx"
                my_printf("value:%u\r\n", raw);
                sendAnswer(0, "value sent");
            } else {
                // 移除局部msg数组与my_sprintf，直接输出应答，节省栈内存
                my_printf("error invalid adc channel (0...%d)\r\n", NUMBER_OF_ADC_CHANNELS - 1);
            }
            break;
        }
        // ========== 浮点ADC分支注释掉，防止拉入浮点库 ==========
        /*
        case MCODE_GET_ADC_SCALED: {
            int16_t nRaw = parseParameter('N', -1);
            char *chipidStr = parseParameterStr('P', NULL);
            if(nRaw != -1 && !validFeederAddr(nRaw))
            {
                sendAnswer(1, "addr invalid");
                break;
            }
            uint8_t runThisCmd = CheckTargetFeeder(nRaw, chipidStr);
            if(!runThisCmd)
            {
                break;
            }
            int8_t channel = (int8_t)parseParameter('A', -1);
            if (channel >= 0 && channel < NUMBER_OF_ADC_CHANNELS) {
                float scaled = ADC_ReadScaled((uint8_t)channel);
                // 输出 4 位小数
                my_printf("value:%.4f\r\n", scaled);
                sendAnswer(0, "value sent");
            } else {
                sendAnswer(1, "invalid adc channel");
            }
            break;
        }
        case MCODE_SET_SCALING: {
            int16_t nRaw = parseParameter('N', -1);
            char *chipidStr = parseParameterStr('P', NULL);
            if(nRaw != -1 && !validFeederAddr(nRaw))
            {
                sendAnswer(1, "addr invalid");
                break;
            }
            uint8_t runThisCmd = CheckTargetFeeder(nRaw, chipidStr);
            if(!runThisCmd)
            {
                break;
            }
            int8_t channel = (int8_t)parseParameter('A', -1);
            if (channel >= 0 && channel < NUMBER_OF_ADC_CHANNELS) {
                // 获取当前缩放参数作为默认值
                // 注意：这里假设 ADC 模块有获取当前 slope/offset 的函数，
                // 或者直接传入新值。为简化，这里要求必须同时提供 S 和 O。
                float slope = parseParameter('S', 1.0f);
                float offset = parseParameter('O', 0.0f);
                
                ADC_SetScaling((uint8_t)channel, slope, offset);
                
                // TODO: 将缩放参数保存到 EEPROM
                // IAP_WriteBlock(EEPROM_ADC_SCALING_OFFSET + channel*8, ...);
                
                sendAnswer(0, "scaled");
            } else {
                sendAnswer(1, "invalid adc channel");
            }
            break;
        }
        */
#endif
        /* ----------------- 系统控制指令 ----------------- */
        case MCODE_FACTORY_RESET: {
            int16_t nRaw = parseParameter('N', -1);
            char *chipidStr = parseParameterStr('P');
            // N不为空时校验地址合法性
            if(nRaw != -1 && !validFeederAddr(nRaw))
            {
                sendAnswer(1, "addr invalid");
                break;
            }
            // 【X52】广播(N255)与单点(P)互斥
            if(CheckBroadcastConflict(nRaw, chipidStr)) break;
            uint8_t runThisCmd = CheckTargetFeeder(nRaw, chipidStr);
            if(!runThisCmd)
            {
                break;
            }

            // 【可选安全锁，如需禁止全局复位，取消下面这段注释】
            /*
            if(nRaw == 255)
            {
                sendAnswer(1, "reset bad");
                break;
            }
            */

            // 【X59】先应答后擦除：IAP 扇区擦除为 ms 级阻塞（IAP_WriteBlock 内部先擦整扇区
            // 再逐字节写），先擦后答会让主机等待超时、迟到应答被 485 方向切换当噪声丢弃。
            // 先回 ok 释放总线，再执行擦写；掉电兜底：擦写中途断电 → 扇区0 config_ver 为
            // 脏值(0xFF/半写) ≠ CONFIG_VERSION，下次上电 Config_CheckNeedInit 自动恢复出厂。
            sendAnswer(0, "reset");
            Feeder_FactoryReset(&feeders[0]);
            // 【X61】真正执行应答里的 "restart now"：
            //   X59 起 Feeder_FactoryReset 直接把出厂默认值写回 EEPROM（config_ver=CONFIG_VERSION），
            //   但原实现只写不重启 —— 数码管仍停在旧地址，必须人工断电再上电才变 99（用户反馈）。
            //   这里复刻 M603 的时序做软复位，让"恢复出厂"一次命令闭环：
            //   - EEPROM 已是出厂值 → 重启后 Config_CheckNeedInit 判定通过，直接加载默认地址 99，
            //     不会再擦写、也不会二次复位（不会形成复位循环）；
            //   - 扇区1 的兜底 ChipID 不在本次擦除范围，复位后 ChipID 保持不变；
            //   - 应答最后一个字节已由 putchar 轮询 TI 发完、DE 已切回接收态，再留 5ms 余量
            //     确保 485 收发器把停止位移出，复位不会截断应答；
            //   - SWBS=0 → 复位回用户程序区 0x0000（≠ M788 的 SWBS=1 进 ISP 引导区）。
            SYS_DelayUs(3000);
            // 【X62】统一改为 IAP_CONTR 整字写入（理由见 M788 与 config.h）：
            //   bit7 IAPEN=1 / bit6 SWBS=0 / bit5 SWRST=1 → 复位回用户程序区 0x0000。
            IAP_CONTR = IAP_CONTR_SWRST_TO_USER;
            while(1);                // 复位即将生效，不应返回
            break;
        }
        case MCODE_ISP_RESET: {
            // 在线升级入口：N/P 寻址匹配后应答，再软复位进 ISP 引导区等待上位机下载
            int16_t nRaw = parseParameter('N', -1);
            char *chipidStr = parseParameterStr('P');
            if(nRaw != -1 && !validFeederAddr((uint8_t)nRaw))
            {
                sendAnswer(1, "addr invalid");
                break;
            }
            // 【X52】广播(N255)与单点(P)互斥
            if(CheckBroadcastConflict(nRaw, chipidStr)) break;
            if(!CheckTargetFeeder(nRaw, chipidStr))
            {
                break;
            }
            SYS_DelayUs(3000);   // 等 ESP32 把 485 DE 切回接收，避免应答撞车
#if USE_IAP_BOOTLOADER
            /* 安全 flag：先写 REQUEST 并回读校验，失败则不复位，避免 BL 直接跳回 App */
            if (iap_flag_write_request(0, 0, FW_YYMM, FW_DD_BN) != 0) {
                sendAnswer(1, "flag write failed");
                break;
            }
#endif
            sendAnswer(0, "ISP reset");
            // 【X62】执行点确认：这一行走不到，说明卡在 sendAnswer 之前；
            //   走得到却没复位，说明 IAP_CONTR 写入未生效（先改 config.h 的
            //   IAP_CONTR_SWRST_TO_ISP=0x60 再试）。调试完可整行删除。
            my_printf("ISP: reset now\r\n");
            // 应答与上面这行都由 putchar 轮询 TI 发完（sendAnswer 末尾已有 100us 余量），
            // 这里只补 500us ≈ 5 个字节时间 @115200，足够 485 收发器把停止位移出并释放总线；
            // 原 5000us 是多余的长延时（期间主循环不跑，且可能掩盖真实问题）。
            SYS_DelayUs(3000);
            // STC8H：IAP_CONTR 整字写入 → 软复位进系统 ISP 监控程序区，
            //   复位后以 9600 等 0x7F 握手（STC 官方下载协议）。
            //   不用 IAP_SetRestartFrom()+IAP_SoftReset()：那两个宏是"读-改-写"，
            //   若 SWBS 位读回为 0 会把 SWBS 冲掉 → 复位回用户区，永远进不了 ISP。
#if USE_IAP_BOOTLOADER
            /* IAP Bootloader 模式：不再写 0x6A00 flag。
             * BL 每次启动都会先等 3 秒 HELLO；App 只需软复位到 0x0000。 */
            IAP_CONTR = IAP_CONTR_SWRST_TO_USER;   // 0xA0：复位回 0x0000 Bootloader
            while(1);
#else
            IAP_CONTR = IAP_CONTR_SWRST_TO_ISP;    // 0xE0：ROM ISP（旧路径）
            while(1); // 复位即将生效，不应返回
#endif
            break;
        }
        default:
            // 【X17修复·评审§2.3】错误应答用 error 前缀（0=ok 语义错误）
            sendAnswer(1, "unknown cmd");
            break;

    }
}

/* ========================================================================
 *  串口接收处理 (在主循环 while(1) 中不断调用)
 * ======================================================================== */
void listenToSerialStream(void)
{
    // 只要 UART 缓冲区有数据就持续读取
    while (UART_Available()) {
        char receivedChar = UART_Read();
#ifdef DEBUG
        putchar(receivedChar); // 回显调试
#endif
        if (receivedChar == '\n') {
            // 1. 去除注释 (截断 ';' 之后的内容)
            char *semicolon = my_strchr(inputBuffer, ';');
            if (semicolon != NULL) {
                *semicolon = '\0';
            }
            // 2. 去除首尾空白字符 (Trim)
            char *start = inputBuffer;
            while(*start == ' ' || *start == '\t' || *start == '\r') start++;

            char *end = start;
            uint16_t trimGuard = MAX_BUFFFER_MCODE_LINE;
            while(*end && trimGuard--) end++;
            while(end > start && (*(end-1) == ' ' || *(end-1) == '\t' || *(end-1) == '\r')) end--;
            *end = '\0';

            uint8_t lineLen = end - start;
            // 3. 如果去除空白后还有内容，则执行解析
            if(lineLen > 0)
            {
                if (start != inputBuffer) {
                    memmove(inputBuffer, start, lineLen + 1);
                }
                // 【X58】总线保护延时：命令帧收齐后、解析执行前，先等主机 DE 释放 / 方向切换 /
                // 总线电平稳定，再即时应答，避免应答首字节被主机方向切换窗口吞掉。
                // 落点在命令入口而非应答通路：M602 N255 全局错峰与 M600 周期完成应答
                // 距命令已远、总线早已空闲，不经此延时，不挤压 5ms 错峰槽位。
                SYS_Delay(RS485_REPLY_GUARD_MS);
                processCommand();
            }
            // 4. 清空缓冲区，准备接收下一行
            bufLen = 0;
            // memset(inputBuffer, 0, sizeof(inputBuffer));
            
        } else if (receivedChar != '\r') {
            // 忽略 '\r'，只追加有效字符到缓冲区，防止溢出
            if (bufLen < MAX_BUFFFER_MCODE_LINE - 1) {
                inputBuffer[bufLen++] = receivedChar;
                inputBuffer[bufLen] = '\0'; // 保持字符串结束符
            }
        }
    }
}


