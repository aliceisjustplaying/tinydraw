#!/usr/bin/env python3
"""Request an RP2350 TinyDraw framebuffer and save it as a PNG."""

from __future__ import annotations

import argparse
import binascii
import os
from pathlib import Path
import re
import select
import struct
import termios
import time
import zlib

HEADER = re.compile(rb"TINYDRAW_FRAME (\d+) (\d+) RGB565BE (\d+)\r?\n")
METRICS = re.compile(rb"TINYDRAW_PERF [^\r\n]+\r?\n")


def read_until(fd: int, pattern: re.Pattern[bytes], timeout: float) -> re.Match[bytes]:
    received = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], min(0.25, deadline - time.monotonic()))
        if not ready:
            continue
        chunk = os.read(fd, 4096)
        if chunk:
            received.extend(chunk)
            match = pattern.search(received)
            if match:
                trailing = received[match.end() :]
                read_until.trailing = bytes(trailing)
                return match
    raise TimeoutError("timed out waiting for framebuffer header")


read_until.trailing = b""


def read_exact(fd: int, initial: bytes, size: int, timeout: float) -> bytes:
    received = bytearray(initial[:size])
    deadline = time.monotonic() + timeout
    while len(received) < size and time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], min(0.25, deadline - time.monotonic()))
        if not ready:
            continue
        chunk = os.read(fd, min(16384, size - len(received)))
        if chunk:
            received.extend(chunk)
    if len(received) != size:
        raise TimeoutError(f"received {len(received)} of {size} framebuffer bytes")
    return bytes(received)


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", binascii.crc32(body))


def write_png(path: Path, width: int, height: int, rgb565: bytes) -> None:
    expected = width * height * 2
    if len(rgb565) != expected:
        raise ValueError(f"expected {expected} RGB565 bytes, got {len(rgb565)}")

    rows = bytearray()
    offset = 0
    for _ in range(height):
        rows.append(0)  # PNG filter: None
        for _ in range(width):
            value = (rgb565[offset] << 8) | rgb565[offset + 1]
            offset += 2
            red = ((value >> 11) & 0x1F) * 255 // 31
            green = ((value >> 5) & 0x3F) * 255 // 63
            blue = (value & 0x1F) * 255 // 31
            rows.extend((red, green, blue))

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(rows, level=6))
        + png_chunk(b"IEND", b"")
    )


def configure_serial(fd: int) -> None:
    attributes = termios.tcgetattr(fd)
    attributes[0] = 0
    attributes[1] = 0
    attributes[2] |= termios.CLOCAL | termios.CREAD | termios.CS8
    attributes[3] = 0
    attributes[4] = termios.B115200
    attributes[5] = termios.B115200
    attributes[6][termios.VMIN] = 0
    attributes[6][termios.VTIME] = 1
    termios.tcsetattr(fd, termios.TCSANOW, attributes)
    termios.tcflush(fd, termios.TCIOFLUSH)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--metrics", action="store_true", help="request timing metrics")
    parser.add_argument("port", help="RP2350 serial port, e.g. /dev/cu.usbmodem1101")
    parser.add_argument("output", nargs="?", type=Path, help="output PNG path")
    args = parser.parse_args()
    if not args.metrics and args.output is None:
        parser.error("output is required unless --metrics is used")

    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        configure_serial(fd)
        time.sleep(0.5)
        termios.tcflush(fd, termios.TCIFLUSH)
        if args.metrics:
            os.write(fd, b"P\n")
            match = read_until(fd, METRICS, timeout=5.0)
            print(match.group().decode("ascii").strip())
            return

        os.write(fd, b"S\n")
        match = read_until(fd, HEADER, timeout=5.0)
        width, height, byte_count = (int(value) for value in match.groups())
        expected = width * height * 2
        if byte_count != expected:
            raise ValueError(f"device announced {byte_count} bytes; expected {expected}")
        pixels = read_exact(fd, read_until.trailing, byte_count, timeout=30.0)
        write_png(args.output, width, height, pixels)
    finally:
        os.close(fd)

    print(f"Captured {width}x{height} framebuffer to {args.output}")


if __name__ == "__main__":
    main()
