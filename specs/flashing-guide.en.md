# DD Feeder Controller — Flashing Guide (End-User Edition)

> 64-Channel RS-485 SMT Feeder Controller

For upgrading or recovering the **DD Feeder Controller** firmware and Web pages. Intended for field operators/maintenance staff; no development knowledge is required.

## 1. When to Flash

| Situation | File |
|---|---|
| New board / device bricked and cannot boot | Full image `DDFC_<ver>_FULL.bin` |
| Upgrade Main Controller firmware | **OTA image `DDFC_<ver>_OTA.bin` (recommended)**; full image only for new boards/recovery |
| Update Web pages only | Page package `DDFC_<fsver>_FS.bin` |
| Device has Internet access | Use **Upgrade** in the Web UI; no USB flashing needed |

> ⚠️ **`DDFC_<ver>_FULL.bin` clears all settings** (WiFi, admin password, feed count, event log; stored in the NVS partition). WiFi must be reconfigured afterwards.
> **For normal firmware upgrades use `DDFC_<ver>_OTA.bin`**, which writes only the app partition and preserves all settings.
>
> The full image is written from `0x0`; the NVS at `0x9000` is overwritten with `0xFF`, clearing WiFi, admin password, feed count, and the event log. This is not caused by page upgrades.
>
> The project's `upload_full` task also writes the `0x0` full image and therefore clears NVS.
>
> If the Web UI is reachable, prefer online upgrade. Use this guide only when the device is offline, bricked, or recovery is required.

## 2. Tools and Files

- **PC** (Windows) and a **USB data cable** (connects to the controller's USB port).
- **ESP Flash Download Tool** (Espressif official flasher; download the ESP32 version from Espressif).
- **Firmware files** from the release `bin/` directory:

| File name | Purpose |
|---|---|
| `DDFC_26110205_FULL.bin` | Full image (firmware + pages; ⚠️ clears settings and feed count; first install/recovery only) |
| `DDFC_26110205_OTA.bin` | Main Controller firmware only (OTA; recommended; preserves settings) |
| `DDFC_20261102735b7d3a_FS.bin` | Web pages only |

> The numbers in the file name are version identifiers. Always use the files from the latest release.

## 3. Upgrade the Main Controller (Recommended: OTA, Settings Preserved)

1. **Connect:** USB cable between the controller and the PC. The on-board USB-serial bridge creates a COM port.
2. **Open the tool:** run `FlashDownloadTool.exe`; select **ESP32**.
3. **Configure the download:**
   - Click `…` on the first file row and select `DDFC_26110205_OTA.bin`.
   - Set the address to **0x10000**.
   - Set **SPI SPEED = 80 MHz**, **SPI MODE = DIO**.
4. **Select the port:** choose the COM port detected by Windows (COM3/COM8, etc.; see Device Manager → Ports).
5. **Start:** click **START** and wait for the green completion message.
6. **Reboot:** power-cycle the device.

> If START hangs: **hold the BOOT button**, click START, and release it when flashing begins.

> **Flashing only `DDFC_<ver>_OTA.bin` does not clear any settings** (WiFi password, accounts, etc.); use it for normal upgrades.

### First Use / Recovery: Full Image

- File `DDFC_<ver>_FULL.bin`; address **0x0**; other steps are the same.
- ⚠️ The full image **clears all settings**, including the feed count. Reconfigure WiFi through the `SMT-Feeder-AP` hotspot after flashing.

## 4. Other Flash Modes

### Main Controller Firmware Only (keep pages)

| File | Address |
|---|---|
| `DDFC_<ver>_OTA.bin` | 0x10000 |

### Web Pages Only (keep firmware)

| File | Address |
|---|---|
| `DDFC_<fsver>_FS.bin` | 0x390000 |

Use the same procedure as §3; only the file and address change. Flashing pages does not affect settings.

## 5. Verifying Success

- After reboot, the WiFi/power indicators are normal. Connect to the device hotspot, or to the router and its assigned IP.
- Open the Web UI; the **Firmware** version on the Overview should match the flashed file (e.g. `26110205`).
- Pages render and feeders can be scanned.

## 6. Cautions

1. **Close other serial tools** during flashing; otherwise the COM port may be busy.
2. **Do not remove power or cables during flashing.** An interruption may brick the device and require a full re-flash.
3. **Flashing only `DDFC_<ver>_OTA.bin` does not update pages or settings**; flash `DDFC_<fsver>_FS.bin` separately when pages need updating.
4. **The full image contains everything but clears all settings**, including the feed count. Use it only for first install/recovery; reconfigure WiFi afterwards. For normal upgrades use `DDFC_<ver>_OTA.bin`.
5. **First boot after flashing is slow**; wait 10–20 s before opening the page.
6. If the device is on the network and the Web UI is reachable, prefer online upgrade.

## 7. Troubleshooting

| Symptom | Action |
|---|---|
| No COM port | Try another USB data cable (some cables charge only); install the CH340/CP210 driver |
| START does nothing | Hold BOOT, click START, release when flashing starts; check baud rate 921600 |
| Flash error / failure | Re-flash the full image at `0x0` with stable power |
| Device does not boot | Power-cycle; if still dead, re-flash `DDFC_<ver>_FULL.bin` |
| Pages open but no feeders found | Check RS-485 wiring and bus power; unrelated to flashing |

---

**Version information:** this guide corresponds to firmware `26110205` and page version `20261102735b7d3a`. Obtain the matching guide when using a newer release.
