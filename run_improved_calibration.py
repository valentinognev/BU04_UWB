#!/usr/bin/env python3
"""Full improved calibration: reset delays, force setup, 2-pass anchor TWR, PDOA RNGOFF trim."""

from __future__ import annotations

import statistics
import sys
import time

# calibrate.prompt_yes_no checks sys.argv for non-interactive mode
if "--non-interactive" not in sys.argv:
    sys.argv.append("--non-interactive")

from bu04_at import Bu04Device, DevConfig
from calibrate import (
    DEFAULT_NETWORK,
    calibrate_distance_pdoa,
    calibrate_distance_twr,
    collect_twr_distance_samples,
    ensure_roles_configured,
    median,
    pair_discovered_tags,
    reset_modules_daplink,
    setup_pdoa_anchor,
    setup_pdoa_tag,
    start_uwb_ranging,
)

ANCHOR_PORT = "/dev/ttyACM0"
TAG_PORT = "/dev/ttyACM1"
KNOWN_M = 1.09
FACTORY_ANNDELAY = 16336
SAMPLES = 30
INTERVAL_S = 0.5
NETWORK = DEFAULT_NETWORK
DISCOVER_S = 12.0
POST_RESET_WAIT_S = 8.0
AT_TIMEOUT_S = 5.0


def print_step(title: str) -> None:
    print()
    print("=" * 60)
    print(title)
    print("=" * 60)


def reset_anndelay(device: Bu04Device, label: str) -> None:
    cfg = device.get_dev_config()
    if cfg is None:
        raise RuntimeError(f"Could not read GETDEV from {label} on {device.port}")
    if cfg.anndelay == FACTORY_ANNDELAY:
        print(f"  {label}: anndelay already {FACTORY_ANNDELAY}")
        return
    updated = DevConfig(
        cap=cfg.cap,
        anndelay=FACTORY_ANNDELAY,
        kalman_enable=cfg.kalman_enable,
        kalman_q=cfg.kalman_q,
        kalman_r=cfg.kalman_r,
        para_a=cfg.para_a,
        para_b=cfg.para_b,
        pos_enable=cfg.pos_enable,
        pos_dimen=cfg.pos_dimen,
    )
    if not device.set_dev_config(updated):
        raise RuntimeError(f"Failed to reset anndelay on {label}")
    _, ok = device.save()
    if not ok:
        raise RuntimeError(f"Failed to save {label} after anndelay reset")
    print(f"  {label}: anndelay reset {cfg.anndelay} -> {FACTORY_ANNDELAY}")


def reset_rng_offset(anchor: Bu04Device) -> None:
    if anchor.set_rng_offset(0):
        print("  anchor: RNGOFF reset to 0 mm")
    else:
        print("  anchor: RNGOFF reset failed (continuing)")


def prepare_pdoa(anchor: Bu04Device, tag: Bu04Device) -> None:
    anchor.set_uwb_mode(0)
    tag.set_uwb_mode(0)
    setup_pdoa_tag(tag, NETWORK, 0)
    setup_pdoa_anchor(anchor, NETWORK, 0)
    anchor.command_ok("AT+FILTER=0")
    tag.command_ok("AT+FILTER=0")
    anchor.command_ok("AT+ADDTAG=0,0,1,1,0", wait=2.0)


def start_ranging(anchor: Bu04Device, tag: Bu04Device) -> None:
    start_uwb_ranging(anchor, tag)
    anchor.open()
    tag.open()
    anchor.drain(0.5)
    paired = pair_discovered_tags(anchor, wait_s=DISCOVER_S)
    if paired:
        print(f"  paired tags: {paired}")
    time.sleep(5)


def ensure_dw3000(anchor: Bu04Device, tag: Bu04Device) -> None:
    for label, dev in ("anchor", anchor), ("tag", tag):
        dev._ser.write(b"AT+UWBSTART\r\n")
        resp = dev.read_text(4) + dev.read_available(1)
        if "DEVID:0xDECA" in resp:
            print(f"  {label}: DW3000 OK")
            continue
        if label == "anchor" and "IIC Error" in resp:
            print(f"  {label}: IIC error — sending AT+RESTORE")
            dev._ser.write(b"AT+RESTORE\r\n")
            dev.read_text(5)
            time.sleep(2)
        dev._ser.write(b"AT+UWBSTART\r\n")
        resp = dev.read_text(4) + dev.read_available(1)
        if "DEVID:0xDECA" not in resp:
            raise RuntimeError(f"{label} DW3000 unhealthy after recovery: {resp[:150]!r}")


def verify_distance(anchor: Bu04Device, tag: Bu04Device, label: str) -> float:
    prepare_pdoa(anchor, tag)
    ensure_dw3000(anchor, tag)
    start_ranging(anchor, tag)
    values = collect_twr_distance_samples(tag, SAMPLES, INTERVAL_S, anchor=anchor)
    if not values:
        raise RuntimeError(f"{label}: no distance samples (check antennas / external power)")
    med = median(values)
    err_mm = (med - KNOWN_M) * 1000
    stdev_mm = statistics.stdev(values) * 1000 if len(values) > 1 else 0.0
    print(f"  {label}: median={med:.3f} m  error={err_mm:+.0f} mm  stdev={stdev_mm:.0f} mm  n={len(values)}")
    return med


def main() -> int:
    print_step("Step 0: DAPLink reset (restore AT command mode)")
    reset_modules_daplink(wait_s=10)
    time.sleep(POST_RESET_WAIT_S)

    print_step("Step 1: Role setup (anchor ACM0, tag ACM1)")
    with Bu04Device(ANCHOR_PORT, timeout=AT_TIMEOUT_S) as anchor, Bu04Device(
        TAG_PORT, timeout=AT_TIMEOUT_S
    ) as tag:
        time.sleep(2)
        anchor.drain(1)
        tag.drain(1)
        ensure_roles_configured(
            anchor, tag, 0, 0, 1, force=False, interactive_recovery=False
        )

    print_step("Step 2: Reset antenna delays to factory on BOTH modules")
    with Bu04Device(ANCHOR_PORT, timeout=AT_TIMEOUT_S) as anchor, Bu04Device(
        TAG_PORT, timeout=AT_TIMEOUT_S
    ) as tag:
        time.sleep(POST_RESET_WAIT_S)
        anchor.drain(1)
        tag.drain(1)
        reset_anndelay(anchor, "anchor")
        reset_anndelay(tag, "tag")
    reset_modules_daplink(wait_s=10)

    print_step("Step 3: PDOA/TWR prep (filter off, network 4369)")
    with Bu04Device(ANCHOR_PORT, timeout=AT_TIMEOUT_S) as anchor, Bu04Device(
        TAG_PORT, timeout=AT_TIMEOUT_S
    ) as tag:
        time.sleep(POST_RESET_WAIT_S)
        anchor.drain(1)
        tag.drain(1)
        prepare_pdoa(anchor, tag)
        reset_rng_offset(anchor)
        ensure_dw3000(anchor, tag)

    print_step("Step 4: Baseline measurement (factory anndelay)")
    with Bu04Device(ANCHOR_PORT, timeout=AT_TIMEOUT_S) as anchor, Bu04Device(
        TAG_PORT, timeout=AT_TIMEOUT_S
    ) as tag:
        time.sleep(POST_RESET_WAIT_S)
        verify_distance(anchor, tag, "baseline")

    for pass_num in (1, 2):
        print_step(f"Step 5.{pass_num}: Anchor TWR calibration pass {pass_num} ({SAMPLES} samples, RAM only)")
        with Bu04Device(ANCHOR_PORT, timeout=AT_TIMEOUT_S) as anchor, Bu04Device(
            TAG_PORT, timeout=AT_TIMEOUT_S
        ) as tag:
            time.sleep(2)
            anchor.drain(1)
            tag.drain(1)
            prepare_pdoa(anchor, tag)
            start_ranging(anchor, tag)
            new_delay, _ = calibrate_distance_twr(
                tag,
                KNOWN_M,
                SAMPLES,
                INTERVAL_S,
                dry_run=False,
                anchor=anchor,
            )
            print(f"  applied anchor anndelay={new_delay} (not saved yet)")

    print_step(f"Step 6: PDOA RNGOFF fine-trim ({SAMPLES} samples, RAM only)")
    with Bu04Device(ANCHOR_PORT, timeout=AT_TIMEOUT_S) as anchor, Bu04Device(
        TAG_PORT, timeout=AT_TIMEOUT_S
    ) as tag:
        time.sleep(POST_RESET_WAIT_S)
        anchor.drain(1)
        tag.drain(1)
        prepare_pdoa(anchor, tag)
        start_ranging(anchor, tag)
        new_rng = calibrate_distance_pdoa(
            anchor,
            tag,
            KNOWN_M,
            SAMPLES,
            sample_time_s=15.0,
            interval_s=INTERVAL_S,
            dry_run=False,
        )
        print(f"  applied anchor RNGOFF={new_rng} mm (not saved yet)")

    print_step("Step 7: Save anchor calibration once")
    with Bu04Device(ANCHOR_PORT, timeout=AT_TIMEOUT_S) as anchor:
        time.sleep(2)
        anchor.drain(1)
        _, ok = anchor.save()
        if not ok:
            raise RuntimeError("AT+SAVE failed on anchor")
        print("  anchor: saved to NVM")
    reset_modules_daplink(wait_s=10)

    print_step("Step 8: Final verification")
    with Bu04Device(ANCHOR_PORT, timeout=AT_TIMEOUT_S) as anchor, Bu04Device(
        TAG_PORT, timeout=AT_TIMEOUT_S
    ) as tag:
        time.sleep(POST_RESET_WAIT_S)
        anchor.drain(1)
        tag.drain(1)
        acfg = anchor.get_dev_config()
        pdoa = anchor.get_pdoa_config()
        tcfg = tag.get_dev_config()
        print(f"  anchor anndelay={acfg.anndelay if acfg else '?'}")
        print(f"  anchor rng_offset={pdoa.rng_offset if pdoa else '?'} mm")
        print(f"  tag anndelay={tcfg.anndelay if tcfg else '?'} (should stay {FACTORY_ANNDELAY})")
        verify_distance(anchor, tag, "FINAL")

    print()
    print("Improved calibration complete.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        raise SystemExit(1)
