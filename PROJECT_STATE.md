# TinyDraw project state

Last updated: 2026-08-11

Read this file for the current engineering handoff. `FINDINGS.md` records the
performance history, and `INITIAL_RESEARCH.md` contains the original product spec.

## Resume point

ESP32 battery status and PMU off/on now work on the physical board. Autosave has
written flash on-device; verify drawing restore through a battery power cycle next.
Then continue with canvas sizing and USB-C image export. The RP2350 remains at its
documented reduced scope.

Start with:

```sh
git status --short
./scripts/dev test
./scripts/dev asan
./scripts/esp32 build
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

ESP32 drawings autosave to flash. Its passive top-right badge reports battery
percentage and charging without blocking drawing beneath it. RP2350 drawings are
not persistent.

### ESP32-S3 V2

Hardware:

- ESP32-S3, two 240 MHz cores
- CO5300 AMOLED over 60 MHz QSPI
- CST820 touch at 400 kHz
- AXP2101 battery/charger PMU at I²C address `0x34`
- 8 MiB octal PSRAM and 16 MiB flash

Features:

- 736×896 world, four screen areas
- single-finger pan selected from the pen/tools popup
- ten dirty-tile Undos
- debounced, tile-granular flash autosave and boot restore
- touch recording/replay for hands-free demos
- battery percentage and charging status from the AXP2101 PMU
- verified four-second battery shutdown and short-press power-on
- pen, eraser, colors, sizes, and confirmed New
- touch sampling on the second core

The display bus was reduced from 80 MHz to 60 MHz after occasional colored lines
appeared. Recent long strokes average 2.5–3.4 ms per update. Panning was measured
at 9.8–10.1 ms at 80 MHz; current 60 MHz panning needs a fresh capture. With a
battery installed, firmware startup explicitly resets the still-powered CO5300
through the board's I/O expander, preventing black or stale screens after MCU reset.

Autosave keeps a 1,318,912-byte PSRAM shadow in row-major form. Changed 32×32
tiles serialize two per 4 KiB flash sector after 500 ms without touch input. Rapid
strokes coalesce. New schedules the whole world; Undo schedules its viewport; pan
updates the saved origin. A 2 MiB `drawing` partition survives normal app flashes.
One physical 18-sector save took 2.266878 seconds in the background task. Restore
through battery shutdown still needs an explicit visual check.

Short BOOT presses start and stop touch recording; a red toolbar dot shows the
recording state. Holding BOOT replays the latest RAM tape from a blank canvas.
Demo replay does not write flash. Holding the lower PMU button for four seconds
powers the battery-backed board off; a short press starts it again. TinyDraw does
not yet use light or deep sleep. Wi-Fi export remains removed and is documented
as an experiment in `FINDINGS.md`.

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
fill the path between reports. Touch polling now stops when the active-low GPIO 4
signal rises at lift. Reading only on the rise was jittery and was reverted. A
100 Hz register experiment and a five-byte touch burst also made input worse.

RP2350 now uses the same 0.35 streamline setting as ESP32, making velocity-based
width changes more visible. It delays the first raster until the second point can
supply a measured starting width; taps still render on release.

The remaining visible lift issue comes from the midpoint curve: live drawing stops
halfway through the latest sample interval, then draws the withheld half on lift.
Moving that join to 90% did not remove the effect and made circles more angular, so
that experiment was reverted. ESP32 looks better because its tiled ribbon renderer
can replace a provisional tail; RP2350 permanently stamps quadratic circles into
one framebuffer.

`./scripts/rp2350 capture` currently produces an image that can disagree with the
physical panel after banded updates. Timing and touch traces remain useful. Treat
physical inspection as the display correctness check until capture is repaired.

The enclosed RP2350 unit has a battery but its side PWR button has no shutdown
handler in TinyDraw. Unplugging USB does not turn off battery power. A future
handler should blank the panel and enter a tested dormant mode.

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
- the ESP32 physical build passes with 69% of its app partition free;
- the ESP32 QEMU graphics replay passes;
- battery percentage, charging state, deterministic panel reset, and PMU off/on
  have been checked on the ESP32-S3 V2 board.

The RP2350 physical test covered fast medium and XL curves, 1,012 banded display
updates, toolbar use, and repeated drawing without visible corruption.

## Next work

Resume on the ESP32 build:

1. Verify autosave restore, rapid-stroke coalescing, and New/Undo persistence.
2. Measure how far the 736×896 canvas can grow without hurting drawing or panning.
3. Revisit Undo depth and storage alongside the canvas and save format.
4. Export a drawing over USB-C. Start with a small read-only TinyUSB MSC volume
   containing one image, and verify iPhone Files behavior before expanding it.
5. Consider event-driven AXP2101 status refresh and a tested sleep mode separately.

Deferred RP2350 work includes a replaceable provisional tail, bounded vector
Undo/panning, accurate USB framebuffer capture, and PWR-button dormant mode.
Keep both firmware builds and shared-core tests green.
