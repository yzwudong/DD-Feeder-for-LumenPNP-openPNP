# DD Feeder Controller — Specifications / 规范文档

> 64-Channel RS-485 SMT Feeder Controller / 64 路 RS-485 SMT 飞达控制器

Bilingual manuals. 中文与英文版本并列。

| Document | 中文 | English |
|---|---|---|
| User manual | [user-manual.zh.md](user-manual.zh.md) | [user-manual.en.md](user-manual.en.md) |
| Technical manual | [technical-manual.zh.md](technical-manual.zh.md) | [technical-manual.en.md](technical-manual.en.md) |
| Hardware interface | [hardware-interface.zh.md](hardware-interface.zh.md) | [hardware-interface.en.md](hardware-interface.en.md) |
| Flashing guide | [flashing-guide.zh.md](flashing-guide.zh.md) | [flashing-guide.en.md](flashing-guide.en.md) |

## Firmware File Names / 固件文件名

| Product / 产品 | Pattern / 规则 | Example / 示例 |
|---|---|---|
| Main Controller full image / 主控完整镜像 | `DDFC_<ver>_FULL.bin` | `DDFC_26110205_FULL.bin` |
| Main Controller OTA / 主控 OTA | `DDFC_<ver>_OTA.bin` | `DDFC_26110205_OTA.bin` |
| Web page package / Web 页面包 | `DDFC_<fsver>_FS.bin` | `DDFC_20261102735b7d3a_FS.bin` |
| Feeder application / 飞达 APP | `DDF_<ver>_APP.hex` | `DDF_2611102_APP.hex` |
| Feeder full image / 飞达完整镜像 | `DDF_<ver>_FULL.hex` | `DDF_2611102_FULL.hex` |
| Feeder bootloader / 飞达 BL | `DDF_<ver>_BL.hex` | `DDF_2611102_BL.hex` |

- `<ver>` for the Main Controller = 8-digit `yymmddnn`; feeder `<ver>` = `yymmddnn` (7–8 digits, the feeder build number omits the leading zero); `<fsver>` = page-content hash.
- Never use the legacy `DDfeeder...` / `DDfs...` names for release assets.
- `_FULL.bin` is a factory/recovery image; it clears NVS. Normal updates use `_OTA.bin` or `_FS.bin`.

## System Diagram / 系统图

- Chinese: [images/system.svg](images/system.svg)
- English: [images/system-en.svg](images/system-en.svg)
