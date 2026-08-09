#!/usr/bin/env python3
"""Boot the ESP32-S3 firmware and verify its deterministic replay marker."""

import argparse
import os
import re
import select
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BUILD = ROOT / "out/build/esp32"
EXPECTED_COUNTS = (7, 13, 14)
EXPECTED_BOUNDS = (27.83, 37.83, 341.44, 411.44)
BOUNDS_TOLERANCE = 0.05
MARKER = re.compile(
    rb"TINYDRAW_REPLAY_OK accepted=(\d+) primitives=(\d+) tiles=(\d+) "
    rb"bounds=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) checksum=([0-9a-fA-F]{8})"
)


def find_qemu() -> Path:
    tools = Path.home() / ".espressif/tools"
    matches = [
        path
        for path in tools.glob("**/qemu-system-xtensa")
        if not path.name.startswith("._") and os.access(path, os.X_OK)
    ]
    if not matches:
        raise RuntimeError(
            "qemu-system-xtensa is missing; run ./scripts/bootstrap-idf to install it"
        )
    return matches[0]


def stop(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    os.killpg(process.pid, signal.SIGTERM)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, default=DEFAULT_BUILD)
    parser.add_argument("--graphics", action="store_true")
    arguments = parser.parse_args()
    build = arguments.build.resolve()

    qemu = find_qemu()
    environment = os.environ.copy()
    environment["PATH"] = f"{qemu.parent}:{environment['PATH']}"
    qemu_options = "--graphics" if arguments.graphics else "--qemu-extra-args '-nographic'"
    command = f"idf.py -B '{build}' qemu {qemu_options}"
    process = subprocess.Popen(
        ["eim", "run", command],
        cwd=ROOT / "esp32",
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    assert process.stdout is not None
    output = bytearray()
    deadline = time.monotonic() + 30.0

    try:
        while b"TINYDRAW_QEMU_DONE" not in output:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError("QEMU replay timed out")
            readable, _, _ = select.select([process.stdout], [], [], remaining)
            if not readable:
                raise RuntimeError("QEMU replay timed out")
            chunk = os.read(process.stdout.fileno(), 4096)
            if not chunk:
                raise RuntimeError(f"QEMU exited before completion (status {process.wait()})")
            output.extend(chunk)
            if b"TINYDRAW_REPLAY_FAIL" in output or b"stack overflow" in output:
                raise RuntimeError("firmware reported a replay or stack failure")
    except Exception as error:
        stop(process)
        sys.stderr.buffer.write(output)
        print(f"\n{error}", file=sys.stderr)
        return 1

    stop(process)
    if arguments.graphics and b"TINYDRAW_UI_OK controls=6" not in output:
        sys.stderr.buffer.write(output)
        print("\nQEMU graphics run lacked toolbar marker", file=sys.stderr)
        return 1

    match = MARKER.search(output)
    if match is None:
        sys.stderr.buffer.write(output)
        print("\nQEMU completion marker lacked replay results", file=sys.stderr)
        return 1

    counts = tuple(int(value) for value in match.groups()[:3])
    bounds = tuple(float(value) for value in match.groups()[3:7])
    if counts != EXPECTED_COUNTS:
        print(f"unexpected structural counts: {counts} != {EXPECTED_COUNTS}", file=sys.stderr)
        return 1
    if any(abs(actual - expected) > BOUNDS_TOLERANCE for actual, expected in zip(bounds, EXPECTED_BOUNDS)):
        print(f"bounds outside tolerance: {bounds} != {EXPECTED_BOUNDS}", file=sys.stderr)
        return 1

    checksum = match.group(8).decode("ascii")
    print(
        "QEMU replay passed: "
        f"accepted={counts[0]} primitives={counts[1]} tiles={counts[2]} "
        f"bounds={bounds} checksum={checksum} (informational)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
