# Bootloader 骨架（STC8H1K28）

> **2026-10-27 结论：H1K28 可行，但需 EEPROM=28K。**
> H1K28 是特殊型号：**程序空间 28K，整个 28K 均可运行程序**；
> 手册 19.3（PDF 第 1233 页）明确：即使把最后 N K 划为 EEPROM，**这 N K 仍可运行程序**。
> 因此设 EEPROM=28K 后，IAP 覆盖 0x0000–0x6FFF，Bootloader 可写 0x1800–0x69FF 的 App，App 仍从 0x1800 运行。
> 代价：IAP 地址=物理地址，配置区必须从 0x0000 迁到 **0x6C00**、兜底 ID 迁到 **0x6E00**。
> **2026-10-27 实测通过**：`iap_test/DDF_<yymmddnn>_FULL.hex` 已输出
> `BOOT TEST / IAP 0x6A00 OK / EEPROM 0x6C00 OK / JUMP APP / APP OK @0x1800 / APP READ 0x6A00 OK`，
> 证明 H1K28 + EEPROM=28K 下 IAP 写程序 Flash、Boot 跳 App 可行。
> 下一步是把 `boot_iap.c` 的 TODO 和正式协议补全；**当前 bootloader 仍是骨架，不要直接上生产**。

> 这是**独立工程骨架**，不参与现有 `src/` App 编译。
> 设计文档：`../docs/IAP-Bootloader分区升级设计.md`

## 目录

```text
bootloader/
  README.md
  src/
    boot_config.h     地址、帧常量（不重复定义 P3.6）
    boot_main.c       0x0000 启动、读标志、跳 App
    boot_uart.c/.h    UART1 115200 8N1 轮询 + DE 收发
    boot_flag.h/.c    0x6A00 升级标志读写
    boot_iap.h/.c     扇区擦除/128B 写 Flash（TODO）
    boot_proto.h/.c   HELLO + ACK（第一步）；ERASE/DATA 后续
    boot_vectors.asm  0x0000 中断跳转表 -> App 0x1800
```

## P3.6 不重复定义

Bootloader 直接：

```c
#include "../src/config.h"
...
RS485_DE_SET_PP_MODE();
RS485_SetRxMode();
```

DE 端口/引脚只在 `src/config.h` 定义一次；Bootloader 不另写 `P3.6`。

## 第一步已实现

- 0x0000 启动；
- 复用 `config.h` 初始化 P3.6 DE=0；
- UART1 P3.0/P3.1，115200 8N1，轮询；
- 读 0x6A00 标志；
- 无请求等 1s、有请求等 3s，收 HELLO；
- 回 HELLO_ACK；
- `LJMP 0x1800` 跳 App。

**未实现**：ERASE/DATA/CRC32/REBOOT、写 App Flash。

## 编译/链接状态

2026-10-27 用 SDCC 4.6：

```bash
sdas8051 -plosgff -o boot_vectors.rel boot_vectors.asm

sdcc -mmcs51 --iram-size 256 --xram-size 1024 \
     --code-loc 0x0100 --code-size 0x1800 --stack-auto \
     boot_main.rel boot_uart.rel boot_flag.rel boot_iap.rel \
     boot_proto.rel boot_vectors.rel \
     -o bootloader.ihx
```

- Boot C 代码从 **0x0100** 开始；
- `boot_vectors.rel` 在 **0x0000** 放复位/中断跳转表；
- 实测 code 0x0100–0x14B6，**5046B / 6144B**，剩余约 1KB；
- App 从 **0x1800** 开始。

## EEPROM=28K 设置（写 App 的前提）

H1K28 是特殊型号：**程序空间 28K，整个 28K 均可运行程序**。
手册 19.3（PDF 第 1233 页）明确：把最后 N K 划为 EEPROM 后，**这 N K 仍可运行程序**。

所以：

1. STC-ISP 把 **EEPROM 设为 28K**；
2. IAP 地址 `0x0000–0x6FFF` 覆盖全 Flash；
3. Bootloader 可写 `0x1800–0x69FF` 的 App；
4. App 仍从 0x1800 运行；
5. IAP 地址=物理地址，配置区要从 0x0000 迁到 **0x6C00**、兜底 ID 迁到 **0x6E00**。

**未完成 28K 设置 + 配置迁移前，不要打开 `USE_IAP_BOOTLOADER=1`。**

## 链接要求

- Bootloader C：`--code-loc 0x0100 --code-size 0x1800`（6KB；0x0000-0x00FF 为向量表）
- App：`--code-loc 0x1800 --code-size 0x5200`（20,992B）
- 升级标志：`0x6A00–0x6BFF`（512B）
- 配置区：`0x6C00–0x6DFF`，Bootloader 不要碰
- 兜底 ID/ROM 拷贝：`0x6E00–0x6FFF`，Bootloader 不要碰

## 首次烧录

Bootloader 首次必须用 **STC-ISP + USB-TTL 直连 P3.0/P3.1**：

- 24.000MHz 选项必须写入；
- P3.2/P3.3 下载选项保持不勾选；
- 不要用 `stc8isp.py` / `stcflash.py`（不写选项区，会回 11.0592MHz）。

## 中断向量

`boot_vectors.asm` 只是骨架示意。实际工程需要：

- 0x0000：`LJMP boot_main`
- 0x0003/0x000B/...：`LJMP 0x1003/0x100B/...`

Bootloader 自身协议用轮询，不开中断，避免和 App 向量冲突。

## 当前状态

- 代码是骨架，`boot_iap.c` 的擦除/写入和 `boot_proto.c` 的帧收发是 TODO；
- 默认 App 的 `USE_IAP_BOOTLOADER=0`，不会走这里；
- 实现并测试 HELLO/跳转后，再打开 App 的宏。





