#!/usr/bin/env python3
"""Test serial connection to a BU03/BU04 UWB module over AT commands."""

from __future__ import annotations

import argparse
import json
import re
import sys
import time

try:
    import serial
except ImportError:
    print("pyserial is required. Install with: pip install pyserial", file=sys.stderr)
    sys.exit(1)


DEFAULT_PORT = "/dev/ttyUSB0"
DEFAULT_BAUD = 115200
COMMON_BAUDS = (115200, 921600, 57600, 38400, 9600)
DEBUG_LOG_PATH = "/home/valentin/RL/UWB/.cursor/debug-991fe8.log"
DEBUG_SESSION_ID = "991fe8"
AT_COMMANDS = (
    ("AT", "AT framework test"),
    ("AT+GETVER", "software version"),
    ("AT+GETCFG", "device configuration"),
)


# #region agent log
def _debug_log(
    *,
    hypothesis_id: str,
    location: str,
    message: str,
    data: dict,
    run_id: str = "post-fix-v109",
) -> None:
    payload = {
        "sessionId": DEBUG_SESSION_ID,
        "runId": run_id,
        "hypothesisId": hypothesis_id,
        "location": location,
        "message": message,
        "data": data,
        "timestamp": int(time.time() * 1000),
    }
    with open(DEBUG_LOG_PATH, "a", encoding="utf-8") as log_file:
        log_file.write(json.dumps(payload) + "\n")


def _serial_metrics(raw: bytes) -> dict:
    printable = sum(32 <= byte < 127 or byte in (9, 10, 13) for byte in raw)
    total = len(raw) or 1
    return {
        "bytes": len(raw),
        "printable_ratio": round(printable / total, 4),
        "has_strict_ok": bool(STRICT_OK_RE.search(raw.decode("utf-8", errors="replace"))),
        "has_getver": b"getver" in raw.lower(),
        "has_getcfg": b"getcfg" in raw.lower(),
        "has_iic_error": b"IIC Error" in raw,
        "has_v102": b"V1.0.2" in raw,
        "has_v101": b"V1.0.1" in raw,
        "has_v103": b"V1.0.3" in raw,
        "has_v104": b"V1.0.4" in raw,
        "has_v105": b"V1.0.5" in raw,
        "has_v106": b"V1.0.6" in raw,
        "has_v107": b"V1.0.7" in raw,
        "has_v108": b"V1.0.8" in raw,
        "has_v109": b"V1.0.9" in raw,
    }


# #endregion


def open_port(port: str, baud: int, timeout: float) -> serial.Serial:
    ser = serial.Serial(
        port=port,
        baudrate=baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=timeout,
        write_timeout=timeout,
    )
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return ser


INIT_FAILED_MARKER = "INIT FAILED"
STRICT_OK_RE = re.compile(r"(?:^|\r|\n)OK(?:\r|\n|$)", re.IGNORECASE | re.MULTILINE)
GETVER_RE = re.compile(r"getver software:(\S+)", re.IGNORECASE)
GETCFG_RE = re.compile(r"getcfg ID:(\d+),\s*Role:(\d+)", re.IGNORECASE)
MIN_PRINTABLE_RATIO = 0.75


def _printable_ratio(raw: bytes) -> float:
    if not raw:
        return 0.0
    printable = sum(32 <= byte < 127 or byte in (9, 10, 13) for byte in raw)
    return printable / len(raw)


def _looks_like_at_response(raw: bytes) -> bool:
    if not raw:
        return False
    text = raw.decode("utf-8", errors="replace")
    if _printable_ratio(raw) < MIN_PRINTABLE_RATIO and len(raw) > 32:
        return False
    return bool(STRICT_OK_RE.search(text) or GETVER_RE.search(text) or GETCFG_RE.search(text))


def drain_port(ser: serial.Serial, seconds: float = 1.0) -> bytes:
    deadline = time.monotonic() + seconds
    chunks: list[bytes] = []
    while time.monotonic() < deadline:
        data = ser.read(ser.in_waiting or 256)
        if data:
            chunks.append(data)
        else:
            time.sleep(0.05)
    return b"".join(chunks)


def read_response(ser: serial.Serial, wait: float) -> str:
    hard_limit = max(wait + 1.0, wait * 2)
    chunks: list[str] = []
    started = time.monotonic()
    idle_deadline = started + wait

    while time.monotonic() - started < hard_limit:
        data = ser.read(ser.in_waiting or 1)
        if data:
            chunks.append(data.decode("utf-8", errors="replace"))
            text = "".join(chunks)
            upper = text.upper()
            if INIT_FAILED_MARKER in upper and not STRICT_OK_RE.search(text):
                if time.monotonic() - started >= 0.4:
                    break
            if (
                STRICT_OK_RE.search(text)
                or GETVER_RE.search(text)
                or GETCFG_RE.search(text)
                or "\nERR" in upper
                or upper.rstrip().endswith("ERR")
            ):
                break
            idle_deadline = time.monotonic() + 0.25
        elif time.monotonic() >= idle_deadline:
            break
        else:
            time.sleep(0.05)
    return "".join(chunks).strip()


def command_success(command: str, response: str) -> bool:
    raw = response.encode("utf-8", errors="replace")
    upper = response.upper()
    if INIT_FAILED_MARKER in upper and not STRICT_OK_RE.search(response):
        return False
    if not STRICT_OK_RE.search(response):
        return False
    if not _looks_like_at_response(raw):
        return False
    if any(token in upper for token in ("\r\nERR", "\nERR", "\rERR")):
        return False
    if command.upper() == "AT+GETVER":
        return GETVER_RE.search(response) is not None
    if command.upper() == "AT+GETCFG":
        return GETCFG_RE.search(response) is not None
    return True


def send_command(ser: serial.Serial, command: str, wait: float) -> tuple[str, bool]:
    ser.reset_input_buffer()
    idle_raw = ser.read(ser.in_waiting or 0)
    # #region agent log
    _debug_log(
        hypothesis_id="H1",
        location="test_connection.py:send_command:idle",
        message="background bytes before command",
        data={"command": command, **_serial_metrics(idle_raw)},
    )
    # #endregion
    ser.write(f"{command}\r\n".encode())
    response = read_response(ser, wait)
    raw = response.encode("utf-8", errors="replace")
    ok = command_success(command, response)
    # #region agent log
    _debug_log(
        hypothesis_id="H1,H3,H4",
        location="test_connection.py:send_command:response",
        message="command response metrics",
        data={"command": command, "ok": ok, **_serial_metrics(raw)},
    )
    # #endregion
    return response, ok


def probe_baud_rate(port: str, bauds: tuple[int, ...], timeout: float) -> int | None:
    for baud in bauds:
        try:
            with open_port(port, baud, timeout) as ser:
                _, ok = send_command(ser, "AT", wait=timeout)
                if ok:
                    return baud
        except serial.SerialException:
            continue
    return None


def run_tests(port: str, baud: int, timeout: float, auto_baud: bool, boot_wait: float) -> int:
    print(f"Port: {port}")

    if auto_baud:
        print("Scanning common baud rates...")
        detected = probe_baud_rate(port, COMMON_BAUDS, timeout)
        if detected is None:
            print("FAIL: no response to AT on any common baud rate")
            return 1
        baud = detected
        print(f"Detected baud rate: {baud}")

    try:
        ser = open_port(port, baud, timeout)
    except serial.SerialException as exc:
        print(f"FAIL: could not open {port}: {exc}")
        return 1

    # #region agent log
    _debug_log(
        hypothesis_id="H2",
        location="test_connection.py:run_tests:open",
        message="serial port opened",
        data={"port": port, "baud": baud, "timeout": timeout},
    )
    # #endregion

    print(f"Baud: {baud}")
    print("-" * 40)

    passed = 0
    saw_init_failed = False
    with ser:
        if boot_wait > 0:
            boot_noise = drain_port(ser, boot_wait)
        else:
            boot_noise = b""
        boot_noise += drain_port(ser, 0.5)
        # #region agent log
        _debug_log(
            hypothesis_id="H3",
            location="test_connection.py:run_tests:drain",
            message="initial port drain",
            data=_serial_metrics(boot_noise),
        )
        # #endregion
        if boot_noise and _printable_ratio(boot_noise) < MIN_PRINTABLE_RATIO:
            print(
                "Warning: UWB binary telemetry detected on the port. "
                "Run: make -C Refs/STM32F103-BU0x_SDK erase-config then reflash."
            )

        for command, description in AT_COMMANDS:
            print(f"> {command}  ({description})")
            try:
                response, ok = send_command(ser, command, wait=timeout)
            except serial.SerialException as exc:
                print(f"  FAIL: serial error: {exc}")
                continue

            if INIT_FAILED_MARKER in response and "OK" not in response.upper():
                saw_init_failed = True

            if response:
                ver = GETVER_RE.search(response)
                cfg = GETCFG_RE.search(response)
                if ver:
                    print(f"  version: {ver.group(1).strip()}")
                if cfg:
                    print(f"  config: id={cfg.group(1)} role={cfg.group(2)}")
                for line in response.splitlines():
                    if any(
                        token in line.upper()
                        for token in ("OK", "ERR", "GETVER", "GETCFG", "INIT FAILED", "IIC")
                    ):
                        print(f"  {line}")
                if not ver and not cfg and not any(
                    token in response.upper() for token in ("OK", "ERR", "GETVER", "GETCFG")
                ):
                    print("  (binary/noise only — UWB telemetry may be saturating the port)")
            else:
                print("  (no response)")

            if ok:
                print("  PASS")
                passed += 1
            else:
                print("  FAIL")
            print()

    print("-" * 40)
    print(f"Result: {passed}/{len(AT_COMMANDS)} checks passed")

    if saw_init_failed:
        print(
            "Hint: module is stuck in INIT FAILED. Unplug its USB cable for 5 seconds, "
            "replug, then retry."
        )

    if passed == 0:
        print(
            "Hint: verify power, wiring, that AT firmware is loaded, "
            "and try --auto-baud or --port /dev/ttyUSB1"
        )
        print(
            "Hint: if the module was previously configured (AT+SAVE), UWB telemetry "
            "may flood the port. Run: make -C Refs/STM32F103-BU0x_SDK deploy "
            "(erases flash page 127 at 0x0801FC00), replug FTDI USB, then retry."
        )
        return 1
    if passed < len(AT_COMMANDS):
        return 1
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Test BU03/BU04 UWB module connection via AT commands."
    )
    parser.add_argument(
        "--port",
        default=DEFAULT_PORT,
        help=f"Serial port path (default: {DEFAULT_PORT})",
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=DEFAULT_BAUD,
        help=f"Baud rate (default: {DEFAULT_BAUD})",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=1.0,
        help="Read/write timeout in seconds (default: 1.0)",
    )
    parser.add_argument(
        "--auto-baud",
        action="store_true",
        help="Try common baud rates before running tests",
    )
    parser.add_argument(
        "--boot-wait",
        type=float,
        default=2.0,
        help="Seconds to wait after opening port for module boot banner (default: 2.0)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    return run_tests(args.port, args.baud, args.timeout, args.auto_baud, args.boot_wait)


if __name__ == "__main__":
    raise SystemExit(main())
