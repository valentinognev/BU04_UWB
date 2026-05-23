# BU04 Custom Firmware — Work Summary & Handoff

Summary of custom GCC firmware port, dual-module flash/AT tooling, and TWR calibration work for Ai-Thinker BU04 UWB modules (STM32F103 + DW3000).

**Current firmware version:** `V1.0.46`  
**Last verified:** 2026-05-22 — **TWR distance measurement working** on custom GCC firmware. Anchor reports ~1.57–1.60 m at ~1 m physical separation (uncorrected antenna delay). See **`BU04_distance_achievements.md`** for the achievements summary.

**Hardware confirmed by user:** ring antennas facing each other, external power on both modules.

---

## Executive Summary

**Goal:** Get TWR distance measurements (~1 m) between two BU04 modules (anchor + tag).

**Status (V1.0.46):** **Working.** Full Poll → Response → Final exchange completes; anchor `AT+DISTANCE` returns ~1576–1604 mm. Use `calibrate.py` (reads anchor) or anchor `AT+DISTANCE` directly. Tag `cb:0` is expected — STS-SDC DS-TWR computes range on the anchor.

**Achievements doc:** [`BU04_distance_achievements.md`](BU04_distance_achievements.md)

---

## Executive Summary (historical — pre-V1.0.43 debugging)

<details>
<summary>Earlier state when ranging was still failing (click to expand)</summary>

**Bottom line (obsolete):** Ranging failed (`distance: 0`, `cb: 0`) on custom V1.0.42 and vendor V1.0.0 hex. Blockers varied by version: Response RX on tag (V1.0.42), Final TX (V1.0.36).

</details>

---

## Historical Debugging Notes (V1.0.42 and earlier)

**What we proved works (V1.0.42, roles verified):**
- AT commands, NVM role config, DW3000 SPI (`DEVID:0xDECA0312`)
- UWB stack runs (IRQ count rises during ranging)
- Tag state machine reaches Poll TX (`st:8`, `sa:1`, `txs` rising)
- **Anchor receives tag Polls** — 13-byte frames, `FC=0x4188`, `PAN=0x1111`, `type=0x1a` at byte 10, `dest=0x0000` (poll-dest patch)
- Anchor delayed Response TX without driver faults when UART debug is suppressed (`txf:0` on anchor)

**Where it breaks (current session, V1.0.42):**
- **Tag does not receive anchor Responses** — `rxg:0`, `rsp:0`, `rxt` climbs (~160/s); no `0x1b` in tag `rh`
- **`cb: 0`** — no completed TWR exchange, `distance: 0`
- Earlier (V1.0.36): tag *did* receive Response (`b9=27`, `rlen:21`) but **Final TX failed** (`final失败`, `txf:2`)

**Critical configuration trap (rediscovered 2026-05-22):**
- After `make flash-all`, `/dev/ttyUSB0` often stays at **`id=65535 role=0`** (factory default tag).
- **`AT+SAVECFG` needs ≥5 seconds** before reset/reopen — shorter waits cause SAVE to not persist; both modules then behave as tags and ranging cannot work.
- If **both ports stream identical tag-style `distance:` lines**, both MCUs are in tag mode — not an RF bug.
- Always verify with `AT+GETCFG` before `AT+UWBSTART`: anchor `Role:1`, tag `Role:0`.

**Current firmware:** V1.0.42 — poll/final dest patches, SPI fast-mode restore, full `_dbg_printf` suppression during ranging, widened tag timing, RX sniffing, deferred slot bootstrap.

---

## Hardware Setup

Two BU04 modules, each with its own ST-Link (SWD) and FT4232H UART (4 ports per chip; only port A used for AT).

| Module | FTDI serial | ST-Link | USB path | Notes |
|--------|-------------|---------|----------|-------|
| **Anchor** | `/dev/ttyUSB0` | ST-Link **V2.1** (0483:3752) | `3-3` | Serial `066CFF313050353043214925` |
| **Tag** | `/dev/ttyUSB4` | ST-Link **V2** (0483:3752) | `3-2.1.3` | No ASCII serial; select by hub path |

- **Baud:** 115200 on USART1 (PA9/PA10) via FTDI only — do **not** use ST-Link VCP (`/dev/ttyACM1`) for AT.
- **Physical:** modules ~1 m apart, ring antennas facing each other, **external power required** on both.
- **Other FTDI ports** (`ttyUSB1–3`, `5–7`): same hub chips, not wired to these modules' AT.

OpenOCD configs (SDK root):

| File | Purpose |
|------|---------|
| `openocd-common.cfg` | Shared SWD settings; `gdb_port disabled` |
| `openocd-anchor.cfg` | Anchor ST-Link V2.1 by serial |
| `openocd-tag.cfg` | Tag ST-Link V2 by USB path `3-2.1.3` |
| `openocd.cfg` | Defaults to anchor |

---

## Current Module State (as of last session)

After `calibrate.py` configure + `make reset-all` (wait ≥5 s after each SAVE):

```
/dev/ttyUSB0  →  V1.0.42,  id=0, role=1 (anchor), ch=5 (AT param ch=1), rate=6.8M
/dev/ttyUSB4  →  V1.0.42,  id=0, role=0 (tag),    ch=5, rate=6.8M
  PDOA network: 4369 (0x1111), tag anc_id=0
```

Factory defaults after flash/erase (before role setup):

```
id=65535, role=0  (both modules behave as unconfigured tags)
```

**Important:** `make flash-all` erases full flash including NVM — always re-run `SETCFG` + `SAVE` (or `calibrate.py`) after flashing.

---

## TWR Ranging Status (Active Issue)

### What works

| Check | Status |
|-------|--------|
| AT commands on both ports | OK |
| `DEVID:0xDECA0312` on `AT+UWBSTART` | OK (SPI + DW3000 reachable) |
| Role configuration (anchor=1, tag=0) | OK, persists after SAVE |
| UWB stack running | OK — `irq` count rises ~2000+ per 10 s during ranging |
| Tag state machine reaches poll TX | OK — `st:6` observed |
| Slot scheduler bootstrap | OK — `bst:1` after deferred bootstrap |
| **DW3000 chip-level TX/RX** | OK — `txs`/`rxg`/`rxf` rise on both sides |
| **RX frame header sniffing (V1.0.35+)** | OK — captures MAC headers on `RXFCG` before IRQ clears buffer |
| **Poll dest patch (V1.0.36)** | OK — tag sends unicast Poll to `anc_id` (was hardcoded `0xFFFF`) |
| **Poll RF exchange** | OK — anchor `l13` rises; headers show `type=0x1a` |
| **Response RF exchange** | OK — tag receives 21-byte frames with `type=0x1b` |
| **Vendor factory V1.0.0 hex** | Flashed and tested — **also no distance** |

### What does not work yet

| Symptom | Detail |
|---------|--------|
| `distance: 0` always | No non-zero samples from `calibrate.py` |
| `cb: 0` always | `tag_twr_output_user()` never called → TWR never completes |
| Tag `final失败` | Tag receives Response (`0x1b`) but Final TX fails |
| `ptx: 0`, `prx: 0`, `nrx: 0`, `ntx: 0` | Wrap counters unreliable (see note below) |
| Tag `rxt` high | Tag RX timeouts still occur alongside successful Response receives |
| Anchor `GETDLIST` empty | Normal for pure TWR mode; not necessarily an error |

### Diagnostic snapshot (V1.0.36, roles correct, ~1 m)

**Tag (after poll-dest patch):**
```
distance: 0 irq: 12016 cb: 0 txf: 2 tto: 0 ptx: 0 prx: 0 bst: 1 st: 9 pg: 0 sa: 1
txs: 1278 rxg: 639 rxf: 639 rxt: 4489 rxe: 0 sl: 0x0000
rlen: 21 l13: 0 pol: 0 pan: 639 b8: 0 b9: 27 rh: 8841B41111000000001B0F0000
```

**Anchor (after poll-dest patch):**
```
anc: nrx: 0 ntx: 0 ner: 0 st: 8 dn: 1 rf: 0 txs: 638 rxg: 1274 rxf: 1274 rxt: 0 rxe: 0 sl: 0x0000
rlen: 13 l13: 638 pol: 0 pan: 1274 b8: 0 b9: 26 rh: 8841B01111000000001ACBB113
```

**Decoded headers (from `rh` hex dump):**

| Direction | Len | FC | PAN | Dest | Type (byte 10) | Notes |
|-----------|-----|-----|-----|------|----------------|-------|
| Tag → Anchor (Poll) | 13 | `0x4188` | `0x1111` | `0x0000` | `0x1a` (26) | Was `dest=0xFFFF` before V1.0.36 patch |
| Anchor → Tag (Resp) | 21 | `0x4188` | `0x1111` | `0x0000` | `0x1b` (27) | Tag `b9=27` confirms Response type |

**TWR stage status (V1.0.42, roles correct, ~1 m):**

```
Tag --[Poll 0x1a, 13B]--> Anchor     ✓  (anchor l13≈195, rxg≈195, txf:0)
Anchor --[Resp 0x1b, 21B]--> Tag     ✗  (tag rxg:0, rsp:0, rxt rising)
Tag --[Final 0x1c, 110B STS]--> Anchor   ✗  (blocked; cb:0)
```

**TWR stage status (V1.0.36 best run — for comparison):**

```
Tag --[Poll 0x1a, 13B]--> Anchor     ✓
Anchor --[Resp 0x1b, 21B]--> Tag     ✓  (tag b9=27, rlen:21)
Tag --[Final]--> Anchor               ✗  (final失败, cb:0)
```

**Key insight:** Blocker depends on configuration and firmware state. With V1.0.42 and verified anchor role, exchange stalls at **Response RX on tag**. V1.0.36 showed Response working but **Final TX** failing.

### Vendor factory firmware test (2026-05-22)

Flashed `Refs/（2717）BU03_BU04-AT-通用-常规固件V1.0.0_T2024-07-26.hex` to both modules:

```bash
cd Refs/STM32F103-BU0x_SDK
make flash-vendor-refs-all
```

| Step | Result |
|------|--------|
| Flash + verify | OK on both |
| SETCFG+SAVE roles (anchor=1, tag=0, id=0) | OK |
| UWB via staggered `AT+SAVE` | Both enter UWB |
| Distance 30 s | **No output, 0 samples** |

Ranging failure is **not GCC-port-specific** — vendor V1.0.0 fails the same way on this hardware pair.

### Diagnostic field reference (V1.0.29+)

| Field | Meaning | Typical value (current issue) |
|-------|---------|-------------------------------|
| `irq` | EXTI/DW3000 interrupt count | Rising (~400/s) — stack active |
| `cb` | `twr_output_cb_count` — completed TWR exchanges | **0** (authoritative success metric) |
| `txf` | `dwt_tx_fail_count` — delayed TX failures in driver | 2 on tag |
| `tto` | `twr_rx_tag_timeout_cb` count — response RX timeouts | 0 |
| `ptx` | `twr_tx_tag_cb` count (linker `--wrap`) | **0** (unreliable) |
| `prx` | `twr_rx_tag_cb` count (linker `--wrap`) | **0** (unreliable) |
| `bst` | `tag_bootstrap_count` — slot scheduler bootstrap runs | 1 |
| `st` | Tag/anchor `testAppState` (offset 0) | Tag: 6/8/9; Anchor: 8 |
| `pg` | Poll gate (offset 0x231) | 0 |
| `sa` | Slot armed (offset 0x232) | 0 or 1 |
| `nrx` | Anchor `twr_rx_node_cb` count | **0** (unreliable) |
| `ntx` | Anchor `twr_tx_node_cb` count | **0** (unreliable) |
| `ner` | Anchor `twr_rx_error_cb` count | 0 |
| `dn` | Anchor `done` flag (offset 4) | 1 after poll-dest patch |
| `rf` | Anchor RX sub-flag (offset 0x1ab) | 0 |
| `txs` | DW3000 `SYS_STATUS` TXFRS bit hits | Rising on both modules |
| `rxg` | DW3000 `SYS_STATUS` RXFCG bit hits | Rising on both modules |
| `rxf` | DW3000 `SYS_STATUS` RXFR bit hits | Rising on both modules |
| `rxt` | DW3000 `SYS_STATUS` RXFTO bit hits | High on tag, ~0 on anchor |
| `rxe` | DW3000 RX error bits | Usually 0 |
| `sl` | Last sampled `SYS_STATUS` low 16 bits (hex) | Often `0x0000` after IRQ handler |
| **`rlen`** | Length of last good RX frame (from `RX_FINFO`) | 13 (anchor), 21 (tag) |
| **`l13`** | Count of 13-byte RX frames (Poll length) | High on anchor after patch |
| **`pol`** | Count of frames with TWR Poll type (`0x1a` at byte 10) | Rises on anchor after offset fix |
| **`rsp`** | Count of frames with TWR Response type (`0x1b` at byte 10) | **0 on tag (V1.0.42 blocker)** |
| **`pan`** | Count of frames with `PAN=0x1111` at bytes 3–4 | High on anchor when Polls received |
| **`b8`** | Byte 10 of last RX frame (msg type when len≥11) | 26 (0x1a) on anchor polls; 27 (0x1b) on tag responses |
| **`b9`** | Byte 9 of last RX frame (seq) | Varies |
| **`rpf`** | Tag+`0x1d2` response-ready flag before Final TX | 0 until Response processed |
| **`rh`** | Hex dump of first 13 bytes of last RX frame | See decoded headers above |

**Note on wrap counters (`ptx`/`prx`/`nrx`/`ntx`):** Linker `--wrap` only intercepts external symbol references. `tag.o`/`node.o` register real callback addresses with `dwt_setcallbacks()` at init, so wrap counters stay at 0 even when callbacks fire. **`cb: 0` is the authoritative “no completed range” indicator.** Use `rlen`/`l13`/`pol`/`rh` for message-layer visibility.

**Note on message type offset:** TWR message type byte is at **offset 10** in the RX MAC buffer (not offset 8). Tag TX code stores type at `r1+8` in its TX buffer, but received frames show `0x1a`/`0x1b` at byte 10 in sniffed headers.

### Tag state machine values (`st`)

From disassembly of precompiled `tag.o` (`tagtestapprun` jump table):

| `st` | Meaning (inferred) |
|------|---------------------|
| 0 | Init — configures DW3000, PAN, addresses, sets `st=1` |
| 1 | Poll TX — calls `tx_start()` for Poll frame |
| 5 | Poll TX failed — `dwt_starttx` returned error |
| 6 | Poll TX accepted — waiting for Response RX |
| 9 | Post-bootstrap scheduler wait — poll gate check |

Observed progression during ranging: **`st:6` (poll sent) → `st:5` (poll fail) → `st:9` (scheduler wait, `sa:1`, `pg:0`)**.

### Root cause timeline

1. **Early:** Anchor `send resp fault` (delayed TX on Response) — improved with SPI SSM fix + slow/fast rate split.
2. **V1.0.20–24:** `send resp fault` reduced; SPI `/32` during every `writetospi` transfer identified as timing killer.
3. **Mid:** Tag `发送final失败` (Final TX failure) — addressed by widening Poll→Final timing.
4. **V1.0.27–36:** Poll dest patch; RX sniffing confirms MAC headers; V1.0.36 best run shows Response reaching tag.
5. **V1.0.37–42 (2026-05-22 session):**
   - Final dest patch (`patch-tag-final-dest`) — Final frame dest from tag+`0x1ce` instead of `0xFFFF`.
   - `tag_sync_twr_timing()`, immediate + deferred bootstrap; fixed incorrect `anc_id` write to tag+`0x1c0`.
   - **SPI:** `writetospi_serial_hw` calls `SPI_ConfigFastRate` (/32) on every transfer — defeats `port_set_dw_ic_spi_fastrate` (/4). Fix: restore `/4` after each transfer when fast mode active (`uwb_spi_fast_mode` flag in `hal_spi_wrapper.c`). NOP-ing the SPI_Config call **breaks init** (DEVID never prints).
   - **UART:** `_dbg_printf` fault strings (`send resp fault`, etc.) print over polled USART1 during ranging and block IRQ path — **all `_dbg_printf` now suppressed** while `uwb_ranging_active`.
   - **Config trap:** Anchor on `/dev/ttyUSB0` often not saved as `role=1`; need **≥5 s after SAVE** before reset.
   - With correct roles: Poll works; tag Response RX still fails (`rxg:0`).

### Poll destination bug (fixed in V1.0.36)

Disassembly of `tag.o` `tagtestapprun` poll TX (offset `0x3ba`):

```
strh  pan,  [r1+2]   from tag+0x1c2
strh  0xFFFF, [r1+4]  dest hardcoded broadcast  ← bug
strh  src,  [r1+6]   from tag+0x1c0 (nodeAddr)
strb  0x1a, [r1+8]   message type (Poll)
```

**Fix:** Binary patch in Makefile (`patch-tag-poll-dest`) replaces `movw r2, #65535` with `ldrh r2, [r4, #462]` (tag+`0x1ce`). `tag_sync_poll_dest()` writes `s_pdoa.addr` (anc_id) to `0x1ce` before ranging starts.

Before patch: anchor `rh` showed `dest=0xFFFF`. After patch: `dest=0x0000`, anchor `l13` rises, anchor sends Responses (tag sees `0x1b`).

### Configuration trap (critical)

If both modules show `role=0` (tag), ranging will never work. This happens easily when:

- Flashing firmware without re-running SETCFG+SAVE
- Reading `/dev/ttyUSB0` while anchor is still factory default (`id=65535 role=0`)
- Starting UWB before roles are saved

**Always verify before UWBSTART:**
```
Anchor: role=1, id=0
Tag:    role=0, id=0, anc_id=0
```

Both ports streaming identical tag diagnostics is a sign that **both MCUs are in tag mode**.

---

## Session Discoveries (V1.0.37–V1.0.42, 2026-05-22)

### Configuration and roles

| Check | Finding |
|-------|---------|
| Factory default after flash | `id=65535`, `role=0` on both modules |
| `/dev/ttyUSB0` “anchor” port | Often **not** saved as anchor unless SETCFG+SAVE succeeds and **≥5 s** elapses before reset |
| Symptom: both stream `distance:` tag format | **Both modules are tags** — verify `AT+GETCFG` on each port |
| `AT+SETCFG` channel param | `ch=1` in AT command → **RF channel 5** (not “channel 1”); `ch=0` → channel 9 |
| Required before UWBSTART | Anchor: `Role:1 id:0`; Tag: `Role:0 id:0`; tag `PDOASETCFG` with `anc_id=0`, `net=4369` |

### SPI timing (precompiled `uwb.o`)

- Every `writetospi_serial_hw` / `readfromspi_serial_hw` call invokes `SPI_ConfigFastRate` at entry.
- Makefile patches `SPI_ConfigFastRate` prescaler from `/2` (36 MHz, breaks init) to `/32` (2.25 MHz).
- External `port_set_dw_ic_spi_fastrate()` in `hal_spi_wrapper.c` sets `/4` (~18 MHz), but the next SPI transfer resets to `/32`.
- **Fix (V1.0.41+):** Track `uwb_spi_fast_mode`; after each wrapped SPI transfer, re-apply `/4` if fast mode is active.
- **Do not NOP** the `SPI_ConfigFastRate` call in `writetospi` — init hangs (only `OK` on UWBSTART, no `DEVID`).

### UART blocking during ranging

- `_dbg_printf` uses polled `USART1_SendBuffer` (blocking, ~87 µs/char at 115200).
- Previous logic allowed TWR fault strings through during ranging (`send resp fault`, `final失败`, etc.).
- These prints run in the TWR hot path and can cause **delayed TX timestamp to pass** → `send resp fault`.
- **Fix (V1.0.42):** Suppress **all** `_dbg_printf` output while `uwb_ranging_active`. Diagnostics use `f_stream_distance()` / `f_stream_twr_diag()` only (SysTick, every 2–10 s).

### Anchor `send resp fault`

- Printed from precompiled `node.o` when `dwt_starttx` returns `DWT_ERROR` on delayed Response TX.
- Correlates with SPI `/32` during timestamp read + UART blocking.
- With V1.0.42 (SPI restore + UART suppress) and **correct anchor role**: fault string gone, anchor `l13`/`txs`/`rxg` rise, anchor `txf:0`.
- Tag still shows `rxg:0` — Response not received at tag DW3000.

### Tag-side diagnostics (V1.0.42, roles correct)

```
# Anchor (/dev/ttyUSB0)
anc: … st:8 dn:1 txs:195 rxg:195 l13:195 pol:1 txf:0
rh: 8841C21111000000001AC30E86   # Poll 0x1a received

# Tag (/dev/ttyUSB4)
distance: 0 cb: 0 st:8 sa:1 txs:220 rxg: 0 rxt:1756 rsp: 0 rpf: 0
rh: 00000000000000000000000000   # no Response captured
```

### Binary patches (Makefile)

| Patch | Target | Purpose |
|-------|--------|---------|
| `patch-uwb-spi` | `uwb.o` | Symbol rename for SPI wrapper; prescaler `/2`→`/32` |
| `patch-tag-poll-dest` | `tag.o` | Poll dest from tag+`0x1ce` (anc_id), not `0xFFFF` |
| `patch-tag-final-dest` | `tag.o` | Final dest from tag+`0x1ce`, not `0xFFFF` |

### Timing overrides (V1.0.42)

| Parameter | Factory default | Current override |
|-----------|-----------------|------------------|
| `tag_pollTxFinalTx_us` | 4800 µs | **80000 µs** |
| `tag_replyDly_us` | 400–1000 µs | **10000 µs** |

Applied in `f_uwbstart()` to both `sys_para.param_Config` and `app.pConfig`; `tag_sync_twr_timing()` re-syncs tag+`0x1cc` from config on tag stream ticks.

### `dwt_starttx` wrap removed

- `--wrap=dwt_starttx` was added for Final TX diagnostics then **removed** — caused tag RX regression (`rxg:0`). Use `dwt_tx_fail_count` (`txf:`) in stream instead.

---

## Session Discoveries (V1.0.29–V1.0.36)

### Disassembly of precompiled `tag.o`

Key call chain:
```
tag_start → tag_helper → tag_process_init → enable_deca_irq → tag_pdoa_task → tag_instance_run → tagtestapprun
```

- **`tag_instance_run` inner loop:** Clears `done` (offset 4), calls `tagtestapprun`, loops while return==0. Slot scheduler logic (poll gate, deadlines) runs only when return≠0.
- **Natural bootstrap path:** `tagtestapprun` state 1 sets `done=2`, return 2 → `tag_instance_run` runs slot setup at offset 0x232/0x231/0x238 (same as our bootstrap).
- **`--wrap=tag_helper` does not work:** `tag.o` calls `tag_helper` internally; wrap only affects external callers. Switched to wrapping `enable_deca_irq` (V1.0.30), then abandoned wrap entirely (V1.0.31+).
- **Poll destination:** Tag init copies `userConfig.nodeAddr` into tag instance offset `0x1c0` for Poll frame **source**. Dest was hardcoded `0xFFFF` in `tag.o` — **patched in V1.0.36** to read from tag+`0x1ce` (set from `s_pdoa.addr` / `anc_id`).
- **Setting `st=2` was wrong:** Offset 0 is `testAppState`; value 2 is `TA_TXRESP_WAIT_SEND`, not a scheduler bootstrap state. V1.0.30 bootstrap likely harmed the state machine.

### Anchor side (updated V1.0.36)

- Anchor `testapprun` init sets `st=7`, then `dwt_rxenable()` in state 7 → `st=8` (RX listen).
- After poll-dest patch: anchor receives 13-byte Polls (`l13` ≈ `txs`), `rh` shows `type=0x1a`, `dest=0x0000`.
- Anchor `node.o` `twr_uwb_process` validates message type at struct offset 11 (raw byte 10); Poll handler prepares Response (`prepare_twr_resp_msg`).
- Anchor also receives 110-byte frames (STS-SDC length from `node_process_init` `calculate_msg_time` with len=110).
- `nrx:0` wrap counter is unreliable; anchor clearly responds (tag receives `0x1b` frames, anchor `txs` rises).

### V1.0.34–36 DW3000 status and RX sniffing

- `uwb_status.c` wraps `process_deca_irq`, counts `SYS_STATUS` bits, and on `RXFCG` reads `RX_FINFO` + first 16 bytes via `dwt_readrxdata()` **before** the driver clears the buffer.
- Observed: anchor `rxg` ≈ 2× tag `txs`; tag `rxg` ≈ ½ tag `txs` (one Response per ~2 Polls).
- Anchor `rxt:0` while tag `rxt` is high — tag still has RX timeouts despite also receiving valid Responses.

---

## Original Problems & Resolutions

1. **`AT+SETCFG=0,...` → immediate `INIT FAILED`** — Resolved. Firmware called `node_start()` / `tag_start()` right after SETCFG, before SAVE. Modified to start only after `AT+SAVE` + reboot or `AT+UWBSTART`.
2. **Saved flash config auto-started UWB on boot** — Resolved. Binary telemetry flooded USART1, breaking AT. Implemented `workmode=2` boot sentinel which resets after starting UWB once.
3. **False positives in connection tests** — Resolved. Removed random `OK` characters in telemetry.
4. **GCC build issues** — Resolved. Garbled serial, empty fields, and broken printf formatting fixed.
5. **NVM at wrong flash page** — Resolved. Moved NVM to `0x0801FC00` (page 127) to avoid font `.rodata` overlaps.
6. **`DEV_ID: 0xFFFFFFFF` (SPI/Power Failure)** — Resolved. External power supply required; both chips return `0xDECA0312`.
7. **Double Initialization Conflict (`AT+UWBSTART`)** — Resolved in V1.0.18. Removed duplicate `dwt_initialise()` in `f_uwbstart`; library handles full init internally.
8. **Static `distance: 0` Output** — Partially resolved in V1.0.19/V1.0.20. Callbacks wired in `user_out.c`, but callback never fires until TWR completes (`cb: 0`).
9. **SPI1 dropping to slave mode (reads 0xFF)** — Resolved. `hal_spi_wrapper.c` forces SSM=1 before each SPI transaction; split slow (/32) init vs fast (/4) runtime rates.
10. **Final TX failure (`发送final失败`)** — Improved in V1.0.25+. Widened `tag_pollTxFinalTx_us` to 20000 µs; suppressed non-fault `_dbg_printf` during ranging.
11. **Tag slot scheduler / TWR completion** — V1.0.30–34: deferred bootstrap lets tag reach Poll TX; chip-level TX/RX confirmed.
12. **Poll/Response RF exchange (V1.0.35–36)** — RX header sniffing confirms Poll (`0x1a`, 13B) and Response (`0x1b`, 21B). Poll dest patch fixes broadcast→unicast. **Final TX still fails** (`cb: 0`).

---

## Custom Firmware (GCC + OpenOCD)

**Source:** `Refs/STM32F103-BU0x_SDK/` (official Ai-Thinker SDK, built with `arm-none-eabi-gcc`).

### Build & deploy

```bash
cd Refs/STM32F103-BU0x_SDK
make              # build only → build/bu04_firmware.bin (~84 KB text)
make flash-anchor # anchor ST-Link only
make flash-tag    # tag ST-Link only (see tag flash quirk below)
make flash-all    # both sequentially — erases NVM; re-configure roles after!
make reset-anchor / make reset-tag / make reset-all
make list-stlinks # verify USB mapping
make deploy       # erase NVM (anchor) + flash anchor
make deploy-all   # erase NVM + flash both
make erase-config / make erase-config-tag  # NVM page 127 only
make flash-vendor-refs-all                 # flash vendor hex from Refs/ (both modules)
```

### Firmware changes by version

| Version | Change |
|---------|--------|
| **V1.0.10** | **`AT+UWBSTART`** — start UWB without NVM reboot; **`AT+GETDEV` / `AT+DISTANCE`** use integer formatting (distance in **mm**) |
| **V1.0.11** | Fix `f_setuwbmode` return string; add `f_stream_distance()` called from SysTick every 200 ms |
| **V1.0.12** | Add `DEV_ID` SPI diagnostic before starting library |
| **V1.0.18** | Fix double-init conflict: simplified `f_uwbstart` to a lightweight SPI read |
| **V1.0.19** | Wire `node_twr_output_user` and `tag_twr_output_user` to write global `distance` |
| **V1.0.20** | Debug output in `tag_twr_output_user` for non-zero distance values |
| **V1.0.21–V1.0.23** | SPI wrapper (`hal_spi_wrapper.c`): SSM fix, slow/fast rate split; Makefile symbol renames for precompiled `uwb.o` |
| **V1.0.24** | `_dbg_printf` suppressed during ranging; `f_stream_distance()` adds `irq:` and `cb:` fields; anchor streaming disabled |
| **V1.0.25** | Bump tag TWR timing before `tag_start()`: `tag_pollTxFinalTx_us=10000`, `tag_replyDly_us=800` |
| **V1.0.26** | Default timing in `default_config.h`; tag-only SysTick heartbeat every 3 s (later removed) |
| **V1.0.27** | Timing → 20000/1000 µs; selective fault print passthrough in `_dbg_printf`; `dwt_tx_fail_count` in `deca_device.c`; print timing at UWBSTART |
| **V1.0.28** | `twr_diag.c`: wrap timeout/error callbacks; stream adds `txf:` and `tto:`; one-shot 10 s diagnostic from SysTick |
| **V1.0.29** | Wrap `twr_tx_tag_cb`, `twr_rx_tag_cb`, `twr_tx_node_cb`, `twr_rx_node_cb`; anchor one-shot diag (`anc: nrx ntx ner`); `f_pdoasetcfg`/`f_setuwbmode` sync `sys_para` |
| **V1.0.30** | `tag_bootstrap.c`: slot scheduler bootstrap via `--wrap=enable_deca_irq` — **regressed** (IRQ≈2, bootstrap too early) |
| **V1.0.31** | Remove `enable_deca_irq` wrap; defer bootstrap to SysTick +100 ms; fix bootstrap state value; add `bst:` counter |
| **V1.0.32** | Periodic tag stream every 2 s; add `st:`, `pg:`, `sa:` scheduler diagnostics |
| **V1.0.33** | Periodic anchor stream every 2 s (`anc: nrx ntx ner`); bootstrap matches natural `return==2` path |
| **V1.0.34** | `uwb_status.c`: wrap `process_deca_irq`, sample `SYS_STATUS` (`txs rxg rxf rxt rxe sl`); anchor adds `st dn rf`; Makefile `flash-vendor-refs-all` |
| **V1.0.35** | RX frame header capture on `RXFCG`: `rlen`, `l13`, `pol`, `pan`, `rh` (8 bytes) |
| **V1.0.36** | Poll dest binary patch (`patch-tag-poll-dest` in Makefile); `tag+0x1ce` + `tag_sync_poll_dest()`; extended `rh` to 13 bytes, add `b8`/`b9`; poll type offset fixed to byte 10; timing → 40000/1500 µs |
| **V1.0.37** | Final dest patch (`patch-tag-final-dest`); `tag_sync_twr_timing()`; immediate bootstrap in `f_uwbstart()`; fix bootstrap writing `anc_id` to wrong offset (`0x1c0`); timing → 80000/2000 µs; `tag_read_resp_flag()` (`rpf:`) |
| **V1.0.38–39** | SPI `SPI_ConfigFastRate` literal patch experiments (/4); reverted NOP patch (breaks init) |
| **V1.0.40–41** | SPI fast-mode restore after each transfer (`uwb_spi_fast_mode` in `hal_spi_wrapper.c`) |
| **V1.0.42** | Suppress **all** `_dbg_printf` during ranging; anchor stream `txf:`; anchor diag interval 10 s; `tag_replyDly_us` → 10000 µs |

### Key source files

| File | Purpose |
|------|---------|
| `Components/APP/cmd_fn.c` | `f_uwbstart`, `f_setcfg`, `f_stream_distance`, `f_stream_twr_diag`, timing patch before `tag_start()` |
| `Components/APP/user_out.c` | `tag_twr_output_user`, `twr_output_cb_count`, distance callback + stream on complete |
| `Components/APP/twr_diag.c` | TWR stage counters via linker `--wrap` (tag/anchor TX/RX, timeout, error) |
| `Components/APP/tag_bootstrap.c` | Deferred slot scheduler bootstrap; poll dest at tag+`0x1ce`; `tag_sync_poll_dest()` |
| `Components/APP/uwb_status.c` | DW3000 `SYS_STATUS` sampling + RX frame header capture on `RXFCG` |
| `Makefile` | `patch-uwb-spi`, `patch-tag-poll-dest`, **`patch-tag-final-dest`** |
| `Components/HAL/HAL/hal_spi_wrapper.c` | SPI SSM fix; slow/fast rate; **`uwb_spi_fast_mode` restore after each SPI xfer** |
| `Components/HAL/HAL/hal_usart.c` | **`_dbg_printf` fully suppressed during ranging** (no fault passthrough) |
| `Components/Main/stm32f10x_it.c` | `uwb_ranging_active`, EXTI IRQ count, deferred bootstrap tick, 2 s streaming |
| `Components/Main/main.c` | Sets `tag_bootstrap_pending` before `tag_start()` on SAVE boot path |
| `Components/HAL/DW/decadriver/deca_device.c` | `dwt_tx_fail_count` on delayed TX error |
| `Components/HAL/DW/twr_pdoa/inc/default_config.h` | Default TWR timing constants |
| `build/keil_objs/twr/tag.o`, `node.o` | Precompiled TWR library (not source; disassembled for debug) |

### TWR timing overrides (GCC port)

Default library values are too tight once SPI latency and IRQ handling are accounted for:

| Parameter | Factory default | Current override (V1.0.42) |
|-----------|-----------------|---------------------------|
| `tag_pollTxFinalTx_us` | 4800 µs | **80000 µs** |
| `tag_replyDly_us` | 400 µs | **10000 µs** |

Applied in both `sys_para.param_Config` and `app.pConfig` immediately before `tag_start()` in `f_uwbstart()`.

### Streaming behavior (V1.0.42)

| Role | Interval | Output |
|------|----------|--------|
| Tag (role=0) | Every 2 s | `distance … irq cb txf tto ptx prx bst st pg sa txs rxg … rlen l13 pol rsp pan b8 b9 rpf rh` |
| Anchor (role=1) | Every 10 s | `anc: nrx ntx ner st dn rf txs rxg … txf rlen l13 pol … rh` |
| Both | Once at 10 s | One-shot duplicate via `f_stream_twr_diag()` |

Distance is also streamed from `tag_twr_output_user()` on each completed range (when `cb>0`).

---

## OpenOCD / Flash Issues & Fixes

- **Tag `reset halt` timeout**: SRST often leaves PC in SRAM instead of flash reset vector.
- **Tag HardFault during flash** (`pc: 0xfffffffe`): use `OPENOCD_FLASH_BODY_TAG` (halt + erase + write + vector boot, no `reset halt`).
- **Tag won't boot after flash**: Resolved via **`OPENOCD_TAG_BOOT`** in Makefile:
  ```makefile
  VECTOR_SP = $(shell python3 -c "import struct;print('0x%08x'%struct.unpack('<I',open('$(TARGET).bin','rb').read(4))[0])")
  VECTOR_PC = $(shell python3 -c "import struct;print('0x%08x'%struct.unpack('<I',open('$(TARGET).bin','rb').read()[4:8])[0])")
  OPENOCD_TAG_BOOT = reg sp $(VECTOR_SP); reg pc $(VECTOR_PC); resume
  ```
- **Anchor silent after flash**: may need `make flash-anchor` + `make reset-all`; wait a few seconds before AT.

---

## Serial & AT Verification

```bash
python3 test_connection.py --port /dev/ttyUSB0 --boot-wait 3
python3 test_connection.py --port /dev/ttyUSB4 --boot-wait 5
```

---

## Role Configuration

Firmware format (4 parameters):

```
AT+SETCFG=<id>,<role>,<ch>,<rate>
```

| Param | Meaning |
|-------|---------|
| `id` | Device ID (`0`–`10`; factory default `65535`) |
| `role` | `0` = tag, `1` = anchor |
| `ch` | `0` = channel 9, `1` = channel 5 |
| `rate` | `0` = 850k, `1` = 6.8M |

Example — anchor + tag, both id=0:

```
# Anchor (USB0)
AT+SETUWBMODE=0
AT+SETCFG=0,1,1,1
AT+SAVE

# Tag (USB4)
AT+SETUWBMODE=0
AT+SETCFG=0,0,1,1
AT+SAVE
AT+PDOASETCFG=1,1,4369,0,100,1,0   # anc_id=0 (not factory 65535)
```

Then **`make reset-all`** — returns both to AT mode with persisted roles.

---

## Python Tools (repo root)

| Script | Purpose |
|------|---------|
| `test_connection.py` | AT / GETVER / GETCFG smoke test |
| `bu04_at.py` | `Bu04Device` module manager; `DISTANCE_RE` parses optional diagnostic fields |
| `calibrate.py` | Two-module anchor/tag setup, TWR/PDOA calibration, runtime diagnosis |

### calibrate.py workflow

```bash
# Setup + run ranging (dry-run collects samples without writing calibration)
python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
  --non-interactive --mode twr --known-distance 1.0 --samples 10 --dry-run
```

**Python changes:**
- TWR mode sets tag `AT+PDOASETCFG` with `anc_id=0` (factory default 65535 breaks ranging at id=0)
- Diagnosis reports tag `anc_id`, notes when `cb=0` but IRQs active
- `DISTANCE_RE` extended for optional `irq`, `cb`, `txf`, `tto` fields (not yet `st`/`pg`/`sa`/`bst`)

---

## Test Commands

```bash
# Build + flash both (re-configure roles after!)
cd Refs/STM32F103-BU0x_SDK && make flash-all

# Full calibration dry-run
cd /home/valentin/RL/UWB
python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 \
  --non-interactive --mode twr --known-distance 1.0 --samples 10 --dry-run
```

### Dual-port capture during ranging

Monitor both ports while UWB is active:

```bash
# After calibrate.py or manual SETCFG+SAVE+UWBSTART:
# Tag (USB4):  distance: … st: … pg: … sa: …
# Anchor (USB0): anc: nrx: … ntx: … ner: …
```

Success criteria during ranging:
- Tag: `pol>0` or anchor `l13>0` (Poll frames received); tag `b9=27` (Response received); eventually **`cb>0`**, non-zero `distance`
- Anchor: `l13` rising, `rh` shows `…1A…` (Poll type 0x1a at byte 10)

---

## What Remains

1. **Tag Response RX failure (current blocker, V1.0.42)** — With verified anchor role, anchor receives Polls (`l13` rising) but tag never receives Responses (`rxg:0`, `rsp:0`). Investigate:
   - `prepare_twr_resp_msg` in `node.o` — delayed-TX timestamp margin, dest address, frame length (21 B non-STS)
   - Tag RX window after Poll TX — `tag_replyDly_us`, state `st:8`, `rxt` timeouts
   - Whether anchor Response actually leaves the antenna vs. driver status only (`txs` vs OTA)
2. **Tag Final TX failure (V1.0.36 path)** — When Response *does* reach tag (`b9=27`), Final TX fails (`final失败`). Investigate `prepare_twr_final_msg`, 110-byte STS frame, `tag_pollTxFinalTx_us` at tag+`0x1cc`.
3. **Get `cb > 0` and non-zero distance** at ~1 m — ultimate success criteria.
4. **Operational reliability:**
   - Ensure `calibrate.py` / manual setup waits **≥5 s after SAVE** before reset
   - Always verify anchor `Role:1` on `/dev/ttyUSB0` before ranging
5. **Code improvements:**
   - Disassemble / patch `node.o` response timing if margin insufficient
   - Add tag-side Response RX stage counter (beyond `rsp:` sniff)
   - Extend `bu04_at.py` `DISTANCE_RE` for new sniff fields
6. **Hardware checks:** swap modules, try `rate=0` (850k), verify antenna orientation (~0.5–2 m).

### Build note

Use `make -j1` (not `-j4`) — parallel build can race `extract-keil-objs` vs `patch-uwb-spi`/`patch-tag-*-dest`, causing linker errors for `writetospi_serial_hw`.

After `make flash-all`, always re-run SETCFG+SAVE on **both** modules; wait **≥5 s** after each SAVE before `make reset-all`.

---

## Git / Repo Layout

| Path | Repo | Notes |
|------|------|-------|
| `/home/valentin/RL/UWB/` | Main repo | Python tools, Refs/ |
| `/home/valentin/RL/UWB/Refs/STM32F103-BU0x_SDK/` | Nested git repo | Firmware, Makefile, OpenOCD |
