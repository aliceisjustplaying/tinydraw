# Curved authority glass receipt — 2026-08-16

Build: `931c7cf-dirty` on ESP32-S3 / CO5300 production V2 firmware.

## Change

Committed overview, resident-tile updates, and cold replay now follow the live
midpoint-quadratic centerline. Previously they joined stored samples with
straight capsules, so lift or zoom replaced the smooth preview with visibly
polygonal authority.

## Automated guards

- V2 host tests: 218/218 passed, including a sparse three-point curve pixel
  that live geometry covers and the former straight replay missed.
- Core host tests: 143/143 passed.
- Integration slice: 24/24 passed.
- Adversarial 400% host replay: five runs from 53.237 to 54.223 ms,
  53.636 ms mean, exact output on every run. The pre-change mean was
  48.659 ms, a 10.2% increase within the 15% experiment guard.
- ESP-IDF production build passed; app image retained 61% partition headroom.

## Owner glass pass

The owner drew circles and dense hairlines, lifted, cycled through 25%, 50%,
100%, 200%, and 400%, and panned at high zoom. Across the captured stroke
groups:

- event→submit mean: 1.471–1.820 ms; maximum: 7.186 ms;
- event→DMA mean: 2.118–2.755 ms; maximum: 8.584 ms;
- zero submit-over-16-ms or complete-over-33-ms samples;
- zero presentation failures, touch overflows, or authority mismatches;
- some aged touch events occurred during the mixed zoom/pan session (maximum
  54.269 ms), but none produced a visible completion-latency failure.

The prior accepted visual-first baseline was 1.89 ms event→submit and 3.22 ms
event→DMA on average, with 4.364/12.400 ms maxima. This pass therefore shows no
new visible-ink latency regression; submit maximum was noisier while DMA
maximum improved.

## Product verdict

Authority replacement is much improved and the former overt angular regression
is fixed. The owner still sees more angularity than desired in the live curve,
so smoothness remains **yellow**. There was no overt change at lift or zoom,
which isolates the remaining issue to the live midpoint curve itself.

An adaptive four-span curve experiment improved geometric smoothness but raised
400% host replay to 58.6–61.3 ms after batching, roughly 22% above the frozen
pre-change baseline. It was rejected and never flashed. Further smoothing must
share a cheaper curve raster or arrive with the settled-AA pass without
reopening ink latency or cold replay.
