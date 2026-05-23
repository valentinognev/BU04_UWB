# BU04 TWR Distance Measurement — Achievements Summary

**Date:** 2026-05-22  
**Firmware:** V1.0.46 (custom GCC port)  
**Hardware:** Two Ai-Thinker BU04 modules (STM32F103 + DW3000), anchor `/dev/ttyUSB0`, tag `/dev/ttyUSB4`

---

## Goal

Measure real UWB two-way ranging (TWR) distance between two BU04 modules at ~1 m separation, using custom firmware built from `Refs/STM32F103-BU0x_SDK` and configured via `calibrate.py` or AT commands.

---

## Bottom Line

**TWR distance measurement is working.** With anchor `role=1`, tag `role=0`, and both modules on the same channel/network, the anchor reports stable range readings of **~1.57–1.60 m** (physical setup ~1 m, uncorrected antenna delay).

Example:

```
anchor AT+DISTANCE → distance: 1604 mm
calibrate.py dry-run → measured 1.590 m (known 1.000 m)
```

Calibration can now trim the ~590 mm bias via antenna delay (`AT+DEVCFG` / `calibrate.py`).

---

## What We Achieved

### 1. End-to-end TWR exchange on custom firmware

All three DS-TWR stages complete over the air:

| Stage | Message | Size | Status |
|-------|---------|------|--------|
| Tag → Anchor | Poll (`0x1a`) | 13 B | ✓ Anchor `l13` rising |
| Anchor → Tag | Response (`0x1b`) | 21 B | ✓ Tag `rlen:21`, `b9:27`, `rpf:1` |
| Tag → Anchor | Final (`0x1c`, STS) | 110 B | ✓ Anchor `rlen:110` |

### 2. Distance output on the anchor

This stack uses **DS-TWR with STS-SDC**: the **anchor (responder) computes range** from the Final message timestamps. Distance is **not** produced on the tag (`tag cb:0` is expected, not a failure).

- **Read range from the anchor:** `AT+DISTANCE` on `/dev/ttyUSB0`
- V1.0.46 also streams `distance:` lines from the anchor after each completed exchange

### 3. Custom GCC firmware port (V1.0.37 → V1.0.46)

Built and flashed from `Refs/STM32F103-BU0x_SDK` with OpenOCD + Makefile targets (`make flash-all`, `reset-all`).

Key fixes that made ranging work:

| Area | Fix |
|------|-----|
| **Poll targeting** | Binary patch: tag poll destination from `tag+0x1ce` (was hardcoded `0xFFFF` broadcast) |
| **Final targeting** | Binary patch: final message dest from `tag+0x1ce` |
| **SPI / DW3000** | SSM fix in `hal_spi_wrapper.c`; slow rate for init, fast rate after `tag_start()` / `node_start()` |
| **UART debug** | Suppress `_dbg_printf` during ranging (fault strings blocked delayed TX) |
| **Tag bootstrap** | Slot scheduler bootstrap, poll-dest sync, timing sync via `tag_bootstrap.c` |
| **Timing** | `tag_replyDly_us=450`; `tag_pollTxFinalTx_us=20000` (uint16 — **80000 overflowed to 14464**) |
| **Config sync** | Both `sfConfig` and `sfConfig_pdoa` updated in `f_uwbstart()` |
| **Wrong offset removed** | Stopped writing `tag+0x1ca` (not used by `tag.o`; could corrupt state) |
| **Anchor distance stream** | `node_twr_output_user()` calls `f_stream_distance()` on completed ranges |

### 4. Host tooling

**`calibrate.py`** updated to collect TWR samples from **anchor `AT+DISTANCE`** (STS-SDC mode), with tag UART stream as fallback.

Verified command:

```bash
cd /home/valentin/RL/UWB
python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
  --non-interactive --mode twr --known-distance 1.0 --samples 5 --dry-run --skip-setup
```

Result: 5 non-zero samples, measured **1.590 m**, suggested antenna delay **16239** (from 16336).

### 5. Diagnostics infrastructure

RX frame sniffing in `uwb_status.c` (`rlen`, `l13`, `pol`, `rsp`, `pan`, `b8`, `b9`, `rpf`, `rh`), TWR callback wraps in `twr_diag.c`, and streaming fields in `cmd_fn.c` — used to isolate each TWR stage.

---

## Critical Operational Learnings

1. **After `make flash-all`, reconfigure roles** — flash erases NVM; modules default to `id=65535 role=0`.
2. **`AT+SAVECFG` needs ≥8 s** before reset/reopen or anchor role may not persist.
3. **Verify before `AT+UWBSTART`:** anchor `Role:1`, tag `Role:0`, tag `anc_id=0`, net `4369`.
4. **`AT+SETCFG ch=1` → RF channel 5** (not literal channel 1).
5. **Both modules streaming identical tag-style lines** = both in tag mode, not an RF bug.
6. **Read distance from the anchor**, not the tag, for this firmware’s STS-SDC TWR mode.
7. **Build with `make -j1`** to avoid patch race errors on Keil object patches.

---

## Typical Workflow

```bash
# Build & flash
cd Refs/STM32F103-BU0x_SDK
make -j1 && make flash-all && make reset-all

# Configure & test (~1 m, antennas facing)
cd ../..
python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
  --non-interactive --mode twr --known-distance 1.0 --samples 10 --dry-run

# Live calibration (writes antenna delay to tag)
python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
  --non-interactive --mode twr --known-distance 1.0 --samples 10
```

Manual ranging check during active session:

```bash
# On anchor serial (/dev/ttyUSB0)
AT+DISTANCE
# → distance: 1576  (mm)
```

---

## Remaining / Optional Work

| Item | Notes |
|------|--------|
| Antenna delay calibration | ~590 mm bias at 1 m; run `calibrate.py` without `--dry-run` |
| Flash V1.0.46 on both modules | Enables anchor distance streaming on UART |
| Tag-side distance | Would require tag-local range calc or downlink; not how STS-SDC responder mode works today |
| `calibrate.py` anchor-role guard | Already waits 8 s after SAVE; could add explicit post-SAVE role verify |
| Vendor V1.0.0 hex | Tested earlier on same hardware — no distance; custom port now succeeds |

---

## Key File Paths

| Path | Purpose |
|------|---------|
| `Refs/STM32F103-BU0x_SDK/Components/APP/cmd_fn.c` | `f_uwbstart`, timing overrides, streaming |
| `Refs/STM32F103-BU0x_SDK/Components/APP/tag_bootstrap.c` | Slot bootstrap, poll dest, timing sync |
| `Refs/STM32F103-BU0x_SDK/Components/APP/uwb_status.c` | RX frame capture on RXFCG |
| `Refs/STM32F103-BU0x_SDK/Components/APP/user_out.c` | `node_twr_output_user` / `tag_twr_output_user` |
| `Refs/STM32F103-BU0x_SDK/Components/HAL/HAL/hal_spi_wrapper.c` | SPI SSM + slow/fast mode |
| `Refs/STM32F103-BU0x_SDK/Makefile` | `patch-tag-poll-dest`, `patch-tag-final-dest`, `patch-uwb-spi` |
| `calibrate.py` | Role setup, UWBSTART, anchor distance collection |
| `bu04_at.py` | AT command wrapper, `read_distance()` |

---

## Diagnostic Quick Reference

**Success:** non-zero anchor `AT+DISTANCE` or `distance:` stream; anchor receives Polls (`l13`); tag receives Responses (`rlen:21`, `b9:27`); anchor receives Finals (`rlen:110`).

**Misleading on tag:** `cb:0` — normal in STS-SDC mode (range computed on anchor).

| Field | Meaning |
|-------|---------|
| `l13` | 13-byte Poll frames received (anchor) |
| `rlen:21` / `b9:27` | Response received (tag) |
| `rpf:1` | Tag response-ready before Final |
| `rlen:110` | STS Final received (anchor) |
| `txf` | Delayed TX fail count (anchor should stay 0) |

---

*For detailed debugging history and earlier failed states (V1.0.36–V1.0.42), see `BU04_firmware_work_summary.md`.*
