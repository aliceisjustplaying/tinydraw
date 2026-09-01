#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pyserial==3.5"]
# ///
"""Capture and validate one TinyDraw Tier-B timing boot."""

from __future__ import annotations

import argparse
import datetime
import json
import time

import serial

from tier_b_ndjson import CaptureValidator, ValidationError


FAIL_MARKERS = (
    b"Guru Meditation Error",
    b"Stack canary watchpoint triggered",
    b"assert failed:",
    b"abort() was called",
    b"TINYDRAW_TIER_B_FAILED",
)


def stamp(line: bytes) -> bytes:
    now = datetime.datetime.now().strftime("[%H:%M:%S] ").encode()
    return now + line.rstrip(b"\r") + b"\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("output")
    parser.add_argument("timeout_s", type=float)
    parser.add_argument(
        "--cells",
        default="all",
        help="comma-separated cohort cell IDs, or all",
    )
    parser.add_argument("--no-reset", action="store_true")
    args = parser.parse_args()

    validator = CaptureValidator()
    validation_error: str | None = None
    hardware_failure = False
    done = False
    line_number = 0
    buffer = b""
    device = serial.Serial(args.port, 115200, timeout=0.25)
    try:
        if not args.no_reset:
            device.dtr = False
            device.rts = True
            time.sleep(0.2)
            device.rts = False
        time.sleep(0.5)
        device.write(f"TIER_B_SELECT {args.cells}\n".encode())
        device.flush()

        deadline = time.time() + args.timeout_s
        with open(args.output, "wb") as output:
            while time.time() < deadline and not done and validation_error is None:
                data = device.read(4096)
                if not data:
                    continue
                buffer += data
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    line_number += 1
                    output.write(stamp(line))
                    output.flush()
                    if any(marker in line for marker in FAIL_MARKERS):
                        hardware_failure = True
                    try:
                        done = validator.feed_line(line.decode(errors="replace"), line_number)
                    except ValidationError as error:
                        validation_error = str(error)
                        break

            tail_deadline = time.time() + 0.25
            while time.time() < tail_deadline:
                data = device.read(4096)
                if data:
                    buffer += data
            for line in buffer.split(b"\n"):
                if line.strip():
                    output.write(stamp(line))
    finally:
        device.close()

    if validation_error is None:
        try:
            tally = validator.finalize()
        except ValidationError as error:
            validation_error = str(error)

    if validation_error is not None or hardware_failure:
        print(
            json.dumps(
                {
                    "ok": False,
                    "output": args.output,
                    "error": validation_error,
                    "hardwareFailure": hardware_failure,
                },
                sort_keys=True,
            )
        )
        return 2
    print(json.dumps({"ok": True, "output": args.output, **tally.as_dict()}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
