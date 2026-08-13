#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pyserial==3.5"]
# ///
"""Timestamped ESP32 serial capture: resets the board, logs until an end marker."""

import argparse
import datetime
import time

import serial

DEFAULT_END_MARKERS = [
    b"TINYDRAW_INTERACTIVE_PAN_DONE",
    b"TINYDRAW_PRODUCTION_WALK_DONE",
    b"TINYDRAW_TILE_CENSUS_APP_DONE",
    b"TINYDRAW_BENCH_ALLOC_FAIL",
    b"TINYDRAW_BENCH_SETUP_FAIL",
]

parser = argparse.ArgumentParser()
parser.add_argument("port")
parser.add_argument("output")
parser.add_argument("timeout_s", type=float)
parser.add_argument("--no-reset", action="store_true")
parser.add_argument(
    "--end-marker",
    action="append",
    default=[],
    help="additional UTF-8 line marker that ends capture",
)
args = parser.parse_args()
end_markers = DEFAULT_END_MARKERS + [marker.encode() for marker in args.end_marker]

ser = serial.Serial(args.port, 115200, timeout=0.25)
if not args.no_reset:
    ser.dtr = False
    ser.rts = True
    time.sleep(0.2)
    ser.rts = False

deadline = time.time() + args.timeout_s
buf = b""
done = False
with open(args.output, "wb") as f:
    while time.time() < deadline and not done:
        data = ser.read(4096)
        if not data:
            continue
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.rstrip(b"\r")
            stamp = datetime.datetime.now().strftime("[%H:%M:%S] ").encode()
            f.write(stamp + line + b"\n")
            f.flush()
            if any(marker in line for marker in end_markers):
                done = True
    # Drain a short tail so records following the end marker are retained.
    tail_deadline = time.time() + 2.0
    while time.time() < tail_deadline:
        data = ser.read(4096)
        if not data:
            continue
        buf += data
    for line in buf.split(b"\n"):
        if line.strip():
            stamp = datetime.datetime.now().strftime("[%H:%M:%S] ").encode()
            f.write(stamp + line.rstrip(b"\r") + b"\n")
ser.close()
print("capture complete:", args.output, "end_marker=" + str(done))
