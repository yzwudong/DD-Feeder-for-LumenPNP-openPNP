# H1K28 IAP / Bootloader / EEPROM 最小验证

> 独立测试工程，**不修改原 `src/` 应用**。
>
> **2026-10-27 实测通过**（USB-TTL，115200 8N1）：
>
> ```text
> BOOT TEST
> IAP 0x6A00 OK
> EEPROM 0x6C00 OK
> JUMP APP
> APP OK @0x1800
> APP READ 0x6A00 OK
> ```
>
> 说明 EEPROM=28K 下 IAP 写程序 Flash、Boot 跳 App、MOVC 回读均可行。

> 目标：验证三件事
> 1. EEPROM=28K 时，IAP 能否擦写 0x6A00（程序 Flash 区）；
> 2. EEPROM 0x6C00 能否独立读写；
> 3. Bootloader 0x0000 能否跳转到 App 0x1800，App 能否运行并 MOVC 读回 IAP 写入的数据。

---

## 1. 前提

- 芯片：STC8H1K28；
- STC-ISP：**EEPROM 大小 = 28K**；
- 这是测试板操作：设 28K 会改变 IAP 地址映射，**不要在生产飞达上直接做**；
- 测试只碰：
  - `0x6A00`（IAP 测试/标志区）
  - `0x6C00`（EEPROM 测试）
  - 不碰 `0x0000–0x0FFF`（Boot）和 `0x1800–0x69FF`（App 代码）。

---

## 2. 编译

```powershell
powershell -ExecutionPolicy Bypass -File iap_test/build_test.ps1
```

输出：

```text
iap_test/build/boot.hex       0x0000–0x0FFF
iap_test/build/app.hex        0x1800–0x69FF
iap_test/build/combined.hex   Boot + App 合并（推荐烧录）
```

App 链接必须带 `-Wl-bHOME=0x1800`，否则 0x1800 的复位向量会跳到 0x004E（Boot 区）。`build_test.ps1` 已包含这个参数。

当前大小：

- Boot：约 963B / 4KB；
- App：约 698B / 23KB；
- combined：约 1651B 数据。

---

## 3. 烧录

### 3.1 STC-ISP 设置

- MCU：STC8H1K28
- **EEPROM 大小：28K**
- IRC：24.000MHz
- P3.2/P3.3 下载选项：不勾选
- 如果用 USB-485：RS485 控制引脚 P3.6，勾选“本次使用RS485进行控制下载”

### 3.2 烧 `combined.hex`（推荐）

1. 全片擦除；
2. 打开 **`iap_test/build/combined.hex`**；
3. 确认 EEPROM=28K、24MHz；
4. 一次下载。

> **不要分两次烧 app.hex**：STC-ISP 只烧 app 时，0x0000–0x0FFF 没有记录，会被填成 0xFF，Bootloader 就没了。  
> `combined.hex` 包含 Boot 0x0000 和 App 0x1800，一次写入两者。

如果仍要分两次：先烧 boot.hex，再烧 app.hex 时**取消擦除**；但 STC-ISP 仍可能填充 0x0000–0x0FFF，所以优先用 combined。

---

## 4. 串口观察

UART1：P3.0/P3.1，**115200 8N1**；或 485 总线。

### 4.1 完全没反应：先烧 `uart.hex`

```powershell
powershell -ExecutionPolicy Bypass -File iap_test/build_uart.ps1
```

`iap_test/build/uart.hex` 在 **0x0000**，只做：

- 初始化 P3.6 DE=0；
- UART1 115200 8N1；
- 循环打印 `UART OK 115200 24MHz`。

烧录条件：

- **EEPROM=1K**（不要 28K，避免干扰）；
- 24MHz；
- 全片擦除，只烧 `uart.hex`；
- USB-TTL **TXD→P3.0、RXD→P3.1、GND 共地**；
- 串口助手 115200 8N1。

结果判断：

| 结果 | 说明 |
|---|---|
| 能看到 `UART OK` | 芯片/时钟/UART/接线正常，问题在 Boot/App 或 combined |
| 完全没输出 | 查 TX/RX 是否交叉、24V、GND、P3.0/P3.1、24MHz 选项 |
| 乱码 | IRC 不是 24MHz；STC-ISP 重写 24.000MHz |
| 只有上电一瞬间乱码 | 复位/启动向量或波特率问题 |

> 如果 UART 最小程序都没输出，不要继续查 Bootloader；先解决硬件/时钟/接线。

正常 Boot + App 输出：

```text
BOOT TEST
IAP 0x6A00 OK
EEPROM 0x6C00 OK
JUMP APP

APP OK @0x1800
APP READ 0x6A00 OK
```

含义：

| 输出 | 证明 |
|---|---|
| `IAP 0x6A00 OK` | EEPROM=28K 时 IAP 能擦写程序 Flash |
| `EEPROM 0x6C00 OK` | IAP 能访问 0x6C00 EEPROM |
| `JUMP APP` | Boot 执行到跳转 |
| `APP OK @0x1800` | App 链接/运行在 0x1800 |
| `APP READ 0x6A00 OK` | MOVC 能读回 IAP 写入的程序区数据 |

---

## 5. 失败排查

| 现象 | 可能原因 |
|---|---|
| 只有 `BOOT TEST`，IAP FAIL | EEPROM 没设 28K；IAP_TPS 不是 24；IAP 地址映射不对 |
| 只有 `BOOT TEST`，EEPROM FAIL | 0x6C00 不在 IAP 范围，或扇区被擦 |
| 没有 `APP OK` | App 没烧到 0x1800；或 Boot 跳转失败 |
| `APP READ 0x6A00 FAIL` | MOVC 地址/写入地址不一致；或 IAP 写入未生效 |
| 串口乱码 | IRC 不是 24MHz；重新用 STC-ISP 写 24MHz 选项 |

---

## 6. 测试后恢复

1. STC-ISP 恢复 **EEPROM=1K**；
2. 重新烧回正式 Boot/App；
3. 或直接烧回原 X62 固件，恢复 24MHz 选项和 1K EEPROM。

**注意**：设 28K 后 IAP 地址=物理地址，原 App 的 1K 配置偏移会错位；测试完必须恢复 1K 或迁移配置。



