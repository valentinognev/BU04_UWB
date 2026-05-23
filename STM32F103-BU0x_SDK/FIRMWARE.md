# BU04 Custom Firmware — STM32F103 + DW3000

Custom **GCC + OpenOCD** port of the Ai-Thinker BU03/BU04 SDK for the **STM32F103CB** (128 KB flash) and **Qorvo DW3000** UWB transceiver.

**Version:** `V1.0.47` (`Components/APP/Generic.h`)  
**Target binary:** `build/bu04_firmware.bin` / `build/bu04_firmware.hex`

---

## Capabilities

| Feature | Mechanism |
|---------|-----------|
| AT command interface | USART1 @ 115200 (PA9/PA10) |
| TWR ranging | Precompiled `node.o` (anchor) / `tag.o` (tag), DS-TWR with STS-SDC |
| PDOA azimuth | STS+SDC + M3 PDOA mode; angle latched on Poll RX; `Tag_Addr:` UART output |
| UWB data transfer | Custom `uwb_data.c`; message type `0xD0`; up to 1010 B payload |
| NVM config | Flash page 127 (`0x0801FC00`); persists role, PDOA, calibration |

---

## Hardware Interfaces

| Interface | Pins / device | Usage |
|-----------|---------------|-------|
| USART1 | PA9 TX, PA10 RX | AT commands and measurement streams (FT4232H) |
| USART2 | — | WiFi module path (unused in typical setup) |
| USB | — | Optional; `data_tx_msg()` telemetry |
| SPI1 | DW3000 | Wrapped for SSM fix and slow/fast prescaler |
| EXTI | DW3000 IRQ | `process_deca_irq` wrapper for status + PDOA/data sniff |
| SWD | ST-Link | Flash and debug via OpenOCD |

**DW3000 device ID:** expect `DEVID:0xDECA0312` on `AT+UWBSTART` (C0 PDOA-capable part).

---

## Build System

```bash
cd STM32F103-BU0x_SDK

make -j1              # build only
make flash-anchor     # anchor ST-Link (openocd-anchor.cfg)
make flash-tag        # tag ST-Link (openocd-tag.cfg)
make flash-all        # both — erases NVM; reconfigure roles after!
make reset-all        # NVIC reset both targets
make list-stlinks     # verify USB mapping
make erase-config     # NVM page only (anchor)
make flash-vendor-refs-all   # flash vendor hex from ../Refs/ (both modules)
```

**Toolchain:** `arm-none-eabi-gcc`, `openocd`  
**Linker script:** `linker/STM32F103CB.ld`  
**Always use `make -j1`** — parallel builds can race `extract-keil-objs` vs binary patches.

### OpenOCD configs

| File | Purpose |
|------|---------|
| `openocd-common.cfg` | Shared SWD; `gdb_port disabled` |
| `openocd-anchor.cfg` | Anchor ST-Link (serial-based) |
| `openocd-tag.cfg` | Tag ST-Link (USB path-based) |
| `openocd.cfg` | Default → anchor |

Tag flash uses a special boot path (`OPENOCD_TAG_BOOT`) when `reset halt` fails — see Makefile comments.

---

## Runtime Architecture

### Boot and config

1. `main.c` loads NVM into `sys_para`; if invalid, factory defaults (`id=65535`, `role=0`).
2. `AT+SETCFG` updates RAM config; **`AT+SAVE`** writes NVM and reboots.
3. **`AT+UWBSTART`** starts ranging without requiring another reboot:
   - SPI sanity check (`DEVID`)
   - `node_start()` or `tag_start()` from precompiled libraries
   - Custom hooks: `tag_bootstrap.c`, `uwb_pdoa_apply_after_start()`, `uwb_data_init()`

### UWB mode selection

| `AT+SETUWBMODE` | `twr_pdoa_mode` | Stack |
|-----------------|-----------------|-------|
| `0` | 0 | **`node.o` / `tag.o`** — working TWR + PDOA path |
| `1` | 1 | `ds_twr_sts_sdc_*` example objects — **not used** for this project |

Use **`SETUWBMODE=0`** for all production workflows.

### TWR exchange (STS-SDC)

```
Tag --[Poll 0x1a, 13 B]--> Anchor
Anchor --[Response 0x1b, 21 B]--> Tag
Tag --[Final 0x1c, 110 B STS]--> Anchor  →  anchor computes distance
```

- Range is computed on the **anchor** (responder).
- `node_twr_output_user()` / `f_stream_distance()` emit `distance:` (mm).
- Tag `cb:0` in stream is normal when range is anchor-side only.

### PDOA path

1. `uwb_pdoa_prepare_config()` forces STS+SDC, STS length 64, `PDOAMODE_M3`.
2. On anchor Poll RX, `__wrap_process_deca_irq` runs vendor handler **first**, then latches `dwt_readpdoa()`.
3. `node_pdoa_output_user()` prints `Tag_Addr:` via direct `USART1_SendBuffer` (bypasses `_dbg_printf` suppression).
4. Angle: `(raw / 2048) * 180 / π` degrees; UART field is **tenths of degrees**.

### UWB data path (V1.0.47)

1. Host: `AT+UWBDATA=<dest>,<len>` + binary body, or inline hex for ≤32 B.
2. `uwb_data_queue_tx()` holds one pending packet.
3. `uwb_data_tick()` (SysTick, ~500 ms rate limit) transmits type `0xD0` frame.
4. Receiver: non-TWR lengths captured **before** vendor IRQ handler → `+UWBDATA:<src>,<len>` + raw bytes.
5. `App_Module_Process_USART_CMD()` runs during ranging so AT works while UWB is active.

---

## Binary Patches (Makefile)

Precompiled Keil objects are patched at build time:

| Patch target | Purpose |
|--------------|---------|
| `patch-uwb-spi` | Rename SPI symbols; prescaler `/2`→`/32` in `uwb.o` |
| `patch-tag-poll-dest` | Poll destination from `tag+0x1ce` (anc_id), not `0xFFFF` |
| `patch-tag-final-dest` | Final destination from `tag+0x1ce`, not `0xFFFF` |

`tag_sync_poll_dest()` writes `s_pdoa.addr` into `tag+0x1ce` before ranging.

---

## Linker Wraps

From `Makefile` `LDFLAGS`:

- `process_deca_irq` — DW3000 status sampling, RX header sniff, PDOA/data latch
- `dwt_configure` — STS+SDC defaults for PDOA
- `port_set_dw_ic_spi_slowrate` / `port_set_dw_ic_spi_fastrate` — SPI wrapper
- TWR callbacks (`twr_tx_tag_cb`, `twr_rx_tag_cb`, `twr_tx_node_cb`, `twr_rx_node_cb`, timeout/error) — diagnostics in `twr_diag.c`

**Note:** Wrap counters (`ptx`, `prx`, `nrx`, `ntx`) are unreliable when the real symbol is defined inside `tag.o`/`node.o`. Use `cb`, `rlen`, `l13`, `rh` instead.

---

## Key Source Files

| File | Role |
|------|------|
| `Components/APP/cmd_fn.c` | All AT handlers; `f_uwbstart`, timing overrides, command table |
| `Components/APP/Generic_cmd.c` | UART/USB command routing; binary upload after `AT+UWBDATA` header |
| `Components/APP/user_out.c` | TWR distance callback; `Tag_Addr:` PDOA output |
| `Components/APP/tag_bootstrap.c` | Slot scheduler bootstrap; poll dest sync; TWR timing sync |
| `Components/APP/uwb_status.c` | IRQ wrap, `SYS_STATUS` bits, RX frame capture |
| `Components/APP/uwb_pdoa.c` | PDOA config, STS apply, angle conversion |
| `Components/APP/uwb_data.c` | UWB data TX queue, RX delivery, frame build |
| `Components/APP/twr_diag.c` | TWR stage counters via linker wrap |
| `Components/HAL/HAL/hal_spi_wrapper.c` | SSM fix; slow init / fast runtime; post-xfer rate restore |
| `Components/HAL/HAL/hal_usart.c` | `_dbg_printf` suppressed during `uwb_ranging_active` |
| `Components/Main/stm32f10x_it.c` | SysTick: streaming, bootstrap deferral, AT + `uwb_data_tick` |
| `Components/HAL/DW/twr_pdoa/inc/default_config.h` | Default TWR timing, STS, extended PHR |
| `build/keil_objs/twr/tag.o`, `node.o` | Precompiled vendor TWR library (disassembled for debug) |

---

## TWR Timing Overrides

Factory defaults are too tight for GCC port latency. Applied in `f_uwbstart()` to both `sfConfig` and `sfConfig_pdoa`:

| Parameter | Vendor default | Current override |
|-----------|----------------|------------------|
| `tag_pollTxFinalTx_us` | 4800 µs | **20000 µs** (uint16 — do not use 80000) |
| `tag_replyDly_us` | 400–1000 µs | **450 µs** (`DEFAULT_TAG_POLL_TX_RESPONSE_RX_US`) |

---

## UART Output During Ranging

| Role | Interval | Content |
|------|----------|---------|
| Tag | ~2 s | `distance: … irq cb txf … rlen l13 pol rsp rh` |
| Anchor | ~10 s | `anc: nrx ntx ner st dn rf txs … pdor pdm …` |
| On range complete | Event | `distance: <mm>` from anchor |
| PDOA | Event | `Tag_Addr:… Range:… Angle:…` |
| UWB data RX | Event | `+UWBDATA:<src>,<len>` + binary |

`_dbg_printf` is fully suppressed while ranging to avoid blocking delayed TX.

---

## Version History (summary)

| Version | Highlights |
|---------|------------|
| V1.0.10 | `AT+UWBSTART`; distance in mm |
| V1.0.18 | Fix double `dwt_initialise` in `f_uwbstart` |
| V1.0.19–24 | Distance callbacks; SPI wrapper; `_dbg_printf` suppression |
| V1.0.28–35 | TWR diagnostics; RX header sniffing (`rlen`, `l13`, `rh`) |
| V1.0.36 | Poll dest binary patch |
| V1.0.37 | Final dest patch; timing sync |
| V1.0.40–42 | SPI fast-mode restore; full debug suppress |
| V1.0.46 | **TWR + PDOA working**; `uwb_pdoa.c`; anchor distance stream |
| **V1.0.47** | **`AT+UWBDATA`**; extended PHR; `uwb_data.c` |

Full debugging timeline: `../Refs/BU04_firmware_work_summary.md`

---

## Diagnostic Quick Reference

| Field | Meaning |
|-------|---------|
| `cb` | Completed TWR exchanges — **0 = no range yet** |
| `l13` | 13-byte Poll frames received (anchor) |
| `rlen:21`, `b9:27` | Response received (tag) |
| `rlen:110` | STS Final received (anchor) |
| `txf` | Delayed TX failures — should stay 0 |
| `pdor` | Latched PDOA raw (non-zero = PDOA working) |
| `send resp fault` | Anchor Response TX failed — should be ≈0 |

**Success (TWR):** anchor `AT+DISTANCE` non-zero; anchor `l13` rising; tag `rlen:21`.  
**Success (PDOA):** `Tag_Addr:` with non-zero `Angle` and `Range:1.xxx`.  
**Success (data):** `+UWBDATA` on peer within ~5–20 s of send.

---

## Related Documentation

- Project workflow: [`../Refs/BU04_project_guide.md`](../Refs/BU04_project_guide.md)
- AT commands: [`../Refs/BU03_BU04_AT_commands.md`](../Refs/BU03_BU04_AT_commands.md)
- TWR / PDOA / UWBDATA notes: [`../Refs/BU04_*_achievements.md`](../Refs/)
