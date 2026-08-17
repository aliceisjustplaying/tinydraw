# TinyDraw

<p align="center">
  <a href="assets/readme/tinydraw-drawing.jpg"><img src="assets/readme/tinydraw-drawing.jpg" alt="TinyDraw running on the AMOLED board" width="32%"></a>
  <a href="assets/readme/tinydraw-colors.jpg"><img src="assets/readme/tinydraw-colors.jpg" alt="TinyDraw color palette" width="32%"></a>
  <a href="assets/readme/tinydraw-sizes.jpg"><img src="assets/readme/tinydraw-sizes.jpg" alt="TinyDraw brush sizes" width="32%"></a>
</p>

TinyDraw is a small finger-drawing app for Waveshare's 368×448 ESP32-S3 and
RP2350 Touch AMOLED 1.8-inch boards. The supported macOS frontend and QEMU
target run Raster V1; ESP32 firmware supports both Raster V1 and Vector V2.
All targets share the C++20 drawing and UI core where their models overlap.

## Current state

The ESP32 has two supported product generations: raster-authoritative TinyDraw
V1 and vector-authoritative TinyDraw V2. V2 is feature complete; cleanup, known
bug fixes, and its final performance campaign remain. Current status lives in
[`PROJECT_STATE.md`](PROJECT_STATE.md), and [`V2_ROADMAP.md`](V2_ROADMAP.md)
owns the remaining V2 work.

### TinyDraw V1 — Raster

- Variable-width, Perfect Freehand-style ink with 4×4 edge smoothing
- Solid self-overlaps, rounded sharp turns, twelve colors, and four sizes
- Pan, ten Undos, a 3×3 canvas, autosave, battery status, and USB PNG export
- Onboard clock, one-shot NTP correction, demos, and battery-powered off/on

### TinyDraw V2 — Vector

- Vector operations are authoritative for pen and eraser strokes.
- The bounded 1472×1792 world supports 25%, 50%, 100%, 200%, and 400% zoom.
- A complete overview provides fallback while a 448-slot world-aligned tile
  cache refines visible detail.
- Touch sampling remains independent of cooperative rendering work.
- The refined toolbar uses the V1 tool icons, reflects the active tool, and
  opens compact tool, size, document, and two-page round-swatch color popups.
  Span-rasterized swatches and frame reuse open the full color dialog in about
  27.6 ms on device (4.81× faster than its captured baseline).
- New has a confirmation dialog. Whole-Stroke Undo/Redo keeps internal chunks
  grouped, preserves at least ten levels, and replays bounded pen/eraser damage.
  Host, sanitizer, firmware, and owner glass checks are green; high-zoom cold
  rebuilding remains in the final optimization round.
- Autosave appends whole-Stroke, history, New, navigation, and tool-state
  Journal commits to the 3 MiB drawing partition. A low-priority flash worker
  publishes a CRC-checked final marker last; startup restores vector authority,
  Redo, zoom-return positions, and selections, then rebuilds derived pixels.
  Full-journal compaction is deliberately deferred.
- Export streams the vector authority to flash with visible progress, then
  opens an explicit read-only USB mode with an on-screen **Return to Drawing**
  action. Host eject keeps the medium absent
  instead of allowing later probes to re-expose it.
- The document popup's clock action performs an on-demand asynchronous
  Wi-Fi/NTP correction, writes the onboard RTC, and shows connecting, syncing,
  success, or error feedback. Terminal feedback appears only after Wi-Fi is
  stopped and deinitialized.
- Battery status, a five-level zoom rail, and a live interactive minimap with
  tap-to-jump and zoom-scaled drag intent are visible over the canvas.
- Export produces an editable `DRAWING.SVG` and a settled anti-aliased
  `DRAWING.PNG` from one vector-authority snapshot. SVG retains one
  painter-ordered filled path per physical finger-down/up Stroke and no
  synthetic background rectangle. PNG rows stream through bounded settled
  render and encoder workspaces. Both files are exposed read-only in one
  synthesized FAT16 volume after a shared metadata-last commit.

Immediate ink is refined to the accepted settled anti-aliased output after
lift. Dated receipts preserve historical measurements; the scorecard in
[`PROJECT_STATE.md`](PROJECT_STATE.md) is the current acceptance record.

The RP2350 build currently provides screen-sized ink, erasing, colors, sizes,
and New. It has no pan, Undo, or persistence. Native replays, exact snapshots,
ASan/UBSan, QEMU, and device telemetry cover the shared core.

## Run the Raster V1 macOS app
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
./scripts/esp32 build             # build TinyDraw V2
./scripts/esp32 vector-v2 PORT    # build + flash TinyDraw V2
./scripts/esp32 raster-v1         # build TinyDraw V1
./scripts/esp32 raster-v1 PORT    # build + flash TinyDraw V1
./scripts/esp32 graphics-test
```

V1 and V2 use separate build directories. Use an explicit serial port when flashing.
Export temporarily replaces USB serial with a read-only drive;
**Return to Drawing** stops the USB device stack without resetting the board.
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
