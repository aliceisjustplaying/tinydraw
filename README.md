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

`main` contains two independent ESP32 applications. **Raster V1** remains the
default firmware while **Vector V2** moves toward feature parity. V2 is the
accepted architecture, but it is not yet feature complete or ready to replace
V1. Current status lives in [`PROJECT_STATE.md`](PROJECT_STATE.md); the remaining
work is tracked in [`V2_ROADMAP.md`](V2_ROADMAP.md), and
[`vector_v2/README.md`](vector_v2/README.md) defines the module boundaries.

### Raster V1

- Variable-width, Perfect Freehand-style ink with 4×4 edge smoothing
- Solid self-overlaps, rounded sharp turns, twelve colors, and four sizes
- Pan, ten Undos, a 3×3 canvas, autosave, battery status, and USB PNG export
- Onboard clock, one-shot NTP correction, demos, and battery-powered off/on

Raster V1 long strokes average 2.5–3.4 ms per update. Export mounts the full
1104×1344 drawing as `TINYDRAW/DRAWING.PNG` with the RTC-backed local time.
Autosave, charging, power-off/on, and macOS Finder mounting are verified.

### Vector V2

- Vector operations are authoritative for pen and eraser strokes.
- The bounded 1472×1792 world supports 25%, 50%, 100%, 200%, and 400% zoom.
- A complete overview provides fallback while a 384-slot world-aligned tile
  cache refines visible detail.
- Touch sampling remains independent of cooperative rendering work.
- The first-pass toolbar includes tools, two 16-color PICO-8 palette pages,
  four sizes, New, and Export.
- Full-world 1472×1792 PNG export has been decoded on the host and opened from
  the physical read-only USB drive.

V2's immediate renderer is intentionally hard-edged until the settled
anti-aliasing gate lands. The latest performance slice cut adversarial 400% cold
refinement p95 from 1.45 seconds to 646 ms and made a 3,751-sample 400% stroke
flow smoothly. It also exposed deferred debt: lower-zoom drawing against a warm
multi-zoom cache reached 120–132 ms per commit chunk, real pan remains around
20 FPS, and PNG encoding triggers the five-second task watchdog before the USB
drive appears. These findings and raw evidence are recorded in
[`PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md`](vector_v2/hardware-receipts/PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md).

The next milestone is a bounded UI refinement round: popup behavior, color
picker changes, visible zoom controls, battery status, export progress, and a
minimap skeleton. Performance work resumes afterward with drawing latency as
the first priority and an 800 ms cold-render p95 ceiling.

The RP2350 build currently provides screen-sized ink, erasing, colors, sizes,
and New. It has no pan, Undo, or persistence. Native replays, exact snapshots,
ASan/UBSan, QEMU, and device telemetry cover the shared core.

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
./scripts/esp32 raster-v1
./scripts/esp32 vector-v2 PORT
./scripts/esp32 graphics-test
```

`./scripts/esp32 build` remains an alias for `raster-v1`. Both named commands build in separate
directories; `vector-v2 PORT` also flashes the V2 app. Use an explicit serial port. Export replaces USB serial with a read-only drive.
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
