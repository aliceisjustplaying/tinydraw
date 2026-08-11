# TinyDraw findings

Snapshot: 2026-08-11

TinyDraw runs on Waveshare's 368×448 ESP32-S3 and RP2350 Touch AMOLED 1.8-inch
boards. Both use the shared ink and toolbar code. The ESP32 build adds ten Undos
and a 736×896 canvas.

## Numbers for the demo

- A 500-point XL host stroke fell from **4,479 ms to 220 ms**, a **20.4× speedup**.
- The largest observed board update fell from **72.9 ms to 16.1 ms**, about **4.5× faster**.
- Recent long strokes average **2.5–3.4 ms per drawing update**.
- Fast XL diagonals can peak at **16.1 ms**.
- The CST820 reports a new position every **13–14 ms**, or **75–77 Hz**.
- At 80 MHz, panning fell from **71–74 ms to 9.8–10.1 ms per frame**, about **7× faster**.
- An export experiment produced a **35,010-byte PNG** in **0.69 s** and sent it in **1.27 s**.
- One physical autosave wrote **18 flash sectors in 2.266878 s** on its background task.
- Removing the export experiment cut the active firmware to **310,784 bytes**, with **70%** of its
  1 MiB app partition free.
- On RP2350, full-width display bands cut average drawing updates from **9.44 ms to
  1.17 ms**, an **8.1× speedup**.

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
| Current long traces | 2.5–3.4 ms | 4.8–10.1 ms | 29–62 ms |

A slow circle produced 107 positions over 1.45 seconds. A fast 300-pixel diagonal can
contain only six to nine. Curve fitting makes sparse input smoother while a short live
segment follows the newest position. The touch queue usually stays empty, with 12–15
microseconds of measured queue delay.

## Board and memory

| Part | Value |
| --- | --- |
| MCU | ESP32-S3 rev 0.2, two cores at 240 MHz |
| Display | CO5300, 368×448 RGB565 AMOLED, 60 MHz QSPI |
| Touch | CST820 `0xB7`, firmware `0x02`, 400 kHz I²C |
| Power | AXP2101 `0x34`, battery gauge, charger, and hardware power button |
| Memory | 8 MiB octal PSRAM at 80 MHz, 16 MiB flash |
| 736×896 world | 1,318,912 bytes in PSRAM |
| Two viewport canvases | 659,456 bytes in PSRAM |
| Ten Undo entries | 3,440,640 bytes in PSRAM |
| Autosave shadow | 1,318,912 bytes in PSRAM |
| Active coverage | 164,864 bytes in internal RAM |
| Three display buffers | 49,152 bytes in internal RAM |

Physical bring-up established the V2 pin layout, panel offset, and color format. A full
startup redraw clears panel memory left by the factory demo or an earlier session. Once
a battery was installed, resetting only the ESP32 could leave the CO5300 powered in a
stale state, producing a black screen. Startup now pulses panel power and reset through
the board's `0x20` I/O expander before initializing the display.

TinyDraw keeps two full canvases in PSRAM: committed ink and the current visible image.
Three smaller internal-RAM buffers queue panel transfers. The CO5300 accepts pixel updates
rather than a framebuffer swap, so a conventional full-screen flip would still send all
329,728 bytes. At 80 MHz quad SPI, its raw transfer floor is about 8.2 ms before setup and
copying. At the current 60 MHz setting, that floor is about 11.0 ms. Dirty-region
transfers keep the under-finger path shorter.

## UI and Undo

The default toolbar is `[undo] [pen] [eraser] [color] [size] [new]`. Color and size open
a second, modal row. Board testing led to larger tap targets, controls fitted inside the
rounded corners, and confirmation before New.

Undo keeps before-images only for touched regions. A 1,000-point trace restores 209,920
bytes; batching adjacent regions reduced display submissions from 105 to 14. Physical
examples range from 10.8 ms for 2 tiles, through 35.5 ms for 43 tiles, to 109.2 ms for the
full 168-tile canvas.

## Autosave

ESP32 autosave has a dedicated 2 MiB flash partition and a 1,318,912-byte PSRAM
shadow of the 736×896 world. A stroke marks world-aligned 32×32 tiles as its input
arrives. Two serialized tiles fit one 4 KiB erase sector. At lift, only those tiles
are copied into the world and save shadow.

Flash work begins after 500 ms without touch input and runs on a low-priority task.
New schedules the complete world, Undo schedules the current viewport, and panning
updates the saved viewport origin. Rapid strokes update the pending shadow before it
is written. Boot reads the tiled sectors, reconstructs the row-major world, and shows
the saved origin.

Host tests cover full-world serialization, tile-selective writes, origin restore, and
partial world capture. On-device autosave is active; one 18-sector write took 2.266878
seconds on the low-priority background task. Drawing restore through battery shutdown
still needs a direct visual check. Power loss inside the 500 ms idle window can lose the
newest changes.

## Battery and power

The AXP2101 reports percentage, battery voltage, charging direction, and USB power over
the touch controller's I²C bus. TinyDraw samples it every five seconds while touch is
idle, keeping PMU traffic out of the drawing path. The top-right badge is a passive
overlay: strokes remain in the canvas beneath it and touch passes through it.

The CO5300 requires even transfer-window bounds. An odd-width battery refresh distorted
the badge; aligning all four bounds fixed it. The current rendered icon and percentage
are both exactly 17 pixels tall. The charging bolt is centered to the nearest half pixel.

The lower PMU button is device-verified: holding it for four seconds cuts battery power,
and a short press starts the board. This is a full shutdown and cold boot, not sleep.
Light and deep sleep are not implemented.

## Larger canvas and export experiment

The drawing world is 736×896, four times the display area. Early panning copied the whole
viewport and took 71–74 ms per frame. Reading the world directly, swapping two pixels at a
time, using 80 MHz QSPI, and enlarging DMA chunks brought it to 9.8–10.1 ms. Random
colored lines later appeared on the panel. With Wi-Fi removed, they still recurred at
80 MHz. A 60 MHz build passed 30 seconds idle followed by drawing and fast panning without
an artifact. This points to display-bus timing rather than Wi-Fi, though more device time is
needed to call it conclusive. Current 60 MHz panning timing has not yet been recaptured.

A short-lived Wi-Fi prototype served the full world as a PNG. Its state and 512 KiB output
buffer lived in PSRAM. Light compression produced one 35,010-byte image; 4 KiB writes
prevented socket stalls. iOS connectivity and caching made the demo unreliable. Wi-Fi was
removed from the active firmware before the RP2350 port; it was not proven to cause the
reported display stripes.

## RP2350 port

The RP2350 board uses an SH8601 display, an FT3168 touch controller, and 520 KiB of
SRAM. One 368×448 framebuffer takes 329,728 bytes. The toolbar popup backup and
other static data bring BSS to 479,776 bytes, leaving no room for the ESP32's
larger canvas or Undo history.

The first pressure-aware XL build averaged 33.4 ms per update. Keeping pixels in
panel byte order reduced that to 17.0 ms. Fixed-point edge smoothing and cheap
inside/outside tests reached 9.44 ms, of which 8.84 ms was the full-screen QSPI
transfer.

A single 160×160 partial update worked in 1.394 ms. Repeated arbitrary rectangles
produced visible panel corruption. Full-width horizontal bands use contiguous
framebuffer rows and held up for 1,012 drawing updates without a physical artifact.
That run averaged 1.17 ms and peaked at 3.28 ms. A later run averaged 1.39 ms and
peaked at 3.23 ms.

Touch sampling runs on the second RP2350 core, so display transfers cannot block
I²C reads. The FT3168 normally supplies new coordinates every 14.8–16.3 ms, about
60–68 Hz. Polling is gated by the controller's active-low touch signal to avoid
reads after lift. A 100 Hz register experiment did not improve the report rate and
was removed. Fast motion can still leave 20–30 pixels between raw points; midpoint
curves smooth those gaps.

RP2350 shares ESP32's velocity-based width calculation and now uses the same 0.35
streamline setting. Its simpler raster permanently stamps quadratic circles. The
ESP32 renderer keeps a replaceable ribbon tail, which gives it cleaner curves and
lift behavior. An RP experiment that exposed 90% rather than 50% of the newest
segment made circles more angular and was reverted.

The USB framebuffer capture currently disagrees with the physical panel after
banded updates. Repeated captures are stable, while the panel remains correct.
Treat physical inspection and timing logs as the current RP2350 display evidence
until that diagnostic path is fixed.

## Verification

Twenty native test groups cover exact replays, snapshots, Perfect Freehand examples,
self-overlaps, seams, input states, UI, Undo, panning, and a 1,000-point XL workload. ASan,
UBSan, headless QEMU, and visible QEMU pass. Device logs report drawing, display, touch,
lift, panning, Undo, autosave, and power status. Physical checks cover battery reporting,
charging, deterministic panel startup, and PMU off/on.

## Current limits and next work

Fast diagonals can still show the stroke being drawn. Frame work can take 16.1 ms and the
next touch position arrives 13–14 ms later. A full-canvas Undo sends 329,728 bytes and is
visibly slower.

ESP32 persistence currently keeps one autosaved drawing. It has no document picker,
manual save slots, redundant crash-safe snapshot, or sleep mode. Battery percentage
refreshes every five seconds. The canvas remains a fixed 2×2 raster. The RP2350 has a
screen-sized canvas without persistence, Undo, or pan.
Fast RP2350 curves remain limited by the FT3168's roughly 60 Hz coordinate stream,
and its USB framebuffer capture needs repair.

## Five-minute demo

1. Draw one slow curve and one fast diagonal to show touch sampling limits.
2. Cross an XL stroke over itself, then make a sharp turn to show solid joins.
3. Extend the stroke to show that old points no longer slow new input.
4. Pan across the 2×2 world, then undo and demonstrate confirmed New.
5. Point out autosave, charging status, and battery-powered off/on.
6. Close with **4,479 ms to 220 ms** for ink and the measured **74 ms to 10 ms**
   80 MHz panning result; the stability build now uses 60 MHz.
