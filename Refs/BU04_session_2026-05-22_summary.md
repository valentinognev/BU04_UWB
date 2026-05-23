# BU04 Session Summary — 2026-05-22 (Evening, Post-Distance-Working)

**Firmware version at session start:** V1.0.46  
**Previous milestone:** TWR distance working (anchor reports ~1.57–1.60 m at ~1 m separation).  
**This session's goal:** Move toward proper (calibrated) distance measurement and azimuth (PDoA angle) measurement.

---

## What Was Done This Session

### 1. SYS_TICK Debug Probe (Added & Removed)

A temporary 1 s heartbeat `USART1_SendBuffer("SYS_TICK\r\n")` was added inside `SysTick_Handler` in `stm32f10x_it.c` (role == 1 guard) to confirm the anchor's SysTick is firing and the UART path works during ranging. Confirmed working: `SYS_TICK` appeared in the anchor serial output. The probe was then **removed** before the final firmware build.

**File affected:** `Components/Main/stm32f10x_it.c`

---

### 2. PDOA Output Fixed — Bypassed `_dbg_printf` Suppression

The `node_pdoa_output_user()` and `tag_pdoa_output_user()` functions in `Components/APP/user_out.c` previously used `_dbg_printf()`, which is **fully suppressed during ranging** (a V1.0.42 fix to prevent delayed TX faults). This meant PDOA angle/distance output (`Tag_Addr:..., Angle:...`) was silently swallowed and never reached the UART.

**Fix:** Both functions were rewritten to use `sprintf` + `USART1_SendBuffer` directly, bypassing `_dbg_printf`.

```c
// user_out.c — node_pdoa_output_user() and tag_pdoa_output_user()
char buf[128];
int len = sprintf(buf, "Tag_Addr:%04X, Seq:%d, Xcm:%.0f, Ycm:%.0f, Range:%.3f, Angle:%.0f\r\n",
                  Res->addr16, (int) Res->rangeNum, Res->x_cm, Res->y_cm,
                  Res->dist_cm / 100.0, Res->angle);
USART1_SendBuffer(buf, (uint16_t) len, 1);
```

**Expected output format (when PDOA exchange completes):**
```
Tag_Addr:45B5, Seq:5, Xcm:0, Ycm:100, Range:1.590, Angle:-12
```

---

### 3. Firmware Rebuilt & Flashed (V1.0.46 with PDOA fix)

```bash
make -C Refs/STM32F103-BU0x_SDK && make -C Refs/STM32F103-BU0x_SDK flash-all
```

Both modules flashed successfully. Tag occasionally requires the fallback 100 kHz adapter speed path due to HardFault during flash algorithm — this is a known intermittent quirk, handled automatically by the Makefile fallback.

---

### 4. Configured Devices for PDOA Mode

`configure_devices.py` was used, which does:
- **Anchor** (`/dev/ttyUSB0`): `AT+SETCFG=0,1,1,1` → role=1, id=0, ch=5, rate=6.8M  
  Then `AT+ADDTAG=45B5,45B5,1,1,0` → registers tag addr `0x45B5` in known-tag list  
  Then `AT+SAVE`
- **Tag** (`/dev/ttyUSB4`): `AT+SETCFG=17845,0,1,1` → role=0, id=17845 (=0x45B5), ch=5, rate=6.8M  
  Then `AT+SAVE`

> **Why id=17845 for tag?** Tag ID `17845 = 0x45B5` — the anchor's `AT+ADDTAG` registers this address so the anchor's PDOA node scheduler knows to accept that tag in a slot.

---

### 5. Diagnosis Run — TWR Still Working, PDOA Not Yet Firing

`diagnose_pdoa.py` was run: sends `AT+UWBSTART` to both ports and monitors for 10 s.

**Observed:**
- Tag streams `distance: 0 ... cb: 0 txs:60/80/100/120/140 rxg:0 rxt:480/648/808/968/1128 ...`
- Tag: `rxg:0`, `rsp:0`, `sl:0x0001` — **tag is not receiving Responses from anchor**
- Anchor: Only prints startup banner; no `anc:` diagnostic lines seen yet in the 10 s window
- No `Tag_Addr:` PDOA output (expected — `node_pdoa_output_user` only fires when the PDOA node stack processes a full PDOA exchange, which requires tag slot registration and a different UWB mode)

**`sl:0x0001`** on the tag: SYS_STATUS bit 0 = `IRQS` (IRQ status, latched). Not a critical error; the tag DW3000 is running.

---

## Current Understanding: TWR vs. PDOA Mode

The firmware has **two separate UWB stacks**:

| Mode | Condition | Anchor role | Tag role | Distance where |
|------|-----------|-------------|----------|----------------|
| **TWR (STS-SDC)** | `twr_pdoa_mode == 0` (default) | `node_start()` → `node.o` | `tag_start()` → `tag.o` | Anchor computes, reads via `AT+DISTANCE` |
| **PDOA** | `twr_pdoa_mode == 1` | `ds_twr_sts_sdc_responder()` | `ds_twr_sts_sdc_initiator()` | Anchor computes range+angle; printed by `node_pdoa_output_user` |

The `twr_pdoa_mode` is set via `AT+SETUWBMODE=<mode>`:
- `mode=0` → TWR-only (STS-SDC responder/initiator, but using `node.o`/`tag.o` PDOA stack)
- `mode=1` → pure PDOA test examples (`ds_twr_sts_sdc_responder` / `ds_twr_sts_sdc_initiator` from `keil_objs/pdoa/`)

The **working TWR** from the previous milestone uses `twr_pdoa_mode == 0` with `node_start()` / `tag_start()`. This uses the `node.o`/`tag.o` precompiled library that does **TWR + optional PDOA**. The PDOA angle is computed inside `node.o` and delivered via `node_pdoa_output_user()`.

---

## What Needs to Happen Next

### Primary question: Does `node_pdoa_output_user` ever get called?

The anchor uses `node.o` (precompiled). Inside `node.o`, after a successful TWR exchange, it calls `node_twr_output_user()` (confirmed working — distance streams). For PDOA angle, it needs to call `node_pdoa_output_user()` — but **this requires PDOA mode to be enabled in the DW3000 chip config**.

The DW3000 has `pdoaMode` in `dwt_config_t`. For the BU04 modules, the DW3000 chip is `0xDECA0312` which is the **C0 PDOA-capable chip** (`DWT_C0_PDOA_DEV_ID`). PDoA requires **two RX antennas**; the BU04 antenna configuration needs to be confirmed.

### Action items for next agent:

1. **Verify if BU04 has dual antennas for PDOA**  
   - Check hardware schematic / Ai-Thinker BU04 datasheet for antenna count
   - If single antenna: PDOA angle is physically impossible; TWR-only distance is the realistic goal
   - If dual antenna: PDOA requires `pdoaMode = DWT_PDOA_M3` in `dwt_config_t`

2. **Check what pdoaMode is configured in `f_uwbstart`**  
   Look in `cmd_fn.c` `f_uwbstart()` (around line 454–526) and in `default_config.h`. The `app.pConfig->s.userConfig.pdoaMode` field controls whether `node.o` activates PDOA angle computation.

   ```bash
   grep -n "pdoaMode" Refs/STM32F103-BU0x_SDK/Components/APP/cmd_fn.c
   grep -n "pdoaMode" Refs/STM32F103-BU0x_SDK/Components/HAL/DW/twr_pdoa/inc/default_config.h
   ```

3. **Investigate the `AT+SETUWBMODE` command**  
   In `cmd_fn.c`, search for `f_setuwbmode`. This sets `twr_pdoa_mode`. If it's 0, `node.o` is used (with potential PDOA). Understand what `AT+SETUWBMODE=0` vs `AT+SETUWBMODE=1` does exactly.

4. **Tag not receiving Responses (regression from last session)**  
   The current run shows `rxg:0 rsp:0` — tag is again not receiving the anchor's Response. This was working at the end of the **previous** session (`BU04_distance_achievements.md` confirms working TWR). Possible causes:
   - `configure_devices.py` uses `AT+SAVE` without waiting ≥8 s and immediately opens the port for `AT+UWBSTART` → anchor role may not have persisted
   - The anchor's `AT+ADDTAG=45B5,...` may be incompatible with TWR mode (it registers for PDOA slot scheduler, not TWR `node.o`)
   - The tag ID 17845 doesn't match what `node.o` expects — TWR with `node.o` typically uses `nodeAddr` for addressing, not the PDOA tag list

5. **Fix `configure_devices.py` for TWR mode**  
   For plain TWR (`node.o`/`tag.o`):
   - Anchor: `AT+SETCFG=0,1,1,1` + `AT+SAVE` (no need for `AT+ADDTAG` — `node.o` manages tag list differently)
   - Tag: `AT+SETCFG=0,0,1,1` + `AT+PDOASETCFG=1,1,4369,0,100,1,0` (anc_id=0, net=4369) + `AT+SAVE`
   - Wait **≥8 s** after each `AT+SAVE` before closing the port
   - Then use `calibrate.py` for UWB start

   **Working command from previous session:**
   ```bash
   python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
     --non-interactive --mode twr --known-distance 1.0 --samples 5 --dry-run --skip-setup
   ```

6. **Antenna delay calibration**  
   Current bias is ~590 mm at 1 m (reads ~1.59 m). To calibrate:
   ```bash
   python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
     --non-interactive --mode twr --known-distance 1.0 --samples 10
   ```
   This writes corrected antenna delay via `AT+SETDEV` / `AT+DEVCFG` to the anchor.

---

## Current File State

### `Components/APP/user_out.c` (modified this session)
- `node_pdoa_output_user()` and `tag_pdoa_output_user()` now use `sprintf` + `USART1_SendBuffer` instead of `_dbg_printf`
- `#include <stdio.h>` added at the end of the file (cosmetically ugly but compiles correctly)
- Consider moving the `#include` to the top of the file

### `Components/Main/stm32f10x_it.c` (no net change)
- SYS_TICK debug probe was added then removed — file is back to V1.0.46 state

### No other firmware files were modified this session.

---

## Quick Reference — Working State Recap

**TWR distance working (from `BU04_distance_achievements.md`):**
```
anchor AT+DISTANCE → distance: 1604 mm   (at ~1 m physical)
calibrate.py dry-run → measured 1.590 m
```

**Correct configuration for TWR:**
```
Anchor /dev/ttyUSB0: id=0, role=1, ch=5(ch=1), rate=6.8M
Tag    /dev/ttyUSB4: id=0, role=0, ch=5(ch=1), rate=6.8M
Tag PDOASETCFG: net=4369, anc_id=0
```

**Build:**
```bash
cd Refs/STM32F103-BU0x_SDK && make -j1 && make flash-all
# Then re-configure roles (make flash-all erases NVM)
python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
  --non-interactive --mode twr --known-distance 1.0 --samples 5 --dry-run
```

**Key observation for PDOA azimuth:**
- `node_pdoa_output_user()` is now wired to USART1 (this session's fix)
- Whether `node.o` actually calls it depends on `pdoaMode` in DW3000 config **and** dual-antenna hardware
- BU04 hardware antenna count is **unconfirmed** — this is the critical unknown

---

## Scratch Tools (in conversation artifacts dir)

| Script | Path | Purpose |
|--------|------|---------|
| `configure_devices.py` | `~/.gemini/antigravity/brain/ca54b6ff-.../scratch/configure_devices.py` | Sets anchor (id=0,role=1) + tag (id=17845,role=0), adds tag to anchor's tag list, saves |
| `diagnose_pdoa.py` | same dir | Sends `AT+UWBSTART` to both ports, monitors both for 10 s |
| `read_anchor.py` | same dir | Opens `/dev/ttyUSB0` and reads raw output for a few seconds |

> **Note:** `configure_devices.py` currently uses tag id=17845 (0x45B5) and `AT+ADDTAG` which is the PDOA-mode configuration. For TWR mode, switch both to id=0 and remove `AT+ADDTAG`; use `calibrate.py` instead.
