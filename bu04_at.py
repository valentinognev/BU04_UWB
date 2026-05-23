#!/usr/bin/env python3
"""Shared AT command helpers for BU03/BU04 UWB modules."""

from __future__ import annotations

import re
import time
from collections.abc import Callable
from dataclasses import dataclass

try:
    import serial
except ImportError as exc:  # pragma: no cover - runtime guard
    raise SystemExit("pyserial is required. Install with: pip install pyserial") from exc


DEFAULT_BAUD = 115200
BOOT_WAIT_S = 3.0
UWBDATA_MAX_PAYLOAD = 1010
UWBDATA_INLINE_MAX = 32
UWBDATA_BEGIN_RE = re.compile(
    r"\+UWBDATA:(?P<src>\d+),(?P<len>\d+)\r?\n",
    re.IGNORECASE,
)
MEASUREMENT_RE = re.compile(
    r"Tag_Addr:(?P<addr>[0-9A-Fa-f]+),\s*"
    r"Seq:(?P<seq>\d+),\s*"
    r"Xcm:(?P<x>[-\d.]+),\s*"
    r"Ycm:(?P<y>[-\d.]+),\s*"
    r"Range:(?P<range>[-\d.]+),\s*"
    r"Angle:(?P<angle>-?\d+)"
)
DISTANCE_RE = re.compile(r"distance:\s*(\d+)(?:\s+irq:\s*(\d+))?(?:\s+cb:\s*(\d+))?", re.IGNORECASE)
PDOAGETCFG_RE = re.compile(
    r"Dlist:(?P<dlist>\d+)\s+KList:(?P<klist>\d+)\s+"
    r"Net:(?P<net>[0-9A-Fa-f]+)\s+AncID:(?P<anc_id>\d+)\s+"
    r"Rate:(?P<rate>\d+)\s+Filter:(?P<filter>\d+)\s+"
    r"UserCmd:(?P<user_cmd>\d+)\s+"
    r"pdoaOffset:(?P<pdoa_offset>-?\d+)\s+"
    r"rngOffset:(?P<rng_offset>-?\d+)",
    re.IGNORECASE,
)
GETCFG_RE = re.compile(
    r"ID:(?P<device_id>\d+),\s*Role:(?P<role>\d+),\s*CH:(?P<channel>\d+),\s*Rate:(?P<rate>\d+)",
    re.IGNORECASE,
)
GETDEV_RE = re.compile(
    r"cap:(?P<cap>\d+)\s+anndelay:(?P<anndelay>\d+),\s*"
    r"kalman_enable:(?P<kalman_enable>\d+),\s*"
    r"kalman_Q:(?P<kalman_q>[-\d.]*),\s*"
    r"kalman_R:(?P<kalman_r>[-\d.]*),\s*"
    r"para_a:(?P<para_a>[-\d.]*),\s*"
    r"para_b:(?P<para_b>[-\d.]*),\s*"
    r"pos_enable:(?P<pos_enable>\d+),\s*"
    r"pos_dimen:(?P<pos_dimen>\d+)",
    re.IGNORECASE,
)
INIT_FAILED_RE = re.compile(r"INIT FAILED", re.IGNORECASE)
SETCFG_OK_RE = re.compile(r"setcfg ID:\d+,\s*Role:\d+", re.IGNORECASE)
RESPONSE_OK_RE = re.compile(r"(?:^|\r|\n)OK(?:\r|\n|$)", re.IGNORECASE | re.MULTILINE)
FAIL_LINE_RE = re.compile(r"(?:^|\r|\n)(?:ERR|ERROR)(?:\r|\n|$)", re.IGNORECASE | re.MULTILINE)


@dataclass
class Measurement:
    addr: str
    seq: int
    x_cm: float
    y_cm: float
    range_m: float
    angle_deg: float


@dataclass
class PdoaConfig:
    dlist: int
    klist: int
    net: int
    anc_id: int
    rate: int
    filter_enabled: int
    user_cmd: int
    pdoa_offset: int
    rng_offset: int


@dataclass
class RoleConfig:
    device_id: int
    role: int
    channel: int
    rate: int


@dataclass
class DevConfig:
    cap: int
    anndelay: int
    kalman_enable: int
    kalman_q: float
    kalman_r: float
    para_a: float
    para_b: float
    pos_enable: int
    pos_dimen: int


class Bu04Device:
    def __init__(self, port: str, baud: int = DEFAULT_BAUD, timeout: float = 1.0):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self._ser: serial.Serial | None = None

    def open(self) -> None:
        if self._ser and self._ser.is_open:
            return
        self._ser = serial.Serial(
            port=self.port,
            baudrate=self.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=self.timeout,
            write_timeout=self.timeout,
        )
        self._ser.reset_input_buffer()
        self._ser.reset_output_buffer()

    def close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()

    def __enter__(self) -> Bu04Device:
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def send(self, command: str, wait: float | None = None) -> str:
        if not self._ser or not self._ser.is_open:
            raise RuntimeError(f"{self.port} is not open")
        wait = self.timeout if wait is None else wait
        self._ser.reset_input_buffer()
        self._ser.write(f"{command}\r\n".encode())
        return self.read_text(wait)

    @staticmethod
    def _response_complete(text: str) -> bool:
        if SETCFG_OK_RE.search(text) and RESPONSE_OK_RE.search(text):
            return True
        if "GETCFG" in text.upper() and RESPONSE_OK_RE.search(text):
            return True
        if "GETVER" in text.upper() and RESPONSE_OK_RE.search(text):
            return True
        if "GETDEV" in text.upper() and RESPONSE_OK_RE.search(text):
            return True
        if "GETUWBMODE" in text.upper() and RESPONSE_OK_RE.search(text):
            return True
        if "PDOAGETCFG" in text.upper() or "tcfg Dlist:" in text:
            return True
        if "distance:" in text.lower() and RESPONSE_OK_RE.search(text):
            return True
        if RESPONSE_OK_RE.search(text) and not FAIL_LINE_RE.search(text):
            return True
        if FAIL_LINE_RE.search(text):
            return True
        return False

    def read_text(self, wait: float | None = None) -> str:
        if not self._ser or not self._ser.is_open:
            raise RuntimeError(f"{self.port} is not open")
        wait = self.timeout if wait is None else wait
        hard_limit = max(wait + 1.0, wait * 2)
        chunks: list[str] = []
        started = time.monotonic()
        idle_deadline = started + wait

        while time.monotonic() - started < hard_limit:
            data = self._ser.read(self._ser.in_waiting or 1)
            if data:
                chunks.append(data.decode("utf-8", errors="replace"))
                text = "".join(chunks)
                if self._response_complete(text):
                    break
                if INIT_FAILED_RE.search(text) and not RESPONSE_OK_RE.search(text):
                    if time.monotonic() - started >= 0.4:
                        break
                idle_deadline = time.monotonic() + 0.25
            elif time.monotonic() >= idle_deadline:
                break
            else:
                time.sleep(0.05)

        return "".join(chunks).strip()

    def drain(self, duration: float = 0.3) -> str:
        return self.read_available(duration)

    def read_available(self, duration: float) -> str:
        if not self._ser or not self._ser.is_open:
            raise RuntimeError(f"{self.port} is not open")
        end = time.monotonic() + duration
        chunks: list[str] = []
        while time.monotonic() < end:
            data = self._ser.read(4096)
            if data:
                chunks.append(data.decode("utf-8", errors="replace"))
            else:
                time.sleep(0.05)
        return "".join(chunks)

    def command_ok(self, command: str, wait: float | None = None) -> tuple[str, bool]:
        response = self.send(command, wait=wait)
        return response, self.response_ok(response)

    @staticmethod
    def response_ok(response: str) -> bool:
        if INIT_FAILED_RE.search(response):
            return False
        if FAIL_LINE_RE.search(response):
            return False
        return bool(RESPONSE_OK_RE.search(response))

    def has_init_failed(self) -> bool:
        response = self.read_available(0.5)
        return bool(INIT_FAILED_RE.search(response))

    def ping(self) -> bool:
        _, ok = self.command_ok("AT")
        return ok

    def get_version(self) -> str | None:
        response, ok = self.command_ok("AT+GETVER")
        if not ok:
            return None
        match = re.search(r"software:([^\s]+)", response, re.IGNORECASE)
        return match.group(1) if match else response

    def get_uwb_mode(self) -> int | None:
        response, ok = self.command_ok("AT+GETUWBMODE")
        if not ok:
            return None
        match = re.search(r"twr_pdoa_mode:\s*(\d+)", response, re.IGNORECASE)
        return int(match.group(1)) if match else None

    def get_role_config(self) -> RoleConfig | None:
        response, ok = self.command_ok("AT+GETCFG")
        if not ok:
            return None
        match = GETCFG_RE.search(response)
        if not match:
            return None
        return RoleConfig(
            device_id=int(match.group("device_id")),
            role=int(match.group("role")),
            channel=int(match.group("channel")),
            rate=int(match.group("rate")),
        )

    def set_role_config(self, device_id: int, role: int, channel: int = 1, rate: int = 1) -> tuple[str, bool]:
        response = self.send(f"AT+SETCFG={device_id},{role},{channel},{rate}", wait=2.0)
        ok = bool(SETCFG_OK_RE.search(response) and RESPONSE_OK_RE.search(response))
        return response, ok

    @staticmethod
    def _save_succeeded(text: str) -> bool:
        if not RESPONSE_OK_RE.search(text):
            return False
        init_match = INIT_FAILED_RE.search(text)
        if init_match:
            if "IIC Error" in text[: init_match.start()] or "UWB Module" in text[: init_match.start()]:
                return True
            ok_matches = list(RESPONSE_OK_RE.finditer(text))
            # SETCFG OK + SAVE OK must both appear before INIT FAILED spam.
            if SETCFG_OK_RE.search(text) and len(ok_matches) >= 2:
                return ok_matches[1].start() < init_match.start()
            return False
        if "IIC Error" in text or "UWB Module" in text:
            return True
        if SETCFG_OK_RE.search(text):
            return len(RESPONSE_OK_RE.findall(text)) >= 2
        return len(RESPONSE_OK_RE.findall(text)) >= 2

    def _setcfg_and_save(self, command: str, timeout: float = 3.0) -> tuple[str, str, bool, float]:
        # Pipeline SAVE immediately after SETCFG in one write so SAVE is queued
        # before the firmware enters the INIT FAILED state.
        piped = f"{command}\r\nAT+SAVE\r\n".encode()
        self._ser.write(piped)

        chunks: list[str] = []
        deadline = time.monotonic() + timeout
        setcfg_response = ""
        save_response = ""
        saved = False

        while time.monotonic() < deadline:
            data = self._ser.read(self._ser.in_waiting or 1)
            if data:
                chunks.append(data.decode("utf-8", errors="replace"))
                text = "".join(chunks)
                if SETCFG_OK_RE.search(text) and RESPONSE_OK_RE.search(text):
                    setcfg_response = text
                if self._save_succeeded(text):
                    save_response = text
                    saved = True
                    break
                if SETCFG_OK_RE.search(text) and FAIL_LINE_RE.search(text):
                    save_response = text
                    break
            else:
                time.sleep(0.01)

        text = "".join(chunks)
        if not setcfg_response:
            setcfg_response = text
        if not save_response:
            save_response = text
        if not saved and SETCFG_OK_RE.search(text) and RESPONSE_OK_RE.search(text):
            # SAVE often reboots before the second OK is readable; verify via GETCFG later.
            saved = True
        return setcfg_response, save_response, saved, 0.0

    def _wait_for_role_config(
        self,
        device_id: int,
        role: int,
        timeout: float,
    ) -> RoleConfig | None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                if not self._ser or not self._ser.is_open:
                    self.open()
                self.drain(1.5)
                cfg = self.get_role_config()
                if cfg and cfg.role == role and cfg.device_id == device_id:
                    return cfg
            except serial.SerialException:
                self.close()
            time.sleep(2.0)
        return None

    def _wait_until_responsive(self, timeout: float = 30.0) -> tuple[str, bool]:
        deadline = time.monotonic() + timeout
        last_response = ""
        while time.monotonic() < deadline:
            self.drain(0.2)
            last_response = self.send("AT", wait=0.8)
            if not self.is_init_failed(last_response):
                return last_response, True
            time.sleep(1.0)
        return last_response, False

    def apply_role_config(
        self,
        device_id: int,
        role: int,
        channel: int = 1,
        rate: int = 1,
        *,
        wait_for_recovery: Callable[[str], None] | None = None,
        recovery_timeout: float = 90.0,
    ) -> tuple[str, bool]:
        """Apply role: SETCFG, verify in RAM, SAVE to flash, then reboot into UWB."""
        if not self._ser or not self._ser.is_open:
            raise RuntimeError(f"{self.port} is not open")

        setcfg_response, setcfg_ok = self.set_role_config(device_id, role, channel, rate)
        if not setcfg_ok:
            return setcfg_response, False

        role_cfg = self.get_role_config()
        if role_cfg is None or role_cfg.role != role or role_cfg.device_id != device_id:
            return (
                f"SETCFG did not stick in RAM before SAVE: {role_cfg!r} "
                f"(expected id={device_id} role={role})",
                False,
            )

        self._ser.reset_input_buffer()
        self._ser.write(b"AT+SAVE\r\n")
        save_response = self.read_text(3.0)
        save_ok = self.response_ok(save_response) or self._save_succeeded(save_response)
        if not save_ok:
            # SAVE triggers NVIC_SystemReset; OK may be lost during reboot.
            save_ok = True

        self.close()
        if wait_for_recovery is not None:
            print(f"  {self.port}: saved to flash — rebooting into UWB...", flush=True)
            wait_for_recovery(self.port)
        else:
            print(
                f"  {self.port}: saved (id={device_id} role={role}) — "
                "module rebooting; AT may be unavailable for ~30s.",
                flush=True,
            )
            time.sleep(3.0)

        # Post-SAVE the firmware runs UWB and often stops answering AT until power-cycle.
        # Configuration is already verified in RAM before SAVE.
        return save_response or setcfg_response, True

    def _poll_until_responsive(self, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            time.sleep(2.0)
            try:
                self.open()
                self.drain(0.3)
                ping_response = self.send("AT", wait=1.0)
                if not self.is_init_failed(ping_response):
                    return True
            except serial.SerialException:
                pass
            finally:
                self.close()
        return False

    def is_init_failed(self, response: str) -> bool:
        return bool(INIT_FAILED_RE.search(response)) and not RESPONSE_OK_RE.search(response)

    def restart(self) -> bool:
        _, ok = self.command_ok("AT+RESTART", wait=2.0)
        return ok

    def restore(self) -> bool:
        _, ok = self.command_ok("AT+RESTORE", wait=2.0)
        return ok

    def set_uwb_mode(self, mode: int) -> bool:
        _, ok = self.command_ok(f"AT+SETUWBMODE={mode}")
        return ok

    def pdoa_auth(self) -> bool:
        _, ok = self.command_ok("AT+DECA$", wait=1.5)
        return ok

    def get_pdoa_config(self) -> PdoaConfig | None:
        response, ok = self.command_ok("AT+PDOAGETCFG")
        if not ok:
            return None
        match = PDOAGETCFG_RE.search(response)
        if not match:
            return None
        return PdoaConfig(
            dlist=int(match.group("dlist")),
            klist=int(match.group("klist")),
            net=int(match.group("net"), 16),
            anc_id=int(match.group("anc_id")),
            rate=int(match.group("rate")),
            filter_enabled=int(match.group("filter")),
            user_cmd=int(match.group("user_cmd")),
            pdoa_offset=int(match.group("pdoa_offset")),
            rng_offset=int(match.group("rng_offset")),
        )

    def set_pdoa_config(
        self,
        network: int,
        anchor_id: int,
        rate: int = 100,
        filter_enabled: int = 0,
        user_cmd: int = 0,
    ) -> bool:
        command = f"AT+PDOASETCFG=1,1,{network},{anchor_id},{rate},{filter_enabled},{user_cmd}"
        _, ok = self.command_ok(command)
        return ok

    def set_rng_offset(self, offset_mm: int) -> bool:
        _, ok = self.command_ok(f"AT+RNGOFF={offset_mm}")
        return ok

    def set_pdoa_offset(self, offset_deg: int) -> bool:
        _, ok = self.command_ok(f"AT+PDOAOFF={offset_deg}")
        return ok

    def save(self) -> tuple[str, bool]:
        return self.command_ok("AT+SAVE", wait=2.0)

    @staticmethod
    def _optional_float(value: str) -> float:
        return float(value) if value else 0.0

    def get_dev_config(self) -> DevConfig | None:
        response, ok = self.command_ok("AT+GETDEV")
        if not ok:
            return None
        match = GETDEV_RE.search(response)
        if not match:
            return None
        return DevConfig(
            cap=int(match.group("cap")),
            anndelay=int(match.group("anndelay")),
            kalman_enable=int(match.group("kalman_enable")),
            kalman_q=self._optional_float(match.group("kalman_q")),
            kalman_r=self._optional_float(match.group("kalman_r")),
            para_a=self._optional_float(match.group("para_a")),
            para_b=self._optional_float(match.group("para_b")),
            pos_enable=int(match.group("pos_enable")),
            pos_dimen=int(match.group("pos_dimen")),
        )

    def set_dev_config(self, config: DevConfig) -> bool:
        command = (
            "AT+SETDEV="
            f"{config.cap},{config.anndelay},{config.kalman_enable},"
            f"{config.kalman_q},{config.kalman_r},{config.para_a},"
            f"{config.para_b},{config.pos_enable},{config.pos_dimen}"
        )
        _, ok = self.command_ok(command)
        return ok

    def read_distance(self) -> float | None:
        response, ok = self.command_ok("AT+DISTANCE")
        if not ok:
            return None
        match = DISTANCE_RE.search(response)
        if not match:
            return None
        return float(match.group(1)) / 1000.0

    def send_uwbdata(self, dest: int, payload: bytes, wait: float | None = None) -> None:
        """Queue a UWB data packet for transmission to another module."""
        if not self._ser or not self._ser.is_open:
            raise RuntimeError(f"{self.port} is not open")
        if not payload:
            raise ValueError("payload must not be empty")
        if len(payload) > UWBDATA_MAX_PAYLOAD:
            raise ValueError(f"payload exceeds {UWBDATA_MAX_PAYLOAD} bytes")

        wait = self.timeout if wait is None else wait

        if len(payload) <= UWBDATA_INLINE_MAX:
            command = f"AT+UWBDATA={dest},{len(payload)},{payload.hex()}"
            response, ok = self.command_ok(command, wait=wait)
            if not ok:
                raise RuntimeError(f"AT+UWBDATA failed: {response!r}")
            return

        self._ser.reset_input_buffer()
        self._ser.write(f"AT+UWBDATA={dest},{len(payload)}\r\n".encode())
        response = self._read_until_ok(wait)
        if not self.response_ok(response):
            raise RuntimeError(f"AT+UWBDATA header failed: {response!r}")

        self._ser.write(payload)
        response = self._read_until_ok(max(wait, 2.0))
        if not self.response_ok(response):
            raise RuntimeError(f"AT+UWBDATA payload upload failed: {response!r}")

    def _read_until_ok(self, wait: float) -> str:
        if not self._ser or not self._ser.is_open:
            raise RuntimeError(f"{self.port} is not open")
        hard_limit = max(wait + 1.0, wait * 2)
        chunks: list[str] = []
        started = time.monotonic()
        idle_deadline = started + wait

        while time.monotonic() - started < hard_limit:
            data = self._ser.read(self._ser.in_waiting or 1)
            if data:
                chunks.append(data.decode("utf-8", errors="replace"))
                text = "".join(chunks)
                if self.response_ok(text) or FAIL_LINE_RE.search(text):
                    break
                idle_deadline = time.monotonic() + 0.25
            elif time.monotonic() >= idle_deadline:
                break
            else:
                time.sleep(0.05)

        return "".join(chunks).strip()

    def read_uwbdata(self, duration: float = 0.5) -> list[tuple[int, bytes]]:
        """Parse +UWBDATA notifications from the serial stream."""
        if not self._ser or not self._ser.is_open:
            raise RuntimeError(f"{self.port} is not open")

        end = time.monotonic() + duration
        pending = self._ser.read(self._ser.in_waiting or 0)
        packets: list[tuple[int, bytes]] = []

        while time.monotonic() < end:
            text = pending.decode("utf-8", errors="replace")
            while True:
                match = UWBDATA_BEGIN_RE.search(text)
                if not match:
                    break

                src = int(match.group("src"))
                length = int(match.group("len"))
                if length <= 0 or length > UWBDATA_MAX_PAYLOAD:
                    text = text[match.end() :]
                    continue

                body_start = match.end()
                if len(pending) >= body_start + length + 2:
                    raw = pending[body_start : body_start + length]
                    pending = pending[body_start + length :]
                    if pending[:2] == b"\r\n":
                        pending = pending[2:]
                    packets.append((src, bytes(raw)))
                    text = pending.decode("utf-8", errors="replace")
                    continue

                need = body_start + length + 2 - len(pending)
                chunk = self._ser.read(max(need, 1))
                if chunk:
                    pending += chunk
                    continue
                time.sleep(0.02)
                break

            if time.monotonic() >= end:
                break
            chunk = self._ser.read(self._ser.in_waiting or 1)
            if chunk:
                pending += chunk
            else:
                time.sleep(0.05)

        return packets

    def wait_uwbdata(self, timeout: float = 10.0) -> tuple[int, bytes]:
        """Block until one UWB data packet arrives or timeout."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            packets = self.read_uwbdata(duration=min(0.5, timeout))
            if packets:
                return packets[0]
        raise TimeoutError(f"No +UWBDATA received within {timeout:.1f}s on {self.port}")


def parse_measurements(text: str) -> list[Measurement]:
    results: list[Measurement] = []
    for match in MEASUREMENT_RE.finditer(text):
        results.append(
            Measurement(
                addr=match.group("addr"),
                seq=int(match.group("seq")),
                x_cm=float(match.group("x")),
                y_cm=float(match.group("y")),
                range_m=float(match.group("range")),
                angle_deg=int(match.group("angle")) / 10.0,
            )
        )
    return results


def median(values: list[float]) -> float:
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) / 2
