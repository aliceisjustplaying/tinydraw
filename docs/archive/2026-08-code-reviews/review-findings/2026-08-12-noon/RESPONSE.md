(•̀ᴗ•́)و## 1. Verdict

**Pivot within vector authority — option 2 — with about 85% confidence.**

Keep the ordered vector document as the authoritative representation. Stop developing the current double-buffered, camera-aligned 3×3 RGB565 atlas as though it were the production cache. Replace it with:

1. A complete low-resolution overview.
2. A fixed-memory, world-aligned raster tile ring for the active zoom.
3. A deliberately fast, noncanonical **settled renderer**.
4. The existing high-quality renderer as an idle **exact renderer**.
5. Append-time geometry LOD and incremental tile updates.

I do **not** recommend raster authority yet, and I do **not** recommend abandoning broad zoom. The strongest evidence says the present failures come from avoidable initialization, scheduling, repeated work, and measurement problems—not from an inherent vector-authority ceiling.

That recommendation also fits the product contract: drawing and pan have strict latency requirements, while canonical convergence may occur later and slight rendering differences are acceptable. 

The existing architecture was a useful prototype. It proved direct raster pan, progressive validity, and vector reconstruction. Its job is now complete.

---

## 2. Evidence

### Strongest evidence supporting continued vector authority

| Evidence                                                                         | Interpretation                                                                     |
| -------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------- |
| Drawing update p95/p99: **5.1/7.2 ms**                                           | The live raster path is already inside the target.                                 |
| Direct pan p95: **about 33.8 ms**                                                | A warm raster cache can meet the stated pan gate.                                  |
| No reported zoom request failures                                                | The latest coordinator fixed earlier hard transition failures.                     |
| Zero known-invalid pixels among 620 presented pan frames                         | Validity tracking is operational, although this metric is censored by pan refusal. |
| Earlier physical prototype showed a full approximate preview in **64.7–65.7 ms** | Sub-100-ms preview is already demonstrated on the same hardware class.             |
| Earlier 1,000-stroke 200% refinement completed in about **1.2 s**                | The current 11–14 seconds is not an immutable vector-reconstruction floor.         |

The latest headline measurements are documented in the review request. 

The earlier 65-ms result is especially important: `V2_PHASE2_PROTOTYPE_FINDINGS.md:75-90` reports approximately 19–20 ms of nearest-neighbor preview generation followed by a physical display result at approximately 65 ms. That is stronger evidence about the feasibility of the first-valid target than speculation about future optimization.

### Strongest evidence against the current cache/coordinator

| Evidence                                                   | Interpretation                                                                                       |
| ---------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| First reported valid zoom: **175–233 ms**                  | Current immediate path fails the target by roughly 2×.                                               |
| 200% event-to-present p95/max: **252/421 ms**              | Pan can visibly stop at unfinished cache edges.                                                      |
| 200% center/full exact: **11.1/14.0 s**                    | Canonical replay is scheduled and partitioned poorly for interactive refinement.                     |
| Entire inactive 3×3 atlas is cleared before each zoom      | Approximately 3 MB of unnecessary PSRAM writes are on the critical path.                             |
| Benchmark document is exactly 1,000 × 12 generated samples | It is useful as a regression workload, but is not yet evidence for a realistic handwriting document. |
| Only 50%, 100%, and 200% were exercised                    | Nothing in this run validates 12.5%, 25%, 400%, or 800%.                                             |
| Two giant raster arenas consume most PSRAM                 | There is insufficient production headroom for vector growth, LOD, checkpoints, and robust caching.   |

---

## 3. Benchmark and instrumentation corrections

Several benchmark labels are materially wrong or incomplete. These should be corrected before using the numbers for a go/no-go decision.

### 3.1 “First physical valid” is not a physical-completion timestamp

`hardware_app.cpp:1109-1112` records the timestamp after `display.push_world()` returns.

`PhysicalDisplay::push_world()` divides 372 rows into approximately 22-row transactions at `hardware_app.cpp:411-419`. `push_rect()` then waits for a staging buffer, copies and byte-swaps pixels, and calls `esp_lcd_panel_draw_bitmap()` at `hardware_app.cpp:325-389`.

The ESP-IDF LCD transaction API queues color transfers; actual transmission occurs through DMA and interrupts, and the completion callback is the point at which a color buffer may be recycled. ([Espressif Systems][1])

Therefore, the current metric is approximately:

> input event → all visible chunks prepared and submitted, with queue backpressure included

It is **not**:

* time to first physical zoom pixel;
* time to first completed strip;
* or time to final visible transfer completion.

With the configured queue depth of three, several final transactions can still be in flight when the metric is recorded.

The next benchmark needs three separate endpoints:

1. First strip submitted.
2. First strip transfer-complete callback.
3. Last visible strip transfer-complete callback.

### 3.2 Zoom timing excludes cancellation

`interactive_pan_benchmark.cpp:786-820` starts `event_started` only after waiting for the old render generation to cancel. The report separately shows cancellation as high as 43.7 ms, but that delay is absent from the first-valid and fallback measurements.

A user-visible zoom metric must start when the input is received, before cancellation.

### 3.3 The reported p95 is based on only the first 256 of 599 frames

The timing arrays have capacity 256 at `interactive_pan_benchmark.cpp:37,51-55`. `record_frame()` stops retaining samples after that capacity at `interactive_pan_benchmark.cpp:1068-1079`.

The 200% report says:

* `frames=599`
* `samples=256`

Thus the p95 is the p95 of the **first 256 chronological frames**, not the full gesture run. It may overstate or understate the actual tail.

Use at least a 1,024-entry ring for this experiment, or use streaming histograms. The supplied patch raises the temporary capacity to 1,024.

### 3.4 Zero missing frames does not mean zero pan stalls

`interactive_pan_benchmark_view_changed()` rejects an origin whenever `missing_pixels()` is nonzero:

* `interactive_pan_benchmark.cpp:1059-1065`
* `interactive_pan_benchmark.cpp:593-615`

Rejected movements never reach `record_frame()`. Consequently, `miss_frames=0` proves that no known-invalid pixels were presented. It does **not** prove that every requested pan was presented.

The user-visible failure appears instead as the 252-ms p95 and 421-ms maximum event latency. Add:

* requested frames;
* accepted frames;
* refused frames;
* refusal duration;
* refusal reason;
* overview-fallback frames.

### 3.5 “Settled” has different meanings depending on zoom direction

For zoom-in, `settled_us` is recorded immediately after fallback preparation, before the display push:

* `interactive_pan_benchmark.cpp:923-927`

For 100% and lower levels, it is recorded after `record_zoom_present()`:

* `interactive_pan_benchmark.cpp:933-946`

Those values are not comparable. At 200%, the report’s 174-ms “settled” value is a preparation timestamp; visually settled output cannot have appeared before the later 233-ms presentation timestamp.

Define settled operationally:

> Every pixel in the currently visible physical region has quality ≥ settled for the current document revision, and the final affected transfer has completed.

### 3.6 “Center exact” is not “visible exact”

`center_ready()` requires all fourteen 32-row bands in the 448-row center cache cell:

* `interactive_pan_benchmark.cpp:387-395`

Only 372 rows are displayed. More importantly, `choose_job()` gives zero interval distance to some neighboring seam bands, so neighboring jobs can run before the offscreen bottom of the center cell:

* `interactive_pan_benchmark.cpp:398-420`

The reported 11.1 seconds can therefore materially overstate the time at which the actual visible 372 rows became exact.

Track these independently:

* focal region exact;
* visible rows settled;
* visible rows exact;
* center cell exact;
* complete runway exact.

### 3.7 Drawing p95 is an inner-loop metric

The timer surrounds only `canvas.raster().update()`:

* `hardware_app.cpp:1475-1483`
* `hardware_app.cpp:1533-1541`

It excludes:

* waiting for background rendering to stop during pen-down;
* vector mutation setup;
* final raster finish;
* full viewport capture into `WorldCanvas`;
* vector commit and cache invalidation.

`interactive_pan_benchmark_begin_stroke()` can wait for rendering for up to two seconds before the measured update begins at `interactive_pan_benchmark.cpp:949-965`.

The 5.1-ms p95 is encouraging, but it is not the product’s event-to-visible drawing latency.

### 3.8 The workload should not be called realistic handwriting

`populate_coherent_handwriting()` creates deterministic sine-wave strokes with exactly twelve samples each:

* `interactive_pan_benchmark.cpp:180-216`

That workload should be called something like:

> synthetic periodic handwriting, 1,000 strokes × 12 samples

The current vector capacity is 1,100 strokes and 16,384 samples, or only 14.9 samples per stroke at full stroke capacity:

* `hardware_app.cpp:69-71`

That is not a realistic production capacity for 1,000 captured strokes unless touch samples are aggressively coalesced or compressed.

---

## 4. Hard limits versus implementation artifacts

### Actual or near-actual limits

**Panel payload.** The firmware configures a 60 MHz, four-data-line transfer at `hardware_app.cpp:203-213`. An ideal 368×372 RGB565 payload is therefore approximately:

[
368 \times 372 \times 16 \div (60,000,000 \times 4) \approx 9.1\text{ ms}
]

A complete 368×448 payload is approximately 11.0 ms before commands, transaction gaps, staging, contention, and driver overhead. Quad mode is defined as transmission over four data lines. ([Espressif Systems][2])

This means panel bandwidth alone does **not** make a 35-ms pan or 100-ms first feedback target inconsistent. The real path is slower because it performs PSRAM reads, internal-buffer copies, byte swapping, queue waits, and many transactions.

**Exact rendering scales with visible ink.** No renderer can guarantee sub-500-ms canonical reconstruction for arbitrary stroke count, radius, overlap, and sample count. A document can intentionally cover every pixel with many ordered pen and eraser operations.

**Memory is fixed.** A full raster pyramid from 12.5% to 800% is impossible in 8 MiB. Higher levels must be sparse and evictable.

**Ordered eraser semantics impose work.** If an old operation changes or is removed, affected tiles must replay from a checkpoint or earlier operation. Append-only pen and eraser updates are much cheaper.

### Current implementation artifacts

The following are not hard limits:

* A complete inactive-atlas clear before every zoom.
* Bilinear RGB565 filtering on the first-feedback path.
* Reconstructing geometry separately for every 32-row publication band.
* Filling invalid runway bands before visible settled refinement.
* Measuring the whole center cache cell instead of the physical viewport.
* Omitting the previously proven two-lane renderer executor.
* Holding the cache mutex during display staging and queue waits.
* Rescanning all strokes to prove source-region validity.
* Stopping pan instead of degrading to overview-derived pixels.
* Running toolbar work through the touch event queue after every published band.
* Retaining two camera-aligned 3×3 raster arenas.

### The biggest avoidable cost: the full inactive-atlas clear

At `interactive_pan_benchmark.cpp:875-888`, every zoom clears:

[
1104 \times 1344 \times 2 = 2,967,552\text{ bytes}
]

before writing only the visible approximately 274 KB.

The repository previously measured a 330 KB PSRAM viewport fill at about 9.2 ms, approximately 36 MB/s (`V2_PHASE1_FINDINGS.md:40-47`). At that rate, the 2.97 MB clear costs roughly **82 ms**.

That explains a large portion of the failed 100-ms gate:

* Current 100% fallback preparation: about 116 ms.
* Estimated avoidable clear: about 82 ms.
* Remaining preparation: roughly 34 ms.
* Current reported preparation-plus-submit result: about 175 ms.

For 200%, removing the clear and replacing bilinear fallback with the already-proven nearest preview should plausibly move the full first preview below 100 ms.

My confidence that **first valid physical pixels can fall below 100 ms is approximately 90%**.

---

## 5. Performance ceiling

These ranges distinguish measured results from engineering estimates. The estimates assume the recommended overview/tile/LOD architecture and are not claims about the supplied patch.

| Situation                                    | Current evidence                                            | Realistic target/expectation                                                                   |
| -------------------------------------------- | ----------------------------------------------------------- | ---------------------------------------------------------------------------------------------- |
| Warm drawing update                          | 5.1-ms p95 inner raster update                              | **6–10 ms** event-to-dirty-submit for ordinary strokes; callback should be measured separately |
| Warm pan, cache hit                          | 33.8-ms p95 queue-return                                    | **25–35 ms** submit path; roughly **30–50 ms** final callback depending on staging             |
| Zoom, first completed valid strip            | Not measured                                                | **40–90 ms**                                                                                   |
| Zoom, complete visible fallback              | Reported queue-return 175–233 ms                            | **65–130 ms** after no-clear nearest preview and pipelining                                    |
| Ordinary visible settled, ≤400%              | Not consistently measured                                   | **150–450 ms** with LOD/2×2 or analytic settled rendering                                      |
| Ordinary 800% settled                        | Unmeasured                                                  | Approximately **300–700 ms**; should have a separate launch gate                               |
| Ordinary visible exact                       | Current “center” 1.8–11.1 s depending on run and definition | Approximately **1–3 s**, allowed as idle convergence                                           |
| Dense or long-stroke exact                   | Earlier dense cases 2.7–5.2 s; current atlas up to 14 s     | **5–15+ s** is plausible and should remain nonblocking                                         |
| Cold load with persisted overview/checkpoint | Not implemented                                             | First valid screen in approximately **50–150 ms**, exact reconstruction later                  |
| Cold load with only raw vector replay        | Initial code waits for full atlas, up to 30 s               | Seconds; unsuitable as the production cold path                                                |

A several-second exact result is not itself a reason to abandon vector authority. A several-second **settled** result would be.

---

## 6. Optimization ranking

Effects are nonadditive. “Scope” is a rough estimate for one engineer already familiar with the codebase.

| Priority | Change                                                                     |                                                   Plausible impact |                     Scope | Main risk/dependency                                  |
| -------- | -------------------------------------------------------------------------- | -----------------------------------------------------------------: | ------------------------: | ----------------------------------------------------- |
| P0       | Add transfer-completion timestamps and uncensored interaction metrics      |                        No direct speedup; prevents wrong decisions |              0.5–1.5 days | ISR-safe bookkeeping                                  |
| P0       | Remove interaction-time 3×3 clears                                         |                            **70–95 ms per zoom** in current design |                 0.5–1 day | Must preserve known-white/validity invariant          |
| P0       | Use nearest-neighbor first preview                                         |                        **3–8×** faster than current bilinear stage |                 0.5–1 day | More pixelated temporary output                       |
| P0       | Generate and submit 22/32-row strips as they become ready                  |                             First feedback roughly **2–4× sooner** |                  1–2 days | Transfer ordering and completion tracking             |
| P0       | Define and prioritize actual visible settled rows                          |        Potential **3–8×** reduction in relevant refinement latency |                  1–2 days | Scheduler rewrite                                     |
| P0       | Never refuse ordinary pan; use overview-derived fallback                   |                                     Removes 250–420-ms edge stalls |                  2–5 days | Requires complete overview or equivalent valid source |
| P0       | Release cache mutex before display queue waits                             |                  Lower interaction tail and less renderer blocking |                  1–2 days | Need slot/version pinning                             |
| P0       | Remove/coalesce refinement events from the touch queue                     |                                     Lower input/event interference |                 0.5–1 day | None substantial                                      |
| P1       | Restore the proven two-lane tile executor                                  |              Approximately **1.2–1.7×** canonical renderer speedup |                  1–2 days | Core-1 contention; keep below touch priority          |
| P1       | Add fast 2×2-coverage settled mode                                         |                      Approximately **1.8–3×** raster-stage speedup |                  2–4 days | Visual differences                                    |
| P1       | Add centerline/capsule or analytic scanline settled renderer               |                        Approximately **3–6×** for suitable strokes |                  4–7 days | Larger visual/correctness surface                     |
| P1       | Append-time multiresolution centerlines and bounds                         |             Approximately **2–5×** ordinary refinement improvement |                  4–8 days | Memory and simplification policy                      |
| P1       | Decouple render supertasks from publication tiles                          |        **1.5–3×** less geometry/query overhead in narrow-band work |                  2–4 days | Scratch and cancellation granularity                  |
| P1       | Complete overview plus world-aligned tile ring                             | Removes clears/rebases and invalid-edge stalls; frees multiple MiB | 5–10 days for a prototype | Architectural migration                               |
| P2       | Cache validity proofs and add whole-source fast paths                      |          Roughly **10–60 ms** on transitions depending on document |                  1–2 days | Revision correctness                                  |
| P2       | Repair macrogrid overflow behavior or replace it with tile operation lists |                          **1.1–3×** only in index-degenerate cases |                  2–4 days | Painter-order preservation                            |
| P2       | Precompute resampler x maps and split interior/edge loops                  |                                        **1.5–2.5×** bilinear stage |                  1–3 days | First path should use nearest anyway                  |
| P2       | Bounded fixed-point camera and geometry transforms                         |                    Approximately **1.1–1.3×** overall exact render |                  2–5 days | Golden-image differences                              |
| P2       | Benchmark panel-endian tiles and PSRAM-direct DMA                          |                         Perhaps **5–30%** display-path improvement |                  2–4 days | PSRAM direct DMA has a documented speed limit         |
| P2       | IRAM/SIMD/assembly tuning                                                  |       Perhaps **10–50%** in selected kernels, much less end-to-end |       2–5 days per kernel | Internal RAM pressure and maintenance                 |

ESP-IDF specifically recommends avoiding double precision on ESP32-S3 because it is software-emulated, and recommends fixed-point/integer arithmetic where practical. It also describes IRAM as useful but limited and traded against DRAM. ([Espressif Systems][3])

The current bounded-canvas product makes the use of `double` in every `camera_project()` call particularly questionable:

* `core/src/camera.cpp:13-20`
* `core/include/tinydraw/graphics/camera.h:7-18`

For a 4,096- or 8,192-unit bounded world, Q16.16, Q20.12, or ordinary local `float` coordinates are sufficient. Fixed power-of-two zoom levels make the transform even simpler.

ESP-DSP’s published benchmarks show that optimized ESP32-S3 assembly can substantially outperform portable implementations for some packed numeric kernels, but that evidence does not imply the same multiplier for this renderer. Profile first. ([Espressif Systems][4])

---

## 7. Architecture recommendation

### 7.1 Data flow

```text
Ordered vector operation log
        |
        +--> append-time bounds + sample coalescing + LOD centerlines
        |
        +--> complete coarse overview, incrementally updated
        |
        +--> resident world-aligned tiles, updated in operation order
                     |
                     +--> overview-derived quality
                     +--> fast settled renderer
                     +--> canonical exact renderer during idle
                                  |
                                  v
                         internal DMA strip buffers
                                  |
                                  v
                                panel
```

### 7.2 Authoritative representation

Keep an immutable ordered operation log containing:

* pen/eraser operation ID;
* color/tool;
* raw or compressed samples;
* conservative bounds;
* multiresolution centerline offsets/counts;
* generation or clear epoch.

The live stroke should be separate from the committed document. Drawing must not pause the background renderer before pen-down. Render the live stroke into a transient overlay or current visible tiles, then atomically append it on release.

The current `begin_stroke()` cancellation barrier is acceptable for a prototype but not for production interaction.

### 7.3 Complete overview

Maintain one complete low-resolution raster that is always valid for the document revision.

Assuming the current macrogrid’s implied 4,096×4,096 world bound:

* 12.5% overview: 512×512 RGB565.
* Memory: **512 KiB**.

For an 8,192×8,192 world:

* 12.5% overview: 1,024×1,024 RGB565.
* Memory: **2 MiB**.

That difference is load-bearing. The intended world bound must be fixed before freezing the memory design. For an 8,192-unit world, consider:

* RGB332 overview at 1 MiB;
* a complete 6.25% level plus sparse 12.5%;
* or compressed/flash-backed overview tiles.

Use the overview for:

* immediate zoom fallback;
* missing high-resolution pan tiles;
* cold startup;
* zoom-out;
* navigator/minimap behavior;
* never-checkerboard guarantees.

### 7.4 Active tile cache

Use world-aligned keys:

```cpp
struct TileKey {
  uint8_t level;      // 12.5, 25, 50, 100, 200, 400, 800
  int16_t tile_x;
  int16_t tile_y;
};

struct TileMetadata {
  uint32_t document_revision;
  uint32_t applied_operation_id;
  RasterQuality quality;  // invalid, overview, settled, exact
  uint16_t pin_count;
};
```

A practical starting point is a **64×32 RGB565 publication tile**:

* 4,096 bytes per tile.
* Visible 368×372 region needs at most 6×12 = 72 tiles.
* A 128-slot ring costs 512 KiB.
* That leaves 56 slots for directional runway and transition overlap.

Keep the existing 32×32 coverage microtile internally. Do not force one dimension to serve all three purposes.

Recommended separation:

| Role                      | Suggested size                                   |
| ------------------------- | ------------------------------------------------ |
| Coverage/raster microtile | 32×32                                            |
| Cache/publication tile    | 64×32 or 64×64                                   |
| Geometry render supertask | Approximately 128×96 or 128×128                  |
| Panel publication chunk   | Current 22-row capacity, or measured alternative |

A render supertask should generate geometry once, bin it into multiple microtiles, and publish smaller cache tiles as they complete. Progressive publication should not imply progressive geometry reconstruction.

### 7.5 Zoom flow

On zoom input:

1. Record the input timestamp before cancellation.
2. Retag or acquire destination tile slots; do not clear a giant atlas.
3. Produce a nearest-neighbor preview from the overview or best available source.
4. Start at the gesture focal region, then proceed center-out.
5. Submit each panel strip immediately.
6. Record first and last completion callbacks.
7. Render all visible tiles with the settled renderer.
8. Fill predicted pan runway.
9. Run exact visible tiles.
10. Run exact offscreen/cache tiles only when idle.

A conceptual version of the critical path is:

```diff
- clear_entire_inactive_3x3_atlas();
- resample_bilinear(full_visible_region);
- exchange_complete_atlas();
- push_complete_visible_region();
- schedule_all_invalid_runway();
- schedule_exact_bands();

+ for (Rect strip : focal_center_out_strips(visible, 22)) {
+   materialize_nearest_from_best_valid_source(strip);
+   submit_with_completion_token(strip);
+ }
+ schedule_visible(Quality::Settled);
+ schedule_predicted_runway(Quality::Overview);
+ schedule_visible(Quality::Exact);
+ schedule_idle_cache(Quality::Exact);
```

### 7.6 Pan behavior

Pan should never stop at an unfinished exact or settled edge.

Use an exponential moving average of finger velocity and prioritize by estimated time-to-edge:

* approximately 0.75–1.0 viewport ahead;
* approximately 0.25 viewport behind;
* small cross-axis margin;
* aggressively cancel prefetch after direction reversal.

On a high-resolution cache miss:

1. Materialize the tile immediately from the complete overview.
2. Mark it `Overview`.
3. Present it.
4. Replace with `Settled`.
5. Replace with `Exact` during idle.

The visible quality may temporarily degrade. Validity must not.

A ring cache should reassign slot metadata as the camera crosses tile boundaries. It should not rebase by clearing, copying, or exchanging a multi-megabyte camera-aligned raster.

### 7.7 Settled renderer

I recommend testing two settled modes.

**First experiment: 2×2 coverage.** This is the smallest change relative to `CoverageTile` and should reduce sampling work materially while preserving the existing primitive model.

**Likely production path: simplified centerline/capsule rasterization.** At append time, retain centerlines simplified for each fixed zoom level. For a settled tile:

* select the appropriate simplification;
* rasterize variable-radius line segments/capsules;
* use simple edge coverage or 2×2 sampling;
* preserve operation order and eraser behavior.

Use screen-space simplification criteria such as:

* projected displacement below approximately 0.5 pixel;
* radius change below approximately 0.25 pixel;
* bounded angular error;
* conservative bounds expanded for antialiasing.

The current 4×4 supersampled ribbon renderer remains the exact path. The repository’s own phase instrumentation says rasterization is 60–80% of runtime (`V2_PHASE1_FINDINGS.md:49-61`), so reducing coverage work has much more leverage than further tuning the macrogrid.

### 7.8 Incremental pen and eraser updates

Yes, ordered append-only pen and eraser operations can update cached zoom levels without replaying the full document.

For every committed operation:

1. Assign a monotonic operation ID.
2. Determine intersected overview and resident cache tiles.
3. Rasterize the operation onto those tiles in document order.
4. Set each tile’s `applied_operation_id`.
5. Queue nonresident neighboring levels during idle.

This is valid under the current opaque white background model, where eraser behaves as ordered background-color painting.

For future transparency or layers, an eraser may expose content below rather than paint white. Then affected tiles need checkpoint replay.

Undoing or editing an older operation similarly requires:

* a per-tile checkpoint every N operations;
* operation deltas;
* or replay from the last checkpoint.

Command/checkpoint representation is therefore a useful complement to vector authority, not a reason to replace it.

### 7.9 Memory budget

The current major PSRAM allocations are approximately:

| Allocation                                    | Approximate size |
| --------------------------------------------- | ---------------: |
| 3×3 `WorldCanvas`                             |         2.83 MiB |
| Ten-slot raster undo arena                    |         3.28 MiB |
| Committed + visible viewport buffers          |         0.63 MiB |
| Display overlay                               |         0.31 MiB |
| Subtotal before vector data and other objects |     **7.05 MiB** |

Relevant code:

* `core/include/tinydraw/graphics/world_canvas.h:21-25`
* `core/include/tinydraw/graphics/tile_undo_history.h:28-35`
* `esp32/main/firmware_canvas.cpp:19-30`
* `esp32/main/firmware_canvas.cpp:87-93`

The benchmark repurposes the undo arena as the second 3×3 materialization arena. That is clever for a prototype, but demonstrates why it is not a production cache shape.

For a 4,096-unit bounded world, I would target:

| Production allocation                    |          Budget |
| ---------------------------------------- | --------------: |
| Complete 12.5% RGB565 overview           |        0.50 MiB |
| Active tile ring                         |        0.50 MiB |
| Previous-level/transition/prefetch slots |   0.25–0.50 MiB |
| Live/visible raster buffers              |   0.45–0.65 MiB |
| Compressed vector document + LOD + index |   0.75–1.25 MiB |
| Renderer geometry/scratch                |   0.40–0.70 MiB |
| Tile checkpoints/undo                    |   0.50–0.80 MiB |
| Overlay and miscellaneous PSRAM          |   0.35–0.50 MiB |
| **Required free reserve**                | **1.5–2.0 MiB** |

The production target should be:

* steady-state PSRAM allocation no more than approximately 6.0–6.5 MiB;
* free PSRAM at least 1.5 MiB;
* largest free block at least 512 KiB.

The raw vector document also needs a different capacity strategy. Store fewer input samples and use compact coordinates rather than assuming twelve samples per stroke.

---

## 8. Alternative architecture comparison

| Design                                                                     | Assessment                                                                                                                                                               |
| -------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Current camera-aligned 3×3 atlas                                           | Good proof of direct pan; poor final memory shape; expensive clearing/reuse; awkward invalid edges. Retire after the next experiment.                                    |
| Complete overview + one active world tile ring                             | **Recommended.** Best combination of validity, bounded memory, pan behavior, and vector authority.                                                                       |
| Sparse multiresolution raster pyramid with vector reconstruction on misses | Also recommended in a limited form: complete only the lowest overview, sparse/evictable at higher levels.                                                                |
| Full raster pyramid                                                        | Impossible within 8 MiB.                                                                                                                                                 |
| Raster authority + retained vector metadata                                | Fastest simple append behavior, but compromises deterministic vector reconstruction, future editing, and export semantics. Current evidence does not justify this pivot. |
| Display-list or geometry checkpoints                                       | Strong complement to the recommended architecture. Useful for load, undo, and reducing replay.                                                                           |
| Hybrid command/checkpoint authority                                        | A reasonable later evolution if persistence or undo replay becomes the dominant problem; not needed to solve current zoom latency.                                       |

### Zoom range

Fixed 12.5%–800% levels are feasible only with on-demand high-resolution tiles.

I would implement all seven level identifiers now:

```text
12.5, 25, 50, 100, 200, 400, 800
```

But I would gate product launch at 400% until 800% passes the realistic workload. An 800%-only failure should constrain the range, not invalidate vector authority.

Fixed power-of-two levels are particularly favorable here:

* simple world-aligned tile keys;
* exact level relationships;
* inexpensive nearest upsampling/downsampling;
* simpler fixed-point camera transforms;
* predictable LOD selection.

---

## 9. Next decisive hardware experiment

This should be a bounded modification to the existing coordinator, not the complete tile-cache migration.

**Estimated engineering scope: approximately 3–5 focused days**, excluding extended visual tuning.

### Exact changes

1. Apply the supplied correctness/hot-path patches.
2. Add sequence-numbered LCD completion callbacks.
3. Record input time before cancellation.
4. Remove the interaction-time full-atlas clear.
5. Use nearest-neighbor for first preview at every direction.
6. Generate focal/center-out 22- or 32-row strips and submit each immediately.
7. Define visible settled over exactly the physical 372 rows.
8. Schedule visible settled before any offscreen derived or exact work.
9. Restore the Phase 2 two-lane executor.
10. Add a 2×2 settled-renderer mode.
11. Remove per-band refinement events from the touch queue.
12. Track pan refusal and overview-fallback counters separately.

This experiment can continue using the existing atlas storage. Its purpose is to test whether:

* the display/preview path can meet 100 ms;
* and a cheaper renderer can meet 500 ms.

Only after that result should the full tile ring be implemented.

### Workloads

Run all of these:

| Workload                                           | Purpose                                   |
| -------------------------------------------------- | ----------------------------------------- |
| Current synthetic 1,000×12 workload                | Regression comparability                  |
| Captured realistic 1,000-stroke document           | Product evidence                          |
| Realistic sample-count distribution                | Vector capacity and geometry cost         |
| Dense 100-stroke overlapping curves                | Raster worst case                         |
| One 900-sample long/self-overlapping stroke        | Large-stroke fallback complexity          |
| Border and out-of-grid strokes                     | Macrogrid fallback and source validity    |
| 50↔100↔200 rapid repeated zoom, at least 50 cycles | Cancellation and arena reuse              |
| 25↔400 and 100↔800 transitions                     | Actual range evidence                     |
| Repeated high-velocity pan reversals               | Prediction and cancellation               |
| Draw and erase during refinement                   | Mutation latency and revision correctness |
| Near-capacity vector document                      | Allocation and failure behavior           |

### Instrumentation

Record:

* input received;
* cancellation finished;
* preview strip ready;
* strip submitted;
* strip transfer completed;
* first visible-valid callback;
* last visible-valid callback;
* visible settled callback;
* visible exact callback;
* mutex wait and hold times;
* display staging and queue-wait time;
* bytes cleared/read/written;
* strokes tested and selected;
* samples processed;
* primitives and primitive-tile visits;
* accepted/refused/fallback pan frames;
* touch queue depth and dropped events;
* pen-down to first dirty submit/callback;
* release to vector commit completion;
* free PSRAM and largest block.

### Pass/fail gates

Use at least 50 measured transitions per zoom level.

| Gate                                    |                     Required result |
| --------------------------------------- | ----------------------------------: |
| Input → first completed valid strip p95 |                             ≤100 ms |
| Input → first completed valid strip max |                             ≤150 ms |
| Input → complete visible valid p95      |                         ≤150–180 ms |
| Input → visible settled p95             |                             ≤500 ms |
| Known-invalid/checkerboard pixels       |                                   0 |
| Ordinary pan refusal                    |                                   0 |
| Pathological pan refusal                |        ≤1%, none longer than 100 ms |
| Pan queue-return p95                    |                              ≤35 ms |
| Pan final-completion p95                | Report separately; target ≤45–50 ms |
| Pen event → dirty submit p95            |                              ≤10 ms |
| Pen event → dirty completion p95        |                              ≤15 ms |
| Cancellation p95/max                    |                           ≤10/50 ms |
| Free PSRAM                              |                            ≥1.5 MiB |
| Largest free PSRAM block                |                            ≥512 KiB |

Exact convergence should be reported but should not be a pass/fail gate unless it blocks interaction.

### What each result implies

**Preview and settled both pass:** proceed with vector authority and implement the overview/tile ring.

**Preview passes, settled fails:** display path is solved; invest specifically in centerline LOD, analytic coverage, and geometry reuse.

**Preview still fails after removing the clear and using nearest strips:** the staging/display path is the bottleneck. Abandon the camera-aligned atlas immediately and prototype direct overview/tile publication.

**Settled still exceeds 500 ms at 200–400% on realistic content after LOD and 2×2/analytic rendering:** constrain document density or pivot toward a command/checkpoint or raster-hybrid authority.

**Only 800% fails:** launch with a 400% maximum.

---

## 10. Stop conditions

These are the falsifiable conditions I would use.

1. **Stop investing in the current 3×3 atlas as a production design now.** One final instrumented experiment is justified; more incremental atlas tuning is not.

2. **Pivot away from vector reconstruction for interactive settled output** if overview + world tile cache + LOD/fast renderer still produces visible-settled p95 above 500 ms at 200% and 400% on a captured realistic 1,000-stroke document.

3. **Pivot the display/cache path** if no-clear nearest preview cannot produce a first completed valid strip under 100 ms while instrumentation shows rendering is no longer dominant.

4. **Constrain pan behavior or cache footprint** if ordinary human swipes still produce more than 1% fallback/refusal events or any stalls longer than 100 ms.

5. **Constrain content or change vector encoding** if a realistic 1,000-stroke document plus LOD/index cannot fit while retaining at least 1.5 MiB free PSRAM.

6. **Redesign mutation isolation** if background work causes pen event-to-submit p95 above 10 ms after introducing a pending-stroke snapshot.

7. **Cap zoom at 400%** if only 800% misses the settled gate.

8. Do **not** stop solely because canonical exact output takes seconds. That is explicitly compatible with the stated product target.

---

## 11. Important code findings

| Severity | Location                                        | Finding                                                                                                | Consequence/status                                                                            |
| -------- | ----------------------------------------------- | ------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------- |
| Critical | `interactive_pan_benchmark.cpp:875-888`         | Clears the complete 2.97 MB inactive atlas on every zoom                                               | Approximately 82 ms of avoidable critical-path traffic; patched                               |
| Critical | `hardware_app.cpp:1109-1112`, `325-419`         | “First physical” recorded after queueing all chunks, not from transfer callback                        | Benchmark endpoint is mislabeled                                                              |
| High     | `interactive_pan_benchmark.cpp:786-820`         | Event timer starts after cancellation                                                                  | Hides up to 43.7 ms of user latency; patched                                                  |
| High     | `interactive_pan_benchmark.cpp:37,1068-1079`    | p95 retains only first 256 frames                                                                      | 200% p95 excludes 343 later frames; temporary capacity patch supplied                         |
| High     | `interactive_pan_benchmark.cpp:1041-1048`       | Draw total continues after sample array is full, but average divides by retained count                 | Average becomes incorrect after capacity; patched                                             |
| High     | `interactive_pan_benchmark.cpp:1059-1065`       | Invalid pan origins are refused before recording                                                       | Zero-miss result is censored                                                                  |
| High     | `interactive_pan_benchmark.cpp:923-927,933-946` | Settled timestamp semantics differ by zoom direction                                                   | Values are not comparable                                                                     |
| High     | `interactive_pan_benchmark.cpp:387-420`         | Center readiness includes offscreen rows; scheduler can run neighboring seam jobs first                | “Center exact” overstates visible exact; revision checks patched                              |
| High     | `interactive_pan_benchmark.cpp:491-538`         | All source-derived invalid bands are filled before canonical work begins                               | Runway production delays visible refinement                                                   |
| High     | `interactive_pan_benchmark.cpp:553-564`         | Every 32-row exact job invokes a fresh renderer pass                                                   | Repeats candidate queries and raw-sample geometry                                             |
| High     | `interactive_pan_benchmark.cpp:540-544`         | Current coordinator omits `ViewportRenderOptions::execute`                                             | Proven dual-core tile compositing is disabled                                                 |
| High     | `interactive_pan_benchmark.cpp:511-529,569-577` | Cache lock is held while preparing/submitting display work                                             | Pan, toolbar, and publication can block behind display queue waits                            |
| High     | `hardware_app.cpp:953-974`                      | Requested pan origin is sent to benchmark before `WorldCanvas` clamps it                               | Benchmark/cache origin can diverge at boundaries; patched                                     |
| High     | `interactive_pan_benchmark.cpp:949-965`         | Stroke-start timeout clears mutation flag but leaves coordinator paused                                | Failed pen-down can strand rendering; patched                                                 |
| High     | `interactive_pan_benchmark.cpp:983-1024`        | Subtracts one before proving the stroke list is nonempty; offscreen invalid jobs retain stale revision | Underflow/stale-state risk; patched                                                           |
| High     | `interactive_pan_benchmark.cpp:345-384`         | Source coordinates are already pixels, but bilinear halo is divided by source zoom                     | At high source zoom the validity proof can under-check a required neighboring sample; patched |
| High     | `core/src/viewport_renderer.cpp:238-278`        | Off-region primitives consume arena capacity before being rejected                                     | Narrow renders can fall into expensive large-stroke path; patched and regression-tested       |
| High     | `core/src/viewport_renderer.cpp:298-415`        | Large-stroke fallback reconstructs the entire stroke once per touched tile                             | Complexity approaches samples × tiles; add long-stroke benchmark and segment indexing         |
| Medium   | `core/src/viewport_renderer.cpp:514-542`        | Coverage buffer is fully reset for every stroke range in a tile                                        | Ignores existing dirty-rect clearing; patched                                                 |
| Medium   | `core/src/coverage_tile.cpp:313-334`            | Fully opaque pixels still execute three alpha blends                                                   | Common interior fast path missing; patched                                                    |
| High     | `core/src/stroke_macrogrid.cpp:66-80`           | One out-of-grid stroke permanently enables all-strokes fallback                                        | A single border stroke disables the index globally                                            |
| Medium   | `core/src/stroke_macrogrid.cpp:103-106`         | Strict upper-bound comparison excludes strokes touching the exact grid edge                            | Easy accidental fallback at the document boundary                                             |
| High     | `hardware_app.cpp:69-71`                        | 16,384 samples for 1,100 strokes                                                                       | Only 14.9 samples/stroke at capacity; insufficient evidence for realistic content             |
| Medium   | `core/src/camera.cpp:13-20`                     | Software-emulated double arithmetic is used per projected sample                                       | Avoidable on a bounded canvas                                                                 |
| Medium   | `hardware_app.cpp:592-596,1337-1340`            | Every refinement publishes through the shared touch queue                                              | Event pollution and avoidable toolbar work                                                    |
| Medium   | `hardware_app.cpp:251-314`                      | `set_toolbar()` redraws the toolbar buffer even when no toolbar field changed                          | CPU work for every refinement event                                                           |
| High     | `interactive_pan_benchmark.cpp:771-783`         | Initial setup waits for complete exact 3×3 atlas, up to 30 seconds                                     | No viable production cold-start path                                                          |
| Low      | `third_party/pngenc/src/PNGenc.h:24-32`         | Checks nonstandard `__LINUX__` rather than normal Linux compiler macros                                | Host builds can incorrectly include `Arduino.h` unless a custom define is supplied            |

I did not find a clearly proven unsynchronized C++ data race in the central document/cache state. The more immediate concurrency problems are coarse lock scope, task scheduling, event pollution, and measurement endpoints.

---

## 12. Supplied diffs and validation

The combined patch contains two distinct classes of changes:

* host-tested renderer hot-path/capacity fixes;
* coordinator correctness and temporary benchmark fixes.

It is intentionally **not** the complete tile-ring architecture.

### Validation performed

* Combined patch dry-runs cleanly against the supplied snapshot.
* Host release build: all three CTest targets pass.
* Clang ASan/UBSan build: all three CTest targets pass.
* New regression test exercises a 900-sample stroke whose geometry is mostly outside a 32-row render region.
* I did not compile the coordinator patch with ESP-IDF or run it on the physical device.

The host band benchmark supports a restrained interpretation:

* At 200%, 32-row publication primitive count fell from **26,604 to 10,945**.
* Composited tiles fell from **336 to 168**.
* Host wall time remained around 38 ms, within noise.
* At 100%, the same case improved from approximately **56.5 to 52.9 ms**, about 6%.

So the renderer patch is primarily a capacity-cliff and wasted-work fix. It is not evidence of a major hardware speedup by itself.

### Downloads

* [Combined unified diff](sandbox:/mnt/data/tinydraw-review-suggested-changes.patch)
* [Host-tested renderer hot-path patch](sandbox:/mnt/data/tinydraw-safe-renderer-hotpath.patch)
* [Coordinator correctness and metrics patch](sandbox:/mnt/data/tinydraw-coordinator-correctness-and-metrics.patch)
* [Baseline band microbenchmark](sandbox:/mnt/data/tinydraw_review/band_bench_baseline.txt)
* [Patched band microbenchmark](sandbox:/mnt/data/tinydraw_review/band_bench_patched.txt)

Combined patch SHA-256:

```text
89606809a793368e1d058b04c84be146f80f48f1bf45a605c8d46bc71006e3cf
```

**Bottom line:** the hardware appears capable of the intended experience, but not with “canonical 32-row replay into a double-buffered camera-aligned 3×3 atlas” as the production path. Preserve vector authority, make a complete overview the validity foundation, make the tile cache world-aligned, and treat settled rendering as an explicitly different performance tier from exact rendering.

[1]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/index.html "https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/index.html"
[2]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/spi_lcd.html "https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/lcd/spi_lcd.html"
[3]: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/speed.html "https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/speed.html"
[4]: https://docs.espressif.com/projects/esp-dsp/en/latest/esp-dsp-benchmarks.html "https://docs.espressif.com/projects/esp-dsp/en/latest/esp-dsp-benchmarks.html"
