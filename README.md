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

The runnable firmware remains the raster-authoritative 3×3 product described below. The active
production branch is migrating to a vector-authoritative 4×4 canvas through an isolated,
host-tested production island; it is not ready to ship. See [`PROJECT_STATE.md`](PROJECT_STATE.md)
for current direction and [`vector_v2/README.md`](vector_v2/README.md) for migration guardrails.

- Variable-width, Perfect Freehand-style ink with 4×4 edge smoothing
- Solid self-overlaps, rounded sharp turns, twelve colors, and four sizes
- ESP32: pan, ten Undos, 3×3 canvas, autosave, battery, and USB PNG export
- ESP32: onboard clock, one-shot NTP correction, demos, and battery-powered off/on
- RP2350: screen-sized ink, eraser, colors, sizes, and confirmed New
- Native replays, exact snapshots, ASan/UBSan, QEMU, and device telemetry

ESP32 long strokes average 2.5–3.4 ms per update. Autosave, charging, and PMU
power-off/on are verified. Export mounts the full 1104×1344 drawing as
`TINYDRAW/DRAWING.PNG` with the RTC-backed local time; macOS Finder is verified.
Wi-Fi shuts down after correcting the clock. The RP2350 averages 1.2–1.4 ms
but has no pan, Undo, or persistence. [`PROJECT_STATE.md`](PROJECT_STATE.md) has details.

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
`esp32/`; use an explicit port. Export replaces USB serial with a read-only drive.
To flash again on battery, power off, hold BOOT, and short-press power for a cold
boot. Short BOOT records/replays demos. Hold the lower button four seconds to
power off; short-press it to start. There is no sleep mode yet.

## Build RP2350 firmware
```sh
./scripts/rp2350 bootstrap
./scripts/rp2350 build
./scripts/rp2350 metrics PORT
./scripts/rp2350 trace PORT
```

The RP2350 uses one SRAM framebuffer. Repeated arbitrary SH8601 rectangles were
unreliable; full-width bands stay fast and physically stable. Historical
measurements are archived in
[`FINDINGS.md`](docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md).

## License

MIT
