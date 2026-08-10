# TinyDraw findings

Snapshot: 2026-08-10

TinyDraw is a finger-drawing app for a 368×448 Waveshare ESP32-S3 Touch AMOLED
1.8-inch V2 board. It now runs on the physical CO5300 display with CST820 touch,
variable-width ink, 4×4 antialiasing, a compact toolbar, and ten levels of Undo.

## The short version

We started with a renderer whose cost grew with the length of the stroke. A
500-point XL stroke took 4,479 ms on the host, and each successive block of 50
points became slower. The dirty-tile streaming renderer reduced the same work
to 220 ms, about 20× faster, with cost bounded by the pixels changing in the
current frame.

Physical hardware exposed a second set of limits. Early XL captures reached
72.9 ms for one update and 105.1 ms at lift. The current build keeps long curves
near 5.7 ms per update. Fast XL diagonals average 4.9–10.1 ms, peak at 16.1 ms,
and finish in 12.2–23.8 ms in the latest capture.

The remaining visible lag on fast diagonals is mostly explained by the CST820.
The ESP32 polls it at 1 kHz on a dedicated core; distinct coordinates arrive
every 13–14 ms, or about 75–77 Hz. A fast 300-pixel diagonal may contain six to
nine real coordinates. TinyDraw reconstructs a smooth quadratic path between
those reports while keeping a provisional tail at the newest coordinate.

## Hardware profile

| Part | Measured or confirmed value |
| --- | --- |
| Board | Waveshare ESP32-S3 Touch AMOLED 1.8 V2 |
| MCU | ESP32-S3 rev 0.2, dual core, 240 MHz |
| Display | CO5300, 368×448 RGB565 AMOLED |
| Display bus | 40 MHz QSPI, the rate used by Waveshare |
| Touch | CST820, chip ID `0xB7`, firmware `0x02` |
| Touch bus | 400 kHz I²C |
| Touch polling | 1 kHz task pinned to core 1 |
| Distinct touch reports | Usually one every 13–14 ms |
| PSRAM | 8 MiB octal PSRAM at 80 MHz |
| Flash | 16 MiB |
| Firmware image | 279,792 bytes; 73% of the 1 MiB app partition remains free |

Waveshare's V1 board uses an SH8601 display and FT3168 touch. This V2 board uses
the CO5300 display adapter and CST820 touch adapter.

## Performance, from first prototype to hardware

### Host algorithm benchmark

The first renderer rebuilt work from the full stroke history.

| 500-point XL prefix | Time |
| --- | ---: |
| Original growing-stroke renderer | 4,479 ms |
| Dirty-tile `StrokeRaster` | 220 ms |
| Improvement | 20.4× |

The original 50-point blocks grew from 41 ms to 1,040 ms. The replacement no
longer slows down as a stroke gets older. Its work depends on dirty tiles and the
small append-stable geometry batch.

### Physical-board captures

Serial telemetry from the hardware sessions produced these measurements:

| Stage | Average update | Worst update | Lift |
| --- | ---: | ---: | ---: |
| Early physical XL worst case | 19.9 ms | 72.9 ms | 105.1 ms |
| 32×32 active raster tiles, XL worst case | 21.0 ms | 65.9 ms | 64.7 ms |
| Scanline 4×4 raster, difficult stroke | 10.1 ms | 25.5 ms | 35.1 ms |
| Internal-SRAM coverage, representative | 5.8–7.7 ms | 9.1–20.4 ms | 19.1–38.8 ms |
| Tight display bounds, fast diagonals | 8.1–8.4 ms | 15.3–22.0 ms | 18.9–28.8 ms |
| Current curved path, long curves | 5.7–5.8 ms | 10.7–11.3 ms | 51–55 ms total lift work |
| Current curved path, fast diagonals | 4.9–10.1 ms | 7.4–16.1 ms | 12.2–23.8 ms |

The largest captured update fell from 72.9 ms to 16.1 ms, about 4.5×. Long
current curves finish in 51–55 ms, roughly half the early 105.1 ms worst case;
fast diagonals finish in 12.2–23.8 ms. Active drawing now stays near the touch
controller's 13–14 ms report interval in most frames.

Undo captures and restores the tiles touched by each stroke. One full RGB565
canvas is 329,728 bytes; short strokes touch a small fraction of it.

## What made it faster

### 1. Stream append-stable geometry

`InkStream` turns timestamped touch samples into stable positions, pressure, and
radius. The ribbon stream emits newly stable geometry plus a small replaceable
tail. Long strokes do not make later frames more expensive.

### 2. Keep a persistent coverage plane

The active stroke lives in an 8-bit coverage plane. New geometry is max-unioned
there before one RGB565 composition. Self-overlaps remain solid and keep the
same color as the rest of the stroke.

### 3. Work in dirty 32×32 tiles

The physical path loads, rasterizes, composites, and presents only tiles touched
by the old provisional tail or new geometry. The committed 368×448 canvas stays
in PSRAM. Tile scratch remains in fast internal memory.

### 4. Rasterize 4×4 coverage with scanlines

The current convex raster computes four horizontal strips per pixel row. This
reduced difficult strokes from roughly 20 ms per update to about 10 ms while
preserving 4×4 coverage.

### 5. Put hot coverage in internal SRAM

The committed and visible RGB565 canvases belong in PSRAM. The 164,864-byte
active coverage plane is touched constantly, so moving it to internal SRAM
reduced common updates to roughly 3–6 ms before later geometry changes.

### 6. Present one tight region

Rendering each dirty tile separately made tile-shaped updates visible and added
many panel submissions. TinyDraw now composes all dirty tiles first and presents
one tight, even-aligned geometry region through three DMA staging buffers.

### 7. Sample touch on the second core

Rendering runs on core 0. A fixed 32-event touch queue receives timestamped
samples from a 1 kHz task on core 1. Typical captures show `max_queue=0`; ordinary
curves reach the renderer with about 12–15 microseconds of queueing delay.

### 8. Reconstruct curves from sparse CST820 reports

Faster motion does not produce more CST820 reports. A slow circle produced 107
points over 1.45 seconds, while a very fast gesture produced only a handful.
A midpoint-quadratic stream now uses three real samples to infer a smooth curve.
Stable geometry lags one sample, while a replaceable straight tail reaches the
latest measured point.

The first version exposed pale AA seams where two convex curve pieces met.
Adjacent pieces now overlap by 0.75 pixel. A regression test measured 207/255
coverage at the seam before the fix and 255/255 afterward.

## Memory design

| Allocation | Location | Approximate size |
| --- | --- | ---: |
| Committed RGB565 canvas | PSRAM | 329,728 bytes |
| Visible RGB565 canvas | PSRAM | 329,728 bytes |
| Ten-entry tile Undo arena | PSRAM | 3,440,640 bytes reserved |
| Active stroke coverage | Internal SRAM | 164,864 bytes |
| Three display staging buffers | DMA-capable SRAM | 24,576 bytes |

The Undo arena has fixed worst-case capacity. Each entry records touched tiles,
and the oldest entry is evicted when the ten-entry history fills.

The curve geometry batch initially overflowed ESP-IDF's 3,584-byte main-task
stack. The batch now has an eight-primitive bound, and the main-task stack is
6,144 bytes. The shared core continues to pass ASan and UBSan.

## Hardware checks and reversions

- Extra diagonal tile-intersection checks increased frame time. Commit `3bd5c21`
  reverted the change.
- Streamline smoothing at `0.6` left fast curves angular and slowed broad strokes.
  Commit `ee1c6b8` restored the previous setting.
- The touch task polls at 1 kHz over 400 kHz I²C. Distinct CST820 coordinates
  continue to arrive every 13–14 ms.

## Correctness and feedback loops

The shared C++20 core runs on macOS, QEMU, and the ESP32-S3. Current automated
coverage includes:

- 13 native CTest entries;
- process-level drawing, overlap, tight-join, toolbar, invalid-input, and Undo
  replays;
- exact PPM golden snapshots;
- a 1,000-sample sustained XL performance and memory-traffic guardrail;
- ASan and UBSan for SDL-free project code;
- headless and visible ESP-IDF QEMU paths with modeled 8 MiB PSRAM;
- fixed-capacity and malformed-geometry tests;
- the curved-ribbon AA seam regression.

Physical sessions add serial telemetry for sample count, update average and max,
lift time, display preparation, transfer submission, touch interval, input lag,
and queue depth. That instrumentation showed when the bottleneck moved from the
renderer to the touch controller.

## Limits that remain

Fast diagonal latency now consists of:

- 13–14 ms between CST820 coordinates, about 75–77 Hz;
- occasional 14–16 ms XL rendering updates;
- CO5300 transfers over the vendor's 40 MHz QSPI configuration;
- 4×4 coverage rasterization.

Possible performance experiments include a predicted provisional tip, with
corner overshoot and retraction to measure, and 2×2 AA, with edge quality to
compare against the current 4×4 output.

Known product work after this snapshot:

- make toolbar tap targets more forgiving;
- ask for confirmation before New clears the canvas;
- fix the edge-release case where a stroke can disappear;
- evaluate a minimal Save design;
- consider panning and a canvas larger than the display.

## A five-minute demo

1. Show the 368×448 host app at physical scale and mention that the same core runs
   on macOS, QEMU, and the ESP32.
2. Draw a long XL curve on the board. The frame cost stays bounded as the stroke
   grows.
3. Draw over the same area to show solid overlap and 4×4 AA.
4. Tap Undo several times. Show that Undo traffic scales with the touched tiles;
   one full RGB565 canvas is 329,728 bytes.
5. Draw one slow and one fast circle. Use the 13–14 ms CST820 cadence to explain
   why the firmware reconstructs midpoint curves.
6. Show the before/after numbers: 4,479 ms to 220 ms on the host algorithm;
   72.9 ms to 16.1 ms for the largest observed hardware update.
7. Show the latest telemetry beside the CST820's 13–14 ms coordinate interval
   and the current 14–16 ms worst-case drawing frames.

## Useful one-slide numbers

- 368×448 AMOLED, 165k pixels
- 240 MHz dual-core ESP32-S3
- 8 MiB octal PSRAM
- 4×4 antialiasing in RGB565
- 10 levels of dirty-tile Undo
- 20.4× host long-stroke improvement
- 4.5× reduction in the largest observed hardware update
- 75–77 Hz measured touch-coordinate cadence
- 5.7 ms average for current long curves
- 4.9–10.1 ms average for current fast diagonals
- 13 native test lanes plus ASan/UBSan and QEMU
