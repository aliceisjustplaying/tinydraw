#!/usr/bin/env python3
"""Timestamped ESP32 serial capture: resets the board, logs until an end marker."""

import datetime
import sys
import time

import serial

PORT = sys.argv[1]
OUT = sys.argv[2]
TIMEOUT_S = float(sys.argv[3])
END_MARKERS = [
    b"TINYDRAW_INTERACTIVE_PAN_DONE",
    b"TINYDRAW_PRODUCTION_WALK_DONE",
    b"TINYDRAW_TILE_CENSUS_APP_DONE",
    b"TINYDRAW_BENCH_ALLOC_FAIL",
    b"TINYDRAW_BENCH_SETUP_FAIL",
]
RESET = len(sys.argv) < 5 or sys.argv[4] != "--no-reset"

ser = serial.Serial(PORT, 115200, timeout=0.25)
if RESET:
    ser.dtr = False
    ser.rts = True
    time.sleep(0.2)
    ser.rts = False

deadline = time.time() + TIMEOUT_S
buf = b""
done = False
with open(OUT, "wb") as f:
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
            if any(marker in line for marker in END_MARKERS):
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
print("capture complete:", OUT, "end_marker=" + str(done))
