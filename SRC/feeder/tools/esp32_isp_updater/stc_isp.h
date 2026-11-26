/*
 * stc_isp.h - STC8 系列 ISP 在线下载模块（上位机侧）
 *
 * 适用芯片：STC8H1K28（本项目飞达板），协议与 tools/stcflash.py 的 STC8 分支一致
 * 运行环境：ESP32 / 任意带 UART 的 MCU（底层串口通过回调接入）
 *
 * 协议要点（STC8 系列，与 stcflash.py 对照）：
 *   - 起始 9600bps + 偶校验(EVEN)，发 0x7F 等待 46 B9 68 应答帧
 *   - 波特率协商：0x8F 测试 → 0x8E 确认 → 切 115200（8N1? 仍偶校验）
 *   - 擦除：0x05(读序列号) → 0x03(擦除)
 *   - 写固件：128 字节/帧，首帧 cmd 0x22，后续 cmd 0x02
 *   - 结束：0xFF
 *
 * 帧格式（主机→芯片）：46 B9 6A | N(2B) | cmd | dat | chksum(2B) | 16
 * 帧格式（芯片→主机）：46 B9 68 | N(2B) | cmd | dat | chksum(2B) | 16
 * 校验和：从帧头第 3 字节(6A/68)起累加，chkmode=2（2 字节校验和，偶校验）
 */
#ifndef STC_ISP_H
#define STC_ISP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 平台串口回调（由使用者实现，对接 ESP32 UART / RS485 DE 控制） */
typedef struct {
    /* 发送 len 字节（阻塞） */
    void (*write)(const uint8_t *buf, uint32_t len);
    /* 读取 len 字节，返回实际读取字节数；timeout_ms 为等待总超时 */
    int  (*read)(uint8_t *buf, uint32_t len, uint32_t timeout_ms);
    /* 切换波特率 */
    void (*set_baud)(uint32_t baud);
    /* 设置校验：0=无校验，1=偶校验(EVEN) */
    void (*set_parity)(int parity);
    /* 阻塞延时 ms */
    void (*delay_ms)(uint32_t ms);
} StcIspPort_t;

/* 下载进度回调：done=已写入字节，total=固件总长；返回非 0 可中止 */
typedef int (*StcIspProgress_t)(uint32_t done, uint32_t total);

/* 错误码 */
#define STC_ISP_OK           0
#define STC_ISP_ERR_TIMEOUT  (-1)   /* 握手/应答超时 */
#define STC_ISP_ERR_BAUD     (-2)   /* 波特率协商失败 */
#define STC_ISP_ERR_ERASE    (-3)   /* 擦除失败 */
#define STC_ISP_ERR_WRITE    (-4)   /* 写入失败 */
#define STC_ISP_ERR_PARAM    (-5)   /* 参数非法 */

/**
 * @brief 执行一次完整 ISP 下载（9600 握手 → 115200 → 擦除 → 写入 → 结束）
 * @param port   平台回调（必须全部实现）
 * @param bin    固件二进制（Intel HEX 解析后的结果，见 intel_hex.h）
 * @param len    bin 长度
 * @param progress 进度回调（可传 NULL）
 * @return STC_ISP_OK 或错误码
 * @note 调用前请确保芯片已处于 ISP 引导区：
 *        方式一：上位机先发 M788（本工程固件已实现软复位进 ISP；原 M997）
 *        方式二：断电重启且 P3.2/P3.3 为低（冷启动进 ISP）
 */
int stc_isp_download(StcIspPort_t *port,
                     const uint8_t *bin, uint32_t len,
                     StcIspProgress_t progress);

#ifdef __cplusplus
}
#endif

#endif /* STC_ISP_H */
