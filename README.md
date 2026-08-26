# TinyDraw

<p align="center">
  <a href="assets/readme/tinydraw-drawing.jpg"><img src="assets/readme/tinydraw-drawing.jpg" alt="TinyDraw running on the AMOLED board" width="32%"></a>
  <a href="assets/readme/tinydraw-colors.jpg"><img src="assets/readme/tinydraw-colors.jpg" alt="TinyDraw color palette" width="32%"></a>
  <a href="assets/readme/tinydraw-sizes.jpg"><img src="assets/readme/tinydraw-sizes.jpg" alt="TinyDraw brush sizes" width="32%"></a>
</p>

TinyDraw is a finger-drawing app for Waveshare's 368×448 ESP32-S3 and RP2350
Touch AMOLED 1.8-inch boards. Raster V1 and Vector V2 are both supported ESP32
products. The macOS frontend and QEMU use Raster V1; shared C++20 modules are
tested natively.

Vector V2 is released. Current acceptance and measurements are
in [`PROJECT_STATE.md`](PROJECT_STATE.md), the product contract is
[`SHIP_CONTRACT.md`](SHIP_CONTRACT.md), and later work is in
[`docs/POST_RELEASE.md`](docs/POST_RELEASE.md). The full history of successful
and rejected performance experiments is preserved in
[`docs/PERFORMANCE_CHRONICLE.md`](docs/PERFORMANCE_CHRONICLE.md) and dated
receipts.

## Raster V1

- Variable-width Perfect Freehand-style ink with 4×4 edge smoothing.
- Twelve colors, four sizes, eraser, pan, ten Undos, New, and a 3×3 canvas.
- Raster autosave, battery status, onboard clock, one-shot NTP, demos, hardware
  power off/on, and USB PNG export.
- Supported on ESP32. The native macOS app and QEMU also exercise this core.

## Vector V2

- Pen and eraser operations are document authority across a bounded 1472×1792
  world at five user-facing zoom levels: 1×, 2×, 4×, 8×, and 16×. Internally,
  those remain the 25–400 percent rendering scales.
- Whole-gesture Undo/Redo, focus-centered zoom, absolute minimap navigation,
  asynchronous authority journaling, and settled analytic antialiasing.
- A complete overview and 604-slot world-aligned tile pool keep every viewport
  covered while detail is rendered or retained.
- The 4 MiB journal restores vector authority and Redo. Navigation, tool state,
  and derived pixels restart from defaults.
- Export creates settled `DRAWING.PNG` and editable `DRAWING.SVG` from one
  authority snapshot. SVG preserves variable-width curves, transparent erasing,
  and one painter-ordered path per physical gesture.
- The synthesized FAT16 drive is read-only. Host eject or **EJECT & EXIT**
  returns to drawing and restores USB Serial/JTAG without resetting the board.
- New starts blank in product firmware. Clock sync, battery status, and the
  four-second hardware shutdown are supported.

The ESP32-S3 product uses the full 16 MiB flash: 1.75 MiB application, 4 MiB
drawing journal, 10.125 MiB export volume, and 64 KiB coredump. Raster V1 files
remain Raster V1 files and are not silently converted into V2 documents.

## Run the Raster V1 macOS app

```sh
./scripts/bootstrap-macos   # once
./scripts/dev run           # run-2x and run-3x open larger windows
```

Draw with the mouse, use `Cmd-Z` to undo, `C` for New, and `Esc` to quit.

## Test

```sh
./scripts/dev test
./scripts/dev release
./scripts/dev asan
./scripts/dev perf
./scripts/dev vector-perf
./scripts/dev format-check
```

## Build ESP32 firmware

```sh
./scripts/bootstrap-idf                 # once; ESP-IDF v6.0.2
./scripts/esp32 build                   # build Vector V2
./scripts/esp32 vector-v2 PORT          # build and flash Vector V2
./scripts/esp32 raster-v1               # build Raster V1
./scripts/esp32 raster-v1 PORT          # build and flash Raster V1
./scripts/esp32 graphics-test           # Raster V1 QEMU check
```

V1 and V2 use separate build directories. Supply an explicit serial port when
flashing. Export temporarily replaces USB serial with mass storage. On battery,
enter the ROM flasher by powering off, holding BOOT, and short-pressing power.
Hold the lower button for four seconds to power off; short-press it to start.

## Build RP2350 firmware

```sh
./scripts/rp2350 bootstrap
./scripts/rp2350 build
./scripts/rp2350 metrics PORT
./scripts/rp2350 trace PORT
```

The RP2350 build currently provides screen-sized ink, erasing, colors, sizes,
and New. It has no pan, Undo, or persistence.

Development details and both ESP32 product workflows are in
[`DEVELOPING.md`](DEVELOPING.md).

## License

MIT
