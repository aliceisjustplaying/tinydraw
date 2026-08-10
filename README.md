# TinyDraw

<p align="center">
  <a href="assets/readme/tinydraw-drawing.jpg"><img src="assets/readme/tinydraw-drawing.jpg" alt="TinyDraw running on the AMOLED board" width="32%"></a>
  <a href="assets/readme/tinydraw-colors.jpg"><img src="assets/readme/tinydraw-colors.jpg" alt="TinyDraw color palette" width="32%"></a>
  <a href="assets/readme/tinydraw-sizes.jpg"><img src="assets/readme/tinydraw-sizes.jpg" alt="TinyDraw brush sizes" width="32%"></a>
</p>

TinyDraw is a small finger-drawing app for Waveshare's 368×448 ESP32-S3 and
RP2350 Touch AMOLED 1.8-inch boards. A macOS app and ESP-IDF/QEMU targets
exercise the same C++20 drawing and UI core.

## Current state

- Variable-width, Perfect Freehand-style ink with smooth sparse-input curves
- Solid self-overlaps, rounded sharp turns, and 4×4 edge smoothing
- ESP32: pan, ten Undos, and a fixed 736×896 canvas in 8 MiB PSRAM
- RP2350: screen-sized ink, eraser, colors, sizes, and confirmed New
- Native replays, exact snapshots, ASan/UBSan, QEMU, and device telemetry

ESP32 long strokes average about 2.5–3.4 ms per update. RP2350 strokes average
about 1.2–1.4 ms using reliable full-width display bands. Drawings are not yet
persistent.

## Run the macOS app

```sh
./scripts/bootstrap-macos   # once
./scripts/dev run            # use run-2x or run-3x for a larger demo window
```

The window opens near the panel's physical size on a 14-inch 2021 MacBook Pro
at default scaling. Draw with the mouse, use `Cmd-Z` to undo, `C` for New, and
`Esc` to quit.

## Test and profile

```sh
./scripts/dev test          # native tests and end-to-end replays
./scripts/dev release       # optimized build and tests
./scripts/dev asan          # ASan and UBSan on the shared core
./scripts/dev perf          # sustained XL work and memory traffic
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
eim run "idf.py -B ../out/build/esp32 -p PORT flash monitor"
```

QEMU verifies firmware integration and the 8 MiB memory model. Performance
measurements in [`FINDINGS.md`](FINDINGS.md) come from the host and physical
board. See [`DEVELOPING.md`](DEVELOPING.md) for the development loop.

## Build the RP2350 app
```sh
./scripts/rp2350 bootstrap
./scripts/rp2350 build
./scripts/rp2350 metrics PORT
./scripts/rp2350 trace PORT
```

The RP2350 uses one SRAM framebuffer. Full-width SH8601 bands keep drawing fast;
arbitrary display rectangles proved unreliable. Its smaller memory budget
currently excludes the ESP32's pan and Undo features.
## License
MIT
