# DD Feeder Controller — Hardware Interface

> 64-Channel RS-485 SMT Feeder Controller

> For hardware, integration, and maintenance staff. For operation, see the [User Manual](user-manual.en.md).
>
> Main Controller: ESP32-WROOM-32E, 4 MB flash, dual app partitions + LittleFS.
> Feeder: STC8H1K series, RS-485 bus, P3.3 servo PWM.

## 1. External Interfaces

| Interface | Connection | Purpose |
|---|---|---|
| Power | 5 V / GND | Main Controller and RS-485 bus supply |
| USB | On-board USB serial | Debug, flashing, serial log |
| Host serial | SMT machine host | M-code pass-through |
| Feeder bus | A / B / GND | RS-485, up to 64 feeders |
| Display | TJC USART HMI (optional) | Local HMI |
| Factory-reset key | Inside the Main Controller | Hold 5 s to reset |

## 2. Main Controller GPIO

| GPIO | Function | Notes |
|---|---|---|
| GPIO26 / GPIO27 | UART1 | Host pass-through |
| GPIO17 / GPIO16 | UART2 | RS-485 TX/RX |
| GPIO4 | RS-485 DE | Direction control |
| GPIO18 / GPIO19 | Software serial | HMI panel (optional) |
| GPIO21 | Bus power switch | SGM2593AD EN |
| GPIO13 | Power fault | SGM2593AD nFAULT, active low |
| GPIO25 | Buzzer | Passive, PWM |
| GPIO14 / GPIO32 | Calibration limits 0° / 90° | Normally open, grounded when reached |
| GPIO34 | Factory-reset key | External 10 kΩ pull-up, hold 5 s |

> GPIO numbers are for wiring and service only. The User Manual intentionally does not list pin numbers.

## 3. Feeder Board Interface

- RS-485: A / B / GND; add 120 Ω termination at the bus end as required.
- Power: 5 V / GND; N20 and servo supply depend on the feeder board revision.
- Servo: P3.3, 20 ms PWM period, pulse width about 445–2430 µs.
- Limits/feedback: wire according to the feeder board revision.
- Programming: UART ISP; see the [Flashing Guide](flashing-guide.en.md).

## 4. Calibration Limits

| Signal | Main Controller pin | Notes |
|---|---|---|
| 0° limit | GPIO14 | Normally open, grounded when reached |
| 90° limit | GPIO32 | Normally open, grounded when reached |

Calibration is a test feature. The machine must be stopped and the feeder idle.

## 5. Storage, Partitions, and Upgrade

| Region | Offset | Size | Content |
|---|---|---|---|
| bootloader | 0x1000 | — | Second-stage bootloader |
| partition table | 0x8000 | — | Partition table |
| NVS | 0x9000 | 0x5000 | WiFi, passwords, event log, feed count `feedcnt` |
| otadata | 0xE000 | 0x2000 | OTA boot selection |
| app0 | 0x10000 | 0x1C0000 | Main Controller firmware |
| app1 | 0x1D0000 | 0x1C0000 | OTA spare |
| LittleFS | 0x390000 | 0x70000 | Web page resources |

### Full image clears NVS

`DDFC_<ver>_FULL.bin` is a 4 MB full image written from `0x0`. The image contains `0xFF` at `0x9000`, so:

```text
write FULL @0x0  ->  0x9000 NVS becomes FF  ->  WiFi/passwords/count/event log cleared
```

- FULL is for **new-board first flash** or **brick recovery** only.
- For normal upgrades use Web OTA, or flash only the APP/page package.
- To preserve NVS, do not flash the `0x0` full image.
- The `upload_full` task writes the `0x0` full image, so it clears NVS.

### Page upgrade

Web page resources are stored in LittleFS. Page OTA writes only the `0x390000` partition and does not touch NVS, so the feed count is preserved.

### Factory reset

`M907` / Web factory reset / holding the internal key runs `nvs_flash_erase`, clearing NVS including the feed count.

## 6. RS-485 Bus

- Half-duplex RS-485; the Main Controller drives DE.
- Use a daisy-chain bus, not a star.
- Add 120 Ω termination at the bus ends as required.
- Bus power can be switched by Main Controller GPIO21 and is cut automatically on a fault.
