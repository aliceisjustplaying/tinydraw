# TinyDraw findings

Snapshot: 2026-08-10

TinyDraw runs on the 368×448 Waveshare ESP32-S3 Touch AMOLED 1.8-inch V2 board.
The physical build supports variable-width ink, 4×4 antialiasing, a compact
toolbar, and ten levels of Undo.

## Headline numbers

- A 500-point XL host stroke fell from **4,479 ms to 220 ms**, a **20.4×**
  improvement.
- The largest observed hardware update fell from **72.9 ms to 16.1 ms**, about
  **4.5×**.
- Current long curves average **5.7–5.8 ms per update**.
- Fast XL diagonals average **4.9–10.1 ms** and peak at **16.1 ms**.
- The CST820 supplies coordinates every **13–14 ms**, about **75–77 Hz**.

## Findings from the first build

### The host loop

A C++20 core and thin macOS shell established the 368×448 RGB565 drawing loop
before the board arrived. Recorded strokes produce byte-comparable snapshots.

SDL already returned logical mouse coordinates. Applying Retina scaling again
mapped a bottom-right click near the center; corner cases now have regression
coverage. ASan/UBSan covers the SDL-free core because the Homebrew SDL loader
aborts under Apple's sanitizer runtime.

### Perfect Freehand

Pinned Perfect Freehand fixtures verify the ported batch geometry within
floating-point tolerance. Upstream can revise early radii using ten points.
TinyDraw gives accepted points immutable radii; the next point stabilizes their
forward direction.

Complete outline polygons produced self-intersection holes. The visible stream
retains two points and emits bounded convex spans, circles, and triangle
fallbacks.

### Coverage and lifecycle

Active strokes use `coverage = max(existing, incoming)` in an 8-bit plane,
followed by one RGB565 composition. Self-overlaps keep a solid color. Single-quad
spans, round joins, and a 0.75-pixel overlap close pale shared edges; the seam
test improved from 207/255 to 255/255 coverage.

Explicit begin, update, finish, and cancel operations handle input lifecycle.
Regressing timestamps use a nominal interval, finish reaches the lift coordinate,
and cancellation clears active state.

### Embedded memory

Raster scratch uses capability-placed object storage. The curved batch exceeded
ESP-IDF's 3,584-byte main-task stack; it now holds at most eight primitives, and
the main task uses 6,144 bytes.

## Performance progression

The original renderer revisited the full stroke prefix. Successive 50-point
blocks grew from 41 ms to 1,040 ms. `StrokeRaster` made frame cost depend on the
current dirty region.

| Stage | Average update | Worst update | Lift |
| --- | ---: | ---: | ---: |
| Early physical XL worst case | 19.9 ms | 72.9 ms | 105.1 ms |
| 32×32 active raster tiles | 21.0 ms | 65.9 ms | 64.7 ms |
| Scanline 4×4 raster | 10.1 ms | 25.5 ms | 35.1 ms |
| Internal-SRAM coverage | 5.8–7.7 ms | 9.1–20.4 ms | 19.1–38.8 ms |
| Tight display bounds | 8.1–8.4 ms | 15.3–22.0 ms | 18.9–28.8 ms |
| Current long curves | 5.7–5.8 ms | 10.7–11.3 ms | 51–55 ms |
| Current fast diagonals | 4.9–10.1 ms | 7.4–16.1 ms | 12.2–23.8 ms |

Append-stable geometry, dirty 32×32 tiles, scanline coverage, internal-SRAM
coverage, tight display regions, DMA staging, and core-1 touch sampling produced
the gains. Diagonal intersection checks and `0.6` streamline smoothing increased
latency and were reverted in `3bd5c21` and `ee1c6b8`.

## Physical hardware

| Part | Value |
| --- | --- |
| MCU | ESP32-S3 rev 0.2, dual core, 240 MHz |
| Display | CO5300, 368×448 RGB565 AMOLED, 40 MHz QSPI |
| Touch | CST820 `0xB7`, firmware `0x02`, 400 kHz I²C |
| Touch task | 1 kHz on core 1 |
| Distinct coordinates | Every 13–14 ms |
| Memory | 8 MiB octal PSRAM at 80 MHz, 16 MiB flash |
| Firmware image | 279,792 bytes; 73% of the app partition remains free |

A slow circle produced 107 points over 1.45 seconds; a fast 300-pixel diagonal
can contain six to nine. Midpoint quadratics smooth the path, and a provisional
tail reaches the newest coordinate. Typical captures show `max_queue=0` and
12–15 microseconds of queueing delay. Some XL frames take 14–16 ms.

## Memory and Undo

| Allocation | Location | Size |
| --- | --- | ---: |
| Two RGB565 canvases | PSRAM | 659,456 bytes |
| Ten-entry tile Undo arena | PSRAM | 3,440,640 bytes |
| Active coverage | Internal SRAM | 164,864 bytes |
| Three display buffers | DMA SRAM | 24,576 bytes |

Undo stores touched-tile before-images and evicts the oldest of ten entries. It
remained fast through the hardware renderer changes.

## Verification

The project has 13 native CTest entries, process-level replays, exact snapshots,
Perfect Freehand fixtures, a 1,000-sample XL traffic guardrail, ASan/UBSan, and
headless plus visible QEMU. Hardware telemetry records render, lift, display,
touch cadence, input lag, and queue depth. QEMU verifies integration and memory
placement; hardware captures provide timing.

## Current limits and next work

Fast diagonal latency combines a 13–14 ms touch interval, occasional 14–16 ms
XL frames, 40 MHz display transfers, and 4×4 coverage. Remaining experiments
include provisional tip prediction and a measured 2×2 versus 4×4 AA comparison.

Product work includes larger toolbar tap targets, confirmation before New,
the edge-release stroke bug, a minimal Save design, and eventually panning over
a canvas larger than the display.

## Five-minute demo

1. Draw a long XL curve to show bounded frame cost.
2. Cross it over itself to show solid coverage union and 4×4 AA.
3. Undo several operations; history traffic scales with touched tiles.
4. Draw slow and fast circles, then show the 13–14 ms CST820 cadence.
5. Finish with **4,479 ms to 220 ms** on the host and **72.9 ms to 16.1 ms**
   for the largest observed hardware update.
