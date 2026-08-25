# Developing TinyDraw

Raster V1 and Vector V2 are active products. Keep their build directories and
firmware variants separate. Current release status is in
[`PROJECT_STATE.md`](PROJECT_STATE.md); architectural details live in
[`docs/design/`](docs/design/).

## Host setup and daily loop

The native ARM64 macOS build is the fastest development loop. `tinydraw_core`
has no SDL or ESP-IDF dependency; SDL is a thin shell around the 368×448 RGB565
framebuffer.

```sh
./scripts/bootstrap-macos       # one-time Homebrew setup
./scripts/dev test              # Debug build and native tests
./scripts/dev release           # optimized build and tests
./scripts/dev asan              # ASan and UBSan on SDL-free code
./scripts/dev run               # interactive Raster V1 app
./scripts/dev run-2x            # larger interactive window
./scripts/dev perf              # Raster V1 work/traffic characterization
./scripts/dev vector-perf       # retained Vector V2 benchmarks
./scripts/dev format-check
./scripts/dev tidy              # Vector V2 clang-tidy scope
./scripts/dev cppcheck          # Vector V2 Cppcheck scope
```

The baseline is CMake 3.28+, Ninja, C++20 Clang, and SDL2. Raw CMake commands
also work:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Build products and `compile_commands.json` are under `out/build/<preset>/`.

## Raster V1 replay and UI checks

```sh
out/build/host-debug/host/tinydraw_host \
  --replay testdata/strokes/zigzag.stroke \
  --output /tmp/zigzag.ppm

out/build/host-debug/host/tinydraw_host \
  --ui-preview \
  --output /tmp/ui-preview.ppm

out/build/host-debug/host/tinydraw_host --undo-e2e
```

CTest regenerates approved images and compares them byte-for-byte. These are
determinism checks. Device timing and drawing feel still require hardware.

The native app uses `Cmd-Z` for Undo, `C` for New, and `Esc` to quit. Raster V1
stores a 1104×1344 RGB565 world, autosaves changed 32×32 tiles after 500 ms
idle, and exports `DRAWING.PNG`.

## Vector V2 host checks

Vector V2 is platform-independent below its application adapters. Important
host targets cover operation authority, rasterization, history, settlement,
journaling, export, and interaction geometry. The captured drawing can
be checked directly:

```sh
zstd -d --stdout testdata/documents/captured-drawing-2026-08-19.journal.zst \
  > /tmp/captured-drawing-2026-08-19.journal

out/build/host-release/vector_v2/tinydraw_vector_v2_journal_corpus_check \
  /tmp/captured-drawing-2026-08-19.journal \
  testdata/documents/captured-drawing-2026-08-19.tdoc
```

The raw journal preserves real flash transactions and recovery behavior. The
TDOC preserves the same 102 active operations and 2,706 samples in a compact
form embedded by the physical battery.

## ESP32 setup

ESP-IDF is pinned by `.idf-version` and isolated from the host build.

```sh
./scripts/bootstrap-idf       # installs ESP-IDF v6.0.2 and Xtensa QEMU
```

Use an explicit port when flashing. The currently attached board normally
appears as `/dev/cu.usbmodem*`.

### Vector V2 product

```sh
./scripts/esp32 build
./scripts/esp32 vector-v2 PORT
./scripts/esp32 vector-v2-demo [PORT]
```

The product uses 604 cache slots. Its 16 MiB partition map is fixed across
product and gate firmware: 1.75 MiB app at `0x10000`, 4 MiB drawing journal at
`0x1D0000`, 10.125 MiB export at `0x5D0000`, and 64 KiB coredump at `0xFF0000`.
Flashing the app does not erase the drawing partition.

A short press of the top button hides or restores the battery, minimap, and zoom controls. The
bottom toolbar remains visible and interactive. Long-press the button once in the demo variant to
reset to a blank canvas and record (the red dot is lit), again to stop, and again to replay on the device.
Later long presses replay the same take; reboot to discard it and record another. The tape lives
only in PSRAM, and demo drawing authority is never written to autosave.

Vector V2 journals vector authority through a low-priority flash worker. It
exports `DRAWING.PNG` and `DRAWING.SVG` through read-only TinyUSB mass storage.
Host eject and **EJECT & EXIT** tear down TinyUSB and reacquire USB Serial/JTAG.

### Raster V1 product

```sh
./scripts/esp32 raster-v1          # build
./scripts/esp32 raster-v1 PORT     # build and flash
```

Raster V1 has its own build directory and compile-time variant. It keeps the
established raster document, tile autosave, PNG export, demo, and QEMU paths.

### Physical Vector V2 battery

```sh
./scripts/esp32 vector-v2-gate-harness PORT 604 verify
```

The gate embeds diagnostic corpora and must not be left on the device as the
product image. A release run is complete only when every verdict is true, no
failure marker appears, and the product image from the same source revision is
flashed and booted afterward.

Other diagnostic variants:

```sh
./scripts/esp32 panel-probe PORT [CELL]
./scripts/esp32 vector-v2-tearing-probe PORT
./scripts/esp32 vector-v2-tile-census PORT
```

### Raster V1 QEMU

```sh
./scripts/esp32 qemu
./scripts/esp32 graphics-test
./scripts/esp32 graphics       # Ctrl-A, then X to exit
```

QEMU proves integration and structural agreement. It does not model physical
PSRAM, panel timing, touch, or drawing feel.

## Hardware behavior

The active ESP32-S3 board has a CO5300 display, CST820 touch controller, 8 MiB
octal PSRAM, 16 MiB flash, and AXP2101 power manager. The panel runs at an
effective 40 MHz QSPI clock. Measured limits and optical follow-up are in
[`CO5300_PANEL_LIMITS_2026-08-15.md`](docs/receipts/hardware/CO5300_PANEL_LIMITS_2026-08-15.md).

Export owns the internal USB PHY while mounted. To enter the ROM flasher on
battery, power off, hold BOOT, and short-press power. The lower PMU button
performs a four-second shutdown and a short-press cold boot.

## Dependency policy

- `core/`: C++ standard library only; no SDL, ESP-IDF, or desktop assumptions.
- `vector_v2/`: platform-independent vector authority and rendering modules.
- `host/`: SDL2-compatible APIs found through pkg-config.
- `tests/`: pinned doctest 2.5.3 under `third_party/doctest`.
- `third_party/pngenc`: vendored low-memory streaming PNG encoder.
- ESP32 USB export: pinned Espressif TinyUSB component through the IDF component
  manager.

Perfect Freehand reference provenance is in
[`reference/PERFECT_FREEHAND.md`](reference/PERFECT_FREEHAND.md). It is not a
build dependency or submodule. Add new dependencies only when a maintained test
or product path needs them.
