# TinyDraw

TinyDraw is a small finger-drawing app for the Waveshare ESP32-S3 Touch AMOLED
1.8-inch board. The display is 368×448 pixels. The interface borrows tldraw's
basic shape while staying practical for a tiny touchscreen and an ESP32.

This is an early hardware-independent build. The drawing engine, macOS test app,
and ESP-IDF/QEMU firmware path work. Physical display and touch drivers still
need a board.

## Current state

- Pressure-like variable-width ink with Perfect Freehand-style geometry
- 4×4 antialiasing into an RGB565 canvas
- Bounded dirty-tile rendering for long strokes and self-overlaps
- Pen, eraser, four colors, four sizes, New, and ten levels of Undo
- Dirty-tile Undo stored in PSRAM instead of full-canvas copies
- A tldraw-inspired one-row toolbar with color and size popups
- Shared C++20 drawing core for macOS and ESP32-S3
- Debug, release, ASan/UBSan, replay, QEMU, and graphics tests

The native app is useful now:

```sh
./scripts/bootstrap-macos   # once
./scripts/dev run
```

It opens near the physical size of the real 1.8-inch panel on a 14-inch 2021
MacBook Pro at default scaling. Resize it if you want a closer look. Draw with
the mouse, use `Cmd-Z` to undo, `C` for a new drawing, and `Esc` to quit.

## Development

```sh
./scripts/dev test          # native debug tests and end-to-end replays
./scripts/dev release       # optimized build and tests
./scripts/dev asan          # ASan and UBSan on SDL-free project code
./scripts/dev perf          # deterministic raster and memory-traffic report
./scripts/dev format-check
```

ESP-IDF v6.0.2 and QEMU stay isolated from the native toolchain:

```sh
./scripts/bootstrap-idf     # once
./scripts/esp32 build
./scripts/esp32 qemu        # headless firmware replay
./scripts/esp32 graphics    # visible virtual framebuffer; Ctrl-A, X to close
```

QEMU checks integration and an 8 MiB modeled PSRAM allocation. It does not tell
us how fast the physical board will draw.

## Hardware work ahead

Waveshare sells two revisions:

- V1: SH8601 display and FT3168 touch
- V2: CO5300 display and CST820 touch

The incoming board could be either one. We will identify it before choosing the
display and touch adapters, then measure PSRAM bandwidth, stroke-lift latency,
Undo latency, partial display writes, and touch-to-pixel feel on-device.

See [`PROJECT_STATE.md`](PROJECT_STATE.md) for the detailed engineering handoff
and [`DEVELOPING.md`](DEVELOPING.md) for setup and test notes.

## License

MIT
