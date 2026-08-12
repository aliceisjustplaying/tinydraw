# TinyDraw vector canvas: architecture and optimization review request

## Requested decision

Please perform a fresh, skeptical architecture review of this repository at commit `4bf944987a5fcc600b885628e6d53bd15ecd8957` on branch `prototype/vector-materialized-cache`.

We need a candid recommendation among:

1. **Continue the current architecture** — vector authority plus incrementally materialized raster caches.
2. **Pivot within vector authority** — retain the vector document but materially change renderer/cache/LOD strategy.
3. **Pivot to another authority model** — for example sparse raster authority or a hybrid command/checkpoint representation.
4. **Stop pursuing broad zoom on this hardware** — if the measured ceiling makes the intended experience unrealistic.

A dead-end conclusion is acceptable, but distinguish an architectural limit from an immature prototype. Likewise, do not recommend continuing merely because substantial work has already been invested.

## Product target

Hardware:

- ESP32-S3, two 240 MHz cores
- 8 MiB octal PSRAM at 80 MHz
- 16 MiB flash
- 368×448 RGB565 AMOLED over 60 MHz QSPI
- touch polling on core 1

Desired product:

- vector-authoritative drawing;
- large bounded canvas rather than mathematically infinite;
- likely fixed power-of-two zoom levels, ideally about 12.5%–800%;
- drawing p95 under 10 ms;
- direct pan p95 under 35 ms;
- first valid zoom pixels under 100 ms;
- visibly settled zoom under 500 ms in a realistic 1,000-stroke document;
- zero checkerboard/uninitialized pixels;
- exact/canonical convergence may happen later;
- slight rendering differences are acceptable for significant performance gains;
- Undo/history, persistence, and final export design are deferred.

A brief pixelated zoom is acceptable; several seconds before visually acceptable output is not.

## Current architecture

- `VectorDocument` is intended to become authoritative.
- Existing `WorldCanvas` storage is repurposed as a disposable 3×3 raster cache.
- Pan remains a direct strided display push from cached RGB565 pixels.
- Zoom immediately resamples valid source raster into a visible fallback.
- Background jobs refine 32-row bands using the canonical vector renderer.
- A macrogrid supplies conservative candidate-stroke sets.
- Cache bands track invalid/derived/settled/exact quality and document revision.
- Invalid bands cannot be presented; pan currently stops at unfinished edges.
- Live drawing remains raster-fast while recording vector samples, then invalidates affected bands on commit.

The 3×3 cache is a prototype pan runway, not necessarily the final cache shape or document bound.

## Latest hardware result

The latest coherent 1,000-stroke benchmark is documented in:

`benchmark-results/materialized-cache/zoom-publication-v2/RESULTS.md`

Key measurements:

| Measurement | Result |
|---|---:|
| Invalid/missing pan frames | 0 across 620 frames |
| 50% first physical valid | 179 ms |
| 100% first physical valid | 175 ms |
| 200% first physical valid | 233 ms |
| 200% settled fallback preparation | 174 ms |
| 200% visible-center exact | 11.1 s |
| 200% full 3×3 exact | 14.0 s |
| Pan direct p95 | about 33.8 ms |
| 200% event-to-present p95/max | 252/421 ms |
| Drawing p95/p99 | 5.1/7.2 ms |
| Zoom failures | 0/12 |
| Maximum cancellation | 43.7 ms |

Earlier key evidence:

- Production raster pan is approximately 25.45 ms because it only changes an origin and streams a strided raster window.
- Exact full-viewport reconstruction ranges from hundreds of milliseconds to several seconds depending on stroke geometry.
- A prior prototype showed invalid cache exposure and failed zoom-out; the latest iteration eliminated those failures.
- Cheap fallback now arrives in 175–233 ms, while exact refinement remains far too slow.

Please treat the raw benchmark text and source as authoritative over narrative summaries where they differ.

## Questions

### Architecture and viability

1. Do the measurements support continued investment in vector authority plus materialized raster caches?
2. Is the current result evidence of a viable but unoptimized system, or are the targets internally inconsistent on this hardware?
3. Which measured behaviors reflect hard lower bounds, and which are implementation artifacts?
4. What concrete future observation should trigger a stop or pivot decision?

### Optimization opportunities

5. Audit the current renderer and coordinator for the highest-leverage optimizations. Estimate plausible speedup ranges and identify dependencies or correctness costs.
6. Can first valid physical zoom realistically fall below 100 ms? Consider regional/pipelined resampling, display transfer overlap, reduced precision, DMA behavior, cache layout, and avoiding unnecessary RGB565 work.
7. How should visible refinement be reorganized so visually settled output reaches under 500 ms even when canonical output takes seconds?
8. Could an intentionally noncanonical settled renderer—centerline strokes, lower supersampling, scanline fill, zoom-specific LOD, geometry simplification, or another method—produce acceptable output quickly enough?
9. How should pan-direction prediction, runway refill, rebasing, and quality degradation work so repeated human swipes do not stall or reveal invalid pixels?
10. Which work should move to append time or idle time? In particular, assess geometry caches, per-stroke multiresolution representations, incremental tile updates, and precomputed low-resolution overview levels.
11. Would different tile/band dimensions or a ring/tile cache materially improve throughput or only scheduling?
12. Are there viable ESP32-specific techniques being missed: fixed-point/SIMD instructions, DMA overlap, internal-RAM staging, PSRAM access patterns, task/core placement, or panel transfer tricks?

### Alternative designs

13. Compare the current design against:
    - complete low-resolution overview plus one active high-resolution sliding cache;
    - a sparse multiresolution raster tile pyramid with vector reconstruction only for misses;
    - raster authority plus retained vector/command metadata;
    - display-list or geometry checkpoints;
    - any better architecture you identify.
14. Can ordered pen and eraser operations update cached zoom levels incrementally without replaying the whole document?
15. Given 8 MiB PSRAM, what cache/document/renderer memory budget would you recommend?
16. Is 12.5%–800% feasible with fixed levels and on-demand tiles, or should the product constrain the range further?

### Next experiment

17. Propose the smallest next hardware iteration that most strongly tests your recommendation. Specify:
    - exact code/design changes;
    - workloads;
    - metrics and instrumentation;
    - pass/fail gates;
    - estimated implementation effort;
    - what each possible result would imply.

## Requested response format

1. **Verdict:** continue, pivot, or stop, with confidence.
2. **Evidence:** strongest supporting and contradicting measurements.
3. **Performance ceiling:** realistic warm, cold, ordinary, and pathological expectations.
4. **Optimization ranking:** prioritized table with expected impact, effort, and risk.
5. **Architecture recommendation:** concrete data flow and memory layout.
6. **Next decisive experiment:** bounded implementation and gates.
7. **Stop conditions:** explicit falsifiable criteria.
8. **Code findings:** file/line references for important bugs, wasted work, races, or optimization seams.

## Reading order

Start with:

1. `ORACLE_REVIEW_REQUEST.md`
2. `benchmark-results/materialized-cache/zoom-publication-v2/RESULTS.md`
3. `DECISIVE_VECTOR_CACHE_PROTOTYPE_PLAN.md`
4. `VECTOR_CANVAS_SECOND_OPINION_BRIEF.md` (historical evidence; some status is superseded)
5. `V2_PHASE1_FINDINGS.md`
6. `V2_PHASE2_PROTOTYPE_FINDINGS.md`
7. source under `core/`, `esp32/main/`, and `tests/`

Important implementation files:

- `core/src/viewport_renderer.cpp`
- `core/src/raster_materializer.cpp`
- `core/src/stroke_macrogrid.cpp`
- `core/include/tinydraw/graphics/viewport_renderer.h`
- `core/include/tinydraw/graphics/world_canvas.h`
- `esp32/main/interactive_pan_benchmark.cpp`
- `esp32/main/hardware_app.cpp`
- `tests/viewport_renderer_test.cpp`
- `tests/raster_materializer_test.cpp`
