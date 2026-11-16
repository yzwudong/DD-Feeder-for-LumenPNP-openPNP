# Changelog

## 26111603 — 2026-11-16

### ESP32 controller

- Calibration limit inputs moved to GPIO32 (0°) / GPIO33 (90°).
- GPIO14 reserved as service key:
  - short press: 485 bus power ON/OFF
  - double press: rescan feeders
  - long press: no action
- Factory reset remains GPIO34, hold 5 s.
- `app.js` v62.
- 485 power nFAULT / auto-reset / LOCK / RECOVER messages added to UART0.
- Normal Web M600 pacing: minimum 700 ms after the previous M600 reply.
- Event log ring increased from 16 to 32 entries.
- OTA/FS update fix: `ensureUnlock()` re-checks `/api/unlock/status` instead of trusting stale page state.

### Feeder

- Servo power-on initialization staggered by RS-485 address:
  - delay = address × 50 ms; address 63 starts about 3.15 s later.
- Unconfigured address 99 uses `ChipID[2] & 0x3F` for the same stagger.
- Servo is driven to RETRACT, then PWM is switched off after `time_to_settle`.
- Reduces the 64-feeder simultaneous 5 V inrush.
- Removed unused `UART_Write`, `UART_WriteString`, `HEX_TABLE`, `SYS_SetClock`, `SYS_TrimClock`.
- App ROM: 20,836 / 20,992 bytes (156 B free).

## 26111602 — 2026-11-16

### Feeder

- First version of power-on servo stagger by RS-485 address.
- RETRACT settle then PWM off.

## 26111601 — 2026-11-16

### ESP32 controller

- UART0 diagnostics for 485 bus power:
  - `[PWR] nFAULT low -> auto-reset`
  - `[PWR] nFAULT low -> BUS POWER OFF`
  - `[PWR] nFAULT recovered`
  - `[PWR] repeat fault in watch window -> LOCK OFF`

## 26111510 — 2026-11-15

### ESP32 controller

- Event log ring increased from 16 to 32 entries.
- Web/app strings updated to "latest 32".

## 26111508 / 26111507 / 26111505 / 26111503 — 2026-11-15

### ESP32 controller

- Web automatic feed:
  - sent count N/X;
  - stop button;
  - `/api/feed/stop` clears queued M600;
  - production lock cancels pending automatic feed.
- Lightweight `/api/feed/status` polling.
- Fixed FS upload `locked` after OTA reboot by re-querying unlock state.
- Added missing Chinese `feeder.feedSent` / `feedStop` strings.
- Material feed stop/progress and production-lock integration.

## 26111501 — 2026-11-15

### ESP32 controller

- 485 bus power switch moved from GPIO5 to GPIO21 (non-strapping).
- Buzzer remains GPIO25.
- nFAULT remains GPIO13; external 10 kΩ pull-up recommended, internal pull-up as fallback.

## 26110800 — 2026-11-08

### ESP32 controller

- Production lock idle release 10 s → 30 s.
- Overview and feeder detail show production-lock text in Chinese/English.
- Event log `ev.timeHint` English fix; immediate re-render on language change.
- Feeder detail M600 reply wait 3 s → 6 s.

## 26110701 / 26110700 — 2026-11-07

### ESP32 controller

- Production queue and production lock:
  - UART1 M600/M601 highest priority;
  - UART1 M610 S1 priority but does not start lock;
  - first UART1 M600 starts lock;
  - manual release until next UART1 M600;
  - Web M600/M601/M602/M603/M610 and scan rejected during lock;
  - heartbeat only when queues idle;
  - normal command TTL 5 s;
  - M602 de-duplication;
  - M600 10 s / M601 5 s timeouts.

## 26110501 — 2026-11-05

### ESP32 controller

- Feeder detail N20 timeout default 800 → 1000 ms.

## 26110401 — 2026-11-04

### Feeder

- N20 X0/X1 semantics:
  - X0: feedback required; timeout = E1 error.
  - X1: feedback optional; timeout = ok, no error.
- N20 timeout default 1000 ms.
- Final F4/F8/F16/F24 segment stops at FULL.
- M601 performs post-pick retract from FULL.

## 26110400 — 2026-11-04

### ESP32 controller

- Web feed distance selection no longer reset to 4 mm by auto refresh.
