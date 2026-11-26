/*
 * main_updater_example.c - ESP32 端 ISP 在线升级集成示例（ESP-IDF）
 *
 * 功能：通过 Web（或任意来源）拿到 .hex 文本 → 解析 → 让飞达进 ISP → 下载固件
 *
 * 接线（ESP32 UART2 直连飞达板 TTL 串口，跨接 485 芯片未焊时直接连 MCU 引脚）：
 *   ESP32 GPIO17(TX2)  → 飞达板 RXD
 *   ESP32 GPIO16(RX2)  → 飞达板 TXD
 *   GND 共地
 * 若走 RS485 总线：另接 SP3485，ESP32 GPIO 控制 DE，且升级时其他飞达需断电隔离
 * （STC ISP 握手是全总线广播，同一时刻只允许一块板子在线）
 *
 * 本示例只给出核心流程；Web 服务（HTTP 上传 / WebSocket / 管理页）按项目需要接入，
 * 上传得到 hex 文本后调用 run_isp_update(hex_text, hex_len, board_addr, force_downgrade)
 * 即可：board_addr=255 跳过板上版本比对（未配址新板/离线），force_downgrade=1 允许
 * 包旧于板上时强制降级（Web 端人工确认后再传 1）。
 *
 * 依赖：本目录 stc_isp.c / stc_isp.h / intel_hex.c / intel_hex.h
 * 编译：把 4 个文件加入 CMakeLists 的 SRCS 与 INCLUDE_DIRS
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "stc_isp.h"
#include "intel_hex.h"

#define TAG "updater"

#define UART_PORT       UART_NUM_2
#define UART_TX_PIN     17
#define UART_RX_PIN     16
#define UART_BUF_SIZE   1024

/* ---- 平台回调：对接 ESP32 UART ---- */

static void port_write(const uint8_t *buf, uint32_t len)
{
    uart_write_bytes(UART_PORT, buf, len);
}

static int port_read(uint8_t *buf, uint32_t len, uint32_t timeout_ms)
{
    return uart_read_bytes(UART_PORT, buf, len, pdMS_TO_TICKS(timeout_ms));
}

static void port_set_baud(uint32_t baud)
{
    uart_set_baudrate(UART_PORT, baud);
}

static void port_set_parity(int parity)
{
    uart_parity_t p = (parity == 1) ? UART_PARITY_EVEN : UART_PARITY_DISABLE;
    uart_set_parity(UART_PORT, p);
}

static void port_delay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

static StcIspPort_t g_port = {
    .write      = port_write,
    .read       = port_read,
    .set_baud   = port_set_baud,
    .set_parity = port_set_parity,
    .delay_ms   = port_delay,
};

/* ---- 进度回调（可选，用于 Web 端推送进度） ---- */

static int on_progress(uint32_t done, uint32_t total)
{
    ESP_LOGI(TAG, "progress %lu/%lu", (unsigned long)done, (unsigned long)total);
    return 0; /* 返回非 0 可中止升级 */
}

/* ---- 升级决策返回码（与 STC_ISP_* 的 -1~-5 错开） ---- */
#define UPD_ERR_PARSE_HEX    (-20)  /* hex 解析失败 */
#define UPD_ERR_PKG_NO_VER   (-21)  /* 包内无 DDFW 静态版本标识（旧格式包，拒绝） */
#define UPD_ERR_PKG_VER_BAD  (-22)  /* 包版本标识格式/范围非法 */
#define UPD_ERR_QUERY_BOARD  (-23)  /* 板上版本查询超时或应答不含 FW: */
#define UPD_ERR_DOWNGRADE    (-24)  /* 包旧于板上且未允许强制降级 */
#define UPD_ERR_VERIFY_FAIL  (-25)  /* 下载复位后回校版本与包版本不一致 */
#define UPD_ALREADY_LATEST  (1)     /* 版本相等，跳过升级（非错误） */

/* 小整数转十进制字符串（不引 stdio），返回写入长度 */
static uint8_t u16_to_dec(char *p, uint16_t v)
{
    char tmp[5]; uint8_t k = 0, len;
    if (v == 0) { p[0] = '0'; return 1; }
    while (v) { tmp[k++] = (char)('0' + (v % 10)); v /= 10; }
    len = k;
    while (k--) *p++ = tmp[k];
    return len;
}

/* ---- 正常总线查询板上固件版本（进 ISP 之前 / 下载复位之后回校均使用） ----
 * 发 M602 N<addr>，从单点应答行解析 FW:<yymm>.<ddbn>。
 * 返回 0=读到版本；-1=超时或应答不含合法 FW: 字段。 */
static int query_board_fw_ver(uint8_t addr, HexFwVer_t *out)
{
    char cmd[20];
    char line[256];
    int  n = 0;
    int  i, rc;
    uint8_t b;
    uint32_t i0;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(300);

    uart_flush_input(UART_PORT);
    cmd[0] = 'M'; cmd[1] = '6'; cmd[2] = '0'; cmd[3] = '2';
    cmd[4] = ' '; cmd[5] = 'N';
    i = 6 + u16_to_dec(cmd + 6, addr);
    cmd[i++] = '\r'; cmd[i++] = '\n';
    uart_write_bytes(UART_PORT, cmd, (size_t)i);

    while ((int32_t)(xTaskGetTickCount() - deadline) < 0) {
        int r = uart_read_bytes(UART_PORT, &b, 1, pdMS_TO_TICKS(50));
        if (r == 1) {
            if (b == '\n') break;
            if (b != '\r' && n < (int)sizeof(line) - 1) line[n++] = (char)b;
        }
    }
    line[n] = '\0';

    for (i0 = 0; i0 + 3 <= (uint32_t)n; i0++) {
        if (line[i0] == 'F' && line[i0 + 1] == 'W' && line[i0 + 2] == ':') {
            rc = intel_hex_parse_ver_text((uint8_t *)line + i0 + 3,
                                          (uint32_t)n - i0 - 3, out);
            return (rc == 0) ? 0 : -1;
        }
    }
    return -1;
}

/* ---- 初始化 UART2（正常通信与 ISP 共用） ---- */

static void uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, -1, -1);
    uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
}

/* ---- 让飞达进入 ISP 引导区 ----
 * 方式 A（推荐）：在正常 115200 下给飞达发 M788（本工程固件已实现软复位进 ISP；原 M997）
 * 方式 B（兜底）：GPIO 控制飞达电源，断电→上电（需板子 P3.2/P3.3 为低进 ISP）
 * 方式 B 示例：gpio_set_level(电源GPIO, 0); vTaskDelay(200ms); gpio_set_level(电源GPIO, 1);
 */
static void enter_isp(void)
{
    const char *cmd = "M788\r\n";
    uart_set_baudrate(UART_PORT, 115200);
    uart_set_parity(UART_PORT, UART_PARITY_DISABLE);
    uart_write_bytes(UART_PORT, cmd, strlen(cmd));
    vTaskDelay(pdMS_TO_TICKS(200)); /* 等板子应答+复位 */
}

/* ---- 总入口：hex 文本 → 版本比对 → 下载固件 → 回校版本 ----
 * @param hex_text      Web 上传得到的 hex 文本
 * @param hex_len       文本长度
 * @param board_addr    板上单点地址（0~63 / 99）；传 255 跳过板上版本比对
 *                      （新板未配址或离线场景）
 * @param force_downgrade 0=包旧于板上默认拒绝；1=允许强制降级（Web 端人工确认后传 1）
 * @return 0=升级成功并回校一致；UPD_ALREADY_LATEST=版本相等跳过；负值=失败
 */
int run_isp_update(const char *hex_text, size_t hex_len, uint8_t board_addr, int force_downgrade)
{
    HexImage_t img;
    HexFwVer_t pkg_ver, board_ver;
    int rc, cmp;

    if (intel_hex_parse(hex_text, hex_len, &img) != 0) {
        ESP_LOGE(TAG, "hex parse failed");
        return UPD_ERR_PARSE_HEX;
    }
    ESP_LOGI(TAG, "firmware size %lu bytes (0x%04lX~0x%04lX)",
             (unsigned long)img.size,
             (unsigned long)img.base, (unsigned long)img.end);

    /* 1) 解析包内静态版本：镜像 code 段扫描 "DDFW:" 前缀 */
    rc = intel_hex_read_fw_ver(&img, &pkg_ver);
    if (rc != 0) {
        ESP_LOGE(TAG, "package version tag missing/invalid (%d)", rc);
        return (rc == -1) ? UPD_ERR_PKG_NO_VER : UPD_ERR_PKG_VER_BAD;
    }
    ESP_LOGI(TAG, "package FW: %u.%u", pkg_ver.yymm, pkg_ver.ddbn);

    /* 2) 正常 115200 总线读板上当前版本（M602 单点查询） */
    if (board_addr != 255) {
        if (query_board_fw_ver(board_addr, &board_ver) != 0) {
            ESP_LOGE(TAG, "query board FW failed, addr=%u", board_addr);
            return UPD_ERR_QUERY_BOARD;
        }
        ESP_LOGI(TAG, "board   FW: %u.%u", board_ver.yymm, board_ver.ddbn);

        /* 3) 版本决策：包>板升级；相等跳过；包<板默认拒绝，强制降级需 force_downgrade=1 */
        cmp = intel_hex_ver_cmp(&pkg_ver, &board_ver);
        if (cmp == 0) {
            ESP_LOGI(TAG, "already latest, skip update");
            return UPD_ALREADY_LATEST;
        }
        if (cmp < 0 && !force_downgrade) {
            ESP_LOGE(TAG, "package older than board, downgrade rejected");
            return UPD_ERR_DOWNGRADE;
        }
    }

    /* 4) 进入 ISP 引导区并下载 */
    enter_isp();
    rc = stc_isp_download(&g_port, img.data + img.base, img.size, on_progress);
    if (rc != STC_ISP_OK) {
        ESP_LOGE(TAG, "ISP download failed, err=%d", rc);
        return rc;
    }

    /* 5) 下载结束后芯片复位：stc_isp 流程内停在 115200/EVEN，先切回 115200/8N1，
          等固件完成启动序列（舵机归位 1s + 上报延时）后回校版本必须等于包版本 */
    g_port.set_baud(115200);
    g_port.set_parity(0);
    g_port.delay_ms(1500);

    if (board_addr != 255) {
        if (query_board_fw_ver(board_addr, &board_ver) != 0 ||
            board_ver.yymm != pkg_ver.yymm || board_ver.ddbn != pkg_ver.ddbn) {
            ESP_LOGE(TAG, "post-update version verify failed");
            return UPD_ERR_VERIFY_FAIL;
        }
    }
    ESP_LOGI(TAG, "ISP download OK, version verified");
    return 0;
}
