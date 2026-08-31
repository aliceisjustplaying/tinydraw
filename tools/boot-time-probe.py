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

usage: uv run --script tools/boot-time-probe.py PORT RUNS [MARKER]
"""

import sys
import time

import serial

port = sys.argv[1]
runs = int(sys.argv[2])
marker = (sys.argv[3] if len(sys.argv) > 3 else "CAL_RECORD").encode()

for run in range(runs):
    ser = serial.Serial(port, 115200, timeout=0.05)
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.reset_input_buffer()
    ser.rts = False
    start = time.monotonic()
    first_rom = None
    entry = None
    first_app = None
    buffer = b""
    while time.monotonic() - start < 8.0:
        chunk = ser.read(4096)
        now = time.monotonic() - start
        if not chunk:
            continue
        buffer += chunk
        while b"\n" in buffer:
            line, buffer = buffer.split(b"\n", 1)
            if first_rom is None and b"ESP-ROM" in line:
                first_rom = now
            if entry is None and line.startswith(b"entry 0x"):
                entry = now
            if first_app is None and marker in line:
                first_app = now
        if first_app is not None:
            break
    ser.close()
    print({"run": run, "firstRomLine": first_rom, "bootloaderEntry": entry,
           "firstAppOutput": first_app})
