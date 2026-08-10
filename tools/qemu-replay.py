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
DEFAULT_BUILD = ROOT / "out/build/esp32-qemu"
EXPECTED_COUNTS = (7, 13, 14)
EXPECTED_BOUNDS = (27.83, 37.83, 341.44, 411.44)
BOUNDS_TOLERANCE = 0.05
MARKER = re.compile(
    rb"TINYDRAW_REPLAY_OK accepted=(\d+) primitives=(\d+) tiles=(\d+) "
    rb"bounds=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+) checksum=([0-9a-fA-F]{8}) "
    rb"frames=(\d+) dirty=(\d+) max_tiles=(\d+) visits=(\d+) "
    rb"max_visits=(\d+) finish_tiles=(\d+) finish_visits=(\d+) "
    rb"display=(\d+) psram_read=(\d+) psram_write=(\d+) "
    rb"max_psram_read=(\d+) max_psram_write=(\d+)"
)
MEMORY_MARKER = (
    b"TINYDRAW_MEMORY_OK committed=329728 coverage=164864 "
    b"history=3440640 world=1318912 scratch=dma_internal"
)
PSRAM_MARKER = b"esp_psram: Found 8MB PSRAM device"


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
    graphics_option = "--graphics" if arguments.graphics else ""
    qemu_options = (
        "--qemu-extra-args '-m 8M'"
        if arguments.graphics
        else "--qemu-extra-args '-nographic -m 8M'"
    )
    command = f"idf.py -B '{build}' qemu {graphics_option} {qemu_options}"
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
            failure = re.search(rb"TINYDRAW_REPLAY_FAIL[^\r\n]*(?:\r?\n)", output)
            if failure is not None or b"stack overflow" in output:
                detail = failure.group(0).decode("ascii", errors="replace").strip() if failure else "stack overflow"
                raise RuntimeError(f"firmware reported failure: {detail}")
    except Exception as error:
        stop(process)
        sys.stderr.buffer.write(output)
        print(f"\n{error}", file=sys.stderr)
        return 1

    stop(process)
    if arguments.graphics and b"TINYDRAW_UI_OK canvas=1 controls=6" not in output:
        sys.stderr.buffer.write(output)
        print("\nQEMU graphics run lacked toolbar marker", file=sys.stderr)
        return 1

    if PSRAM_MARKER not in output or MEMORY_MARKER not in output:
        sys.stderr.buffer.write(output)
        print("\nQEMU completion lacked 8 MB PSRAM/capability marker", file=sys.stderr)
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
    (
        frames,
        dirty,
        maximum_tiles,
        visits,
        maximum_visits,
        finish_tiles,
        finish_visits,
        display,
        psram_read,
        psram_write,
        maximum_psram_read,
        maximum_psram_write,
    ) = (int(value) for value in match.groups()[8:20])
    if (
        frames != EXPECTED_COUNTS[0]
        or maximum_tiles > 20
        or maximum_visits > 80
        or finish_tiles > 48
        or finish_visits > 96
        or dirty == 0
        or visits == 0
        or display == 0
        or psram_read < display
        or psram_write == 0
        or maximum_psram_read > 512 * 1024
        or maximum_psram_write > 512 * 1024
    ):
        print(
            f"unexpected incremental work: frames={frames} dirty={dirty} "
            f"max_tiles={maximum_tiles} visits={visits} max_visits={maximum_visits} "
            f"finish_tiles={finish_tiles} finish_visits={finish_visits} display={display} "
            f"psram_read={psram_read} psram_write={psram_write} "
            f"max_psram_read={maximum_psram_read} max_psram_write={maximum_psram_write}",
            file=sys.stderr,
        )
        return 1
    print(
        "QEMU replay passed: "
        f"accepted={counts[0]} primitives={counts[1]} tiles={counts[2]} "
        f"bounds={bounds} frames={frames} dirty={dirty} max_tiles={maximum_tiles} "
        f"visits={visits} max_visits={maximum_visits} finish_tiles={finish_tiles} "
        f"finish_visits={finish_visits} display={display} psram_read={psram_read} "
        f"psram_write={psram_write} max_psram_read={maximum_psram_read} "
        f"max_psram_write={maximum_psram_write} checksum={checksum} (informational)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
