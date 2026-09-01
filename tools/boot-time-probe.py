# /// script
# requires-python = ">=3.11"
# dependencies = ["pyserial==3.5"]
# ///
"""Reset-to-first-app-output wall time over USB Serial/JTAG.

Pulses RTS the way esp32-capture.py does, then clocks the delay from RTS
release to the first line containing the marker (default CAL_RECORD).
The result includes USB re-enumeration time, so it is an upper bound on
boot-to-app time. Used 2026-08-31 to size a full ESP-IDF boot for the
puck-cycle-accurate emulator work: 0.577 to 0.595 s across three runs.

usage: uv run --script tools/boot-time-probe.py PORT RUNS [MARKER] [--output PATH]
"""

import argparse
import json
import re
import sys
import time
from pathlib import Path

import serial

FAIL_MARKERS = (
    b"Guru Meditation Error",
    b"Stack canary watchpoint triggered",
    b"assert failed:",
    b"abort() was called",
    b"task_wdt",
)

parser = argparse.ArgumentParser()
parser.add_argument("port")
parser.add_argument("runs", type=int)
parser.add_argument("marker", nargs="?", default="CAL_RECORD")
parser.add_argument(
    "--output",
    type=Path,
    help="write one JSON cohort after every run succeeds",
)
parser.add_argument(
    "--line-regex",
    help="require the marker line to match this regular expression",
)
args = parser.parse_args()

port = args.port
runs = args.runs
marker = args.marker.encode()
if runs <= 0:
    raise SystemExit("RUNS must be a positive integer")
if not marker:
    raise SystemExit("MARKER must not be empty")
if args.output is not None and args.output.exists():
    raise SystemExit(f"refusing to overwrite result: {args.output}")
line_pattern = re.compile(args.line_regex) if args.line_regex is not None else None

records = []
for run in range(runs):
    ser = serial.Serial(port, 115200, timeout=0.25)
    ser.dtr = False
    ser.rts = True
    time.sleep(0.2)
    ser.reset_input_buffer()
    ser.rts = False
    start = time.monotonic()
    first_rom = None
    entry = None
    first_app = None
    ready_line = None
    failure_line = None
    buffer = b""
    while time.monotonic() - start < 8.0:
        chunk = ser.read(4096)
        if not chunk:
            continue
        arrived = time.monotonic() - start
        buffer += chunk
        while b"\n" in buffer:
            line, buffer = buffer.split(b"\n", 1)
            if first_rom is None and b"ESP-ROM" in line:
                first_rom = arrived
            if entry is None and line.startswith(b"entry 0x"):
                entry = arrived
            if any(fail_marker in line for fail_marker in FAIL_MARKERS):
                failure_line = line.rstrip(b"\r").decode("utf-8")
                break
            if first_app is None and marker in line:
                first_app = arrived
                ready_line = line.rstrip(b"\r").decode("utf-8")
                if line_pattern is not None and line_pattern.search(ready_line) is None:
                    failure_line = "marker line did not match --line-regex"
        if first_app is not None or failure_line is not None:
            break
    ser.close()
    if first_app is None or failure_line is not None:
        reason = failure_line or "marker timeout"
        raise SystemExit(f"run {run} failed before {marker!r}: {reason}")
    records.append(
        {
            "run": run,
            "firstRomLineSeconds": first_rom,
            "bootloaderEntrySeconds": entry,
            "firstAppOutputSeconds": first_app,
            "readyLine": ready_line,
        }
    )
result = {
    "schemaVersion": 1,
    "port": port,
    "marker": args.marker,
    "runs": runs,
    "samples": records,
}
rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
if args.output is None:
    sys.stdout.write(rendered)
else:
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered)
    print(f"capture complete: {args.output} runs={runs}")
