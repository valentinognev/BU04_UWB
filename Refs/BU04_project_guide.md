# BU04 UWB Project — Overview & Workflow

This repository controls two **Ai-Thinker BU04** UWB modules (STM32F103 + Qorvo DW3000) for **two-way ranging (TWR)**, **phase-difference-of-arrival (PDOA) azimuth**, and **peer-to-peer UWB data transfer**. Host control is over **115200 baud UART** (FT4232H on the dev board); firmware is a custom **GCC port** of the Ai-Thinker SDK.

**Current custom firmware:** `V1.0.47` (`STM32F103-BU0x_SDK/`)  
**Typical ports:** anchor `/dev/ttyUSB0`, tag `/dev/ttyUSB4` (FT4232H)  
**Current lab setup:** anchor `/dev/ttyACM0`, tag `/dev/ttyACM1` (DAPLink CMSIS-DAP on each module)

---

## What Works Today

| Capability | Status | How to read results |
|------------|--------|---------------------|
| **TWR distance** | Working | Anchor `AT+DISTANCE` or `distance:` UART stream (mm) |
| **PDOA range + angle** | Working | Anchor `Tag_Addr:` lines (`Angle` in tenths of degrees) |
| **PDOA calibration** | Verified @ 1.58 m | `rngOffset=-760 mm`, `pdoaOffset=103°` → median **1.58 m**, **~0°** head-on |
| **UWB data channel** | Implemented (V1.0.47+) | `AT+UWBDATA` send → peer `+UWBDATA` receive; run `test_uwbdata.py` to verify on hardware |

All three build on the same vendor TWR stack (`AT+SETUWBMODE=0`, precompiled `node.o` / `tag.o`), not the isolated `SETUWBMODE=1` example code.

---

## Hardware Setup

Two BU04 modules, each with:

- **ST-Link** (SWD) for flash/debug
- **FT4232H UART port A** for AT commands (115200) — use FTDI serial, **not** ST-Link VCP
- **DAPLink** (optional) — CMSIS-DAP + USB serial on `/dev/ttyACM0` / `/dev/ttyACM1`
- **External power** on both modules (required for reliable DW3000 SPI)

| Module | Typical UART | DAPLink (this lab) | Role |
|--------|--------------|--------------------|------|
| Anchor | `/dev/ttyUSB0` | `/dev/ttyACM0` | `role=1`, computes range (and PDOA on anchor) |
| Tag | `/dev/ttyUSB4` | `/dev/ttyACM1` | `role=0`, initiates TWR Poll |

Modules should face each other at ~0.5–2 m with **fronts toward each other** (antenna boresight). Back-to-back orientation gives large bogus azimuth offsets. After every full flash, NVM is erased — **reconfigure roles** before ranging.

**DAPLink flash / reset** (when ST-Link USB paths are unavailable):

```bash
cd STM32F103-BU0x_SDK
openocd -f openocd-anchor-daplink.cfg -c "init; reset run; shutdown"
openocd -f openocd-tag-daplink.cfg    -c "init; reset run; shutdown"
# Full flash (erases NVM):
openocd -f openocd-anchor-daplink.cfg -c "init; reset halt; flash erase_address 0x08000000 0x20000; flash write_image build/bu04_firmware.bin 0x08000000; reset run; shutdown"
```

---

## Repository Layout

```
UWB/
├── bu04_at.py              # AT wrapper (Bu04Device, UWBDATA, Tag_Addr parsing)
├── calibrate.py            # Two-module setup, TWR/PDOA calibration
├── test_connection.py      # AT smoke test
├── test_uwbdata.py         # AT+UWBDATA link test (V1.0.47+)
├── Refs/                   # Vendor PDFs, hex, and project notes
│   ├── BU03_BU04_AT_commands.md
│   ├── BU04_project_guide.md          ← this file
│   └── BU04-kit.pdf
└── STM32F103-BU0x_SDK/     # Custom GCC firmware (nested git repo)
    ├── FIRMWARE.md         # Firmware architecture & build details
    ├── openocd-anchor-daplink.cfg
    ├── openocd-tag-daplink.cfg
    ├── Makefile            # build, flash, binary patches
    └── Components/         # APP + HAL sources
```

---

## End-to-End Workflow

### 1. Build and flash firmware

```bash
cd STM32F103-BU0x_SDK
make -j1                  # use -j1 to avoid patch race errors
make flash-all            # erases NVM on both modules
make reset-all
```

Wait a few seconds, then confirm AT on both ports:

```bash
cd ..
python3 test_connection.py --port /dev/ttyUSB0 --boot-wait 3
python3 test_connection.py --port /dev/ttyUSB4 --boot-wait 5
```

Expect `getver software:V1.0.47` (or newer).

### 2. Configure roles and start ranging

**Recommended:** use `calibrate.py` — it pipelines `AT+SETCFG` + `AT+SAVE`, waits for NVM, sets PDOA network, and sends `AT+UWBSTART` in the right order.

**TWR distance only:**

```bash
python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
  --non-interactive --mode twr --known-distance 1.0 --samples 10 --dry-run
```

**PDOA (distance + azimuth):**

```bash
python3 calibrate.py --anchor /dev/ttyACM0 --tag /dev/ttyACM1 \
  --non-interactive --mode pdoa --known-distance 1.58 --known-azimuth 0 \
  --samples 60 --sample-time 30 --dry-run --force-setup
```

Use `--non-interactive` whenever passing `--known-distance` / `--known-azimuth` on the command line (otherwise the script prompts interactively).

**Apply calibration (writes offsets to NVM):**

```bash
python3 calibrate.py --anchor /dev/ttyACM0 --tag /dev/ttyACM1 \
  --non-interactive --mode pdoa --known-distance 1.58 --known-azimuth 0 \
  --samples 60 --sample-time 30 --force-setup
```

**UWB data transfer test (after ranging works):**

```bash
python3 test_uwbdata.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
  --setup --force-setup
```

### 3. Manual AT sequence (equivalent to calibrate.py)

Use when debugging without Python. **Wait ≥8 s after each `AT+SAVE`** before reset or reopening the port.

**Anchor** (`/dev/ttyUSB0`):

```
AT+SETUWBMODE=0
AT+PDOASETCFG=1,1,4369,0,100,1,1
AT+USER_CMD=1
AT+FILTER=0
AT+SETCFG=0,1,1,1
AT+SAVE
AT+ADDTAG=0,0,1,1,0
AT+UWBSTART
```

**Tag** (`/dev/ttyUSB4`):

```
AT+SETUWBMODE=0
AT+PDOASETCFG=1,1,4369,0,100,0,1
AT+SETCFG=0,0,1,1
AT+SAVE
AT+UWBSTART
```

Then reset both (`make reset-all` in SDK dir) or power-cycle if you saved config and want a clean AT session.

### 4. Read measurements

| Mode | Command / stream | Notes |
|------|------------------|-------|
| TWR | `AT+DISTANCE` on **anchor** | Response `distance: 1576` = **1576 mm** |
| TWR | `distance:` periodic stream on tag UART | Diagnostic fields; range value still from anchor in STS-SDC mode |
| PDOA | `Tag_Addr:0000, Seq:3, Xcm:109, Ycm:142, Range:1.790, Angle:1049` | `Angle:1049` → **104.9°** (tenths of degrees) |
| Data | `+UWBDATA:0,5` + 5 raw bytes | Binary payload on receiver UART |

---

## Standard Configuration Values

| Parameter | Value | Notes |
|-----------|-------|-------|
| Both device IDs | **0** | Tag id 17845 (0x45B5) breaks TWR response RX |
| Anchor role | **1** | Factory default after flash is `role=0` on both |
| Tag role | **0** | |
| Channel | `1` in AT → **RF channel 5** | `0` → channel 9 |
| Rate | `1` → 6.8 Mbps | |
| PDOA network | **4369** (0x1111) | Tag `anc_id=0` |
| UWB algorithm | `AT+SETUWBMODE=0` | Vendor `node.o`/`tag.o` stack |

---

## Python Tools

| Script | Purpose |
|--------|---------|
| `bu04_at.py` | `Bu04Device`: AT helpers, `send_uwbdata()`, `wait_uwbdata()`, `parse_measurements()` |
| `calibrate.py` | Role setup, `AT+UWBSTART`, TWR/PDOA sample collection, antenna delay / RNG / PDOA offset writes |
| `test_connection.py` | `AT`, `AT+GETVER`, `AT+GETCFG` smoke test |
| `test_uwbdata.py` | Bidirectional `AT+UWBDATA` test; requires V1.0.47+ |
| `detect_conflicts.py` | Check for processes holding serial ports |
| `read_raw.py` | Raw serial dump for debugging |

Dependencies: `pip install pyserial`

---

## Critical Operational Rules

1. **`make flash-all` erases NVM** — always rerun SETCFG/SAVE or `calibrate.py --force-setup`.
2. **`AT+SAVE` reboots the module** — wait **≥8 s** before verifying `AT+GETCFG` or opening another session.
3. **Verify roles before `AT+UWBSTART`:** anchor `Role:1`, tag `Role:0`, both `id=0`.
4. **Both ports streaming identical tag-style `distance:` lines** → both modules are tags, not an RF fault.
5. **Read TWR distance from the anchor** — STS-SDC DS-TWR computes range on the responder; tag `cb:0` alone is not a failure.
6. **`AT+SETCFG` channel param:** `ch=1` means RF **channel 5**, not literal channel 1.
7. **Build with `make -j1`** — parallel builds can race Keil object patches.
8. **External power** on both modules — without it, `DEVID:0xFFFFFFFF` on `AT+UWBSTART`.
9. **PDOA orientation** — modules must face **front-to-front** (antenna boresight). Back-to-back gives large bogus azimuth.
10. **`calibrate.py --non-interactive`** — required when passing `--known-distance` / `--known-azimuth` on the CLI.

---

## Architecture Summary

```
Host (Python)  ←UART 115200→  STM32F103  ←SPI→  DW3000  ←UWB→  peer module
```

- **TWR:** Tag sends Poll → Anchor Response → Tag Final (110 B STS) → Anchor computes distance.
- **PDOA:** Same exchange; anchor latches PDOA on Poll RX and emits `Tag_Addr:` with range + angle.
- **Data:** Custom `0xD0` frames queued via `AT+UWBDATA`, sent ~1 packet / 500 ms alongside ranging.

Custom firmware adds GCC build, OpenOCD flash, SPI/UART fixes, binary patches to precompiled `tag.o`, PDOA latch logic, and `uwb_data.c`. See `STM32F103-BU0x_SDK/FIRMWARE.md`.

---

## Calibration Notes

At ~1 m physical separation, uncorrected PDOA range is typically **~1.57–2.37 m** (~590–760 mm bias). At **1.58 m** with fronts facing, verified offsets are **`rngOffset=-760 mm`**, **`pdoaOffset=103°`**, yielding median **1.58 m** range and **~0°** azimuth.

| Calibration | AT command | Firmware applies as |
|-------------|------------|---------------------|
| PDOA distance | `AT+RNGOFF` (mm) | `reported_range = raw_range + rngOffset_mm` |
| PDOA azimuth | `AT+PDOAOFF` (degrees) | `reported_angle = raw_angle - pdoaOffset_deg` |
| TWR distance | `AT+SETDEV` (`anndelay`) | Antenna delay in TWR stack |

**Important:** `calibrate.py` azimuth math must match the subtract convention above (`new_offset = old_offset + (measured - known)`). Angles are noisy before calibration (±10–20° spread is normal); use 30–60 s sample windows.

After `make flash-all` or DAPLink full flash, NVM is erased — rerun `calibrate.py --force-setup` or the manual AT sequence below.

---

## Reference Documents

| Document | Content |
|----------|---------|
| [`BU03_BU04_AT_commands.md`](BU03_BU04_AT_commands.md) | Full AT command reference (vendor + custom extensions) |
| [`../STM32F103-BU0x_SDK/FIRMWARE.md`](../STM32F103-BU0x_SDK/FIRMWARE.md) | Firmware build, patches, calibration offset application, source map |

Vendor PDF: `Refs/BU03_BU04_AT_command_en_v1.0.6.pdf`  
Factory hex (reference only): `Refs/（2717）BU03_BU04-AT-通用-常规固件V1.0.0_T2024-07-26.hex`
