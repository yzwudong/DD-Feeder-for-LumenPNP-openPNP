#include <stdarg.h>
#include <my_sprintf.h>
#include "uart.h"
#include <stdint.h>
#include "my_sprintf.h"

// 输出2位大写十六进制，写入p，返回写完后的指针
static char* hex2str(char *p, uint8_t val)
{
    // 【X40压缩】去 const 查表（省 17B flash），改算术转换
    uint8_t hi = (val >> 4) & 0x0F;
    uint8_t lo = val & 0x0F;
    *p++ = (char)(hi < 10U ? '0' + hi : 'A' + hi - 10U);
    *p++ = (char)(lo < 10U ? '0' + lo : 'A' + lo - 10U);
    return p;
}

static char* num2str(char *p, int16_t num, uint8_t isUnsigned)
{
    static __xdata char tmp[6];   // static xdata：不占栈、不占 data 段（my_printf 深层调用链+ISR打断时防栈溢出）
    uint8_t i = 0;
    uint16_t n;

    if (!isUnsigned && num < 0)
    {
        *p++ = '-';
        n = (uint16_t)(-num);
    }
    else
    {
        n = (uint16_t)num;
    }

    do
    {
        uint16_t rem = 0;   // 【X18修复·评审】uint8_t→uint16_t：减10计数 rem=floor(n/10) 可>255，
                            // FW_YYMM=2610 计数261次截断为5 → 打印"50"（M602 FW:50.1500 的根因）。
                            // 凡 %d/%u ≥2560 都会打印错，uint16_t 覆盖 0~65535。
        while(n >= 10U)
        {
            n -= 10U;
            rem++;
        }
        tmp[i++] = '0' + n;
        n = rem;
    } while(n);

    while (i--)
        *p++ = tmp[i];
    return p;
}

int my_vsprintf(char *str, const char *fmt, va_list ap)
{
    char *p = str;
    const char *s = fmt;
    char ch;
    uint16_t cnt = 0;
    while ((ch = *s++))
    {
        if (cnt >= 128) break;   // 【防御】输出长度上限：防格式化超长写越界破坏 xdata 邻居
        if (ch == '%')
        {
            ch = *s++;
            switch (ch)
            {
                case 'd':
                case 'u':   // 【X40压缩】%d/%u 合并：共用 num2str，仅 isUnsigned 不同
                {
                    int val = va_arg(ap, int);
                    char *p0 = p;
                    p = num2str(p, (int16_t)val, (ch == 'u'));
                    cnt += (uint16_t)(p - p0);
                    break;
                }
                // 【X35压缩】%s 分支全项目无调用（grep 确认仅头文件注释提到），删除省代码
                case 'X':    // 新增：2位大写十六进制
                {
                    int val = va_arg(ap, int);
                    p = hex2str(p, (uint8_t)val);
                    cnt += 2;
                    break;
                }
                // 【X40压缩】%% 分支全项目无调用（grep 确认），删除；default 会输出 '%' 行为等价
                default:
                    *p++ = ch;
                    cnt++;
                    break;
            }
        }
        else
        {
            *p++ = ch;
            cnt++;
        }
    }
    *p = '\0';
    return (int)(p - str);
}
// 【X40压缩】my_printf_uart 全项目无调用点（grep 确认），删除省 flash

// 本地组帧：varargs → va_list 透传给 my_vsprintf，不操作 RS485 DE（供多段拼整帧用）
int my_sprintf(char *str, const char *fmt, ...)
{
    va_list ap;
    int len;
    va_start(ap, fmt);
    len = my_vsprintf(str, fmt, ap);
    va_end(ap);
    return len;
}

// 带RS485 DE控制的打印函数，替代上面直接putchar的my_printf
void my_printf(const char *fmt, ...)
{
    static __xdata char buf[160];   // 160：同上，X15修复32字节缓冲越界（M602段47B>32B，my_vsprintf按128上限写→越界破坏xdata邻居→乱码+CHIP:FFFFFF）
    va_list ap;
    va_start(ap, fmt);
    my_vsprintf(buf, fmt, ap);
    va_end(ap);

    RS485_SetTxMode();   // 总线切发送
    uint8_t i = 0;
    while (buf[i])
    {
        putchar(buf[i++]);
    }
    SYS_DelayUs(100);    // 等待最后一个bit发送完成
    // 【X16】删除发送后 RI=0：理由同 gcode.c sendAnswer（误清命令首字节）
    RS485_SetRxMode();   // 切回接收
}
