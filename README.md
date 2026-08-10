# TinyDraw

TinyDraw is a small finger-drawing app for the 368×448 Waveshare ESP32-S3 Touch
AMOLED 1.8-inch V2 board. It runs on the physical CO5300 display with CST820
touch. A macOS app and ESP-IDF/QEMU build exercise the same C++20 drawing core.

## Current state

- Variable-width, Perfect Freehand-style ink
- Smooth curve reconstruction for sparse touch input
- 4×4 antialiasing into a 16-bit-color canvas
- Bounded 32×32 updates for long strokes and self-overlaps
- Pen, eraser, four colors, four sizes, New, and ten levels of Undo
- One-row tldraw-inspired toolbar with color and size popups
- Dirty-tile Undo stored in the board's 8 MiB PSRAM
- Native replays, exact snapshots, ASan/UBSan, QEMU, and hardware telemetry

The hardware build is usable. Long curves average about 5.7 ms per update. The
CST820 supplies distinct coordinates at roughly 75–77 Hz; very fast XL diagonals
can still show some drawing lag.

Current product work includes larger toolbar tap targets, confirmation before
New, an edge-release stroke bug, and a possible Save feature.

## Run the macOS app

```sh
./scripts/bootstrap-macos   # once
./scripts/dev run
```

It opens near the panel's physical size on a 14-inch 2021 MacBook Pro at default
scaling. Draw with the mouse, use `Cmd-Z` to undo, `C` for New, and `Esc` to quit.

## Test and profile

```sh
./scripts/dev test          # native tests and end-to-end replays
./scripts/dev release       # optimized build and tests
./scripts/dev asan          # ASan and UBSan on the shared drawing core
./scripts/dev perf          # sustained XL work and memory-traffic report
./scripts/dev format-check
```

## Build firmware

ESP-IDF v6.0.2 and QEMU stay isolated from the native toolchain:

```sh
./scripts/bootstrap-idf       # once
./scripts/esp32 build         # physical ESP32-S3 firmware
./scripts/esp32 qemu          # headless firmware replay
./scripts/esp32 graphics-test # automated virtual display check
./scripts/esp32 graphics      # visible QEMU framebuffer
```

Flash a connected board with:

```sh
cd esp32
eim run "idf.py -B ../out/build/esp32 -p PORT flash"
```

QEMU verifies firmware integration and the 8 MiB memory model. Performance
numbers come from the physical board.

See [`FINDINGS.md`](FINDINGS.md) for measurements and demo notes,
[`DEVELOPING.md`](DEVELOPING.md) for the development loop, and
[`PROJECT_STATE.md`](PROJECT_STATE.md) for the detailed engineering handoff.

## License

MIT
