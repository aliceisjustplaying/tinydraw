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
- ESP32: pan, ten Undos, 1104×1344 canvas, flash autosave, and battery status
- ESP32: touch record/replay for hands-free demos and battery-powered off/on
- RP2350: screen-sized ink, eraser, colors, sizes, and confirmed New
- Native replays, exact snapshots, ASan/UBSan, QEMU, and device telemetry

ESP32 long strokes average 2.5–3.4 ms per update. Autosave starts after 500 ms
idle; one 18-sector device write took 2.27 s in its background task. Autosave
restore, charging state, and PMU power-off/on are verified on the board.
The RP2350 averages 1.2–1.4 ms and is not persistent.

The RP2350 build has no pan or Undo, and its permanent circle-stamp raster looks
rougher than the ESP32 ribbon renderer. USB framebuffer captures can disagree
with its physical panel. [`PROJECT_STATE.md`](PROJECT_STATE.md) has the details.

## Run the macOS app
```sh
./scripts/bootstrap-macos   # once
./scripts/dev run           # use run-2x or run-3x for a larger demo window
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
./scripts/bootstrap-idf     # once; isolated ESP-IDF v6.0.2
./scripts/esp32 build
./scripts/esp32 graphics-test
```

Flash with `eim run "idf.py -B ../out/build/esp32 -p PORT flash monitor"` from
`esp32/`. Use an explicit port when both boards are connected. On the physical
board, short BOOT toggles demo recording and holding BOOT replays it. On battery,
hold the lower PMU button for four seconds to turn off; short-press it to start.
There is no sleep mode yet.

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
