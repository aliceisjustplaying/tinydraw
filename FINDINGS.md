# TinyDraw findings

Snapshot: 2026-08-10

TinyDraw runs on the 368×448 Waveshare ESP32-S3 Touch AMOLED 1.8-inch V2 board. It
has variable-width ink, 4×4 edge smoothing, twelve colors, and ten levels of Undo.

## Numbers for the demo

- A 500-point XL host stroke fell from **4,479 ms to 220 ms**, a **20.4× speedup**.
- The largest observed board update fell from **72.9 ms to 16.1 ms**, about **4.5× faster**.
- Long curves average **5.7–5.8 ms per drawing update**.
- Fast XL diagonals average **4.9–10.1 ms** and peak at **16.1 ms**.
- The CST820 reports a new position every **13–14 ms**, or **75–77 Hz**.
- New dialog traffic fell from **237,728 to 106,848 bytes**, a **55% reduction**.
- The firmware is **285,664 bytes**; 73% of its app partition is free.

## From prototype to physical device

### A fast loop before the board arrived

The first milestone was a C++20 drawing core and macOS app at the real 368×448
resolution. Recorded input replays produce exact image snapshots. ASan and UBSan cover
the SDL-free core.

An early Retina bug scaled SDL positions twice, placing a bottom-right click near the
center. Center and corner tests now protect the corrected mapping.

ESP-IDF and QEMU came next. QEMU boots the same core, models 8 MiB of PSRAM, and checks
a headless replay and the visible framebuffer.

### Getting the ink right

Pinned Perfect Freehand examples became an oracle for stroke points and outline geometry.
TinyDraw then moved to a streaming form that finishes old sections while the finger moves.

One complete outline left holes where a stroke crossed itself. The current renderer unions
coverage into a grayscale mask, then applies color once. Overlapping spans and round joins
close pale shared edges; the seam regression went from 207/255 to 255/255 opacity.

Start, move, finish, and cancel are explicit input states. The final lift position is kept,
and touch ending at the edge no longer discards the stroke. Sharp turns over 90 degrees get
a round join, removing chopped tops on thick handwritten letters without changing the
performance benchmark.

### Keeping stroke cost bounded

The first live renderer repeated the whole stroke-so-far. Successive 50-point blocks grew
from **41 ms to 1,040 ms**, and a 500-point XL host stroke took **4,479 ms**.

Streaming geometry, a two-point live tail, and dirty 32×32 regions limit work to the latest
input. The host workload now takes **220 ms**. Large scratch storage moved off the task
stack after the first curved firmware replay exceeded ESP-IDF's default stack.

On the board, the active coverage mask lives in internal RAM. Canvases and Undo history
live in PSRAM. Coverage runs a row at a time, display bounds stay tight, transfers are
queued, and touch sampling runs on the second CPU core. Two experiments that hurt timing
or line quality were reverted.

## Physical performance progression

| Captured stage | Average update | Worst update | Finger lift |
| --- | ---: | ---: | ---: |
| First physical XL capture | 19.9 ms | 72.9 ms | 105.1 ms |
| 32×32 dirty regions | 21.0 ms | 65.9 ms | 64.7 ms |
| Row-based 4×4 smoothing | 10.1 ms | 25.5 ms | 35.1 ms |
| Coverage mask in internal RAM | 5.8–7.7 ms | 9.1–20.4 ms | 19.1–38.8 ms |
| Tight display bounds | 8.1–8.4 ms | 15.3–22.0 ms | 18.9–28.8 ms |
| Long curves | 5.7–5.8 ms | 10.7–11.3 ms | 51–55 ms |
| Fast XL diagonals | 4.9–10.1 ms | 7.4–16.1 ms | 12.2–23.8 ms |

A slow circle produced 107 positions over 1.45 seconds. A fast 300-pixel diagonal can
contain only six to nine. Curve fitting makes sparse input smoother while a short live
segment follows the newest position. The touch queue usually stays empty, with 12–15
microseconds of measured queue delay.

## Board and memory

| Part | Value |
| --- | --- |
| MCU | ESP32-S3 rev 0.2, two cores at 240 MHz |
| Display | CO5300, 368×448 RGB565 AMOLED, 40 MHz QSPI |
| Touch | CST820 `0xB7`, firmware `0x02`, 400 kHz I²C |
| Memory | 8 MiB octal PSRAM at 80 MHz, 16 MiB flash |
| Two canvases | 659,456 bytes in PSRAM |
| Ten Undo entries | 3,440,640 bytes in PSRAM |
| Active coverage | 164,864 bytes in internal RAM |
| Three display buffers | 24,576 bytes in internal RAM |

Physical bring-up established the V2 pin layout, panel offset, and color format. A full
startup redraw clears panel memory left by the factory demo or an earlier session.

TinyDraw keeps two full canvases in PSRAM: committed ink and the current visible image.
Three smaller internal-RAM buffers queue panel transfers. The CO5300 accepts pixel updates
rather than a framebuffer swap, so a conventional full-screen flip would still send all
329,728 bytes. At 40 MHz quad SPI, the raw transfer floor is about 16.5 ms before setup and
copying. Dirty-region transfers keep the under-finger path shorter.

## UI and Undo

The default toolbar is `[undo] [pen] [eraser] [color] [size] [new]`. Color and size open
a second, modal row. Board testing led to larger tap targets, controls fitted inside the
rounded corners, and confirmation before New.

Undo keeps before-images only for touched regions. A 1,000-point trace restores 209,920
bytes; batching adjacent regions reduced display submissions from 105 to 14. Physical
examples range from 10.8 ms for 2 tiles, through 35.5 ms for 43 tiles, to 109.2 ms for the
full 168-tile canvas.

## Verification

Seventeen native test groups cover exact replays, snapshots, Perfect Freehand examples,
self-overlaps, seams, input states, UI, Undo, and a 1,000-point XL workload. ASan, UBSan,
headless QEMU, and visible QEMU pass. Device logs report drawing, display, touch, lift,
dialog, and Undo timing.

## Current limits and next work

Fast diagonals can still show the stroke being drawn. Frame work can take 16.1 ms and the
next touch position arrives 13–14 ms later. A full-canvas Undo sends 329,728 bytes and is
visibly slower.

There is no persistent Save yet. The next steps are a compact save format, then a sparse
canvas larger than the display with panning. Both must preserve bounded drawing work.

## Five-minute demo

1. Draw one slow curve and one fast diagonal to show touch sampling limits.
2. Cross an XL stroke over itself, then make a sharp turn to show solid joins.
3. Extend the stroke to show that old points no longer slow new input.
4. Undo several strokes, then demonstrate New, Cancel, and confirmation.
5. Close with **4,479 ms to 220 ms** on the host and **72.9 ms to 16.1 ms** on the board.
