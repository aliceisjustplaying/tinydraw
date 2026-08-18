#!/usr/bin/env python3
"""Capture TinyDraw serial output with timestamps until AUTO_ZOOM_DONE or timeout."""

import sys
import time

import serial

port, seconds, out_path = sys.argv[1], float(sys.argv[2]), sys.argv[3]
deadline = time.time() + seconds
connection = None
with open(out_path, "w") as out:
    while time.time() < deadline:
        if connection is None:
            try:
                connection = serial.Serial(port, 115200, timeout=1)
                connection.dtr = False
                connection.rts = False
            except Exception:
                time.sleep(0.5)
                continue
        try:
            line = connection.readline()
        except Exception:
            try:
                connection.close()
            except Exception:
                pass
            connection = None
            time.sleep(0.5)
            continue
        if not line:
            continue
        text = line.decode("utf-8", "replace").rstrip()
        stamp = time.strftime("%H:%M:%S")
        out.write(f"[{stamp}] {text}\n")
        out.flush()
        if "TINYDRAW_AUTO_ZOOM_DONE" in text:
            break
print(f"capture complete: {out_path}")
