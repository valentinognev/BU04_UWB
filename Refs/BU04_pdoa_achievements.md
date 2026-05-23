# BU04 PDOA Mode — Achievements Summary

**Date:** 2026-05-22  
**Firmware:** V1.0.46 (custom GCC port, PDOA-enabled build)  
**Hardware:** Two Ai-Thinker BU04 modules (STM32F103 + DW3000 C0 PDOA), anchor `/dev/ttyUSB0`, tag `/dev/ttyUSB4`

---

## Goal

Get **PDOA mode working properly**: simultaneous TWR distance and **non-zero azimuth** in anchor `Tag_Addr:` output, compatible with `calibrate.py --mode pdoa`.

---

## Bottom Line

**PDOA mode is working.** With the vendor TWR stack (`SETUWBMODE=0`, `node.o` / `tag.o`) plus PDOA setup (`AT+PDOASETCFG`, `AT+USER_CMD=1`, `AT+ADDTAG`), the anchor streams:

```
Tag_Addr:0000, Seq:3, Xcm:109, Ycm:142, Range:1.790, Angle:1049
Tag_Addr:0000, Seq:4, Xcm:-120, Ycm:136, Range:1.810, Angle:-1185
```

- **Range:** ~1.7–1.9 m at ~1 m physical (same ~760 mm antenna-delay bias as TWR-only; calibrate with `calibrate.py`)
- **Angle:** non-zero, in **tenths of degrees** (`Angle:1049` → +104.9°)
- **TWR exchange:** stable (`send resp fault` ≈ 0–3 per 30 s, was 40+ before fixes)
- **`calibrate.py --mode pdoa --dry-run`:** 10/10 samples parsed successfully

---

## Architecture (What Actually Works)

| Setting | Value | Notes |
|---------|-------|-------|
| UWB mode | `AT+SETUWBMODE=0` | Vendor TWR stack (`node.o` / `tag.o`), **not** `twr_pdoa_mode=1` example code |
| Anchor output | `AT+USER_CMD=1` | Enables `Tag_Addr:` stream on anchor |
| PDOA config | `AT+PDOASETCFG=…,4369,0,…,1` | Network 4369 (0x1111), anchor id 0 |
| Tag list | `AT+ADDTAG=0,0,1,1,0` | Register tag id 0 for anchor scheduler |
| Both device ids | **0** | Tag id 17845 (0x45B5) breaks TWR response RX |

The vendor PC tool uses this same path: TWR stack + PDOA AT commands, not the separate `ds_twr_sts_sdc_*` objects linked for `SETUWBMODE=1`.

---

## Root Causes Found (and Fixed)

### 1. IRQ handler read RX before vendor TWR code

Early PDOA debug read `dwt_readrxdata()` / `dwt_readdiagnostics()` **before** `process_deca_irq()` ran. That broke anchor delayed Response TX (`send resp fault` spam) and collapsed distance to ~0.2 m.

**Fix:** Run `__real_process_deca_irq()` first; latch PDOA **after** the real handler.

### 2. `--wrap=twr_rx_node_cb` never fired

`node.o` **defines** `twr_rx_node_cb` internally. Linker wraps only apply to undefined symbols, so our `__wrap_twr_rx_node_cb` in `twr_diag.c` was dead code.

**Fix:** Latch PDOA from `__wrap_process_deca_irq` on 13-byte Poll RX (anchor receives tag Poll).

### 3. STS required for M3 PDOA

`dwt_readpdoa()` on DW3000 M3 needs **STS+SDC** frames. Without STS, `pdor` stayed 0 while `ipoa` varied uselessly.

**Fix:** `uwb_pdoa_prepare_config()` + `uwb_pdoa_apply_after_start()` force `STS_MODE_1 | STS_SDC`, `STS_LEN_64`, `PDOAMODE_M3` after vendor `node_start()` / `tag_start()`.

### 4. LNA only on anchor, after init

`tag.o` calls `dwt_setlnapamode(0)` at init when `twr_pdoa_mode=0`. Enabling LNA inside `dwt_configure` or on the tag broke timing.

**Fix:** `dwt_setlnapamode(DWT_LNA_ENABLE)` on **anchor only**, after `node_start()`.

### 5. Stale `pdoa2XY()` → Angle always 0

Vendor `pdoa2XY()` (precompiled) returned angle 0 even when `pdor` was non-zero. `Tag_Addr` showed `Angle:0`.

**Fix:** Compute angle from latched raw PDOA via `uwb_pdoa_raw_to_deg_x10()`; use `pdoa2XY()` only optionally for X/Y correction.

### 6. Range formatting bug in `Tag_Addr`

`node_pdoa_output_user()` treated `dist_cm` as millimeters → `Range:0.195` instead of `Range:1.950`.

**Fix:** `dist_mm = (int)(Res->dist_cm * 10.0f + 0.5f)`.

### 7. Tag id misconfiguration

Tag left at `id=17845` from an earlier PDOA session caused `rxg:0`, no TWR completion. `calibrate.py` now checks device id in `ensure_roles_configured()`.

---

## Firmware Changes (New / Modified Files)

| File | Purpose |
|------|---------|
| `Components/APP/uwb_pdoa.c` | PDOA config prep, angle math, `uwb_pdoa_apply_after_start()` |
| `Components/APP/uwb_pdoa.h` | Exports for PDOA state and latch API |
| `Components/APP/uwb_status.c` | IRQ wrap (real handler first), post-Poll PDOA latch, `dwt_configure` wrap for STS+M3 |
| `Components/APP/user_out.c` | `Tag_Addr` UART output, angle from raw PDOA, fixed range units |
| `Components/APP/cmd_fn.c` | `f_uwbstart` calls prepare/apply; anchor diag fields (`pdor`, `pdm`, `stac`, …) |
| `Components/HAL/DW/twr_pdoa/inc/default_config.h` | Default `STS+SDC`, `PDOAMODE_M3`, `PHASECORR_EN=1` |
| `Components/Main/main.c` | Auto-boot PDOA config prep |
| `Makefile` | Added `uwb_pdoa.c` to build |

### Angle conversion (vendor formula)

```c
deg = (raw / 2048.0f) * 180.0f / M_PI;
```

Raw PDOA is signed Q11 radians; host parses `Angle:` as **tenths of degrees** (`1049` → 104.9°).

### PDOA latch flow

1. Tag sends 13-byte Poll → anchor RX IRQ  
2. `__real_process_deca_irq()` — vendor TWR state machine runs (schedules Response TX)  
3. Post-handler: if `rx_len == 13`, read CIA diagnostics + `dwt_readpdoa()` → store in `uwb_poll_pdoa_raw`  
4. After full exchange, `node_twr_output_user()` → `Tag_Addr:` with distance + latched angle  

---

## Verified Results (Hardware)

| Metric | Value |
|--------|-------|
| Anchor `AT+DISTANCE` | 1890–1951 mm |
| `Tag_Addr` range | 1.71–1.87 m |
| `pdor` (diag) | non-zero (e.g. 3683, -5374, 5360) |
| `pdm` | 3 (M3) |
| `stac` | up to 32 (STS accumulating) |
| `send resp fault` | 0 (typical); occasional 1–3 |
| Angle spread | roughly −179° to +172° (uncalibrated; tag not at 0° reference) |

Example `calibrate.py` dry-run:

```
Collecting up to 10 PDOA samples for 10s...
  received 10 parsed samples
Measured distance:  1.760 m
Known distance:     1.000 m
Correction:         -760 mm
```

---

## Host Setup & Commands

### After every flash (NVM erased)

```bash
cd Refs/STM32F103-BU0x_SDK
make -j1 && make flash-all && make reset-all

cd ../..
python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
  --non-interactive --mode pdoa --known-distance 1.0 --known-azimuth 0 \
  --samples 10 --dry-run --skip-azimuth --force-setup
```

### Distance calibration (writes RNG offset)

```bash
python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
  --non-interactive --mode pdoa --known-distance 1.0 --samples 10 --force-setup
```

### Azimuth calibration (writes PDOA offset)

Place tag at a known bearing, then:

```bash
python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
  --non-interactive --mode pdoa --known-distance 1.0 --known-azimuth 0 \
  --samples 10 --force-setup
```

### Manual PDOA AT sequence (equivalent to `calibrate.py` setup)

```
AT+SETUWBMODE=0
AT+PDOASETCFG=1,1,4369,0,100,0,1    # tag
AT+PDOASETCFG=1,1,4369,0,100,1,1    # anchor (+ auth)
AT+USER_CMD=1                        # anchor
AT+FILTER=0                          # anchor
AT+SETCFG=0,1,1,1                    # anchor
AT+SETCFG=0,0,1,1                    # tag
AT+ADDTAG=0,0,1,1,0                  # anchor
AT+UWBSTART                          # anchor, then tag
```

---

## Critical Operational Learnings

1. **Both modules must use `id=0`** for this two-module TWR+PDOA setup (not 17845/0x45B5).
2. **Read PDOA from anchor** `Tag_Addr:` lines on `/dev/ttyUSB0`; parse angle as `int(Angle)/10.0` degrees.
3. **Flash erases NVM** — always `--force-setup` or full SETCFG/SAVE after `make flash-all`.
4. **`AT+SAVECFG` needs ≥8 s** before reset or role may not persist.
5. **Angles are noisy until azimuth calibration** — large spread is expected before `AT+PDOAOFF` / `calibrate.py` azimuth step.
6. **Do not use `SETUWBMODE=1`** for this hardware pair — it runs isolated example code, not the working vendor TWR+PDOA path.

---

## Diagnostic Quick Reference

| Field | Meaning |
|-------|---------|
| `pdor` | Last latched PDOA raw (non-zero = STS PDOA working) |
| `pdm` | PDOA mode register (3 = M3) |
| `stac` | STS accumulated symbol count |
| `ipoa` / `spoa` | Ipatov / STS phase of arrival |
| `send resp fault` | Anchor delayed Response TX failed (should be ≈ 0) |
| `Tag_Addr:… Angle:N` | N = tenths of degrees |

**Success:** non-zero `pdor`, `pdm:3`, stable `Tag_Addr` with `Range:1.xxx` and varying `Angle`.

---

## Remaining / Optional Work

| Item | Notes |
|------|--------|
| Distance calibration | ~760 mm bias at 1 m; run `calibrate.py` without `--dry-run` |
| Azimuth calibration | Place tag at known bearing; run full `calibrate.py --mode pdoa` |
| Angle stability | May improve after `pdoaOffset_deg` calibration; consider filtering in host |
| Occasional `send resp fault` | Rare (1–3 / 30 s); monitor under your antenna layout |
| Plain TWR mode | Forcing STS in `dwt_configure` when PDOA prep runs may affect non-PDOA TWR; gate on `USER_CMD` if needed |

---

## Related Docs

| File | Content |
|------|---------|
| `BU04_distance_achievements.md` | TWR-only milestone (prerequisite) |
| `BU04_firmware_work_summary.md` | Long debugging history, hardware setup |
| `BU04_session_2026-05-22_summary.md` | Earlier PDOA session (pre-fix state) |
| `calibrate.py` | Host setup, distance + azimuth calibration |
| `bu04_at.py` | AT wrapper, `parse_measurements()` for `Tag_Addr` |

---

*Built from custom GCC firmware in `Refs/STM32F103-BU0x_SDK`. Vendor objects: `node.o`, `tag.o`, `pdoa2XY` in `ds_twr_sts_sdc_responder.o`.*
