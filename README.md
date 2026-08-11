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

- Variable-width, Perfect Freehand-style ink with 4×4 edge smoothing
- Solid self-overlaps, rounded sharp turns, twelve colors, and four sizes
- ESP32: pan, ten Undos, a 736×896 canvas, and tile-granular flash autosave
- RP2350: screen-sized ink, eraser, colors, sizes, and confirmed New
- Native replays, exact snapshots, ASan/UBSan, QEMU, and device telemetry

ESP32 long strokes average 2.5–3.4 ms per update and save after 500 ms idle;
physical save timing awaits reconnect. RP2350 averages 1.2–1.4 ms and is not persistent.

The RP2350 build is intentionally smaller. It has no pan or Undo, its permanent
circle-stamp raster looks rougher than the ESP32 ribbon renderer, and the newest
half-segment appears on lift. USB framebuffer captures can disagree with the
physical panel. [`PROJECT_STATE.md`](PROJECT_STATE.md) records these limits.

## Run the macOS app

```sh
./scripts/bootstrap-macos   # once
./scripts/dev run            # use run-2x or run-3x for a larger demo window
```

The window opens near the panel's physical size on a 14-inch 2021 MacBook Pro.
Draw with the mouse, use `Cmd-Z` to undo, `C` for New, and `Esc` to quit.

## Test and profile

```sh
./scripts/dev test          # native tests and end-to-end replays
./scripts/dev release       # optimized build and tests
./scripts/dev asan          # ASan and UBSan on the shared core
./scripts/dev perf          # sustained XL work and memory traffic
./scripts/dev format-check
```

## Build ESP32 firmware

```sh
./scripts/bootstrap-idf       # once; installs isolated ESP-IDF v6.0.2
./scripts/esp32 build
./scripts/esp32 qemu
./scripts/esp32 graphics-test
./scripts/esp32 graphics
```

Flash with `eim run "idf.py -B ../out/build/esp32 -p PORT flash monitor"` from
`esp32/`. QEMU checks firmware integration and the 8 MiB memory model.

## Build RP2350 firmware

```sh
./scripts/rp2350 bootstrap
./scripts/rp2350 build
./scripts/rp2350 metrics PORT
./scripts/rp2350 trace PORT
```

The RP2350 uses one SRAM framebuffer. Repeated arbitrary SH8601 rectangles were
unreliable; full-width bands stay fast and physically stable. See
[`FINDINGS.md`](FINDINGS.md) for measurements and development history.
## License
MIT
