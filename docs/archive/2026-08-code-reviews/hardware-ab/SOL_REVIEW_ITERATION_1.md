# Sol review iteration 1

Date: 2026-08-12. Reviewer model: GPT-5.6 Sol, high reasoning, read-only review.
Reviewed state: commit `caed9b5` plus the then-current settled-renderer working tree.

This is an independent review artifact. Findings are evidence, not proof. Line numbers refer to the reviewed state and may have moved.

(•̀ᴗ•́)و# Verdict

Request changes. The immutable-fallback mechanism works for an unchanged document, but three blockers prevent accepting the current correctness and performance claims. I found no path that publishes known-invalid raster during the tested unchanged-document zoom cycle.

## Blockers

1. **LOD can remove visible pen and eraser geometry.** [`simplify_stroke_samples()`](core/src/stroke_lod.cpp:22) drops points using distance from the last retained point without measuring curvature. A tight loop, hook, or hairpin contained within nine world units collapses to its endpoints; at 200% this can remove geometry roughly 18 screen pixels away from the replacement chord. The second filter in [`settled_render_region()`](core/src/settled_renderer.cpp:247) is worse: it drops close samples without considering radius, so a stationary pressure peak or eraser dab can disappear entirely. Painter order remains intact, but stroke presence and eraser coverage do not. Replace this with an error-bounded, zoom-bucketed simplifier preserving curvature, direction reversals, radius extrema, and endpoints; remove the pressure-blind second filter. Add loop, hairpin, pressure-pulse, pen/eraser, and endpoint image tests.

2. **The reported settled gate is not physical settled latency.** [`render_task_entry()`](esp32/main/interactive_pan_benchmark.cpp:575) stores `settled_us` when cache bands become ready, before the full viewport is submitted at [line 631](esp32/main/interactive_pan_benchmark.cpp:631). The hardware log reports 200% at 463–464 ms and then spends about 52 ms in publication ([log](second_review_hardware_ab/settled-lod-auto-zoom.log:101)); final DMA completion is later still. Therefore the “200% passes <500 ms” conclusion in [RESULTS.md](second_review_hardware_ab/RESULTS.md:131) is unsupported and likely false. Capture the last settled submission sequence and publish `settled_us` only after its completion callback, guarded by generation, revision, camera, and viewport provenance.

3. **`PhysicalDisplay` has a cross-core C++ data race.** The render task calls `push_rect()` while holding the cache mutex, but [`update_toolbar()`](esp32/main/hardware_app.cpp:959) calls `display.set_toolbar()` before taking that mutex. Refinement events invoke it at [line 1438](esp32/main/hardware_app.cpp:1438), while subsequent runway/exact publication is already running on the other core. `set_toolbar()` mutates `toolbar_`, `overlay_`, and dirty flags while [`push_rect()`](esp32/main/hardware_app.cpp:380) reads them. `reset_timing()` also races with renderer updates to the timing counters. Put every display-state mutation, staging operation, and metric snapshot behind one display scheduler/mutex; callbacks should enqueue immutable publication records.

## High

1. **A post-mutation zoom can repin a partial atlas as “complete.”** Commit invalidates the pin at [line 1327](esp32/main/interactive_pan_benchmark.cpp:1327), but `set_zoom()` validates only source bands needed for the next visible region and then unconditionally exchanges and marks that source pinned at [line 1113](esp32/main/interactive_pan_benchmark.cpp:1113). A rapid zoom can therefore promote an active atlas whose offscreen bands remain invalid, recreating later refusal loops. Track explicit `invalid/partial/complete` source state and pin only after every nonblank band is current; otherwise refuse without exchanging or incrementally repair the pinned source.

2. **Settled publication is not revalidated after acquiring the cache lock.** The generation check occurs before `xSemaphoreTake()` at [line 627](esp32/main/interactive_pan_benchmark.cpp:627); the display push has no check inside the critical section. A zoom or stroke cancellation arriving in that window still queues an obsolete full-screen push and delays cancellation by roughly the 52 ms publication cost. Recheck generation, revision, camera, paused state, and pinned viewport after locking.

3. **Cancellation remains unbounded inside a stroke or capsule.** [`settled_render_region()`](core/src/settled_renderer.cpp:206) checks only between strokes. A long LOD chord can rasterize a large rectangle before observing cancellation, directly affecting first-feedback latency. Check cancellation at bounded capsule/row intervals and make partially rendered scratch unpublishable.

4. **New user strokes never receive the advertised append-time LOD.** The offset/count arrays are sized to the initial 1,000 strokes at [lines 957–958](esp32/main/interactive_pan_benchmark.cpp:957). The first appended stroke has index 1,000, so the condition at [line 1321](esp32/main/interactive_pan_benchmark.cpp:1321) always fails. Its temporary simplification is discarded and all appended strokes use raw samples. Size metadata to `document.stroke_capacity()`, record explicit raw-fallback state, and test near-capacity mutation.

5. **The main 100% bottleneck is repeated band reconstruction.** Every one of twelve visible bands repeats macrogrid query, stroke traversal, projection, LOD reads, and capsule setup at [lines 554–604](esp32/main/interactive_pan_benchmark.cpp:554). The log attributes about 399 ms to rasterization and 85 ms to compositing. Project geometry once for a visible supertask, bin row spans/microtiles, and publish bands from that shared geometry.

6. **Display staging remains inside long cache critical sections.** `set_zoom()` holds `cache_mutex` from [line 1044](esp32/main/interactive_pan_benchmark.cpp:1044) through all resampling and strip queueing at [line 1197](esp32/main/interactive_pan_benchmark.cpp:1197). Settled publication similarly holds it across the full 17-transaction push. Pin `(arena, generation, revision, rect)` under the lock, release it, stage into owned DMA buffers, then submit.

7. **No state-machine regression test covers the new ownership invariant.** The hardware cycle validates only one immutable document and does not inject document mutation, pan, cancellation at each publication boundary, telemetry-ring pressure, or allocation failure. Extract the two-arena coordinator behind a host-testable storage/display interface and exhaustively test generation changes before render, before lock, after copy, after submit, and before completion.

## Medium

- [`SettledRenderOptions`](core/src/settled_renderer.cpp:215) accepts stale or malformed LOD maps without document revision or stroke-count provenance. A valid zero count silently removes a stroke. Validate the complete map once and reject/fallback the whole LOD atomically.

- [`rasterize_capsule()`](core/src/settled_renderer.cpp:55) converts unbounded finite projected floats to `int` before clamping. `VectorDocument` accepts arbitrary finite coordinates/radii, so large cameras or samples can overflow those casts. Range-check projections and clamp in floating point first.

- The claimed 563 KiB PSRAM reserve is measured before benchmark allocations ([hardware_app.cpp](esp32/main/hardware_app.cpp:843), [log](second_review_hardware_ab/settled-lod-auto-zoom.log:88)). The new scratch, timing, index, and LOD arenas consume roughly another 347 KiB. Print free/largest blocks after benchmark initialization and report allocated LOD capacity, not only 77,436 used bytes.

- Existing report metrics remain censored: invalid pan requests are rejected before recording at [line 1397](esp32/main/interactive_pan_benchmark.cpp:1397); `center_ready()` still requires all 448 center-cell rows at [line 434](esp32/main/interactive_pan_benchmark.cpp:434); drawing timings still cover individual raster updates, not pen-down cancellation through physical completion. Record requested/accepted/refused pan events and physical visible-exact/draw endpoints.

- The completion ISR stores a 64-bit C++ atomic timestamp at [hardware_app.cpp:126](esp32/main/hardware_app.cpp:126). The current ESP ELF lowers this to `__atomic_store_8`, which takes IDF’s global atomic spinlock. IDF’s `portENTER_CRITICAL_SAFE` makes it ISR-compatible, but it adds avoidable ISR latency and global contention. A 32-bit modular microsecond timestamp is sufficient for these sub-second measurements.

- Source validation retains a one-pixel “bilinear” halo despite using nearest resampling at [line 400](esp32/main/interactive_pan_benchmark.cpp:400), causing avoidable conservative refusals. It also rescans all strokes per destination band. Derive exact nearest sample bounds and cache provenance/blank summaries per source band.

- Runway and exact publication still enqueue one refinement event per band at [lines 688 and 733](esp32/main/interactive_pan_benchmark.cpp:688). These events trigger redundant toolbar reconstruction and amplify the display race. Coalesce them to one generation-scoped notification.

## Nits

- The `minimum_screen_sample_spacing` comment says it applies only without precomputed LOD, but the implementation applies it to both.
- `fallback_source_pinned` and its comments imply completeness even when the post-mutation path can pin partial content.
- The squared edge ramp uses `max(inner², 0)`; when `inner < 0`, the intended lower squared boundary is zero, not `inner²`.
- `committed_index >= strokes.size()` at [line 1309](esp32/main/interactive_pan_benchmark.cpp:1309) is unreachable after the preceding empty check.

## Confirmed correct

- For an unchanged revision, the first exchange leaves the initial exact atlas in `materialization_storage`, and later rendering writes only the other arena. Source and destination spans do not alias.
- Readiness/revision snapshots are captured after refinement quiesces; source validation rejects noncurrent ink bands and permits only vector-proven blank gaps.
- Per-band settled/exact cache publication rechecks generation before copying and stores revision before ready quality.
- The nearest fallback is staged synchronously into owned DMA buffers, so later cache writes cannot mutate an in-flight transfer.
- Painter order remains document order, candidate bitsets preserve ordering, and the standalone simplifier retains first/last samples.
- New allocation cleanup paths are balanced, and buffer-size checks prevent the observed workload from overflowing the LOD and scratch arenas.
- Current debug and ASan/UBSan binaries both pass 133/133 tests. The current ESP benchmark ELF exists and contains the reviewed code. No files were modified.
