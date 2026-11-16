# DD-Feeder-for-LumenPNP-openPNP

DD Feeder Controller / DD 飞达控制器  
64-Channel RS-485 SMT Feeder Controller / 64 路 RS-485 SMT 飞达控制器

## Firmware / 固件

Current release / 当前版本：

| Device / 设备 | Version / 版本 | File / 文件 |
|---|---|---|
| ESP32 controller / 控制器 | 26111603 | [`firmware/esp32/DDFC_26111603_OTA.bin`](firmware/esp32/DDFC_26111603_OTA.bin) |
| ESP32 web pages / 页面 | 2026111615b4f4e4 | [`firmware/esp32/DDFC_2026111615b4f4e4_FS.bin`](firmware/esp32/DDFC_2026111615b4f4e4_FS.bin) |
| Feeder / 飞达 | 26111603 | [`firmware/feeder/DDF_26111603_APP.hex`](firmware/feeder/DDF_26111603_APP.hex) |

ESP32 升级需要同时更新 **OTA app** 和 **FS 页面**。  
ESP32 upgrade requires **both** the OTA app and the FS image.

Feeder 使用 STC-ISP / 485 IAP 升级 APP。  
The feeder APP is programmed by STC-ISP or 485 IAP.

## Changelog / 更新记录

See [CHANGELOG.md](CHANGELOG.md).

## SHA-256

```text
1ad4788ea379f4de3500bd2838dd65abfa0e1bebb084b1e15a32c96451757344  DDFC_2026111615b4f4e4_FS.bin
8abf82c8f7a6db345a5a049b718308e4ae578c2b06960c055154008b17f2dd1c  DDFC_26111603_OTA.bin
0e2894d875d1aebdcd89c59374c7417daeafcf0aa87e0e94c9fa9927f7514ef0  DDF_26111603_APP.hex
```

## Hardware notes / 硬件说明

- ESP32-WROOM-32E, 4 MB flash
- 485 bus power switch: GPIO21
- nFAULT: GPIO13
- Calibration limits: GPIO32 / GPIO33
- Service key: GPIO14 (short = 485 power, double = rescan)
- Factory reset: GPIO34, hold 5 s
- Buzzer: GPIO25
