# Focused zoom-publication hardware result

Firmware: `e00bea3`.
Device: ESP32-S3, 8 MiB PSRAM, 368x448 RGB565 panel.
Workload: coherent 1,000-stroke vector document plus live strokes.
Raw report: `esp32s3-zoom-publication-v2.txt`.

## Recorded metrics

| Gate | Result | Status |
|---|---:|---|
| Invalid/missing pan frames | 0 across 620 frames | pass |
| 50% first physical valid | 179.246 ms | improved; fail <100 ms target |
| 100% first physical valid | 175.459 ms | fail <100 ms target |
| 200% first physical valid | 233.373 ms | improved from 365.291 ms; fail <100 ms target |
| 200% settled fallback | 174.396 ms | pass <500 ms target |
| 200% center exact | 11.102 s | too slow, but no longer blocks first presentation |
| 200% full 3x3 exact | 14.031 s | background/informational |
| Pan direct p95 | 100%: 33.781 ms; 200%: 33.803 ms | acceptable under 35 ms |
| 200% event-to-present p95 | 251.613 ms | fail; visible interaction stalls occurred |
| Drawing update p95 / p99 | 5.090 / 7.196 ms | pass <10 ms |
| Zoom request failures | 0 / 12 attempts | pass |
| Maximum zoom cancellation | 43.716 ms | pass |

The report retains only the latest transition timings for each zoom level, while attempt counters are cumulative.
No 50% pan gesture was recorded in this run.

## What improved

- Every recorded pan frame used valid raster content: misses fell from 163 in the prior run to zero.
- All 12 zoom requests succeeded, including transitions back from 200%.
- The cheap 200% fallback is now visible in 233 ms and is already settled-quality in 174 ms of preparation. The previous coarse result took 7.376 seconds and required a pan to become visible.
- Drawing remained fast with 1,000 existing strokes.
- Exact refinement is published without user pan.

## Remaining problems

- First physical zoom presentation is still 175-233 ms, about 2x the desired 100 ms target. The fallback computation itself takes 116-174 ms, then the display push adds roughly 59 ms.
- Exact 200% refinement takes 11 seconds for the visible center and 14 seconds for the atlas. This explains prolonged sharpening and some reports that rendering took seconds.
- At 200%, event-to-present p95 reached 252 ms and maximum 421 ms despite a 34 ms direct display p95. The validity guard prevents bad pixels by refusing movement into unfinished bands, so aggressive pan can visibly pause while background refinement catches up.
- Pan transfer rose from roughly 26 ms to roughly 30-34 ms. This is still usable, but the regression needs profiling.
- The visual glitches were not encoded individually in the report. The zero-miss metric rules out publishing bands known invalid; it does not rule out display-order, overlay, or fallback-quality artifacts.

## Interpretation

This is meaningful progress, not a final pass. The architecture now demonstrates all three essential interaction loops with valid content:

- drawing: under 10 ms p99;
- zoom fallback: under 250 ms and no multi-second blocking;
- pan: under 35 ms when cache runway exists, with zero invalid frames.

The seconds-long work is canonical exact refinement, not the initial zoom fallback. That means the next optimization target is narrow: make visible exact refinement incremental and prioritize the actual viewport, while improving runway production enough that the pan guard rarely activates.

## Decision

**CONTINUE.** This run improves the project case materially. It removes the prior hard failures—invalid frames, failed zoom-out, and seven-second first settled output. It does not yet meet the intended product experience.

Recommended next iteration:

1. Split fallback presentation into smaller visible strips and pipeline computation with display transfer to target first physical output under 100 ms.
2. Refine only the current physical viewport first, in smaller dirty bands, before filling the rest of the center cell or 3x3 atlas.
3. Prioritize refinement in the current pan direction and expose a pan-edge stall counter/duration in the report.
4. Capture publication timestamps per band and explicit refusal reasons so visual pauses can be correlated with exact work.
5. Re-test 50% pan, draw-then-zoom, and rapid L/M/S switching, with a short visual glitch log next to the numeric report.
