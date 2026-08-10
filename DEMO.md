# TinyDraw demo — 5 minutes

Use one slide: the performance table in `FINDINGS.md`. Keep the board in your hand.
If the board is unavailable, run the macOS app or QEMU replay.

## 0:00 — Draw while introducing it

> This is a 368×448 AMOLED drawing app on an ESP32-S3. It has variable-width ink,
> twelve colors, panning, ten Undos, and a canvas four times the screen area.

## 0:30 — Explain the original problem

> The first version redrew the entire stroke whenever the finger moved. A 500-point
> host stroke took 4.5 seconds, and every new section cost more than the last.

## 1:00 — Show the development loop

> Before the hardware arrived, I built a native macOS simulator around the same C++
> drawing core. SDL replaces the AMOLED and touch controller with a small window and
> mouse input. It still uses the real 368×448 resolution and opens near the physical
> size of the 1.8-inch display.

> Recorded touch sequences run without a window and produce exact image snapshots.
> That gave me a fast edit-test loop with sanitizers. QEMU is a separate layer that
> boots the ESP-IDF firmware and checks its 8 MB memory setup. The board is still the
> final test for touch rate, display transfer time, power, and visual behavior.

## 1:40 — Four drawing beats

1. Draw a slow curve, then a fast diagonal.
   > The touch controller gives about 75 positions per second. Width is simulated from
   > speed: slow movement is thicker and fast movement is thinner.

2. Cross an XL stroke over itself, then make a sharp turn.
   > Coverage is combined before color is applied, which keeps overlaps solid. Rounded
   > joins prevent chopped corners on thick handwriting.

3. Scribble continuously for several seconds.
   > Old sections become final, so a long stroke does not make each new point slower.

4. Pan around the 736×896 world, then Undo across views.
   > Panning streams the visible part of the world directly to the display.

## 2:50 — Explain why it is fast

> The main optimization was refusing to repeat work.

- Streaming geometry processes only the newest stroke section.
- Dirty 32×32 tiles redraw and transfer only changed pixels.
- Small, frequently used coverage data stays in internal RAM.
- Large canvases and Undo history stay in PSRAM.
- Touch sampling runs on the second CPU core.
- Three DMA buffers queue display transfers.
- Panning reads directly from the larger world instead of copying a viewport first.

Key numbers:

- Host 500-point stroke: **4,479 ms → 220 ms**
- First physical update: **19.9 ms average, 72.9 ms worst**
- Current long strokes: about **2.5–3.4 ms average**
- Panning: **71–74 ms → about 10 ms per frame**

## 3:40 — Why the result is trustworthy

> The shared drawing core has 20 native and end-to-end test groups, exact image
> snapshots, ASan, UBSan, QEMU coverage, and timing captured on the physical board.

## 4:10 — Current limits

- Very fast diagonals can reveal the live drawing update.
- Very fast pans can briefly tear because the panel has no synchronized framebuffer swap.
- Drawings are not persistent after power loss.
- The current world is fixed at 2×2 screens; 3×3 is the next practical size.

## If someone asks

- **Is it pressure sensitive?** No. Width is simulated from movement speed.
- **Why not double-buffer?** The CO5300 accepts pixel updates, not framebuffer swaps.
  A full flip still transfers 329,728 bytes.
- **Why three display buffers?** They total 49,152 bytes and let transfers queue.
- **Why a coverage mask?** It prevents pale holes where a stroke overlaps itself.
- **Memory?** The world uses 1.32 MB, two viewport canvases use 659 KB, and ten Undo
  entries use 3.44 MB of the board's 8 MB PSRAM.
