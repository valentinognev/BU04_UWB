#!/usr/bin/env python3
"""Interactive distance and azimuth calibration for two BU04 modules."""

from __future__ import annotations

import argparse
import json
import re
import sys
import time

from bu04_at import (
    BOOT_WAIT_S,
    Bu04Device,
    DevConfig,
    DISTANCE_RE,
    Measurement,
    median,
    parse_measurements,
)


DEFAULT_ANCHOR = "/dev/ttyUSB0"
DEFAULT_TAG = "/dev/ttyUSB4"
DEFAULT_NETWORK = 4369  # factory default (0x1111); 3333 breaks tag discovery with id 65535
DEFAULT_ANCHOR_ID = 0
DEFAULT_ANCHOR_DEVICE_ID = 0
DEFAULT_TAG_DEVICE_ID = 0
DEFAULT_CHANNEL = 1
ROLE_TAG = 0
ROLE_ANCHOR = 1


def prompt_float(label: str) -> float:
    while True:
        raw = input(f"{label}: ").strip()
        try:
            return float(raw)
        except ValueError:
            print("Enter a numeric value.")


def prompt_yes_no(label: str, default: bool = True) -> bool:
    if "--non-interactive" in sys.argv:
        return default
    suffix = "Y/n" if default else "y/N"
    raw = input(f"{label} [{suffix}]: ").strip().lower()
    if not raw:
        return default
    return raw in {"y", "yes"}


def print_header(title: str) -> None:
    print()
    print(title)
    print("-" * len(title))


def check_device(device: Bu04Device, label: str) -> None:
    response, ok = device.command_ok("AT")
    if device.is_init_failed(response):
        raise RuntimeError(
            f"{label} on {device.port} is stuck in INIT FAILED. "
            "Power-cycle the module, then rerun the script."
        )
    if not ok:
        raise RuntimeError(
            f"{label} on {device.port} did not respond to AT. "
            "Check power, wiring, and that only one program uses the serial port."
        )
    version = device.get_version()
    print(f"{label}: {device.port}  firmware={version or 'unknown'}")


def reboot_device(device: Bu04Device) -> None:
    device.close()
    time.sleep(0.5)
    device.open()
    time.sleep(BOOT_WAIT_S)
    device.drain(0.5)
    response, ok = device.command_ok("AT")
    if device.is_init_failed(response):
        raise RuntimeError(
            f"{device.port} is stuck in INIT FAILED after restart. "
            "Power-cycle the module and rerun the script."
        )
    if not ok:
        raise RuntimeError(
            f"{device.port} did not respond after restart. "
            "Power-cycle the module and run the script again."
        )


def prompt_power_cycle(port: str) -> None:
    print(
        f"\n>>> Unplug {port} for 5 seconds, then replug it.",
        flush=True,
    )
    print(
        ">>> Do this after SETCFG with id=0 — software restart leaves the module stuck.",
        flush=True,
    )
    input(">>> Press Enter after replugging... ")


def configure_device_roles(
    anchor: Bu04Device,
    tag: Bu04Device,
    anchor_id: int,
    tag_id: int,
    channel: int,
    *,
    interactive_recovery: bool = True,
) -> None:
    print("Configuring anchor/tag roles...")
    print(
        "  Using AT+SETCFG=<id>,<role>,<channel>,<rate> "
        f"(anchor id={anchor_id}, tag id={tag_id}, channel={channel})"
    )
    if anchor_id == 65535 or tag_id == 65535:
        print(
            "  Note: device id 65535 is the factory default on firmware V1.0.0. "
            "Changing to id=0 is not supported (INIT FAILED blocks SAVE). "
            "Using factory network 4369 for PDOA."
        )

    recovery = prompt_power_cycle if interactive_recovery else None

    print(f"  configuring anchor on {anchor.port} ...", flush=True)
    anchor_response, anchor_ok = anchor.apply_role_config(
        anchor_id,
        ROLE_ANCHOR,
        channel=channel,
        wait_for_recovery=recovery,
    )
    if not anchor_ok:
        raise RuntimeError(
            f"Failed to configure anchor on {anchor.port}: {anchor_response!r}\n"
            "Unplug/replug the USB cable for that module, wait 5 seconds, then rerun. "
            "If AT+GETCFG already shows role=1, use --skip-setup."
        )
    print("  anchor configured and saved", flush=True)
    time.sleep(3.0)

    tag_cfg = tag.get_role_config()
    if (
        tag_cfg
        and tag_cfg.role == ROLE_TAG
        and tag_cfg.device_id == tag_id
        and tag_cfg.channel == channel
    ):
        print(f"  tag on {tag.port} already has role=0; skipping SETCFG", flush=True)
    else:
        print(f"  configuring tag on {tag.port} ...", flush=True)
        tag_response, tag_ok = tag.apply_role_config(
            tag_id,
            ROLE_TAG,
            channel=channel,
            wait_for_recovery=recovery,
        )
        if not tag_ok:
            raise RuntimeError(
                f"Failed to configure tag on {tag.port}: {tag_response!r}\n"
                "Unplug/replug the USB cable for that module, wait 5 seconds, then rerun. "
                "If AT+GETCFG already shows role=0, use --skip-setup."
            )
        print("  tag configured and saved", flush=True)

    print("  Waiting for modules to finish reboot (power-cycle FTDI if a port stays silent)...")
    time.sleep(8.0)
    for label, device in ("Anchor", anchor), ("Tag", tag):
        try:
            device.open()
            device.drain(2.0)
            cfg = device.get_role_config()
            ver = device.get_version()
            print(f"  {label} {device.port}: firmware={ver} cfg={cfg}")
        except Exception as exc:
            print(f"  {label} {device.port}: not responding yet ({exc})")

    tag_cfg = tag.get_role_config()
    anchor_cfg = anchor.get_role_config()
    if tag_cfg is None or anchor_cfg is None:
        print(
            "  Note: AT unavailable after SAVE (modules may be running UWB). "
            "Roles were verified in RAM before SAVE. Power-cycle both FTDI USB cables, "
            "then continue or rerun with --skip-setup."
        )
        return

    if tag_cfg.role != ROLE_TAG or anchor_cfg.role != ROLE_ANCHOR:
        raise RuntimeError(
            f"Role verification failed: tag role={tag_cfg.role}, anchor role={anchor_cfg.role}"
        )

    print(
        f"  tag:    ID={tag_cfg.device_id} role={tag_cfg.role} CH={tag_cfg.channel}\n"
        f"  anchor: ID={anchor_cfg.device_id} role={anchor_cfg.role} CH={anchor_cfg.channel}"
    )


def ensure_roles_configured(
    anchor: Bu04Device,
    tag: Bu04Device,
    anchor_id: int,
    tag_id: int,
    channel: int,
    force: bool,
    *,
    interactive_recovery: bool = True,
) -> None:
    tag_cfg = tag.get_role_config()
    anchor_cfg = anchor.get_role_config()
    if (
        not force
        and tag_cfg
        and anchor_cfg
        and tag_cfg.role == ROLE_TAG
        and anchor_cfg.role == ROLE_ANCHOR
        and tag_cfg.device_id == tag_id
        and anchor_cfg.device_id == anchor_id
        and tag_cfg.channel == channel
        and anchor_cfg.channel == channel
    ):
        print("Roles already configured; skipping SETCFG.")
        return
    configure_device_roles(
        anchor,
        tag,
        anchor_id,
        tag_id,
        channel,
        interactive_recovery=interactive_recovery,
    )


def setup_pdoa_anchor(anchor: Bu04Device, network: int, anchor_id: int) -> None:
    # Vendor PDOA PC tool uses the node.o TWR stack (SETUWBMODE=0), not the bare
    # ds_twr_sts_sdc_* examples selected by SETUWBMODE=1.
    if not anchor.set_uwb_mode(0):
        raise RuntimeError("Failed to keep anchor on TWR/node stack for PDOA")
    if not anchor.pdoa_auth():
        raise RuntimeError("PDOA authentication failed on anchor")
    if not anchor.set_pdoa_config(network, anchor_id, filter_enabled=0, user_cmd=1):
        raise RuntimeError("Failed to configure anchor PDOA parameters")
    _, ok = anchor.command_ok("AT+FILTER=0")
    if not ok:
        raise RuntimeError("Failed to disable anchor filtering")
    _, ok = anchor.command_ok("AT+USER_CMD=1")
    if not ok:
        raise RuntimeError("Failed to enable anchor measurement output")


def setup_pdoa_tag(tag: Bu04Device, network: int, anchor_id: int) -> None:
    if not tag.set_uwb_mode(0):
        raise RuntimeError("Failed to keep tag on TWR/node stack for PDOA")
    if not tag.pdoa_auth():
        raise RuntimeError("PDOA authentication failed on tag")
    if not tag.set_pdoa_config(network, anchor_id, filter_enabled=0, user_cmd=0):
        raise RuntimeError("Failed to configure tag PDOA parameters")
    _, ok = tag.command_ok("AT+FILTER=0")
    if not ok:
        raise RuntimeError("Failed to disable tag filtering")


def parse_discovery_list(response: str) -> list[dict[str, str]]:
    match = re.search(r'DList"\s*:\s*(\[[\s\S]*?\])', response)
    if not match:
        return []
    try:
        payload = '{"DList": ' + match.group(1) + "}"
        data = json.loads(payload)
        items = data.get("DList", [])
        return items if isinstance(items, list) else []
    except json.JSONDecodeError:
        entries: list[dict[str, str]] = []
        for addr, short in re.findall(
            r'"Addr"\s*:\s*"([0-9A-Fa-f]+)".*?"ShortAddr"\s*:\s*"([0-9A-Fa-f]+)"',
            response,
            flags=re.IGNORECASE | re.DOTALL,
        ):
            entries.append({"Addr": addr, "ShortAddr": short})
        return entries


def pair_discovered_tags(anchor: Bu04Device, wait_s: float = 15.0) -> list[str]:
    print(f"Searching for tags ({wait_s:.0f}s)...")
    deadline = time.monotonic() + wait_s
    seen: set[str] = set()
    paired: list[str] = []

    while time.monotonic() < deadline:
        streamed = anchor.read_available(1.0)
        if streamed.strip():
            print(f"  anchor stream: {streamed.strip()[:120]}")

        response, ok = anchor.command_ok("AT+GETDLIST", wait=0.5)
        if ok:
            for entry in parse_discovery_list(response):
                if isinstance(entry, dict):
                    addr = str(entry.get("Addr") or entry.get("addr") or entry.get("Address") or "")
                    short = str(entry.get("ShortAddr") or entry.get("short") or entry.get("Short") or "0")
                else:
                    addr = str(entry)
                    short = addr[-4:] if len(addr) >= 4 else "1"
                if not addr or addr in seen:
                    continue
                seen.add(addr)
                add_cmd = f"AT+ADDTAG={addr},{short},1,64,0"
                _, add_ok = anchor.command_ok(add_cmd, wait=1.0)
                if add_ok:
                    paired.append(addr)
                    print(f"  paired tag {addr} (short: {short})")
        time.sleep(0.5)

    if not paired:
        print("  no tags discovered automatically")
    return paired


def diagnose_ranging_blocker(anchor: Bu04Device, tag: Bu04Device) -> str:
    """Collect runtime evidence for why ranging produced no samples."""
    anchor_role = anchor.get_role_config()
    tag_role = tag.get_role_config()
    anchor_pdoa = anchor.get_pdoa_config()
    tag_pdoa = tag.get_pdoa_config()
    tag_distance_resp, tag_distance_ok = tag.command_ok("AT+DISTANCE", wait=2.0)
    dlist_resp, dlist_ok = anchor.command_ok("AT+GETDLIST", wait=1.0)

    lines = ["No range samples received. Runtime diagnosis:"]
    if anchor_role:
        lines.append(
            f"  Anchor: id={anchor_role.device_id} role={anchor_role.role} ch={anchor_role.channel}"
        )
    if tag_role:
        lines.append(
            f"  Tag:    id={tag_role.device_id} role={tag_role.role} ch={tag_role.channel}"
        )
    if anchor_pdoa and tag_pdoa:
        lines.append(
            f"  PDOA network: anchor net={anchor_pdoa.net} tag net={tag_pdoa.net}"
        )
        lines.append(f"  Tag PDOA anc_id (poll target): {tag_pdoa.anc_id}")
    lines.append(
        f"  TWR AT+DISTANCE on tag: ok={tag_distance_ok} preview={tag_distance_resp[:80]!r}"
    )
    if not tag_distance_ok:
        lines.extend(
            [
                "",
                "  After AT+SAVE both modules reboot into UWB and may stop answering AT.",
                "  Flash V1.0.10+ and use AT+UWBSTART, or power-cycle both FTDI ports and retry.",
                "  If a module prints INIT FAILED / DEV ID FAILED, fix DW3000 init first.",
            ]
        )
    lines.append(
        f"  Anchor AT+GETDLIST: ok={dlist_ok} preview={dlist_resp[:80]!r}"
    )

    factory_ids = (
        anchor_role
        and tag_role
        and anchor_role.device_id == 65535
        and tag_role.device_id == 65535
    )
    if factory_ids:
        lines.extend(
            [
                "",
                "  LIKELY ROOT CAUSE: both modules use factory device id 65535.",
                "  Firmware V1.0.0 cannot UWB-range at id=65535 (discovery empty, distance 0).",
                "  Vendor docs require id=0, but V1.0.0 INIT FAILED blocks AT+SAVE for id changes.",
                "  See: https://bbs.ai-thinker.com/forum.php?mod=viewthread&tid=78482",
                "",
                "  Options:",
                "    1. Request updated firmware from Ai-Thinker (support@aithinker.com)",
                "    2. Flash modules via ST-Link / STM32CubeProgrammer",
                "    3. Use the vendor PC configuration tool from docs.ai-thinker.com/uwb",
            ]
        )
    else:
        lines.extend(
            [
                "",
                "  Checklist:",
                "    1. Anchor role=1 and tag role=0",
                "    2. Both modules share the same channel and PDOA network",
                "    3. BU04 ring antennas face each other (~0.5-2 m)",
                "    4. Power-cycle any module printing INIT FAILED",
            ]
        )

    return "\n".join(lines)


def collect_pdoa_samples(
    anchor: Bu04Device,
    samples: int,
    timeout_s: float,
) -> list[Measurement]:
    print(f"Collecting up to {samples} PDOA samples for {timeout_s:.0f}s...")
    text = anchor.read_available(timeout_s)
    measurements = parse_measurements(text)
    if not measurements and text.strip():
        print(f"  raw anchor output: {text.strip()[:200]}")
    if len(measurements) > samples:
        measurements = measurements[-samples:]
    print(f"  received {len(measurements)} parsed samples")
    return measurements


def collect_pdoa_or_twr_distance(
    anchor: Bu04Device,
    tag: Bu04Device,
    samples: int,
    sample_time_s: float,
    interval_s: float,
) -> tuple[list[Measurement], list[float]]:
    pdoa = collect_pdoa_samples(anchor, samples, sample_time_s)
    if pdoa:
        return pdoa, []
    twr = collect_twr_distance_samples(tag, samples, interval_s, anchor=anchor)
    return [], twr


def _firmware_supports_uwbstart(version: str | None) -> bool:
    return bool(version and version >= "V1.0.10")


def _trigger_uwb_start(port: str, label: str, *, use_uwbstart: bool) -> None:
    command = b"AT+UWBSTART\r\n" if use_uwbstart else b"AT+SAVE\r\n"
    action = "UWBSTART" if use_uwbstart else "SAVE"
    with Bu04Device(port, timeout=3) as device:
        device.drain(0.5)
        version = device.get_version()
        if not use_uwbstart and _firmware_supports_uwbstart(version):
            command = b"AT+UWBSTART\r\n"
            action = "UWBSTART"
        device._ser.write(command)
        response = device.read_text(4.0 if action == "SAVE" else 2.0)
        if device.response_ok(response):
            print(f"  {label} {action} OK")
        else:
            print(f"  {label} {action} response: {response[:120]!r}")


def start_uwb_ranging(anchor: Bu04Device, tag: Bu04Device) -> None:
    """Start anchor first, then tag. Prefer AT+UWBSTART (V1.0.10+) over rebooting SAVE."""
    use_uwbstart = False
    with Bu04Device(anchor.port, timeout=2) as probe:
        use_uwbstart = _firmware_supports_uwbstart(probe.get_version())

    if use_uwbstart:
        print("Starting UWB (AT+UWBSTART, anchor then tag)...")
    else:
        print("Starting UWB (staggered AT+SAVE: anchor first, then tag)...")

    anchor.close()
    tag.close()
    time.sleep(0.3)

    _trigger_uwb_start(anchor.port, "anchor", use_uwbstart=use_uwbstart)
    time.sleep(3 if use_uwbstart else 12)
    _trigger_uwb_start(tag.port, "tag", use_uwbstart=use_uwbstart)
    # After UWBSTART the tag immediately enters its ranging loop and starts
    # streaming distance: NNN every 200 ms.  Open the port now so the stream
    # collector can start receiving without an extra reconnect delay.
    time.sleep(1)

    anchor.open()
    tag.open()
    anchor.drain(0.5)
    tag.drain(0.5)


def twr_distance_is_active(tag: Bu04Device) -> bool:
    """Check if streaming distance lines are coming from the tag (V1.0.11+)."""
    try:
        if not tag._ser or not tag._ser.is_open:
            tag.open()
        # Read passively for 1 s — firmware streams distance every 200 ms
        raw = tag.read_available(1.0)
        if raw.strip():
            print(f"  tag stream: {raw.strip()[:80]!r}")
        for m in DISTANCE_RE.finditer(raw):
            if int(m.group(1)) > 0:
                return True
    except Exception:
        pass
    return False


def collect_twr_distance_samples(
    tag: Bu04Device,
    samples: int,
    interval_s: float,
    *,
    anchor: Bu04Device | None = None,
) -> list[float]:
    """Collect TWR distance samples during an active UWB session.

    DS-TWR with STS-SDC computes range on the anchor (responder). Prefer
    ``AT+DISTANCE`` on the anchor; fall back to passive tag stream if needed.
    """
    print(
        f"Collecting {samples} TWR distance samples "
        f"(anchor AT+DISTANCE + stream, up to {max(30.0, samples * interval_s):.0f}s)..."
    )
    values: list[float] = []
    deadline = time.monotonic() + max(30.0, samples * interval_s)
    raw_buf = ""
    last_preview = time.monotonic()

    while len(values) < samples and time.monotonic() < deadline:
        got_sample = False
        if anchor is not None:
            try:
                if not anchor._ser or not anchor._ser.is_open:
                    anchor.open()
                dist_m = anchor.read_distance()
                if dist_m is not None and dist_m > 0:
                    values.append(dist_m)
                    got_sample = True
                    if len(values) <= 3:
                        print(f"  anchor sample {len(values)}: {dist_m:.3f} m")
            except Exception as exc:
                print(f"  anchor distance read error: {exc}")

        if not got_sample:
            try:
                if not tag._ser or not tag._ser.is_open:
                    tag.open()
                chunk = tag.read_available(0.2)
                if anchor is not None and anchor._ser and anchor._ser.is_open:
                    chunk += anchor.read_available(0.0) or ""
                if chunk:
                    raw_buf += chunk
                    if time.monotonic() - last_preview >= 2.0:
                        print(f"  stream raw (last 120): {raw_buf[-120:]!r}")
                        last_preview = time.monotonic()
                    for m in DISTANCE_RE.finditer(raw_buf):
                        dist_m = float(m.group(1)) / 1000.0
                        if dist_m > 0:
                            values.append(dist_m)
                            if len(values) <= 3:
                                print(f"  stream sample {len(values)}: {dist_m:.3f} m")
                            break
                    last_nl = raw_buf.rfind('\n')
                    if last_nl >= 0:
                        raw_buf = raw_buf[last_nl + 1:]
            except Exception as exc:
                print(f"  stream read error: {exc}")
                tag.close()
                if anchor is not None:
                    anchor.close()
                time.sleep(0.5)
                try:
                    tag.open()
                    if anchor is not None:
                        anchor.open()
                except Exception:
                    pass

        time.sleep(max(0.05, interval_s))

    print(f"  received {len(values)} non-zero samples")
    if not values and "cb: 0" in raw_buf and "irq:" in raw_buf:
        print(
            "  Note: tag cb may stay 0 in STS-SDC DS-TWR (range is computed on the anchor). "
            "Check anchor AT+DISTANCE and antenna facing (~0.5-2 m)."
        )
    return values


def calibrate_distance_pdoa(
    anchor: Bu04Device,
    tag: Bu04Device,
    known_distance_m: float,
    samples: int,
    sample_time_s: float,
    interval_s: float,
    dry_run: bool,
) -> int:
    config = anchor.get_pdoa_config()
    if config is None:
        raise RuntimeError("Could not read anchor PDOA config")

    measurements, twr_distances = collect_pdoa_or_twr_distance(
        anchor, tag, samples, sample_time_s, interval_s
    )
    if measurements:
        measured_m = median([m.range_m for m in measurements])
        source = "PDOA"
    elif twr_distances:
        measured_m = median(twr_distances)
        source = "TWR fallback"
    else:
        raise RuntimeError(diagnose_ranging_blocker(anchor, tag))

    delta_mm = round((known_distance_m - measured_m) * 1000)
    new_offset = config.rng_offset + delta_mm

    print_header(f"Distance calibration ({source})")
    print(f"Known distance:     {known_distance_m:.3f} m")
    print(f"Measured distance:  {measured_m:.3f} m")
    print(f"Current RNG offset: {config.rng_offset} mm")
    print(f"Correction:         {delta_mm:+d} mm")
    print(f"New RNG offset:     {new_offset} mm")

    if dry_run:
        print("Dry run: offset not written.")
        return 0

    if not prompt_yes_no("Apply distance offset?"):
        print("Skipped distance calibration.")
        return 0

    if not anchor.set_rng_offset(new_offset):
        raise RuntimeError("Failed to write AT+RNGOFF")
    print("Distance offset applied.")
    return new_offset


def calibrate_azimuth_pdoa(
    anchor: Bu04Device,
    known_azimuth_deg: float,
    samples: int,
    sample_time_s: float,
    dry_run: bool,
) -> int:
    config = anchor.get_pdoa_config()
    if config is None:
        raise RuntimeError("Could not read anchor PDOA config")

    measurements = collect_pdoa_samples(anchor, samples, sample_time_s)
    if not measurements:
        raise RuntimeError(
            "No PDOA angle samples received from the anchor. "
            "Check tag pairing and place the tag in the anchor field of view."
        )

    measured_deg = median([float(m.angle_deg) for m in measurements])
    delta_deg = round(known_azimuth_deg - measured_deg)
    new_offset = config.pdoa_offset + delta_deg

    print_header("Azimuth calibration (PDOA)")
    print(f"Known azimuth:      {known_azimuth_deg:.1f} deg")
    print(f"Measured azimuth:   {measured_deg:.1f} deg")
    print(f"Current PDOA offset:{config.pdoa_offset} deg")
    print(f"Correction:         {delta_deg:+d} deg")
    print(f"New PDOA offset:    {new_offset} deg")

    if dry_run:
        print("Dry run: offset not written.")
        return 0

    if not prompt_yes_no("Apply azimuth offset?"):
        print("Skipped azimuth calibration.")
        return 0

    if not anchor.set_pdoa_offset(new_offset):
        raise RuntimeError("Failed to write AT+PDOAOFF")
    print("Azimuth offset applied.")
    return new_offset


def calibrate_distance_twr(
    tag: Bu04Device,
    known_distance_m: float,
    samples: int,
    interval_s: float,
    dry_run: bool,
    *,
    anchor: Bu04Device | None = None,
) -> int:
    config = tag.get_dev_config()
    if config is None:
        raise RuntimeError("Could not read tag TWR device config")

    if anchor is not None and not twr_distance_is_active(tag):
        start_uwb_ranging(anchor, tag)

    values = collect_twr_distance_samples(tag, samples, interval_s, anchor=anchor)
    if not values:
        raise RuntimeError(diagnose_ranging_blocker(anchor, tag) if anchor else (
            "No TWR distance samples received. "
            "Configure one module as anchor and one as tag, then retry."
        ))

    measured_m = median(values)
    # Approximate DW3000 antenna-delay scale used by Ai-Thinker tooling (~164 units per meter).
    delta_delay = round((measured_m - known_distance_m) * 164)
    new_delay = max(0, config.anndelay + delta_delay)

    print_header("Distance calibration (TWR)")
    print(f"Known distance:     {known_distance_m:.3f} m")
    print(f"Measured distance:  {measured_m:.3f} m")
    print(f"Current antenna delay:{config.anndelay}")
    print(f"Delay correction:   {delta_delay:+d}")
    print(f"New antenna delay:  {new_delay}")

    if dry_run:
        print("Dry run: antenna delay not written.")
        return config.anndelay

    if not prompt_yes_no("Apply antenna delay?"):
        print("Skipped TWR distance calibration.")
        return config.anndelay

    updated = DevConfig(
        cap=config.cap,
        anndelay=new_delay,
        kalman_enable=config.kalman_enable,
        kalman_q=config.kalman_q,
        kalman_r=config.kalman_r,
        para_a=config.para_a,
        para_b=config.para_b,
        pos_enable=config.pos_enable,
        pos_dimen=config.pos_dimen,
    )
    if not tag.set_dev_config(updated):
        raise RuntimeError("Failed to write AT+SETDEV")
    print("TWR antenna delay applied.")
    return new_delay


def validate_roles_configured(anchor: Bu04Device, tag: Bu04Device) -> None:
    """Fail fast when --skip-setup but modules are not anchor+tag (common after make flash)."""
    anchor_cfg = anchor.get_role_config()
    tag_cfg = tag.get_role_config()
    if anchor_cfg is None or tag_cfg is None:
        raise RuntimeError(
            "Could not read AT+GETCFG from both modules. "
            "Power-cycle both FTDI USB ports, then retry."
        )
    problems: list[str] = []
    if anchor_cfg.role != ROLE_ANCHOR:
        problems.append(
            f"Anchor {anchor.port} has role={anchor_cfg.role} (need {ROLE_ANCHOR}). "
            f"GETCFG: id={anchor_cfg.device_id} role={anchor_cfg.role}"
        )
    if tag_cfg.role != ROLE_TAG:
        problems.append(
            f"Tag {tag.port} has role={tag_cfg.role} (need {ROLE_TAG}). "
            f"GETCFG: id={tag_cfg.device_id} role={tag_cfg.role}"
        )
    if problems:
        raise RuntimeError(
            "Role mismatch — flash/deploy resets NVM to factory defaults (id=65535, role=tag).\n"
            + "\n".join(f"  • {p}" for p in problems)
            + "\n\nReconfigure without --skip-setup, e.g.:\n"
            "  python3 calibrate.py --anchor /dev/ttyUSB0 --tag /dev/ttyUSB4 "
            "--non-interactive --mode twr --known-distance 1.2"
        )


def show_status(anchor: Bu04Device, tag: Bu04Device) -> None:
    print_header("Device status")
    for label, device in ("Anchor", anchor), ("Tag", tag):
        mode = device.get_uwb_mode()
        mode_name = "PDOA" if mode == 1 else "TWR" if mode == 0 else "unknown"
        role = device.get_role_config()
        role_name = "anchor" if role and role.role == ROLE_ANCHOR else "tag" if role else "unknown"
        print(f"{label}: {device.port}  mode={mode_name}  role={role_name}")
        if role:
            print(f"  id={role.device_id} ch={role.channel} rate={role.rate}")
        pdoa = device.get_pdoa_config()
        if pdoa:
            print(
                f"  net={pdoa.net} anc_id={pdoa.anc_id} "
                f"rng_offset={pdoa.rng_offset} mm  pdoa_offset={pdoa.pdoa_offset} deg"
            )
        dev = device.get_dev_config()
        if dev:
            print(f"  antenna_delay={dev.anndelay}  para_a={dev.para_a}  para_b={dev.para_b}")


def run_calibration(args: argparse.Namespace) -> int:
    with Bu04Device(args.anchor, timeout=args.timeout) as anchor, Bu04Device(
        args.tag, timeout=args.timeout
    ) as tag:
        check_device(anchor, "Anchor")
        check_device(tag, "Tag")

        if args.status:
            show_status(anchor, tag)
            return 0

        if args.diagnose:
            print_header("Ranging diagnosis")
            print(diagnose_ranging_blocker(anchor, tag))
            return 0

        if args.mode == "twr":
            # Set TWR mode in RAM on both modules BEFORE the SETCFG+SAVE cycle
            # so the mode is persisted.  If this fails (AT already dead from a
            # previous SAVE boot), we ignore it and rely on UWBSTART later.
            for label, dev in [("anchor", anchor), ("tag", tag)]:
                resp, ok = dev.command_ok("AT+SETUWBMODE=0")
                if ok:
                    print(f"  {label}: TWR mode set OK")
                else:
                    print(f"  {label}: SETUWBMODE=0 failed ({resp[:60]!r}) — AT may be silent post-SAVE, continuing")

        if not args.skip_setup:
            ensure_roles_configured(
                anchor,
                tag,
                args.anchor_device_id,
                args.tag_device_id,
                args.channel,
                force=args.force_setup,
                interactive_recovery=not args.non_interactive,
            )
        else:
            print("Skipping AT+SETCFG role setup (--skip-setup).")
            validate_roles_configured(anchor, tag)

        # After SAVE the modules reboot into UWB mode automatically.
        # AT is silent at this point.  Reset both to get back to AT command mode,
        # then start UWB explicitly via AT+UWBSTART.
        if not args.skip_setup and args.mode in ("twr", "pdoa"):
            import subprocess
            print("Resetting modules to restore AT command mode after SAVE...")
            subprocess.run(
                ["make", "-C", "Refs/STM32F103-BU0x_SDK", "reset-all"],
                cwd="/home/valentin/RL/UWB",
                check=False,
            )
            time.sleep(6)
            # Re-open ports after reset
            try:
                anchor.close()
                tag.close()
            except Exception:
                pass
            anchor.open()
            tag.open()
            anchor.drain(2.0)
            tag.drain(2.0)
            print("  Anchor post-reset:", anchor.get_version() or "no response")
            print("  Tag   post-reset:", tag.get_version() or "no response")

        if args.mode == "pdoa":
            pdoa_anc_id = args.anchor_device_id if args.anchor_device_id != 65535 else args.anchor_id
            setup_pdoa_tag(tag, args.network, pdoa_anc_id)
            setup_pdoa_anchor(anchor, args.network, pdoa_anc_id)
            tag_id = args.tag_device_id if args.tag_device_id != 65535 else 0
            add_short = f"{tag_id:04X}" if tag_id else "0"
            _, add_ok = anchor.command_ok(
                f"AT+ADDTAG={tag_id},{add_short},1,1,0",
                wait=2.0,
            )
            if add_ok:
                print(f"  anchor: registered tag id={tag_id} in KList for PDOA")
            else:
                print("  anchor: AT+ADDTAG failed — will try discovery after UWBSTART")
            start_uwb_ranging(anchor, tag)
            anchor.open()
            tag.open()
            paired = pair_discovered_tags(anchor, wait_s=args.discover_time)
            if not paired and not add_ok:
                print("  warning: no tags paired — PDOA angle output may not start")
        elif args.mode == "twr":
            # Confirm TWR mode is set (may need re-sending after reset)
            for label, dev in [("anchor", anchor), ("tag", tag)]:
                resp, ok = dev.command_ok("AT+SETUWBMODE=0")
                if ok:
                    print(f"  {label}: TWR mode confirmed OK")
                else:
                    print(f"  {label}: SETUWBMODE=0 still failing — continuing anyway ({resp[:60]!r})")
            # Tag must know which anchor to poll (factory anc_id=65535 breaks ranging at id=0).
            anchor_pdoa_id = args.anchor_device_id if args.anchor_device_id != 65535 else 0
            if tag.set_pdoa_config(args.network, anchor_pdoa_id, filter_enabled=0):
                print(f"  tag: PDOA anchor id set to {anchor_pdoa_id} (TWR poll target)")
            else:
                print("  tag: PDOASETCFG failed — continuing anyway")

        show_status(anchor, tag)

        if args.mode == "pdoa":
            if not args.non_interactive:
                print()
                print("Place the tag at a known distance along the anchor boresight.")
                print("Point the BU04 ring antenna toward the other module.")
                print("Use 0 deg azimuth when the tag is directly in front of the anchor.")
                known_distance_m = prompt_float("Known distance to tag (meters)")
                calibrate_distance_pdoa(
                    anchor,
                    tag,
                    known_distance_m,
                    args.samples,
                    args.sample_time,
                    args.interval,
                    args.dry_run,
                )

                if not args.skip_azimuth:
                    print()
                    print("Keep the same distance and set a known azimuth angle.")
                    known_azimuth_deg = prompt_float("Known azimuth (degrees)")
                    calibrate_azimuth_pdoa(
                        anchor,
                        known_azimuth_deg,
                        args.samples,
                        args.sample_time,
                        args.dry_run,
                    )
            else:
                if args.known_distance is None:
                    raise RuntimeError("--known-distance is required in non-interactive mode")
                calibrate_distance_pdoa(
                    anchor,
                    tag,
                    args.known_distance,
                    args.samples,
                    args.sample_time,
                    args.interval,
                    args.dry_run,
                )
                if not args.skip_azimuth:
                    if args.known_azimuth is None:
                        raise RuntimeError("--known-azimuth is required unless --skip-azimuth is set")
                    calibrate_azimuth_pdoa(
                        anchor,
                        args.known_azimuth,
                        args.samples,
                        args.sample_time,
                        args.dry_run,
                    )
        else:
            if args.known_distance is None and args.non_interactive:
                raise RuntimeError("--known-distance is required in non-interactive mode")
            known_distance_m = args.known_distance or prompt_float("Known distance to tag (meters)")
            calibrate_distance_twr(
                tag,
                known_distance_m,
                args.samples,
                args.interval,
                args.dry_run,
                anchor=anchor,
            )
            if not args.dry_run and prompt_yes_no("Save TWR calibration to tag?", default=True):
                _, saved = tag.save()
                if not saved:
                    raise RuntimeError("Failed to save tag configuration")

        if args.mode == "pdoa" and not args.dry_run:
            if prompt_yes_no("Save PDOA calibration to anchor?", default=True):
                _, saved = anchor.save()
                if not saved:
                    raise RuntimeError("Failed to save anchor configuration")
                print("Calibration saved.")

        print()
        print("Calibration complete.")
        show_status(anchor, tag)
        return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Calibrate BU04 distance and azimuth using two serial-connected modules. "
            "Defaults: anchor=/dev/ttyUSB0, tag=/dev/ttyUSB4, PDOA mode."
        )
    )
    parser.add_argument("--anchor", default=DEFAULT_ANCHOR, help="Serial port for the anchor/base")
    parser.add_argument("--tag", default=DEFAULT_TAG, help="Serial port for the tag")
    parser.add_argument(
        "--mode",
        choices=("pdoa", "twr"),
        default="pdoa",
        help="Calibration mode (default: pdoa for distance + azimuth)",
    )
    parser.add_argument("--network", type=int, default=DEFAULT_NETWORK, help="PDOA network ID")
    parser.add_argument("--anchor-id", type=int, default=DEFAULT_ANCHOR_ID, help="Anchor ID")
    parser.add_argument(
        "--anchor-device-id",
        type=int,
        default=DEFAULT_ANCHOR_DEVICE_ID,
        help="Device ID used in AT+SETCFG for anchor (default: 0)",
    )
    parser.add_argument(
        "--tag-device-id",
        type=int,
        default=DEFAULT_TAG_DEVICE_ID,
        help="Device ID used in AT+SETCFG for tag (default: 0)",
    )
    parser.add_argument(
        "--channel",
        type=int,
        default=DEFAULT_CHANNEL,
        choices=(0, 1),
        help="UWB channel for AT+SETCFG: 0=ch9, 1=ch5 (default: 1)",
    )
    parser.add_argument(
        "--force-setup",
        action="store_true",
        help="Always rerun AT+SETCFG even if roles look correct",
    )
    parser.add_argument(
        "--skip-setup",
        action="store_true",
        help="Skip AT+SETCFG role configuration",
    )
    parser.add_argument("--samples", type=int, default=20, help="Number of measurement samples")
    parser.add_argument(
        "--sample-time",
        type=float,
        default=10.0,
        help="Seconds to listen for PDOA measurements",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=0.5,
        help="Seconds between TWR AT+DISTANCE samples",
    )
    parser.add_argument(
        "--discover-time",
        type=float,
        default=15.0,
        help="Seconds to search for tags before calibration",
    )
    parser.add_argument("--known-distance", type=float, help="Known distance in meters")
    parser.add_argument("--known-azimuth", type=float, help="Known azimuth in degrees")
    parser.add_argument("--skip-azimuth", action="store_true", help="Skip azimuth calibration")
    parser.add_argument("--status", action="store_true", help="Show current offsets and exit")
    parser.add_argument(
        "--diagnose",
        action="store_true",
        help="Check roles/IDs and report why ranging may fail, then exit",
    )
    parser.add_argument("--dry-run", action="store_true", help="Compute offsets without writing")
    parser.add_argument(
        "--non-interactive",
        action="store_true",
        help="Use command-line values instead of prompts",
    )
    parser.add_argument("--timeout", type=float, default=1.0, help="Serial timeout in seconds")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        return run_calibration(args)
    except KeyboardInterrupt:
        print("\nCancelled.")
        return 130
    except RuntimeError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
