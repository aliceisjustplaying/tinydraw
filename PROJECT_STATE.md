# TinyDraw project state

Last updated: 2026-08-10

Read this file for the current engineering handoff. `FINDINGS.md` records the
performance history, and `INITIAL_RESEARCH.md` contains the original product spec.

## Resume point

Current work is on `feat/rp2350-port`. The RP2350 port draws smoothly with the
shared ink and toolbar code. Its latest physical optimization sends full-width
horizontal display bands instead of the whole screen.

Start with:

```sh
git status --short
./scripts/dev test
./scripts/dev asan
./scripts/rp2350 build
```

Keep commits small and push them often. Do not flash by an ambiguous serial port
when both boards are connected.

## Product status

All targets use a 368×448 RGB565 viewport and a tldraw-inspired toolbar:

```text
[undo] [pen/tools] [eraser] [color] [size] [new]
```

The color popup has twelve tldraw colors. The size popup has S, M, L, and XL.
New requires confirmation. Drawing uses variable width, simulated pressure,
rounded joins, and 4×4 edge smoothing.

Drawings are not persistent.

### ESP32-S3 V2

Hardware:

- ESP32-S3, two 240 MHz cores
- CO5300 AMOLED over 60 MHz QSPI
- CST820 touch at 400 kHz
- 8 MiB octal PSRAM and 16 MiB flash

Features:

- 736×896 world, four screen areas
- single-finger pan selected from the pen/tools popup
- ten dirty-tile Undos
- pen, eraser, colors, sizes, and confirmed New
- touch sampling on the second core

The display bus was reduced from 80 MHz to 60 MHz after occasional colored lines
appeared. Recent long strokes average 2.5–3.4 ms per update. Panning was measured
at 9.8–10.1 ms at 80 MHz; current 60 MHz panning needs a fresh capture.

Wi-Fi export was removed. It is documented as an experiment in `FINDINGS.md`.

### RP2350

Hardware:

- RP2350 with 520 KiB SRAM
- SH8601 AMOLED over PIO-driven QSPI
- FT3168 touch at 400 kHz
- Raspberry Pi USB serial `E2EC86EFBB9592DB`

Features:

- screen-sized drawing canvas
- shared `InkStream` and toolbar state/rendering
- pen, eraser, colors, sizes, and confirmed New
- touch polling on core 1 through a bounded queue
- one framebuffer stored in panel byte order

The RP2350 does not have pan, Undo, Wi-Fi, or persistence. The framebuffer alone
uses 329,728 bytes. Static data totals 479,776 bytes, so large histories or a
second framebuffer do not fit safely.

The SH8601 transfers a full screen in about 8.84 ms. Repeated arbitrary partial
rectangles corrupt the physical display. Full-width horizontal bands remained
clean through 1,012 observed updates and reduced average drawing time from
9.44 ms to 1.17 ms. A second run averaged 1.39 ms and peaked at 3.23 ms.

The FT3168 normally reports changed coordinates every 14.8–16.3 ms. Fast curves
can therefore have 20–30 pixel gaps between raw points. Quadratic midpoint curves
fill the path between reports. A 100 Hz register experiment and a five-byte touch
burst both made input worse and were reverted.

`./scripts/rp2350 capture` currently produces an image that can disagree with the
physical panel after banded updates. Timing and touch traces remain useful. Treat
physical inspection as the display correctness check until capture is repaired.

## Safe flashing

Both devices may be connected at once. The RP2350 has Raspberry Pi VID `0x2e8a`;
the ESP32 has Espressif VID `0x303a`.

Build and flash the RP2350 by exact serial:

```sh
./scripts/rp2350 build
serial=E2EC86EFBB9592DB
picotool reboot -u -f --vid 0x2e8a --pid 0x0009 --ser "$serial"
picotool load -v -x out/build/rp2350/tinydraw_rp2350.uf2 \
  -t uf2 --ser "$serial"
```

The RP2350 application usually appears as `/dev/cu.usbmodem1101`, but the path can
change. The serial number is the flash identity.

Build the ESP32 without flashing:

```sh
./scripts/esp32 build
./scripts/esp32 graphics-test
```

Use an explicit ESP32 port when flashing physical firmware.

## Development loop

Host and shared-core commands:

```sh
./scripts/bootstrap-macos   # once
./scripts/dev run
./scripts/dev run-2x
./scripts/dev run-3x
./scripts/dev test
./scripts/dev release
./scripts/dev asan
./scripts/dev perf
./scripts/dev format-check
```

RP2350 commands:

```sh
./scripts/rp2350 bootstrap  # once
./scripts/rp2350 build
./scripts/rp2350 capture PORT /tmp/tinydraw-rp2350.png
./scripts/rp2350 metrics PORT
./scripts/rp2350 trace PORT
```

ESP-IDF v6.0.2 stays isolated behind `scripts/esp32`. Do not add PlatformIO or
source ESP-IDF globally.

## Code map

- `core/`: platform-independent ink, geometry, raster, toolbar, Undo, and world canvas
- `host/`: interactive SDL macOS adapter and replay entry points
- `esp32/`: ESP-IDF physical and QEMU adapters
- `rp2350/`: Pico SDK app plus the Waveshare SH8601/FT3168 board drivers
- `tests/`: shared-core tests, replay checks, snapshots, and performance characterization
- `testdata/`: recorded strokes, snapshots, and Perfect Freehand reference output
- `scripts/`: reproducible host, ESP32, and RP2350 commands
- `tools/`: QEMU replay, Perfect Freehand oracle, and RP2350 capture tools

The RP2350 directly links the existing `core/src/toolbar.cpp` and
`core/src/ink_stream.cpp`. Its simpler raster stays platform-specific for now.
The ESP32 and host share the full tiled ribbon raster and Undo implementation.

## Validation baseline

At this handoff:

- all 20 native CTest entries pass;
- ASan and UBSan pass both sanitizer entries;
- the RP2350 Release build passes without compiler warnings;
- the ESP32 physical build passes with 70% of its app partition free;
- the ESP32 QEMU graphics replay passes.

The RP2350 physical test covered fast medium and XL curves, 1,012 banded display
updates, toolbar use, and repeated drawing without visible corruption.

## Next work

1. Repair or replace RP2350 USB framebuffer capture so it matches the panel.
2. Investigate FT3168 data-ready timing only with trace evidence; keep the stable
   default report configuration until a tested programming guide is available.
3. Decide whether the RP2350 feature set needs a small Undo history. SRAM is the
   binding constraint.
4. Rebase `feat/rp2350-port` onto `main` after review and physical sign-off.

Keep ESP32 tests green while changing shared code. RP2350-only driver work must not
change the ESP32 firmware target.
