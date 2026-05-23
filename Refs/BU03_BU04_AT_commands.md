# BU03/BU04 AT Command Reference

Source: `BU03_BU04_AT_command_en_v1.0.6.pdf`

All commands respond with `OK` on success or `ERR` on failure unless otherwise noted.

---

## 1. Universal Commands

| Command | Description |
|---------|-------------|
| `AT` | Test whether the AT framework is working properly. |
| `AT+SAVE` | Save the current configuration to non-volatile storage. |
| `AT+GETVER` | Get the software version number. Example response: `getver software:V1.0.0` |
| `AT+RESTART` | Reset and restart the device. |
| `AT+RESTORE` | Restore factory default settings. |
| `AT+GETWORKMODE` | Query the working mode. `0` = normal working mode, `1` = production test mode. |
| `AT+SETWORKMODE=X` | Set the working mode. `X`: `0` = normal, `1` = production test. |
| `AT+GETCFG` | Get configuration information (ID, Role, Channel, Rate, Group). Example: `getcfg ID:65535, Role:0, CH:1, Rate:1, Group:1` |
| `AT+SETCFG=X1,X2,X3,1,X4` | Set configuration. Run `AT+SAVE` afterward to persist. |

### `AT+SETCFG` Parameters

| Parameter | Meaning |
|-----------|---------|
| X1 | Device ID (`0`–`10`) |
| X2 | Device role: `0` = tag, `1` = anchor (base station) |
| X3 | Channel: `0` = channel 9, `1` = channel 5 |
| (fixed) | Device rate: `1` = 6.8M (only rate currently supported) |
| X4 | Node group (`0`–`255`). Anchors must be configured; tags default to `0`. |

Example: `AT+SETCFG=0,1,0,1,1` → ID:0, Role:1 (anchor), CH:0, Rate:1, Group:1

---

## 2. Development Board Production Test Commands

| Command | Description |
|---------|-------------|
| `AT+GETSENSOR` | Read accelerometer and angle sensor values for production testing. **BU03 only** — BU04 hardware does not support this. |
| `AT+TESTLED=X` | Start/stop LED (flow light) test. Use before `AT+SETCFG`. `X`: `1` = start, `0` = stop. |
| `AT+TESTOLED=<text>` | Test the OLED display. Use before `AT+SETCFG`. Example: `AT+TESTOLED=HELLO TEST!` |
| `AT+DISTANCE` | Perform a single range measurement (after configuration). Example response: `distance: 0.340000` |

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

---

## 4. PDOA Algorithm Commands

> **Note:** Commands in this section apply to **base stations (anchors) only**, unless stated otherwise.

| Command | Description |
|---------|-------------|
| `AT+DECA$` | PDOA PC software authentication / certification handshake. |
| `AT+GETDLIST` | Get the discovery list of nearby tags. |
| `AT+GETKLIST` | Get the pairing list of registered tags. |
| `AT+ADDTAG=<params>` | Add a tag to the pairing list. Example: `AT+ADDTAG=000000004E818834,8834,1,64,0` |
| `AT+DELTAG=<id>` | Remove a tag from the pairing list. Example: `AT+DELTAG=000000004E818834` |
| `AT+PDOAOFF=<value>` | Set angle correction offset. Example: `AT+PDOAOFF=1` |
| `AT+RNGOFF=<value>` | Set distance correction offset. Example: `AT+RNGOFF=1` |
| `AT+FILTER=<value>` | Enable/disable filtering. Example: `AT+FILTER=1` |
| `AT+UARTRATE=<value>` | Set serial port baud rate. Example: `AT+UARTRATE=100` |
| `AT+USER_CMD=<value>` | Set output data format. Example: `AT+USER_CMD=0` |
| `AT+PDOASETCFG=<params>` | Set PDOA configuration parameters. Example: `AT+PDOASETCFG=1,1,3333,1,100,0,0` |
| `AT+PDOAGETCFG` | Get current PDOA configuration parameters. |

---

## 5. Algorithm Switch Commands

| Command | Description |
|---------|-------------|
| `AT+SETUWBMODE=X` | Select and save the UWB positioning algorithm. `X`: `0` = TWR, `1` = PDOA. |
| `AT+GETUWBMODE` | Query the currently active UWB algorithm. |
