#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pyserial==3.5"]
# ///
"""Record a canonical ink trace from the capture firmware.

Workflow (docs/INK_TRACE_HARNESS.md §1):
  1. Flash the capture firmware: ./scripts/esp32 vector-v2-ink-capture PORT
  2. Run this script with the canonical trace name.
  3. Draw the gesture on the device. Two seconds after the last lift, the
     firmware dumps the capture over serial; this script saves it as a
     canonical CSV with source=recorded and exits.

If the device reports a capture-ring overflow the capture is rejected and the
script keeps waiting so the gesture can be redrawn. Validate the saved file
with tools/ink-trace-check before committing.
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

import serial

MAGIC = "TINYDRAW_INKTRACE"
BEGIN = "TINYDRAW_INKTRACE_CAPTURE_BEGIN"
END = "TINYDRAW_INKTRACE_CAPTURE_END"
READY = "TINYDRAW_INKTRACE_CAPTURE_READY"
HEADER_COLUMNS = "magic,version,name,source,sample_rate_note"
EVENT_COLUMNS = "t_us,kind,x,y"
# Strict event shape: anything else between BEGIN/END (task-watchdog reports,
# stray log lines, corrupted output) is skipped as device noise, not counted.
EVENT_PATTERN = re.compile(r"^\d+,(Down|Move|Up),\d+,\d+$")
# A real gesture at the 1 kHz sampler cadence produces hundreds of events;
# anything tiny is a stray tap and must not overwrite a good trace.
MINIMUM_EVENTS = 100


def parse_marker_fields(line: str) -> dict[str, int]:
    fields = {}
    for token in line.split()[1:]:
        key, _, value = token.partition("=")
        if value.isdigit():
            fields[key] = int(value)
    return fields


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port")
    parser.add_argument("--name", required=True, help="canonical trace name, e.g. fast-curve-400")
    parser.add_argument("--out", default=None, help="output path (default testdata/ink-traces/<name>.csv)")
    parser.add_argument("--note", default="recorded owner finger input via 1kHz sampler stream")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout-s", type=int, default=600)
    parser.add_argument("--allow-short", action="store_true",
                        help=f"accept captures below {MINIMUM_EVENTS} events")
    arguments = parser.parse_args()

    if "," in arguments.name or "," in arguments.note:
        print("name and note must not contain commas (CSV header fields)", file=sys.stderr)
        return 2

    out_path = Path(arguments.out) if arguments.out else Path("testdata/ink-traces") / f"{arguments.name}.csv"
    out_path.parent.mkdir(parents=True, exist_ok=True)

    port = serial.Serial(arguments.port, arguments.baud, timeout=1)
    deadline = time.monotonic() + arguments.timeout_s
    print(f"Waiting for capture on {arguments.port}. Draw '{arguments.name}' on the device;")
    print("the dump starts two seconds after the final lift.")

    while time.monotonic() < deadline:
        raw = port.readline()
        if not raw:
            continue
        line = raw.decode("utf-8", errors="replace").strip()
        if READY in line:
            print(f"[device] {line}")
            continue
        if BEGIN not in line:
            continue

        marker = parse_marker_fields(line[line.index(BEGIN):])
        expected = marker.get("events", 0)
        overflow = marker.get("overflow", 0)
        strokes = marker.get("strokes", 0)
        print(f"[device] {line}")

        header_lines: list[str] = []
        events: list[str] = []
        noise = 0
        while True:
            event_raw = port.readline()
            if not event_raw:
                print("serial timeout inside capture dump", file=sys.stderr)
                return 1
            event_line = event_raw.decode("utf-8", errors="replace").strip()
            if END in event_line:
                break
            if EVENT_PATTERN.match(event_line):
                events.append(event_line)
            elif len(header_lines) < 3 and not events:
                header_lines.append(event_line)
            elif event_line:
                noise += 1
                print(f"[device-noise] {event_line}")

        if overflow:
            print(f"capture overflowed ({expected} events); redraw the gesture", file=sys.stderr)
            continue
        header_ok = (
            len(header_lines) == 3
            and header_lines[0] == HEADER_COLUMNS
            and header_lines[1].startswith(MAGIC)
            and header_lines[2] == EVENT_COLUMNS
        )
        if not header_ok:
            print(f"malformed capture header: {header_lines!r}; redraw", file=sys.stderr)
            continue
        if len(events) != expected:
            print(f"event count mismatch: expected {expected}, got {len(events)} valid "
                  f"({noise} noise lines); redraw", file=sys.stderr)
            continue
        if expected < MINIMUM_EVENTS and not arguments.allow_short:
            print(f"only {expected} events — looks like a stray tap, not a gesture; "
                  f"redraw (or pass --allow-short)", file=sys.stderr)
            continue

        content = [HEADER_COLUMNS,
                   f"{MAGIC},1,{arguments.name},recorded,{arguments.note}",
                   EVENT_COLUMNS, *events]
        out_path.write_text("\n".join(content) + "\n", encoding="utf-8")
        duration_s = int(events[-1].split(",", 1)[0]) / 1e6
        print(f"saved {out_path}: {len(events)} events, {strokes} strokes, {duration_s:.2f} s")
        validator = Path(__file__).with_name("ink-trace-check")
        validation = subprocess.run([validator, out_path], check=False)
        if validation.returncode != 0:
            print("saved capture failed canonical validation", file=sys.stderr)
            return 1
        return 0

    print("timed out waiting for a capture", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
