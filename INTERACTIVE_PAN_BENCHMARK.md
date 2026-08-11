# Interactive vector-cache pan benchmark

> **Throwaway physical prototype.** This benchmark answers a cache-prefetch
> question; it is not the production infinite-canvas implementation.

## Question

While real finger panning keeps the existing direct `WorldCanvas` → AMOLED
path, can a low-priority vector renderer fill nearby raster cache bands before
the viewport reaches them at 50%, 100%, and 200% zoom?

This first experiment measures one 3×3 prefetch window. It does **not** yet
implement sliding cache rebasing, eviction, vector persistence, or vector Undo.
A pass proves useful prefetch runway and fill-rate-versus-finger-speed behavior,
not indefinite travel.

## Workload

Each cache-sized screen cell is rendered as the same synthetic workload of
1,000 intersecting handwriting strokes. This deliberate periodic workload keeps
viewport density comparable at every cache position and zoom. It is not claimed
to be one spatially coherent 9,000-stroke document.

The center screen is ready before interaction begins. Missing neighboring
32-row bands contain a magenta/yellow checkerboard. A priority-1 renderer on
core 1 fills whichever missing band is closest to the current viewport while
the real priority-5 touch task continues polling on that core. Cache publication
and direct display reads share a mutex.

## Run

```sh
./scripts/esp32 interactive-pan-benchmark /dev/cu.usbmodem101
```

When the normal canvas appears, start panning immediately. Checkerboard exposed
by a drag is a cache miss. Use the existing size popup as benchmark controls:

- **S:** 50% zoom
- **M:** 100% zoom
- **L:** 200% zoom
- **XL:** stop background rendering and persist the report

At each zoom, make several ordinary and deliberately fast drags in every
direction. Select XL only after all desired rounds.

Read the report without opening a serial monitor:

```sh
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem101 \
  read-flash 0x90e000 0x2000 /tmp/tinydraw-interactive-pan.bin
tr '\0' '\n' < /tmp/tinydraw-interactive-pan.bin
```

Reading flash resets the board. Restore normal firmware afterward.

## Recorded evidence

For each zoom the report stores:

- direct cache-to-display average, median, p95, and maximum;
- touch-event-to-display-completion average, median, p95, and maximum;
- gesture/frame counts;
- cache-miss frames and maximum missing visible pixels;
- maximum observed finger-pan velocity;
- center-ready and full-3×3-cache render time.

Timing samples are capped at the first 256 frames per zoom; totals and miss
counts continue across all frames.

## Interpretation

A practical pass is:

- direct presentation remains near the measured 25.45 ms path at every zoom;
- ordinary drags rarely expose checkerboard;
- fast-drag misses are brief and refinement catches up after slowing or lifting;
- touch-event latency does not grow without bound while rendering.

A failure is normal dragging repeatedly exposing large checkerboard regions or
cache publication contention materially slowing direct presentation. An
ambiguous result should lead to a sliding/rebasing prototype, not immediate
production migration.
