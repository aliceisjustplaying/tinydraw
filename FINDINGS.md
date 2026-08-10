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

A platform-independent drawing engine and small macOS app established the
368×448 drawing loop before the board arrived. Recorded strokes produce exact
image snapshots.

SDL already returned canvas-sized mouse positions. Applying Retina scaling again
mapped a bottom-right click near the center; automated tests now cover the center
and corners. The memory-safety test runs on the drawing engine because the
Homebrew SDL loader crashes when that test mode starts.

### Perfect Freehand

Pinned Perfect Freehand examples verify TinyDraw's shape calculations. Perfect
Freehand can revise the widths of its first points after seeing up to ten inputs.
TinyDraw locks each accepted point's width and uses the next point to settle its
direction.

Drawing the whole outline as one shape produced holes where it crossed itself.
The live renderer keeps two points and draws small four-sided pieces, circles,
and occasional triangles.

### Solid overlaps and input states

An active stroke first builds a grayscale mask. Overlapping pieces keep the
higher value, and TinyDraw applies the color once. Self-overlaps stay solid.
Four-sided pieces, round joins, and a 0.75-pixel overlap close pale shared edges;
the seam test improved from 207/255 to 255/255 opacity.

Every stroke has start, move, finish, and cancel states. Bad timestamps use a
safe default interval, finish reaches the lift position, and cancel clears the
unfinished stroke.

### Embedded memory

Large drawing scratch buffers live outside the small task stack. The curved
stroke batch exceeded ESP-IDF's 3,584-byte default stack; it now holds at most
eight shapes, and the task has 6,144 bytes.

## Performance progression

The original renderer repeated work for the full stroke-so-far. Successive
50-point blocks grew from 41 ms to 1,040 ms. The current renderer works on the
part of the screen changing in that frame.

| Stage | Average update | Worst update | Lift |
| --- | ---: | ---: | ---: |
| Early physical XL worst case | 19.9 ms | 72.9 ms | 105.1 ms |
| 32×32 changed blocks | 21.0 ms | 65.9 ms | 64.7 ms |
| Row-based 4×4 edge smoothing | 10.1 ms | 25.5 ms | 35.1 ms |
| Hot mask in internal memory | 5.8–7.7 ms | 9.1–20.4 ms | 19.1–38.8 ms |
| Tight display bounds | 8.1–8.4 ms | 15.3–22.0 ms | 18.9–28.8 ms |
| Current long curves | 5.7–5.8 ms | 10.7–11.3 ms | 51–55 ms |
| Current fast diagonals | 4.9–10.1 ms | 7.4–16.1 ms | 12.2–23.8 ms |

The gains came from finalizing small stroke sections as input arrives, updating
changed 32×32 blocks, drawing coverage a row at a time, keeping the hot mask in
fast internal memory, sending compact display updates, queueing panel transfers,
and reading touch on the second CPU core.

Diagonal intersection checks and stronger input smoothing increased latency and
were reverted in `3bd5c21` and `ee1c6b8`.

## Physical hardware

| Part | Value |
| --- | --- |
| MCU | ESP32-S3 rev 0.2, dual core, 240 MHz |
| Display | CO5300, 368×448 16-bit-color AMOLED, 40 MHz QSPI |
| Touch | CST820 `0xB7`, firmware `0x02`, 400 kHz I²C |
| Touch task | 1 kHz on core 1 |
| Distinct coordinates | Every 13–14 ms |
| Memory | 8 MiB octal PSRAM at 80 MHz, 16 MiB flash |
| Firmware image | 279,792 bytes; 73% of the app partition remains free |

A slow circle produced 107 points over 1.45 seconds; a fast 300-pixel diagonal
can contain six to nine. TinyDraw fits smooth curves through recent points and
keeps a short live segment at the newest position. The touch queue usually stays
empty, with 12–15 microseconds of queueing delay. Some XL frames take 14–16 ms.

## Memory and Undo

| Allocation | Location | Size |
| --- | --- | ---: |
| Two 16-bit-color canvases | External PSRAM | 659,456 bytes |
| Ten-entry Undo storage | External PSRAM | 3,440,640 bytes |
| Active stroke mask | Fast internal RAM | 164,864 bytes |
| Three display buffers | Transfer-ready internal RAM | 24,576 bytes |

Undo stores touched-tile before-images and evicts the oldest of ten entries. It
remained fast through the hardware renderer changes.

## Verification

The project has 13 automated test groups, full drawing replays, exact snapshots,
Perfect Freehand examples, a 1,000-point XL workload, memory-safety checks, and
headless plus visible emulation. Device logs record drawing, lift, display,
touch timing, input lag, and queued work. Emulation verifies that the firmware
fits together; hardware captures provide timing.

## Current limits and next work

Fast diagonal latency combines a 13–14 ms touch interval, occasional 14–16 ms
XL frames, 40 MHz display transfers, and 4×4 edge smoothing. Remaining
experiments include predicting the next finger position and comparing 2×2 with
4×4 edge smoothing.

Product work includes larger toolbar tap targets, confirmation before New,
the edge-release stroke bug, a minimal Save design, and eventually panning over
a canvas larger than the display.

## Five-minute demo

1. Draw a long XL curve to show bounded frame cost.
2. Cross it over itself to show solid overlaps and 4×4 edge smoothing.
3. Undo several operations; history traffic scales with touched tiles.
4. Draw slow and fast circles, then show the 13–14 ms CST820 cadence.
5. Finish with **4,479 ms to 220 ms** on the host and **72.9 ms to 16.1 ms**
   for the largest observed hardware update.
