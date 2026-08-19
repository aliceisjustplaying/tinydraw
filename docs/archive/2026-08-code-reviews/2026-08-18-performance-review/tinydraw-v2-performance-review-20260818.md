# TinyDraw Vector V2 performance and correctness review

**Review date:** 2026-08-18<br>
**Reviewed snapshot:** `b63b07f905b40b3d435679911d2de89e57e1f062`<br>
**Primary target:** Waveshare ESP32-S3 Touch AMOLED 1.8, V2 hardware revision<br>
**Scope:** Vector V2, with Raster V1 and the archived prototypes used only as references

## Executive verdict

TinyDraw is not at the ESP32-S3's hard limit. The current approximately 400–515 ms cold-render range is the result of a strong renderer that still does too much *repeated* work: it scans irrelevant operations, rebuilds the same curve geometry for each tile or AA window, and sometimes converts local updates into full-screen transactions. The remaining headroom is therefore mostly architectural rather than another collection of isolated arithmetic tricks.

A roughly **250 ms general cold render is plausible**, especially for sparse and ordinary drawings, but it is not a credible target from micro-optimization alone. The path to it is:

1. spatially index operations before rasterization;
2. reuse prepared operation geometry across groups, windows, and quality tiers;
3. keep the hot raster kernels in proven-fast memory only where device measurements justify it;
4. remove avoidable composition and publication work;
5. make all foreground work genuinely preemptible.

I would not promise 250 ms on the adversarial dense-overlap and hairline corpora. Those cases contain real coverage work that no index can remove. My current expectation, subject to physical A/B measurements, is:

| Workload | Plausible result after the structural changes | Confidence |
|---|---:|---|
| Blank / nearly blank | Tens of milliseconds of compute; panel/presentation becomes dominant | High once the startup occupancy bug is fixed |
| Sparse ordinary drawing | 200–300 ms cold | Medium-high |
| General frozen corpus | 250–350 ms cold | Medium |
| Dense overlap / hairlines | 300–425 ms cold | Medium-low |

The user-visible responsiveness problem is more tractable than the cold wall. Several confirmed control-flow choices can add a full-screen refresh or a long non-preemptible unit immediately before a touch event. Fixing those should materially improve finger-on-glass feel even before cold compute changes.

The most important correctness/performance bug is at startup: ordinary bootstrap calls `publish_overview()`, which marks the entire world as occupied. That disables the “certainly paper” fast path globally, including for a blank document, until a New Document reset occurs. A second related issue makes occupancy monotonic after erase and history changes. Both should be fixed before interpreting any further sparse-document cold measurements.

## The four governing principles

The review converges on four rules that account for most remaining headroom:

1. **Preserve exact derived state instead of replaying authority whenever possible.** Undo/Redo, current/previous cache generations, and prepared geometry should all follow this rule.
2. **Index work before rasterizing it.** A bounding-box rejection after a PSRAM operation fetch is still work paid for every group and every AA window.
3. **A deadline is not preemption.** Every foreground unit must have an upper bound small enough to yield to input; checking a budget only between 76 ms windows does not create an 8 ms slice.
4. **A ring-rotated frame must not destroy locality.** Pan reuse is valuable only if local refinement, history damage, battery updates, and the next ink event remain local while the ring is active.

## Recommended implementation order

This is the order I would use, balancing payoff, risk, and the need to obtain clean measurements before deeper work.

| Order | Change | Expected payoff | Risk / cost |
|---:|---|---|---|
| 1 | Repair exact startup occupancy and affected-cell occupancy recomputation | Restores paper fast path; potentially very large sparse/blank cold win | Low |
| 2 | Add product-safe rerender/cold-cause counters and candidate counters | Makes all later work attributable | Low |
| 3 | Make ring-active region updates and live ink ring-aware | Removes intermittent full-screen catches after pan | Medium |
| 4 | Poll/drain touch before cosmetic work; add an urgent wake flag/notification | Cuts input event-age tails | Low-medium |
| 5 | Merge canvas/minimap/dock damage into one intentional presentation plan | Removes duplicate or oversized sweeps | Low-medium |
| 6 | Replace per-pixel composition division/modulo and the region double-copy | Small-to-medium recurring win | Low |
| 7 | Add an append-maintained spatial operation index | Largest cold/AA/Undo structural multiplier | Medium-high |
| 8 | Add bounded prepared-geometry reuse | Removes repeated sample decode, curve preparation, seed generation, and sort | Medium-high |
| 9 | Make settle, absorption, and full-frame composition resumable below the input-latency target | Eliminates 40–180 ms foreground stalls | Medium-high |
| 10 | Replace 25% tiled settle with one banded full-view pass | Avoids 42 full operation scans | Medium |
| 11 | Add current/adjacent history cache generations with copy-on-write slots | Makes common Undo/Redo a generation swap rather than a cold rebuild | High, but directly addresses a binding requirement |
| 12 | A/B targeted IRAM placement of the actual raster kernels | Stabilizes and may reduce compute time | Medium; scarce internal RAM |
| 13 | Replace O(slot × key) commit/eviction scans with stamps/free lists/CLOCK | Improves drain and worst-case boundedness | Medium |
| 14 | Evaluate a second low-priority render worker only after the above | Potential additional wall reduction | High synchronization and memory complexity |

## Detailed findings and proposals

### F1 — Normal startup disables the paper fast path across the entire world

**Severity:** P0 performance bug<br>
**Confidence:** Confirmed from the call path

At startup, the app fills the overview white, restores autosave authority when present, and always publishes through `MaterializedCanvas::publish_overview()`:

- `esp32/main/vector_v2/vector_v2_app.cpp:287`
- `esp32/main/vector_v2/vector_v2_app.cpp:374-380`

`publish_overview()` then does this:

- clears raw slots;
- clears uniform identities;
- fills every occupancy byte with `0xFF`.

See `vector_v2/src/materialized_canvas.cpp:295-321`, especially line 317. `certainly_paper()` trusts zero occupancy bits (`materialized_canvas.cpp:189-208`), and `TileProducer::produce_next()` consults the paper-group shortcut before beginning replay (`vector_v2/src/tile_producer.cpp:194-210`).

The result is that a normal blank boot, and an exact autosave replay, conservatively declare every occupancy cell non-paper. The fast path is not merely imprecise; it is effectively unavailable in ordinary product sessions until `reset_blank()` is invoked through New Document.

This does not corrupt pixels, but it can force operation replay or fallback handling for regions that are known to be paper. It also invalidates sparse-document benchmarks taken after normal bootstrap as evidence for the intended occupancy optimization.

**Fix**

Use the exact APIs for exact sources:

```cpp
if (!autosave_restored) {
  boot_ok = canvas.reset_blank(log.current_revision());
} else {
  boot_ok = replay_active_overview(log, startup_overview) &&
            canvas.restore_snapshot(log.current_revision(), startup_overview);
}
```

A safer API design would make the distinction impossible to miss:

- `publish_conservative_overview()` for a source whose paper status is unknown;
- `restore_exact_overview()` for a complete exact snapshot that rebuilds occupancy;
- `reset_blank()` for the blank case.

**Validation**

Add boot-path tests that assert:

- blank startup has zero occupied cells;
- an autosaved sparse document has the same occupancy map as a direct replay followed by `restore_snapshot()`;
- a blank high-zoom view publishes paper groups without scanning operations;
- pixel output remains unchanged.

Instrument `paper_groups_published`, `operations_scanned`, and occupied-cell count in normal product firmware. This finding should be fixed before establishing a new cold baseline.

### F2 — Occupancy only becomes dirtier after erase and history operations

**Severity:** P0/P1 performance degradation<br>
**Confidence:** Confirmed

`MaterializedCanvas::finish_revision()` marks the complete affected world bounds occupied (`materialized_canvas.cpp:472-482`). It never clears cells that have become paper. That is conservative for correctness, but it makes the paper map monotonic across erasing, Undo, Redo, and paint-over-paper transitions.

The class already has an exact `rebuild_occupancy_from_overview()` implementation (`materialized_canvas.cpp:264-279`), but it is only used by full snapshot restore. A full-map rebuild after every chunk would be wasteful; an affected-cell rebuild is cheap.

Each 16×16 world occupancy cell corresponds to only 4×4 pixels in the 25% overview. After an exact overview publication, recompute just the cells intersecting `affected_world_bounds`: clear each bit, inspect that cell's overview rectangle, then set it only if any overview pixel is non-paper.

This is especially important for eraser-heavy sessions and repeated Undo/Redo, where the current map can converge toward “everything occupied” even when the drawing becomes sparse again.

**Validation**

Add exact tests for:

- draw then erase completely to paper;
- erase only part of a cell;
- Undo an ink stroke back to paper;
- Redo and branch replacement;
- boundary cells at the world edge.

### F3 — Any local refresh while the frame ring is active becomes a full-screen refresh

**Severity:** P0 responsiveness bug<br>
**Confidence:** Confirmed

`VectorV2Presenter::refresh_region()` immediately falls back to `refresh()` when `frame_ring_bottom_ != 0` (`vector_v2_presenter.cpp:235-242`). Cached pan deliberately leaves the frame in a toroidal ring representation. Therefore, after a successful cached pan, all of these local events can materialize and re-present the complete frame:

- detail-tile refinement;
- settled AA publication;
- a drain swap;
- Undo/Redo damage;
- battery-region updates;
- other local chrome changes.

This explains a coherent class of intermittent stalls: the same action is cheap in a linear frame and unexpectedly full-screen after pan.

**Fix**

Implement a ring-aware local patch path:

1. intersect damage with the visible canvas;
2. compose the damage into ring coordinates, including pending authority overlay;
3. update the corresponding ring segments in `frame_`;
4. stage/present the logical panel rectangle while reading the ring through the existing wrap helper;
5. preserve `frame_ring_bottom_`, `frame_ring_`, and reusability on success.

Do not linearize the complete frame merely to update a rectangle. The existing `copy_ring_to_stage()` and ring row/column helpers already contain most of the coordinate machinery.

**Validation**

A physical sequence should become a permanent gate:

1. create a dense drawing;
2. cached-pan by a small delta;
3. allow one detail or AA tile to publish;
4. start a stroke immediately;
5. assert no full-refresh receipt, bounded touch age, correct ring pixels, and no tear regression.

### F4 — The first Down or Move after pan can synchronously materialize the full frame

**Severity:** P0 ink-feel bug<br>
**Confidence:** Confirmed

Both `show_start()` and `show_update()` call `refresh()` when the ring is active:

- `vector_v2_presenter.cpp:370-388`
- `vector_v2_presenter.cpp:391-435`

The first ink event after a cached pan can therefore pay a complete compose/present before showing the cap or ribbon update. That is precisely the sort of state-dependent lag a controlled straight-line trace can miss and a human notices as “sometimes the finger catches.”

Make provisional and committed live ribbon rendering ring-aware. The live overlay should be rendered in logical panel coordinates and staged through the ring mapping, without altering the canvas-only ring invariant. The current staging callback already paints provisional geometry on the DMA surface (`vector_v2_presenter.cpp:822-829`), so the main missing piece is to stop requiring a linear backing frame first.

### F5 — Pan performs a synchronous authority drain because exposed strips omit pending operations

**Severity:** P1 responsiveness issue<br>
**Confidence:** Confirmed

`begin_pan()` drains all pending operations before pan (`vector_v2_app.cpp:515-523`) because `compose_into_ring()` only calls `canvas_.compose_view()` and does not apply `overlay_pending_operations()` (`vector_v2_presenter.cpp:679-725`).

This is avoidable. Compose the pending-authority overlay into each exposed strip exactly as `compose_and_present()` already does. Then remove the pan boundary drain while retaining the history boundary, where authority ordering genuinely changes.

This change reduces pan-start latency and removes one more foreground path through whole-operation absorption. It must be tested with multi-chunk pen and eraser strokes whose canvas materialization lags authority.

### F6 — Input polling occurs after work that can take tens or hundreds of milliseconds

**Severity:** P0/P1 responsiveness issue<br>
**Confidence:** Confirmed

The main loop does not call `touch_sampler.read_next()` until `vector_v2_app.cpp:612`. Before that it may:

- refresh the complete frame for export host-ejection state (`:534-539`);
- refresh for time-sync state and dismissal (`:541-562`);
- update battery chrome (`:565-585`, which is full-screen when the ring is active because of F3);
- perform a zoom change and full refresh (`:593-606`).

The packet's observed 166–184 ms `poll_max` class and 66 ms Down event age are consistent with this ordering.

**Fix**

At the top of the loop:

1. sample the urgent/queue-nonempty flag;
2. drain a bounded set of queued touch events;
3. handle semantic input edges;
4. only then run cosmetic transitions, fill, settle, repair, and delays.

Cosmetic work should be deferred while the queue is nonempty or contact is active. A battery glyph can wait; a Down event cannot.

### F7 — The app consumes only one touch event per main-loop iteration

**Severity:** P1 latency-tail issue<br>
**Confidence:** Confirmed

The sampler queue already coalesces consecutive Move events while preserving Down and Up (`vector_v2/src/touch_event_buffer.cpp:83-96`), but the main app consumes only one event per loop. After a long presentation block, the queue may hold Down, Move, and Up. Processing one event, then running background work and a delay, artificially stretches the gesture even though the samples are already available.

Add a bounded drain loop. Preserve semantic edges in order, but collapse redundant Moves to the newest point before the next edge. A reasonable policy is “drain through the next edge, or at most N events / M microseconds,” with an immediate yield only when no input remains.

The queue already exposes `pending()` internally; surface a lock-safe `pending()` or atomic urgent flag from `VectorV2TouchSampler`.

### F8 — The touch task has no wake-up path for the main task

**Severity:** P1 input scheduling issue<br>
**Confidence:** Confirmed

The core-1 sampler enqueues under a critical section and then sleeps (`vector_v2_touch_sampler.cpp:104-129`). The main task polls and uses fixed 1–2 ms delays. There is no empty-to-nonempty notification.

Use a direct task notification or an atomic urgent flag plus notification on queue transition from empty to nonempty. The main task can block on the notification with a timer timeout when truly idle. This removes gratuitous polling delay and gives every long-running state machine a cheap `touch_urgent()` cancellation check.

The notification should not be sent for every coalesced Move; empty-to-nonempty is enough. Keep the touch task higher priority than any future renderer worker.

### F9 — The advertised work budgets are not latency budgets

**Severity:** P0 architecture issue<br>
**Confidence:** Confirmed

Three important paths check time only between coarse units:

- fill checks around `TileProducer::produce_next()` calls;
- settle checks only between complete 64×64 windows (`vector_v2_background_pipeline.cpp:343-386`), while one 25% window measured 76.416 ms;
- `absorb_pending_operation()` executes overview rasterization, affected-tile enumeration, visible retention, offscreen retention, and metadata commit as one call. Its deadline is consulted mainly by the offscreen retention pass (`incremental_document.cpp:240-287`, `:310-381`).

The “10 ms” or “8 ms” values are therefore admission/retention policies, not upper bounds on foreground occupancy.

**Fix**

Turn each into an explicit state machine with persistent cursors:

- **Settle:** candidate operations → prepared unit → chord/row → compositing spans → final fold rows → publish.
- **Absorb:** prepare overview rows → enumerate identities → visible uniforms → visible raw tiles → offscreen retention → metadata commit.
- **Full compose:** row bands → overlay rows → ready-to-present transaction.

Yield at a measured useful-work quantum, not at an arbitrary object boundary. The target quantum should be below the desired input-age tail—approximately 0.5–2 ms for most CPU work, with larger units allowed only where the panel transaction contract requires atomicity.

`apply_masked_operation_chord_rows()` already makes rows resumable, but it rebuilds the active chord list on each resume and guarantees at least one complete row even if that row exceeds `max_work_px` (`incremental_rasterizer.cpp:1186-1201`). Persist the `enter` cursor and active list, and consider horizontal segmentation only if a measured row still violates the desired bound.

### F10 — Undo/Redo rebuilds derived state from the complete active authority prefix

**Severity:** P0 product requirement<br>
**Confidence:** Confirmed

`move_history_incrementally()`:

1. prepares the target history state;
2. clears the affected overview patch to white;
3. walks every operation in the target active prefix;
4. bounding-box rejects operations outside the patch;
5. rerasterizes every intersecting operation;
6. commits the new overview and invalidates affected detail.

See `vector_v2/src/incremental_document.cpp:420-475`. The chrome controller then immediately refreshes canvas damage and separately presents the dock (`vector_v2_chrome_controller.cpp:292-324`).

The implementation is transactionally careful, but it guarantees replay cost and visible high-zoom cold detail.

#### Recommended design: adjacent history generations in the existing tile pool

Treat raw slots as **versioned tile objects**, not storage owned exclusively by the current revision.

- Keep a current identity directory and an adjacent-history identity directory.
- Before mutating a slot referenced by the adjacent generation, copy-on-write into another slot; preserve the old slot as the prior version.
- Preserve paper/uniform previous values in a compact side table or materialize them only for the visible/recent affected set.
- Keep a second overview generation.
- Undo/Redo swaps the active overview/directory generation, updates authority, and presents one bounded union of canvas + minimap + dock damage.
- In idle time, materialize the next likely history neighbor visible-first, creating a small “history runway” for repeated taps.
- A new stroke after Undo destroys the Redo generation and releases its unreferenced versions.

The product memory map makes a prototype unusually feasible:

- the inert blank-snapshot reservation is exactly one 368×448 RGB565 overview: **329,728 bytes**;
- the inert rerender-ledger reservation is **27,536 bytes**;
- a second `uint16_t[13,692]` raw identity directory is **27,384 bytes**, leaving 152 bytes in that reservation;
- five worst-case view footprints consume at most 280 of 448 raw slots, leaving 168 slots for copy-on-write versions before fallback.

This does not make the implementation literally free. Previous-generation uniform metadata needs a compact representation, and the two overview buffers need a coherent handoff strategy. A good approach is dirty-block copy-on-write or post-lift background synchronization, avoiding a 329 KB copy on Down. The central point is that the existing pool and inert reservations can hold an adjacent exact visual generation without adding another full tile atlas.

If slot pressure exceeds the copy-on-write allowance, preserve in this order:

1. currently visible affected tiles;
2. most recent view footprints;
3. current zoom near-viewport runway;
4. everything else falls back to exact overview and lazy reconstruction.

That makes the common visible Undo instant without pretending all historical high-resolution derivatives can remain resident forever.

**Required tests**

- multi-chunk whole-Stroke Undo;
- eraser over mixed colors and paper;
- repeated Undo/Redo through at least ten levels;
- branch replacement after Undo;
- adjacent generation slot pressure and eviction;
- failure atomicity during clone/publication;
- epoch/revision alias rejection;
- settled-to-immediate quality transitions;
- rapid repeated history taps before the next runway generation is ready.

### F11 — The largest cold-render multiplier is the absence of a spatial operation index

**Severity:** P0 structural performance opportunity<br>
**Confidence:** High

Every cold group starts with the entire active replay range (`tile_producer.cpp:259-293`). `gate_active_operation()` fetches each operation record newest-first and only then rejects it by bounds (`tile_producer.cpp:313-346`). Operation records and samples are allocated in PSRAM (`vector_v2_app_storage.cpp:81-82`).

Settled AA repeats the same pattern per 64×64 window (`settled_tile.cpp:64-75`). Undo also scans the complete target prefix. A bounding-box rejection is cheap arithmetic, but thousands of repeated PSRAM fetches and loop iterations are not free.

#### Recommended index

Use an append-maintained uniform world grid plus a large-operation list.

A 128-world-unit grid is a practical first point:

- world 1472×1792 → 12×14 = 168 cells;
- 168 cell heads cost only 336 bytes with 16-bit indices, or 672 bytes with 32-bit heads;
- operations are appended in painter order, so a per-cell linked posting list naturally traverses newest-first;
- a `uint16_t` query-stamp array for 4,000 operations costs 8 KB and deduplicates operations returned by several cells;
- each posting can be a packed 32-bit `{operation_index, next}`. At an average four touched cells per operation, postings are about 64 KB.

Operations spanning more than a threshold number of cells should go into a separate large-operation list or a coarser hierarchy rather than exploding postings. Query the cells intersecting a producer group/window, merge the large list, deduplicate, discard entries outside the active prefix, and then do the exact existing bounds and saturation gates.

The index must be an acceleration structure, never authority. Epoch and active-prefix rules remain final. On branch replacement, either truncate/rebuild affected posting tails or stamp postings by epoch and lazily discard stale entries.

#### Why this is the main route to 250 ms

The current renderer has already optimized the cost of painting an intersecting operation. The remaining general-workload waste is often discovering that operations do *not* intersect. A spatial index reduces operation fetches and geometry setup before any pixel loop. It also benefits cold fill, AA, Undo reconstruction, pending overlay composition, export, and possibly hit/damage diagnostics.

Add counters before implementation:

- operations in authority;
- index candidates;
- deduplicated candidates;
- exact bounds intersections;
- operations that paint at least one pixel;
- PSRAM record and sample bytes read.

The ratio between the first two numbers will predict the upper bound before device work begins.

### F12 — Curve geometry is prepared repeatedly for each group and each AA window

**Severity:** P0/P1 structural opportunity<br>
**Confidence:** High

For cold immediate rendering, `prepare_operation_chord_batch()` repeatedly:

- decodes compact samples;
- constructs curved units;
- derives segment bounds and row seeds;
- accumulates clipped bounds;
- sorts up to the batch capacity by `y0`.

See `incremental_rasterizer.cpp:1086-1158`. The settled renderer separately calls `prepare_incremental_curve_unit()` for every operation in every window (`settled_tile.cpp:78-139`). None of that geometry changes between neighboring groups at the same zoom.

A full persistent four-zoom compiled representation for all 80,000 samples is likely too expensive. Use a bounded hierarchy instead:

1. Store cheap append-time, zoom-independent metadata: sample decode coefficients, curvature/flatness class, segment world bounds, and offsets.
2. Lazily compile zoom-specific operation geometry into a bounded cache keyed by `(epoch, operation_index, zoom)`.
3. Clip cached plans to the current group/window instead of regenerating them.
4. Retain recent/visible operations; evict with a simple clock.
5. Reuse the same prepared plans in immediate fill, settle, export, and visible history reconstruction.

The minimum screen-radius rule is zoom dependent, so a cached 400% plan cannot simply be scaled down and assumed exact. Either compile after applying the zoom clamp or prove a shared world-space representation plus zoom-time radius clamp against the frozen oracle.

Small supporting wins:

- replace the comparison sort of at most 96 byte indices with a counting/bucket order by row, or avoid sorting when a cached plan already has order;
- persist the active chord list across row slices;
- cache operation-level level bounds for all five zooms if the arithmetic is still measurable after indexing.

### F13 — The 25% settled pass scans the full operation log 42 times

**Severity:** P1 AA performance issue<br>
**Confidence:** Confirmed

At 25%, `run_settle()` divides the 368×448 overview into 6×7 windows and calls `render_settled_window()` for each (`vector_v2_background_pipeline.cpp:324-386`). Each window starts at the newest operation and scans the entire active log (`settled_tile.cpp:64-75`). The current receipt reports 42 windows, 152.945 ms total, and a 76.416 ms maximum window.

Replace this with one full-view banded settle pass:

- obtain candidates once for the view or per coarse band through the spatial index;
- prepare each candidate operation once per band;
- maintain bounded row-band accumulation buffers;
- emit final RGB565 rows into the frame/presentation staging path;
- yield between operation/chord/row units, not between 64×64 windows.

The existing 64×64 workspace can remain the publication unit for high zoom, but 25% has no reason to pay 42 independent authority scans.

### F14 — Settled AA clears and composites more memory than the touched coverage requires

**Severity:** P1 AA hot-loop opportunity<br>
**Confidence:** Confirmed; speedup must be measured

For each intersecting operation, `render_settled_window()` clears the complete per-operation alpha buffer (`settled_tile.cpp:75`), rasterizes coverage, then loops across every pixel in the window to find nonzero alpha (`:149-170`). The final fold loops across every pixel again (`:178-188`).

Improvements, in safe order:

1. Track the union of touched bounds or touched row spans and composite only those pixels.
2. Use epoch-tagged alpha cells or a touched-index list to avoid a full `memset` for sparse operations.
3. Maintain an 8×8 or 16×16 saturation hierarchy so older operations can skip saturated blocks, not only stop when the complete window is saturated.
4. Build self-overlap coverage as row spans/boundary samples so overlapping chords union before painter compositing.
5. Keep full-interior spans at alpha 255 and perform analytic distance only in the one-pixel boundary annulus.

One exact micro-cleanup is already visible: in the final white-background fold, `255U * remaining / 255U` is exactly `remaining`. Remove the multiply/divide at `settled_tile.cpp:180-185`.

Do not revive the measured four-sample SSAA design; the packet already records it at 808 ms. Likewise, prior word-mask and recurrence experiments regressed on device. New work should build on the accepted analytic annulus model and use physical A/Bs.

### F15 — Composition pays division/modulo inside pixel loops

**Severity:** P1 recurring CPU waste<br>
**Confidence:** Confirmed

`compose_raw_pixels()` calculates `x % 64` and `y % 64` for every copied pixel (`materialized_canvas.cpp:1070-1085`). It can copy each intersecting tile row with `std::copy_n` from a precomputed tile-local start. Update statistics once per rectangle rather than once per pixel.

`compose_fallback_pixels()` performs `x * 25 / percent` and `y * 25 / percent` for every fallback pixel (`materialized_canvas.cpp:1101-1115`). All supported ratios are powers of two relative to 25%:

- 50% → source `x >> 1`;
- 100% → `x >> 2`;
- 200% → `x >> 3`;
- 400% → `x >> 4`.

Use shifts and run expansion, or a 368-entry source-x map prepared once per zoom/view. Consecutive destination pixels share one overview source pixel, so whole runs can be filled from one value.

These changes are not the route to a 2× cold speedup, but they affect full refresh, pan strips, cold presentation, Undo damage, and fallback refinement. They are low-risk and should be taken.

### F16 — Region refresh composes into scratch, copies into the frame, then copies again into DMA staging

**Severity:** P1 memory-bandwidth waste<br>
**Confidence:** Confirmed

`compose_and_present()` composes into `region_`, overlays pending operations, copies every row into `frame_`, and then the panel transport stages the frame into internal DMA buffers (`vector_v2_presenter.cpp:330-367`).

Add a strided destination API:

```cpp
compose_view_into(request, destination, stride, destination_origin);
overlay_pending(..., destination, stride);
```

For a linear frame, compose directly into the target subrectangle. For a ring, compose into one or two wrapped spans. The DMA staging copy remains necessary, but the intermediate PSRAM copy disappears.

### F17 — Minimap revision refresh can inflate a small damage rectangle into one large bounding box

**Severity:** P1 presentation inefficiency<br>
**Confidence:** Confirmed

`present_with_overlays()` unions any requested bounds with the minimap rectangle when the overview revision changes (`vector_v2_presenter.cpp:533-554`). If the two rectangles are far apart, the panel receives the entire bounding rectangle, including unrelated pixels between them.

Choose intentionally between:

- two ordered small windows;
- deferring the minimap to the next dock sweep;
- one union only when its area is cheaper than two transaction overheads.

Instrument pixels transmitted and number of panel windows. The right policy may differ for a tiny local stroke and a nearly full-width damage region.

### F18 — History and drain paths perform separate canvas and dock presentations

**Severity:** P1 presentation inefficiency<br>
**Confidence:** Confirmed

Undo/Redo refreshes canvas damage, then separately presents history controls (`vector_v2_chrome_controller.cpp:315-324`). Drain completion similarly refreshes accumulated canvas damage, then presents the dock (`vector_v2_background_pipeline.cpp:430-446`).

Build one presentation plan containing canvas, minimap, dock, and toast damage. Coalesce only when area/transaction cost says to do so; otherwise submit ordered windows in one TE-aware operation. The current unconditional separation pays extra preparation, TE wait, and completion bookkeeping.

### F19 — Synchronous high-water absorption can reintroduce a long hitch during a stroke

**Severity:** P1 ink-latency risk<br>
**Confidence:** Confirmed mechanism; frequency needs product telemetry

When pending operations reach the high-water threshold, `LiveStrokeSession::commit_ready_chunk()` synchronously calls `absorb_one(kInPlaceRetentionBudgetUs)` before publishing more authority (`vector_v2_live_stroke_session.cpp:169-200`). Because F9 shows that this budget does not cap the complete absorb call, the path can still consume tens of milliseconds.

First make absorption resumable. Then prefer one of these policies:

- reserve enough committed-overlay capacity for a complete physical stroke and absorb only while input is quiet;
- merge adjacent pending chunks of the same gesture into one prepared overlay representation;
- run a tiny, visible-only absorb quantum at high water and leave offscreen work for idle.

Keep counters for high-water entry count, maximum wall time, touch event age immediately after it, and whether the operation was visible.

### F20 — Autosave submission allocates and encodes on the main task

**Severity:** P1 unmeasured latency risk<br>
**Confidence:** Confirmed code path; physical impact unmeasured

The worker owns flash I/O, but `VectorV2AutosaveStore::Impl::submit()` allocates a `PendingWrite`, allocates PSRAM storage, and encodes the transaction while holding a mutex on the caller (`vector_v2_autosave_store.cpp:205-278`). A full-capacity checkpoint could therefore create an input-age tail even though the actual write is asynchronous.

Use two or three preallocated transaction buffers or a slab. Make checkpoint encoding incremental and interruptible, or snapshot compact immutable authority metadata and let the worker perform the bulk copy/CRC under a clearly defined handoff contract.

Add an atomic “autosave submit/encode active” marker to latency receipts so unexplained touch tails can be correlated. This is a hypothesis until measured; it should not displace the confirmed ring and scheduling fixes.

### F21 — Cache commit and replacement contain O(N×K) scans

**Severity:** P1/P2 boundedness opportunity<br>
**Confidence:** Confirmed

Examples in `materialized_canvas.cpp` include:

- `choose_slot()` scans 448 slots and repeatedly evaluates protection/LRU state (`:813-831`);
- `commit_in_place_revision()` checks retained keys with linear search (`:602-629`);
- uniform invalidation uses `std::find()` for each identity in an affected window (`:214-247`);
- incremental commit scans slots against publications.

At 448 slots these are not catastrophic, but they sit in drain and revision-commit paths where worst-case latency matters.

Use:

- a free-slot stack;
- a CLOCK or segmented-CLOCK eviction queue with explicit protected/recent classes;
- an identity bitset or generation-stamp array for retained/publication membership;
- direct retained slot indices when the caller already knows them.

A full identity bitset is only 1,712 bytes. A `uint16_t` stamp array is 27,384 bytes and may be shared with other per-identity transient marking work.

### F22 — Product firmware lacks the diagnostics needed to explain residual déjà vu

**Severity:** P1 observability gap<br>
**Confidence:** Confirmed

The gate harness constructs the rerender ledger, while product firmware reserves an inert block of the same size (`vector_v2_app_storage.cpp:84-93`; `vector_v2_app.cpp:338-365`). Ordinary glass sessions therefore cannot classify a stray rerender as cold miss, expected damage, eviction, stale revision, or unexplained.

Do not necessarily ship the full ledger forever. A product-safe diagnostic mode can use:

- compact counters by cause and zoom;
- last-N group events in a small ring;
- a user-triggered serial dump;
- no hot-path logging.

If the inert block becomes the second history directory, keep at least the counters and a much smaller event ring. Performance work without causal telemetry is likely to optimize the wrong path.

### F23 — Operation records and samples are in PSRAM, and every rejected operation still fetches a record

**Severity:** P1 memory-hierarchy opportunity<br>
**Confidence:** Confirmed; benefit depends on F11

The record array is 4,000 × 20 bytes = 80 KB, and samples are up to 640 KB, both external. An internal mirror of compact render headers—bounds, sample offset/count, tool, color—would make gating deterministic and reduce shared external-cache pressure.

However, implement the spatial index first. Once a group examines only a small candidate list, the value of mirroring all 4,000 headers may fall. Alternatives are:

- cache only candidate headers in internal memory for the active group;
- mirror a 12–16-byte header at 48–64 KB internal cost;
- place bounds/index metadata internally and leave samples external.

Internal SRAM is scarce and also funds DMA and hot scratch, so this must be an A/B, not a reflex.

### F24 — The linker receipt proves the TileProducer controller is in IRAM, not necessarily the raster kernels it calls

**Severity:** P1 device-specific experiment<br>
**Confidence:** Confirmed gap in evidence

`esp32/main/linker.lf:14-21` maps the `tile_producer` object to `noflash_text`. The receipt confirms IRAM addresses for `TileProducer::render_active_batch()` and `produce_next()`. The hot geometry and masked row loops live in `incremental_rasterizer.cpp`, a separate translation unit, and are not demonstrated by that receipt to be in IRAM.

Use the product map and disassembly to locate:

- `prepare_operation_chord_batch()`;
- `apply_masked_operation_chord_rows()`;
- `covers_pixel` and its inlined/non-inlined helpers;
- tile analysis/publish loops;
- fallback composition loops.

Split only the measured hot kernels into a dedicated translation unit and pin that object. Record text bytes, free IRAM/DRAM, stack, cold p50/p95, and battery impact. The packet already shows 2–3% flash-layout sensitivity, so this is credible, but pinning a large rasterizer wholesale could steal too much internal memory.

### F25 — Producer selection still does repeated small scans and fills inactive edge pixels

**Severity:** P2 cold cleanup<br>
**Confidence:** Confirmed

Smaller opportunities in `tile_producer.cpp`:

- maintain a missing-group bitset/cursor instead of repeatedly scanning up to 56 visible tiles;
- deduplicate 2×2 group candidates so the same group is not evaluated through several missing tiles;
- fill only the active group width/height at world edges rather than the complete 128×128 surface (`start_group():280-283` currently fills all 16,384 pixels);
- use shifts/lookup tables for the fixed zoom ratios;
- either use `baseline_revision_` as a stale-baseline guard or remove it; it is assigned/reset but not used in the shown producer path.

These are secondary to candidate indexing and geometry reuse.

### F26 — Idle repair is spatially blind and can spend cache capacity on low-probability views

**Severity:** P2 cache-policy opportunity<br>
**Confidence:** Confirmed policy; benefit workload-dependent

The repair planner favors cardinal neighbors, remembered zooms, and a broad 100% sweep. Improve it with:

- pan velocity and direction;
- recency-weighted view footprints;
- immediate cancellation on touch urgency;
- skipping regions proven paper/uniform;
- stopping before the pool enters a churn regime;
- prioritizing the likely reverse-pan runway and current zoom before other zooms.

The deterministic revisit gate is already green, so this is about residual human navigation rather than repairing a known correctness failure.

### F27 — Streamline 0.4 adds real phase lag even when the compute path is fast

**Severity:** P1 perception/tuning issue<br>
**Confidence:** Confirmed by code and packet measurements

`LiveStrokeSession` sets `config.streamline = 0.4F` (`vector_v2_live_stroke_session.cpp:103-107`). The packet records that this improves joint smoothness but adds trailing lag; the raw clipped tip is shown provisionally to mask much of it.

The author's premise that all remaining finger lag is performance-related is therefore partly wrong. Scheduling stalls are the first target, but some gap is intentional filter phase delay.

After F3–F9 are fixed, run a blind glass A/B of:

- 0.25, 0.30, 0.35, and 0.40;
- velocity-adaptive streamline;
- bounded one-sample prediction to estimated display time, corrected on the next sample.

Prediction can overshoot at corners and lift, so it should affect only the provisional tail, never stored authority. Measure optical finger-to-pixel distance, not only submit/DMA timestamps.

### F28 — Full-frame composition can be cooperative without changing panel ordering

**Severity:** P1 responsiveness opportunity<br>
**Confidence:** High

A full compose currently creates a long CPU unit before presentation. Compose the frame in row bands, checking touch urgency between bands. Once the frame is complete, preserve the existing single ordered, TE-aligned panel sweep. This improves preemption without reopening the optical tearing contract.

Do not split the panel transaction into many windows until optical testing says it is safe. CPU preemption and panel ordering are separable concerns.

### F29 — AA progression is row-major rather than perceptually prioritized

**Severity:** P2 perceived-quality improvement<br>
**Confidence:** Confirmed

`run_settle()` advances a row-major cursor. Once work units are truly bounded, use center-out, checkerboard, largest-error, or active-stroke-nearby ordering. The current 25% path should first become a banded pass; then interleave bands so the user sees distributed improvement rather than a visible wipe.

This does not reduce total compute, but it makes the same compute look faster.

### F30 — A deterministic PSRAM arena would make the current cache-placement knowledge maintainable

**Severity:** P2 architecture/measurement quality<br>
**Confidence:** High

`AppStorage::allocate()` deliberately uses inert reservations, padded allocations, and “allocate dead last” ordering to preserve PSRAM cache-set behavior. That is valid demoscene engineering, but ordinary heap allocation order is a fragile way to encode it.

Allocate one measured PSRAM arena and assign fixed aligned offsets for:

- overview generations;
- frame/region buffers;
- tile pool and metadata;
- authority records/samples;
- AA workspaces;
- history/index/diagnostic structures.

Keep the existing 8 KiB-way/cache-color findings as explicit layout constraints. This makes repurposing the inert blocks safe and makes A/B layouts reproducible. Re-run the 1.5 MiB contiguous export-reserve gate after any arena change.

### F31 — A second renderer core is a later experiment, not the first answer

**Severity:** P2 architectural experiment<br>
**Confidence:** Medium

Independent 2×2 groups can in principle render in parallel on the two LX7 cores. A worker could own private producer scratch, render one selected group, and enqueue a publication for serialized canvas commit. The touch sampler must remain higher priority and able to preempt it.

Costs include:

- another approximately 32 KB surface plus mask, summaries, chord storage, and stack in internal memory if it is to be fast;
- synchronization around operation-log snapshots and publication;
- duplicated spatial/geometry cache state or shared read-only structures;
- more contention for PSRAM and flash/cache bandwidth;
- less deterministic panel/input scheduling.

Do this only after F11/F12 reduce wasted work and F9 creates cancellation points. Otherwise two cores may simply perform the same unnecessary authority scans twice as fast while worsening input tails.

### F32 — Test sources have cross-toolchain portability defects

**Severity:** P2 engineering correctness<br>
**Confidence:** Confirmed in local Clang build

The production `tinydraw_vector_v2` library compiled successfully under local Clang 17 with warnings as errors. The full local test build then failed in test-only code because of missing direct standard-library includes / initialization portability:

- `tests/png_roundtrip.cpp`: `std::copy_n`, `std::abs`;
- `vector_v2/tests/world_export_test.cpp`: `std::copy_n`, `std::count_if`;
- `vector_v2/tests/svg_export_test.cpp:210`: missing designated field under Clang warning-as-error;
- an earlier GCC attempt exposed a most-vexing-parse/direct-initialization issue in `authority_journal_test.cpp`.

The packet records passing tests in its original macOS environment, so this is not evidence that product code is broken. Add Linux Clang and GCC CI, use direct includes, and prefer brace initialization in the affected tests. The packet's retained CTest metadata could not be run locally because the prebuilt test executables were intentionally absent.

### F33 — A transient settled-render failure is silently promoted to permanent completion

**Severity:** P1 correctness / quality-recovery bug<br>
**Confidence:** Confirmed from the control flow

`VectorV2BackgroundPipeline::run_settle()` increments `settle_cursor_` before attempting the window or tile (`esp32/main/vector_v2/vector_v2_background_pipeline.cpp:343-349`). When rasterization, staging, or publication fails, it increments `settle_failures_` but does not retain the failed item for retry (`:350-385`). Once the cursor reaches the end, it marks the pass complete even when failures were recorded (`:394-405`).

A transient allocation, publication, revision-race, or staging failure can therefore leave a visible area at its lower quality indefinitely, until some later fingerprint reset happens to restart settling. The printed failure count makes the event observable in logs but does not repair it.

**Fix**

Advance the durable cursor only for a successful render/publication or for a tile that is intentionally skipped because it is absent/already settled. Put transient failures into a small bounded retry queue keyed by tile/window and revision, with a retry limit plus a distinct permanent-failure diagnostic. Revalidate the source revision immediately before publication so stale work is discarded rather than retried as though it were resource pressure.

This retry mechanism should remain subordinate to touch urgency and should not turn a bad tile into an infinite background loop.

## Architecture proposals in more detail

### A. Spatially indexed authority replay

A useful design keeps all authority semantics in `OperationLog` and adds an immutable/read-only acceleration snapshot:

```text
OperationLog append
    ├─ authoritative record + samples
    ├─ per-operation coarse bounds / large-op class
    └─ posting entries in touched 128-world-unit cells

Group/window query
    ├─ collect cell posting heads newest-first
    ├─ merge large-operation list
    ├─ deduplicate with query stamp[operation]
    ├─ reject op >= active_prefix or wrong epoch
    ├─ exact operation/group bounds gate
    └─ existing saturation + raster path
```

Important details:

- Keep painter order newest-first; do not sort candidates by cell.
- Use operation index as the stable order key after deduplication. If gathering from several cell lists, collect then radix/counting-sort descending by operation index, or use a candidate bitset and scan set bits descending.
- A 4,000-bit candidate bitset is only 500 bytes. Combining cell posting lists into a bitset and scanning 4,000 bits as 125 words may be faster and simpler than stamp + sort on this fixed-capacity product. Benchmark both on device.
- Put very large operations in a global/coarse list to cap posting count.
- Rebuild the index during autosave restore; 4,000 records is small relative to startup replay and avoids journaling derived data.
- On Undo/Redo, the active prefix filters candidates without rebuilding the index.
- On branch replacement, increment epoch and rebuild or lazily invalidate stale posting tails.

A particularly demoscene-friendly variant uses one 4,000-bit bitset per 128-world cell. At 168 cells this is about 84 KB. Query is several wordwise ORs followed by descending bit scans—excellent predictability and no linked PSRAM chasing. The linked-posting variant uses less memory for sparse drawings. The right answer should be selected by a host census plus device A/B:

- dense bitset index: ~84 KB, predictable sequential reads;
- linked postings: perhaps 30–100 KB depending incidence, more random reads;
- hybrid: bitsets for common small operations, global list for large operations.

### B. Prepared geometry cache

The current producer's operation chord batch is an excellent execution format but a poor repeated-work boundary. Make it cacheable.

A staged implementation:

1. **Operation decode cache:** cache decoded/scaled samples and operation-level bounds for the active zoom.
2. **Curve-unit cache:** cache the 1–3 segments per endpoint and their conservative bounds.
3. **Sweep-plan cache:** for hot visible operations, cache row seeds and y order.
4. **Shared consumers:** immediate producer, settled renderer, history reconstruction, pending overlay, PNG export.

Use a byte-budgeted cache, not a fixed operation count. Key entries by epoch, operation index, zoom, and geometry-contract version. Store them in PSRAM if large, but keep the current active entry or row plan in internal SRAM. Sequentially reading a prepared plan from PSRAM may still be much cheaper than repeatedly fetching samples and performing float-heavy setup.

The cache should expose counters:

- prepare hits/misses;
- bytes prepared/read;
- preparation time avoided;
- entry evictions;
- groups/windows sharing an entry.

### C. History generations and a visible-first runway

The highest-value history contract is not “keep every historical tile forever.” It is:

- the first visible Undo/Redo response should not expose a cold rebuild;
- repeated taps should have a small adjacent-state runway;
- exact authority and eventual exact detail remain guaranteed.

A practical two-generation state:

```text
Generation A (current)
  overview A
  directory A -> versioned raw slots / current uniforms

Generation B (adjacent Undo or Redo)
  overview B
  directory B -> preserved versioned raw slots / compact uniform snapshots

shared tile pool
  slot key + generation identity + quality + refcount/directory mask + LRU
```

At a history action, swap A/B and authority cursor, then issue one bounded presentation plan. In idle, derive the next adjacent generation visible-first using the spatial index and prepared geometry. This makes a sequence of history taps analogous to pan runway refill.

Overview synchronization options, in order of likely practicality:

1. dirty-block copy-on-write between the two full buffers;
2. copy affected overview rectangles at gesture boundary while input is up;
3. full 329 KB copy only if measured below the latency budget and never on Down.

### D. Truly preemptible foreground work

Create one shared foreground scheduler contract:

```cpp
struct WorkBudget {
  uint32_t deadline_us;
  bool (*urgent_input)();
  uint32_t max_work_units;
};

enum class StepResult { Progress, Complete, Cancelled, Failed };
```

Every long subsystem exposes `step()`:

- tile producer;
- settled renderer;
- pending absorption;
- overview/history reconstruction;
- autosave checkpoint encode;
- full-frame compose;
- idle repair.

The main loop always drains input before calling another step. A task notification sets the urgent flag. State machines commit only at transactional boundaries, so cancellation never publishes partial authority.

This is preferable to immediately adding threads. It preserves the current serialized correctness model while eliminating the latency tails that make a fast average feel slow.

## Cold-render target assessment

### Why arithmetic-only work will not halve the current wall

The project has already taken the obvious mechanical wins: native arithmetic in hot paths, internal producer scratch, prepared chord batches, operation-level sweeps, saturation, strided publication, O(1) raw lookup, and targeted controller IRAM. The packet also records several intuitive micro-optimizations that regressed on device.

The remaining 2× opportunity comes from changing *how often* work is performed:

- one candidate query instead of scanning all operations per group;
- one geometry preparation reused across neighboring groups/windows;
- one 25% authority traversal instead of 42;
- one local ring patch instead of a full frame;
- one history generation swap instead of a complete replay.

### Measurement sequence to settle 250 ms

For each step, use reset-separated normal-product runs with real journal behavior. Record at least 20 runs per zoom and report p50, p95, maximum, and within-build spread.

Add this breakdown to the cold receipt:

| Counter | Meaning |
|---|---|
| `authority_ops` | active operation count |
| `index_candidates` | operations returned by spatial query |
| `bbox_intersections` | candidates passing exact group bounds |
| `ops_painted` | operations that change at least one pixel |
| `geometry_prepare_us` | sample decode/unit/seed/order work |
| `raster_us` | masked row/pixel work |
| `publish_us` | tile analyze/copy/catalog/slot work |
| `compose_us` | view composition before panel |
| `present_us` | TE wait, staging, wire, completion |
| `psram_record_bytes` / `sample_bytes` | external data touched |
| `iram_free` / `internal_heap_min` | cost of hot-code/scratch experiments |

Run four corpora separately:

1. blank and sparse scribble;
2. general frozen corpus;
3. overlap-heavy corpus;
4. adversarial hairlines/tapered corpus.

Do not use one aggregate number to hide that indexing helps sparse drawings much more than dense overlap.

## Finger-on-glass validation plan

Software timestamps do not include touch-controller scan phase, display scanout position, or optical persistence. Keep the existing traces, but add a high-speed-camera test with an LED or on-screen timing marker synchronized to the sampled Down.

Measure:

- touch contact to first changed pixel;
- finger-tip to provisional-tail distance during constant-speed strokes;
- p50/p95/max after a cached pan;
- p50/p95/max during drain, settle, autosave encode, battery update, and zoom refinement;
- dropped/coalesced events and queue depth;
- ring-active versus linear-frame cases.

The acceptance target should include a tail bound, not just an average. A 2 ms normal path with a 100 ms state-dependent catch still feels laggy.

## Correctness guardrails for the performance work

1. **Spatial indexes are derived.** Every candidate is still checked against epoch, active prefix, exact bounds, and the authoritative operation record.
2. **Prepared geometry is versioned.** Key by epoch/revision contract and zoom; reject stale entries.
3. **Painter order remains exact.** Candidate collection must restore descending operation index before newest-first compositing.
4. **Erasers remain paint-to-paper on device.** Do not accidentally import transparent SVG semantics into raster authority.
5. **Minimum screen radius remains part of geometry identity.** A shared plan must preserve the current zoom-dependent clamp.
6. **History swaps are transactional.** Authority cursor, overview generation, identity directory, and presentation state publish together or not at all.
7. **Ring patches use logical panel damage.** Test both x and y wraps, chrome overlap, provisional ink, minimap, and byte-swapped staging.
8. **Touch draining preserves Down/Up.** Only Move events may be coalesced.
9. **AA approximations require frozen-oracle comparison.** Preserve self-overlap union, painter order, boundary coverage, RGB565 fold, and PNG/device agreement.
10. **Product memory gates remain binding.** Every new index/cache/history structure must re-run export reserve, largest block, stack, DMA allocation, and cold cache-placement receipts.

## Things I would explicitly not prioritize yet

- Another blanket pass of hand-written bit tricks before candidate and preparation counts are reduced.
- A wholesale second-core renderer before cooperative cancellation and read-only work snapshots exist.
- More raw tile slots: 512 already broke the contiguous export reserve, and the present problem is repeated replay, not merely capacity.
- Reintroducing four-sample SSAA or previously rejected word-mask/scanline-recurrence experiments without a materially different design.
- Large internal-SRAM mirrors before the spatial index shows what data remains hot.
- Cosmetic non-performance bugs ahead of the confirmed startup, ring, input-order, and history issues, except where a correctness failure threatens data.

## Review and build notes

- The archive manifest and provenance were inspected; the review targets commit `b63b07f905b40b3d435679911d2de89e57e1f062`.
- The packet reports its macOS debug/release/ASan suites passing in the original environment.
- A local Clang 17 build compiled the production Vector V2 library with warnings as errors. The complete local test build was stopped by the test-only portability issues listed in F32, not by a Vector V2 library compile failure.
- The packet intentionally omits generated executables, so its retained CTest metadata was not runnable locally.
- This is a static and receipt-based review. I did not flash the target board, so all speedup ranges are hypotheses until the same-head physical A/B battery is run.

## Primary platform references

- Waveshare, **ESP32-S3-Touch-AMOLED-1.8** product specification: <https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm>
- Espressif, **ESP-IDF v6.0.2 External RAM** documentation: <https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/api-guides/external-ram.html>
- Espressif, **ESP32-S3 memory types / IRAM** documentation: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/memory-types.html>
- Espressif, **Linker script generation / `noflash_text` fragments**: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/linker-script-generation.html>
- Espressif, **FreeRTOS task notifications**: <https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/freertos_idf.html>

## Bottom line

The codebase is already unusually disciplined for a small embedded graphics system. The next round should not be framed as “find a faster distance formula.” The decisive move is to stop replaying and re-preparing information the system already knows.

Fix the occupancy metadata first so the existing paper optimization actually exists in product boots. Then make ring mode a first-class presentation state, give input an urgent path, and replace coarse nominal budgets with resumable state machines. After that, a spatial operation index and prepared-geometry cache are the credible route from roughly 500 ms toward 250–350 ms. Finally, use the existing spare-generation memory shape to turn Undo/Redo into cached adjacent-state transitions rather than visible cold reconstruction.
