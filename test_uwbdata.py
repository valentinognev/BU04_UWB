#!/usr/bin/env python3
"""Exercise AT+UWBDATA peer transfer between anchor and tag (firmware V1.0.47+)."""

from __future__ import annotations

import argparse
import sys
import time

from bu04_at import UWBDATA_INLINE_MAX, UWBDATA_MAX_PAYLOAD, Bu04Device
from calibrate import (
    DEFAULT_ANCHOR,
    DEFAULT_NETWORK,
    DEFAULT_TAG,
    ensure_roles_configured,
    pair_discovered_tags,
    setup_pdoa_anchor,
    setup_pdoa_tag,
    start_uwb_ranging,
    validate_roles_configured,
)


MIN_FIRMWARE = "V1.0.47"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send/receive UWB data packets via AT+UWBDATA on two BU04 modules.",
    )
    parser.add_argument("--anchor", default=DEFAULT_ANCHOR, help="Anchor serial port")
    parser.add_argument("--tag", default=DEFAULT_TAG, help="Tag serial port")
    parser.add_argument("--dest", type=int, default=0, help="Destination node address")
    parser.add_argument(
        "--direction",
        choices=("tag2anchor", "anchor2tag", "both"),
        default="both",
        help="Which link(s) to test",
    )
    parser.add_argument(
        "--size",
        type=int,
        default=0,
        help="Extra test with this payload size in bytes (0 = small default only)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=20.0,
        help="Seconds to wait for +UWBDATA on receiver",
    )
    parser.add_argument(
        "--setup",
        action="store_true",
        help="Configure roles + PDOA + UWBSTART before testing",
    )
    parser.add_argument(
        "--skip-setup",
        action="store_true",
        help="Assume roles are configured; only validate GETCFG",
    )
    parser.add_argument("--force-setup", action="store_true", help="Rerun SETCFG even if roles look OK")
    parser.add_argument("--anchor-id", type=int, default=0, help="Anchor device id for SETCFG")
    parser.add_argument("--tag-id", type=int, default=0, help="Tag device id for SETCFG")
    parser.add_argument("--channel", type=int, default=1, help="AT+SETCFG channel (1 = ch5)")
    parser.add_argument("--network", type=int, default=DEFAULT_NETWORK, help="UWB PAN / network id")
    parser.add_argument("--discover-time", type=float, default=10.0, help="Tag discovery wait after UWBSTART")
    parser.add_argument("--timeout-at", type=float, default=2.0, dest="at_timeout", help="AT command timeout")
    return parser.parse_args()


def require_firmware(device: Bu04Device, label: str) -> str:
    version = device.get_version()
    if not version:
        raise RuntimeError(f"{label} ({device.port}): no AT+GETVER response")
    if version < MIN_FIRMWARE:
        raise RuntimeError(
            f"{label} ({device.port}): firmware {version} — need {MIN_FIRMWARE}+ for AT+UWBDATA. "
            "Flash Refs/STM32F103-BU0x_SDK/build/bu04_firmware.hex"
        )
    return version


def setup_modules(
    anchor: Bu04Device,
    tag: Bu04Device,
    *,
    anchor_id: int,
    tag_id: int,
    channel: int,
    network: int,
    force_setup: bool,
    discover_time: float,
) -> None:
    print("Setting up modules for UWB data test...")
    for label, dev in ("Anchor", anchor), ("Tag", tag):
        dev.set_uwb_mode(0)

    ensure_roles_configured(
        anchor,
        tag,
        anchor_id,
        tag_id,
        channel,
        force=force_setup,
        interactive_recovery=False,
    )

    setup_pdoa_tag(tag, network, anchor_id)
    setup_pdoa_anchor(anchor, network, anchor_id)

    add_short = f"{tag_id:04X}" if tag_id else "0"
    _, add_ok = anchor.command_ok(f"AT+ADDTAG={tag_id},{add_short},1,1,0", wait=2.0)
    if add_ok:
        print(f"  anchor: registered tag id={tag_id}")
    else:
        print("  anchor: AT+ADDTAG failed — will try discovery after UWBSTART")

    start_uwb_ranging(anchor, tag)
    paired = pair_discovered_tags(anchor, wait_s=discover_time)
    if not paired and not add_ok:
        print("  warning: no tags paired — ranging/data may be unreliable")


def make_payload(label: str, size: int) -> bytes:
    if size <= 0:
        return label.encode("ascii")
    body = bytearray(size)
    seed = label.encode("ascii")
    for i in range(size):
        body[i] = seed[i % len(seed)] ^ (i & 0xFF)
    body[0] = ord("U")
    body[-1] = ord("W")
    return bytes(body)


def run_transfer(
    sender: Bu04Device,
    receiver: Bu04Device,
    *,
    sender_label: str,
    receiver_label: str,
    dest: int,
    payload: bytes,
    timeout: float,
) -> None:
    print(
        f"\n[{sender_label} -> {receiver_label}] "
        f"{len(payload)} bytes "
        f"({'inline hex' if len(payload) <= UWBDATA_INLINE_MAX else 'binary upload'})"
    )
    receiver.drain(0.5)
    sender.send_uwbdata(dest, payload, wait=sender.timeout)

    # UWB TX is queued and sent about every 500 ms on the firmware side.
    wait_budget = max(timeout, 5.0 + len(payload) / 500.0)
    print(f"  waiting up to {wait_budget:.0f}s for +UWBDATA on {receiver.port} ...")
    src, data = receiver.wait_uwbdata(timeout=wait_budget)

    if data != payload:
        raise RuntimeError(
            f"Payload mismatch on {receiver.port}: got {len(data)} bytes, "
            f"expected {len(payload)} (src={src})"
        )
    print(f"  OK: received {len(data)} bytes from src={src}")


def main() -> int:
    args = parse_args()
    if args.setup and args.skip_setup:
        print("Use either --setup or --skip-setup, not both.", file=sys.stderr)
        return 2

    extra_size = args.size
    if extra_size < 0 or extra_size > UWBDATA_MAX_PAYLOAD:
        print(f"--size must be 0..{UWBDATA_MAX_PAYLOAD}", file=sys.stderr)
        return 2

    cases: list[tuple[str, bytes]] = [("hello-uwbdata", make_payload("hello-uwbdata", 0))]
    if extra_size > 0:
        cases.append((f"blob-{extra_size}", make_payload(f"blob-{extra_size}", extra_size)))
    else:
        cases.append(("medium-payload", make_payload("medium", UWBDATA_INLINE_MAX + 8)))

    print("BU04 AT+UWBDATA link test")
    print(f"  anchor: {args.anchor}")
    print(f"  tag:    {args.tag}")
    print(f"  dest:   {args.dest}")

    try:
        with Bu04Device(args.anchor, timeout=args.at_timeout) as anchor, Bu04Device(
            args.tag, timeout=args.at_timeout
        ) as tag:
            anchor_ver = require_firmware(anchor, "Anchor")
            tag_ver = require_firmware(tag, "Tag")
            print(f"  anchor firmware: {anchor_ver}")
            print(f"  tag firmware:    {tag_ver}")

            if args.setup:
                setup_modules(
                    anchor,
                    tag,
                    anchor_id=args.anchor_id,
                    tag_id=args.tag_id,
                    channel=args.channel,
                    network=args.network,
                    force_setup=args.force_setup,
                    discover_time=args.discover_time,
                )
            elif args.skip_setup:
                validate_roles_configured(anchor, tag)
                print("Skipping setup; assuming UWB is already running (AT+UWBSTART).")
                start_uwb_ranging(anchor, tag)
            else:
                print(
                    "No --setup: validating roles and starting UWB if AT is still available.\n"
                    "  For a fresh flash, run with --setup once."
                )
                anchor_cfg = anchor.get_role_config()
                tag_cfg = tag.get_role_config()
                if anchor_cfg and tag_cfg:
                    print(
                        f"  anchor id={anchor_cfg.device_id} role={anchor_cfg.role} | "
                        f"tag id={tag_cfg.device_id} role={tag_cfg.role}"
                    )
                start_uwb_ranging(anchor, tag)

            time.sleep(2.0)
            anchor.drain(0.5)
            tag.drain(0.5)

            directions: list[tuple[str, Bu04Device, Bu04Device]] = []
            if args.direction in ("tag2anchor", "both"):
                directions.append(("tag->anchor", tag, anchor))
            if args.direction in ("anchor2tag", "both"):
                directions.append(("anchor->tag", anchor, tag))

            for case_name, payload in cases:
                print(f"\n=== Case: {case_name} ({len(payload)} bytes) ===")
                for label, sender, receiver in directions:
                    run_transfer(
                        sender,
                        receiver,
                        sender_label=label.split("->")[0],
                        receiver_label=label.split("->")[1],
                        dest=args.dest,
                        payload=payload,
                        timeout=args.timeout,
                    )

    except (RuntimeError, TimeoutError) as exc:
        print(f"\nFAIL: {exc}", file=sys.stderr)
        return 1

    print("\nAll UWBDATA transfers passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
