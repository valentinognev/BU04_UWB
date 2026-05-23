# BU03/BU04 AT Command Reference

Sources:

- Vendor: `BU03_BU04_AT_command_en_v1.0.6.pdf`
- Custom GCC firmware: `STM32F103-BU0x_SDK/` (currently **V1.0.47**)

All commands respond with `OK` on success or `ERR` on failure unless otherwise noted.

---

## 1. Universal Commands

| Command | Description |
|---------|-------------|
| `AT` | Test whether the AT framework is working properly. |
| `AT+SAVE` | Save the current configuration to non-volatile storage, then **reboot**. Wait **≥8 s** before the next AT session. |
| `AT+GETVER` | Get the software version number. Example response: `getver software:V1.0.47` |
| `AT+RESTART` | Reset and restart the device. |
| `AT+RESTORE` | Restore factory default settings. |
| `AT+GETWORKMODE` | Query the working mode. `0` = normal working mode, `1` = production test mode. |
| `AT+SETWORKMODE=X` | Set the working mode. `X`: `0` = normal, `1` = production test. |
| `AT+GETCFG` | Get configuration information. |
| `AT+SETCFG=…` | Set configuration. Run `AT+SAVE` afterward to persist. See **§1.1** for format differences. |

### 1.1 `AT+SETCFG` — vendor vs custom firmware

**Vendor PDF format** (5 parameters):

```
AT+SETCFG=<id>,<role>,<ch>,<rate>,<group>
```

| Parameter | Meaning |
|-----------|---------|
| id | Device ID (`0`–`10`; factory default `65535`) |
| role | `0` = tag, `1` = anchor |
| ch | `0` = RF channel 9, `1` = RF channel 5 |
| rate | `1` = 6.8M (only rate commonly used) |
| group | Node group (`0`–`255`) |

Example: `AT+SETCFG=0,1,0,1,1` → ID:0, Role:1 (anchor), CH:0, Rate:1, Group:1

**Custom GCC firmware format** (4 parameters — group not exposed on CLI):

```
AT+SETCFG=<id>,<role>,<ch>,<rate>
```

Example: `AT+SETCFG=0,1,1,1` → ID:0, Role:1 (anchor), CH param 1 (RF channel 5), Rate:1 (6.8M)

Response: `setcfg ID:0, Role:1, CH:1, Rate:1`

**Important:** the `ch` parameter is an **index**, not the RF channel number: `ch=1` → **RF channel 5**, `ch=0` → **RF channel 9**.

### 1.2 `AT+GETCFG` response

Custom firmware example:

```
getcfg ID:0, Role:1, CH:1, Rate:1
```

Always verify anchor `Role:1` and tag `Role:0` before `AT+UWBSTART`.

---

## 2. Development Board Production Test Commands

| Command | Description |
|---------|-------------|
| `AT+GETSENSOR` | Read accelerometer and angle sensor values for production testing. **BU03 only** — BU04 hardware does not support this. |
| `AT+TESTLED=X` | Start/stop LED (flow light) test. Use before `AT+SETCFG`. `X`: `1` = start, `0` = stop. |
| `AT+TESTOLED=<text>` | Test the OLED display. Use before `AT+SETCFG`. Example: `AT+TESTOLED=HELLO TEST!` |
| `AT+DISTANCE` | Read the last computed range. Example response: `distance: 1576` (**millimeters**, not meters). |

On custom firmware, the anchor also streams `distance:` lines periodically and after each completed TWR exchange while ranging is active.

---

## 3. TWR Algorithm Commands

| Command | Description |
|---------|-------------|
| `AT+GETDEV` | Get device tuning coefficients / algorithm settings. |
| `AT+SETDEV=X1,X2,X3,X4,X5,X6,X7,X8,X9` | Set device tuning coefficients. Run `AT+SAVE` afterward to persist. |

### `AT+SETDEV` Parameters

| Parameter | Meaning |
|-----------|---------|
| X1 | Tag capacity (tag refresh rate) |
| X2 | Antenna delay parameter |
| X3 | Kalman filter enable (`0`/`1`) |
| X4 | Kalman filter parameter Q |
| X5 | Kalman filter parameter R |
| X6 | Correction parameter a |
| X7 | Correction parameter b |
| X8 | Positioning enable bit |
| X9 | Positioning dimension settings |

Example: `AT+SETDEV=5,16336,1,0.018,0.642,1.0000,0.00,0,0`

### `AT+GETDEV` response (custom firmware)

```
cap:5 anndelay:16336, kalman_enable:1, kalman_Q:0.018, kalman_R:0.642,
para_a:1.0000, para_b:0.00, pos_enable:0, pos_dimen:0
```

Antenna delay calibration (`anndelay`) is the primary knob for correcting TWR distance bias.

---

## 4. PDOA Algorithm Commands

> **Note:** Commands in this section apply to **base stations (anchors) only**, unless stated otherwise.

| Command | Description |
|---------|-------------|
| `AT+DECA$` | PDOA PC software authentication / certification handshake. Returns JSON device info. |
| `AT+GETDLIST` | Get the discovery list of nearby tags. |
| `AT+GETKLIST` | Get the pairing list of registered tags. |
| `AT+ADDTAG=<params>` | Add a tag to the pairing list. Example: `AT+ADDTAG=0,0,1,1,0` |
| `AT+DELTAG=<id>` | Remove a tag from the pairing list. Example: `AT+DELTAG=000000004E818834` |
| `AT+PDOAOFF=<value>` | Set angle correction offset (tenths of degrees). |
| `AT+RNGOFF=<value>` | Set distance correction offset (millimeters). |
| `AT+FILTER=<value>` | Enable/disable filtering. `0` recommended for calibration. |
| `AT+UARTRATE=<value>` | Set serial port baud rate. Example: `AT+UARTRATE=100` |
| `AT+USER_CMD=<value>` | Set output data format. **`1`** enables anchor `Tag_Addr:` stream. |
| `AT+PDOASETCFG=<params>` | Set PDOA configuration parameters. |
| `AT+PDOAGETCFG` | Get current PDOA configuration parameters. |

### `AT+PDOASETCFG` (typical two-module setup)

```
AT+PDOASETCFG=1,1,4369,0,100,0,1    # tag  (last flag 0)
AT+PDOASETCFG=1,1,4369,0,100,1,1    # anchor (last flag 1 = auth)
```

Network **4369** = PAN `0x1111`. Tag `anc_id=0` must match anchor device id.

### `AT+PDOAGETCFG` response (custom firmware)

```
Dlist:0 KList:1 Net:1111 AncID:0 Rate:100 Filter:0 UserCmd:1
pdoaOffset:0 rngOffset:0
```

### PDOA measurement stream (`AT+USER_CMD=1`)

Anchor emits lines like:

```
Tag_Addr:0000, Seq:3, Xcm:109, Ycm:142, Range:1.790, Angle:1049
```

| Field | Meaning |
|-------|---------|
| `Tag_Addr` | Tag node address (hex) |
| `Range` | Distance in **meters** (3 decimal places) |
| `Angle` | Azimuth in **tenths of degrees** (`1049` → 104.9°) |
| `Xcm`, `Ycm` | Cartesian estimate (cm) |

Read PDOA from the **anchor** UART. Requires `AT+SETUWBMODE=0` (vendor `node.o`/`tag.o` stack), not `SETUWBMODE=1`.

---

## 5. Algorithm Switch Commands

| Command | Description |
|---------|-------------|
| `AT+SETUWBMODE=X` | Select UWB algorithm mode. `X`: `0` = vendor TWR/PDOA stack (`node.o`/`tag.o`), `1` = isolated PDOA examples. |
| `AT+GETUWBMODE` | Query the currently active UWB algorithm. Response: `twr_pdoa_mode: 0` |

**Use `AT+SETUWBMODE=0`** for all TWR, PDOA, and UWB data workflows in this project. Mode `1` runs separate example code and is not the working production path.

---

## 6. Custom Firmware Extensions (GCC port)

These commands exist in **`STM32F103-BU0x_SDK`** custom builds (V1.0.10+) but are **not** in the vendor PDF.

### 6.1 `AT+UWBSTART`

Start UWB ranging **without** requiring another reboot after `AT+SAVE`.

- Responds `OK`, then prints `DEVID:0xDECA0312` if SPI and DW3000 are healthy.
- Starts `node_start()` (anchor) or `tag_start()` (tag) based on saved role.
- After success, the module leaves AT command mode during active ranging; AT is still processed from SysTick on custom firmware V1.0.47+.
- Requires NVM ready (`flag == 0xAAAA`) — run `AT+SETCFG` + `AT+SAVE` first.

Typical sequence:

```
AT+SETCFG=0,1,1,1
AT+SAVE
# wait ≥8 s, reopen port
AT+UWBSTART          # anchor first, then tag
```

On failure:

```
UWBSTART: NVM not ready (flag != 0xAAAA)
UWBSTART: DW3000 SPI not responding
```

### 6.2 `AT+UWBDATA` — UWB peer data transfer (V1.0.47+)

Queue a UWB data packet for transmission to another module while TWR/PDOA ranging continues. Message type **`0xD0`**, max payload **1010 bytes** (extended PHR).

**Prerequisites:** firmware ≥ V1.0.47, ranging active (`AT+UWBSTART`), both modules configured with matching PAN/network.

#### Send — small packet (≤32 bytes, hex inline)

```
AT+UWBDATA=<dest>,<len>,<hexpayload>
OK
```

Example:

```
AT+UWBDATA=0,5,68656C6C6F
OK
```

#### Send — medium/large packet (33–1010 bytes, binary upload)

```
AT+UWBDATA=<dest>,<len>
OK
<len bytes raw binary>
OK
```

- `<dest>`: destination node address (`0` when both modules use `id=0`)
- `<len>`: payload length (1–1010)
- Second `OK` confirms payload received and queued

Returns `ERR` if: invalid length, queue already full (one packet at a time), or hex decode error.

#### Receive — unsolicited notification

When a UWB data frame arrives, the receiver prints:

```
+UWBDATA:<src>,<len>
<len bytes raw binary>

```

- `<src>`: sender node address from the UWB frame header
- Payload is **binary** (not hex)

#### UWB data frame layout (firmware)

| Offset | Field |
|--------|-------|
| 0–1 | Frame control `0x41 0x88` |
| 2 | Sequence |
| 3–4 | PAN ID (typically `0x1111`) |
| 5–6 | Destination address |
| 7–8 | Source address |
| 9 | Reserved `0x00` |
| 10 | Message type `0xD0` |
| 11+ | User payload |

#### Operational limits

| Constraint | Value |
|------------|-------|
| Max payload | 1010 bytes |
| TX queue depth | 1 packet |
| TX rate | ~1 packet / 500 ms |
| Typical latency | 5–20 s for first packet after queue |
| Bidirectional | Both anchor and tag can send |

#### Host API (`bu04_at.py`)

```python
from bu04_at import Bu04Device

with Bu04Device("/dev/ttyUSB4") as tag, Bu04Device("/dev/ttyUSB0") as anchor:
    tag.send_uwbdata(dest=0, payload=b"hello")
    src, data = anchor.wait_uwbdata(timeout=15.0)
```

Test script:

```bash
python3 test_uwbdata.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 --setup --force-setup
```

---

## 7. UART Streams During Ranging (custom firmware)

While `AT+UWBSTART` is active, modules emit diagnostic and measurement lines on USART1.

### Tag (role=0), ~every 2 s

```
distance: 0 irq: 12016 cb: 0 txf: 0 … rlen: 21 b9: 27 rpf: 1 rh: …
```

### Anchor (role=1), ~every 10 s

```
anc: nrx: 0 ntx: 0 ner: 0 st: 8 dn: 1 txs: 195 rxg: 195 l13: 195 txf: 0 …
```

### Key diagnostic fields

| Field | Meaning |
|-------|---------|
| `cb` | Completed TWR exchanges (0 = no range yet) |
| `l13` | 13-byte Poll frames received (anchor) |
| `rlen` / `b9` | Last RX frame length / byte 9 (seq); `b9=27` → Response type |
| `txf` | Delayed TX failure count |
| `pdor` | Last latched PDOA raw (anchor, PDOA builds) |
| `Tag_Addr:…` | PDOA range + angle (when `AT+USER_CMD=1`) |
| `+UWBDATA:…` | Incoming UWB data packet |

**TWR distance:** read from **anchor** (`AT+DISTANCE` or `distance:` stream). Tag `cb:0` alone is normal in STS-SDC mode.

---

## 8. Quick Setup Reference

Standard two-module TWR + PDOA + data (both `id=0`):

```
# Anchor
AT+SETUWBMODE=0
AT+PDOASETCFG=1,1,4369,0,100,1,1
AT+USER_CMD=1
AT+FILTER=0
AT+SETCFG=0,1,1,1
AT+SAVE
AT+ADDTAG=0,0,1,1,0
AT+UWBSTART

# Tag
AT+SETUWBMODE=0
AT+PDOASETCFG=1,1,4369,0,100,0,1
AT+SETCFG=0,0,1,1
AT+SAVE
AT+UWBSTART
```

Or use `calibrate.py` / `test_uwbdata.py --setup` from the repo root.

---

## Related Documentation

| Document | Content |
|----------|---------|
| [`BU04_project_guide.md`](BU04_project_guide.md) | Full project workflow |
| [`../STM32F103-BU0x_SDK/FIRMWARE.md`](../STM32F103-BU0x_SDK/FIRMWARE.md) | Firmware architecture |
| [`BU04_uwbdata_achievements.md`](BU04_uwbdata_achievements.md) | UWBDATA design notes |
| [`BU04_pdoa_achievements.md`](BU04_pdoa_achievements.md) | PDOA setup and calibration |
