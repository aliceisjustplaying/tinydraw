# Developing TinyDraw

For the current architecture, implemented milestones, known limitations, and exact next steps, read
[`PROJECT_STATE.md`](PROJECT_STATE.md).

The native ARM64 macOS build is the primary development loop. `tinydraw_core` has no SDL or
ESP-IDF dependency. SDL is only a thin host shell around a 368×448 RGB565 framebuffer.

## One-time host setup

Prerequisites are captured in `Brewfile`:

```sh
./scripts/bootstrap-macos
```

The supported baseline is CMake 3.28+, Ninja, a C++20 Clang toolchain, and SDL2. The current
machine was bootstrapped with Apple Clang 21 and SDL 2.32 through `sdl2-compat`.

## Daily loop

```sh
./scripts/dev test          # debug build + all native tests
./scripts/dev run           # SDL host; ten-level Cmd-Z undo, undoable C new, Esc quit
./scripts/dev perf          # deterministic sustained-XL operation/traffic report
./scripts/dev asan          # AddressSanitizer + UndefinedBehaviorSanitizer on SDL-free core
./scripts/dev release       # optimized build + tests
./scripts/dev format-check
./scripts/dev format
./scripts/dev tidy          # clang-tidy, Vector V2 module only
./scripts/dev cppcheck      # Cppcheck, Vector V2 module only
```

Concluded characterization binaries are excluded from normal builds. Configure
with `-DTINYDRAW_BUILD_EXPERIMENTS=ON` only when reproducing their archived receipts.

The wrapper is intentionally thin. Raw commands also work:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Build products and `compile_commands.json` stay below `out/build/<preset>/`. Editors should use
`out/build/host-debug/compile_commands.json`.

## End-to-end replay

The host executable can run without a window and drive the real input/filter/PF-ribbon/render path
from a stroke recording:

```sh
out/build/host-debug/host/tinydraw_host \
  --replay testdata/strokes/zigzag.stroke \
  --output /tmp/zigzag.ppm

out/build/host-debug/host/tinydraw_host \
  --ui-preview \
  --output /tmp/ui-preview.ppm

out/build/host-debug/host/tinydraw_host --undo-e2e
```

CTest regenerates these images and compares it byte-for-byte with the approved host snapshot. This
is a characterization test: it proves determinism and catches unintended changes, but it is not an
independent correctness oracle. PF-derived reference geometry will provide that oracle later. The
same recordings will drive QEMU and hardware, using structural/tolerance comparisons instead of
cross-target pixel equality.

The interactive host opens at roughly the real physical size of the 1.8-inch display on a
14-inch 2021 MacBook Pro at default scaling; `run-2x` and `run-3x` provide larger demo views.
Its direct-drawn floating dock follows tldraw's visual hierarchy. Undo, tools, eraser, selected
color, selected size, and new-drawing fill one row; tapping tools, color, or size opens one row
of large choices above it.

The interactive host renders PF-style ribbons as convex spans and round joins/caps into 8-bit
64×64 coverage tiles. `StrokeRaster` max-unions stable geometry into an active coverage plane and
regenerates only tiles touched by the changing tail. This keeps long-stroke work bounded and
prevents self-overlap holes or repeated edge darkening. The persistent RGB565 canvas remains
unchanged until lift; core tests compare the incremental result with one-pass coverage union.

Undo reserves ten fixed tile-addressed before-image slots. Stroke lift copies only touched tiles
from the raster's already-loaded scratch, Undo restores/submits only those tiles, and New captures
all tiles as one undoable operation. `--undo-e2e` checks draw/erase/New and multiple exact restores
through the host process.

`./scripts/dev perf` runs 1,000 XL samples over an eight-second-equivalent continuous path. It does
not use wall-clock thresholds. It reports and bounds tile visits, display bytes, committed-canvas
traffic, active-coverage traffic, history capture, and Undo restore. Current release
characterization has max per-update work of 6 tiles, 24 primitive/tile visits, 18,432 bytes of
modeled-PSRAM reads, and 4,096 bytes of writes. Stroke completion touches 105 tiles, reads 318,976
raster bytes, and writes 528,896 external-memory bytes including 209,920 history bytes. Undo reads,
restores, and submits 209,920 bytes in 14 display pushes.

The sanitizer preset excludes the host executable because Homebrew's `sdl2-compat` loader aborts
under Apple's sanitizer runtime before application code starts. All SDL-free project code remains
sanitized; full host replay runs in both debug and release presets.

The TypeScript `perfect-freehand` reference and pinned commit are documented in
`reference/PERFECT_FREEHAND.md`. It is cloned locally but is not a build dependency or submodule.

## ESP-IDF / QEMU

ESP-IDF remains isolated from the host CMake project and global shell startup. The pinned version
is in `.idf-version`. One-time setup installs v6.0.2 and the Xtensa QEMU tool:

```sh
./scripts/bootstrap-idf
```

Daily firmware commands:

```sh
./scripts/esp32 build          # build the shipping Vector V2 app
./scripts/esp32 vector-v2 PORT # build and flash the Vector V2 app
./scripts/esp32 qemu           # headless Raster V1 boot + asserted replay marker
./scripts/esp32 graphics-test  # short automated virtual-framebuffer check
./scripts/esp32 graphics       # visible stroke + shared toolbar; Ctrl-A then X to close
./scripts/esp32 clean
```

Firmware configuration uses one `TINYDRAW_FIRMWARE_VARIANT` selector. Product, gate, QEMU,
panel-probe, tearing-probe, and tile-census variants keep diagnostic machinery out of the normal
V2 app.

The firmware embeds the same seven points as `zigzag.stroke` and feeds them incrementally through
`InkStream`, `RibbonStream`, `StrokeRaster`, 4×4 coverage, RGB565 composition, and optionally
`QemuDisplayBackend`. The committed 329,728-byte RGB565 canvas, 164,864-byte active coverage plane,
and 3,440,640-byte ten-entry Undo store are allocated only with `MALLOC_CAP_SPIRAM`.
`StrokeRaster`, `TileUndoHistory`, and both tile scratch buffers use DMA-capable internal RAM.
Runtime external, internal, and DMA pointer checks must emit `TINYDRAW_MEMORY_OK` before the harness
accepts a run.

The headless harness checks accepted-point, primitive, touched-tile, geometry-bound, memory-placement,
and bounded incremental-work results. The seven-point replay submits 46 dirty tiles in total, never
more than 17 in one frame. It also reports 372,736 display bytes, 593,920 modeled-PSRAM read bytes,
438,272 write bytes, a 237,568-byte maximum read frame, and a 372,736-byte maximum write frame. Its
checksum is informational because cross-architecture pixel identity is not an oracle.

The visible graphics build is separate because `esp_lcd_qemu_rgb` requires QEMU's virtual
framebuffer device and cannot be initialized in `-nographic` mode. Each input frame copies only the
dirty 64×64-or-smaller tiles through `DisplayBackend` into QEMU's dedicated RGB565 framebuffer,
redraws the shared toolbar, and refreshes exactly once. `DisplayBackend::push_rect` is synchronous:
a backend must complete or stage a transfer before returning and must not retain the source span.
This screen is a scripted visual integration check; QEMU does not yet provide interactive
pointer/touch input to TinyDraw.

PlatformIO is not used. Espressif's installation manager owns IDF's Python and toolchain, keeping
the host loop independent from the system Python. Flash through an explicit serial port when both
physical boards are connected. Physical builds use performance optimization,
240 MHz, and the ESP32-S3R8's octal PSRAM. QEMU uses a separate build directory and configuration
with the same optimization/CPU settings and its required 8 MB quad-PSRAM model. `scripts/esp32`
asserts those effective settings after each build. This proves our explicit capability-allocation
path against the emulator. It does not prove the physical board's PSRAM capacity, bandwidth, DMA
behavior, or timing; those remain hardware checks. The active target is the V2 board with a CO5300
display, CST820 touch controller, 8 MiB octal PSRAM, 16 MiB flash, and AXP2101 power manager.
The product partition table gives firmware 1.5 MiB while retaining 3 MiB for drawing authority and
5 MiB for transactional exports. The diagnostic gate gets a 1.75 MiB app slot without shrinking
either data partition.

The physical build autosaves changed 32×32 world tiles after 500 ms idle. It samples the PMU every
second only while touch is idle. Short BOOT presses toggle demo recording and a long press
replays it. The lower PMU button performs a four-second hardware shutdown and short-press cold
boot; no sleep mode exists yet.

Raster V1 export streams the 1104×1344 RGB565 world through PNGenc into a raw flash partition.
Vector V2 export renders the 1472×1792 world with the production settled-AA algorithm in one
64-row band plus one 64×64 window, then sends those rows through the same PNG encoder. It also
streams the existing path-based SVG without changing its geometry or formatting. One metadata page
commits both files only when their authority epoch, revision, and operation count still match.

A read-only FAT16 volume is synthesized one requested sector at a time and served through TinyUSB
MSC. Raster V1 exposes `DRAWING.PNG`; Vector V2 exposes `DRAWING.SVG` and `DRAWING.PNG`. Starting
MSC replaces USB Serial/JTAG because both use the S3's internal PHY. Vector V2's on-screen
**Return to Drawing** action deinitializes TinyUSB and releases that PHY without a reset; a failed
shutdown stays modal and can be retried. To enter the ROM flashing port on a battery-powered board,
power off, hold BOOT, and power on.

## Dependency policy

- `core/`: C++ standard library only; no SDL, ESP-IDF, or desktop assumptions.
- `host/`: SDL2-compatible APIs, found through pkg-config.
- `tests/`: pinned doctest 2.5.3 under `third_party/doctest`.
- `third_party/pngenc`: vendored low-memory streaming PNG encoder.
- ESP32 USB export: pinned Espressif TinyUSB component through the IDF component manager.
- Add image-reference, fuzz, and benchmark dependencies only when those loops have real consumers.

Do not assess ESP32 performance from QEMU. Host tests establish correctness, QEMU establishes
integration and structural agreement, and physical hardware establishes timing and drawing feel.
