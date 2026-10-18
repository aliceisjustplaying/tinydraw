# TinyDraw Vector V2 — code, architecture, ink, and performance review

**Reviewed packet:** `tinydraw-review-a560d20`  
**Commit:** `a560d20726d79139eff686358b00690fb9264a86`  
**Review date:** 2026-08-16  
**Primary goal:** exact cold viewport at 400% in **≤500 ms**, with smoother live ink and a credible later AA path  
**Hardware:** ESP32-S3 at 240 MHz, 8 MiB PSRAM, 368×448 RGB565 CO5300, effective 40 MHz panel transfer

---

## 1. Executive verdict

This is a strong architecture that has reached the point where representation and scheduling matter more than ordinary cleanup. The successful panning and tearing work is not accidental: the code has clear authority/derived-state boundaries, explicit revision and provenance checks, center-first progressive publication, fixed caller-owned storage, and unusually good stop/go documentation.

The current cold target cannot be reached by panel tuning, another small cache tweak, or a faster version of the existing variable-radius pixel predicate. The measured maximum is:

| Phase | Current |
|---|---:|
| Compute | 1,165.354 ms |
| Present | 70.182 ms |
| Pacing | 31.526 ms |
| Touch | 2.095 ms |
| **Wall** | **1,269.157 ms** |

If these phases remain serial, compute must fall to about **396.197 ms**, a **66.0% reduction** or **2.94× speedup**. That is too large for one micro-optimization.

The credible route is a stack:

1. **Version the tapered authority to a true convex conical capsule.** This is a representation change, not a math trick. It removes the legacy nonconvex taper dents and makes each row a single exact interval.
2. **Make one interval writer the common raster kernel.** Use word-oriented finalized-mask handling and paired RGB565 writes; prepare fixed-point edges once per segment.
3. **Use both S3 application cores as a pipeline, not a shared-state renderer.** A low-priority worker rasterizes independent 2×2 groups; the main core remains the only canvas/presentation writer and overlaps presentation with worker compute.
4. **Remove multiplicative metadata work.** Add direct tile-identity→raw-slot indexing, maintain the visible-missing count, avoid packed-tile double copies, and use retained-key bitsets.
5. **Prepare a curve unit once.** Count, bounds, and paint currently reconstruct the same midpoint curve.
6. **Decouple authority revision from materialization revision.** Keep finished ink visible as a committed overlay while overview/cache maintenance and exact refresh proceed incrementally.
7. **Record the real owner ink traces before choosing smoothing constants.** The current committed trace corpus is synthetic and is not sufficient to rank smoothness changes.

An illustrative, not predictive, threshold shows why the stack can work: a 1.7× effective dual-core compute speedup, a 20% raster/representation reduction, and a further 15% reduction from geometry/publication/metadata produce about **466 ms of parallel compute work**. If most of the present/pacing time overlaps the worker and startup/drain stays around 25 ms, the result is near **493 ms**. The margin is narrow; device A/B receipts must decide it.

---

## 2. What I verified

### Packet and source integrity

- All 541 packet files matched the manifest.
- The reviewed source tree was left unchanged.
- Temporary build and experiment changes were made only in disposable copies.

### Host build and tests

The default host preset could not build in this environment because SDL was unavailable. With a direct CMake configuration and C++ module scanning disabled:

- Debug: all 10 CTest groups passed.
- Release: the pristine source exposed an `assert`-only validation variable in `RibbonRenderer`; after a disposable runtime-validation fix, all 10 groups passed.
- ASan/UBSan: all tests and fuzz targets passed in the exercised paths.
- Several test translation units rely on transitive standard-library includes; disposable include fixes were required under this Clang toolchain.

No hidden memory corruption appeared in the sanitizer runs, but release-only contracts remain real defects.

### Cold baseline authority

`benchmark-results/wave2-compositor/COLD_GENERAL_BASELINE_RECEIPT.md:9-39`

- View: 400%, origin `(0,0)`, detail tiles discarded.
- Corpus: 910 operations, 12,157 samples.
- Tapered adversarial: 128 operations, 4,096 samples.
- Evil hairlines: 782 operations, 8,061 samples.
- Timed through final exact panel publication and DMA completion.
- Three-run maximum: 1,269.157 ms.
- Specialized hairline path: 327.978 ms at 400%, 445.980 ms at 100%.

The failure is primarily the tapered/general raster path, not the hairline-specialized path.

---

## 3. Architecture strengths worth preserving

### 3.1 Authority and derived state are explicit

The vector operation log is the document authority; overview, raw tiles, uniform tiles, live ribbon, and future settled AA are derived. This is the right foundation for exact export, undo/redo, cache eviction, and quality upgrades.

### 3.2 Revision, epoch, generation, quality, and provenance are not hand-waved

The canvas validates source identity and avoids quality regression. Raw tile pinning uses generations. Overview fallback is explicit rather than masquerading as exact detail.

Relevant code:

- `vector_v2/include/tinydraw/vector_v2/operation_log.h:22-37`
- `vector_v2/src/materialized_canvas.cpp:969-1041`
- `vector_v2/src/materialized_canvas.cpp:1044-1130`

### 3.3 The cold producer has the right broad shape

The producer renders center-first 2×2 tile groups, uses newest-first first-writer-wins masking, skips saturated rows and groups, and publishes only complete exact groups.

Relevant code:

- `vector_v2/src/tile_producer.cpp:286-326`
- `vector_v2/src/tile_producer.cpp:373-411`
- `vector_v2/src/tile_producer.cpp:499-570`

This shape beat a whole-viewport prototype in the host experiments because group-local culling, saturation, and shorter masks outweighed repeated setup.

### 3.4 Progressive presentation and pan frame reuse are successful

The app separates compute and presentation, avoids publishing incomplete groups, and retains a raster presentation path for panning. The project state records tear-free owner-accepted panning around the intended latency.

### 3.5 Fixed storage and overlap validation are unusually disciplined

Most core paths use caller-owned spans and explicitly reject aliasing. This is excellent embedded design and should be retained in the worker API.

### 3.6 The project has a real experimental culture

Rejected experiments and receipts are preserved. The prior work already ruled out several tempting ideas, including internal-RAM coverage as the missing speedup, per-row square-root span setup, larger tile pools that violate export reserve, and full-frame 4-sample SSAA.

---

## 4. Cold-render diagnosis

### 4.1 The hot predicate is fundamentally expensive

`vector_v2/src/incremental_rasterizer.cpp:95-107`

For every tapered candidate pixel, `covers_pixel` performs:

- two coordinate differences;
- a dot product;
- multiplication by inverse length squared;
- clamp;
- projected center reconstruction;
- radius interpolation;
- squared-distance comparison.

The host profile attributed roughly 62% of self time to this predicate and another ~22% to its caller. The exact percentages are host-specific, but the work shape is clear and matches the device symptom.

### 4.2 Constant-radius strokes already exploit spans; tapered strokes do not

`vector_v2/src/incremental_rasterizer.cpp:116-183` walks to the first and last covered pixel, then fills the interior for constant radius.

`vector_v2/src/incremental_rasterizer.cpp:185-239` computes a conservative tapered row range but still invokes the full predicate for every unfinalized candidate pixel.

That asymmetry explains why the hairline-specialized workload is already under the target while the tapered/general workload dominates.

### 4.3 The current tapered footprint is not convex

The current definition uses the centerline projection parameter to interpolate radius, then tests distance to that projected center. With unequal endpoint radii, this set is not generally the convex hull of the endpoint disks.

In the adversarial corpus:

- 7,808 segments generated 759,702 tested rows.
- 22 rows had two disjoint covered runs.
- The largest gap was 28 pixels.

Therefore, a universal one-span shortcut is not exact for the current authority.

### 4.4 Exact decomposition did not pay

An exact three-piece decomposition reduced predicate calls from 208,935 to 68,686 in the instrumented tapered viewport, but the first useful implementation was about 2.4% slower in the adversarial scorecard. Per-row square roots and divisions replaced the eliminated probes.

This is not a route to 500 ms and should remain rejected.

### 4.5 A true conical capsule is the first structurally useful experiment

The proposed primitive is the convex hull of the endpoint disks:

- If `abs(r1-r0) >= segment_length`, the larger disk contains the smaller disk.
- Otherwise, two external tangents and the endpoint arcs define a convex conical capsule.
- Every scanline intersects it in zero or one interval.
- The interior can be filled without a per-pixel distance predicate.

Host experiment results:

| Workload | Current grouped | Conical grouped | Change |
|---|---:|---:|---:|
| Tapered | 5.887 ms | 5.201 ms | ~11.7% faster |
| Combined | 12.564 ms | 11.237 ms | ~10.6% faster |

Forward replay, which cannot exploit group saturation as heavily, improved by roughly 4.2–5.4×.

Visual delta in the tapered-only viewport:

- 128 / 164,864 pixels changed: **0.07764%**
- 31 paper→ink
- 3 ink→paper
- 94 changed color because painter boundaries moved

The final combined viewport was pixel-identical because newer hairline operations covered those boundary differences.

This is host-only evidence. It is strong enough for a device A/B, not strong enough to claim the target is solved.

### 4.6 The authority change also improves live/cold consistency

Live ink is assembled from convex ribbon spans and circles; cold authority uses hard variable-radius centerline capsules. A convex conical capsule is closer to the live envelope than the legacy tapered predicate and removes visible taper dents. This gives one representation change three benefits:

- faster exact scan conversion;
- smoother pressure transitions;
- smaller preview→authority shape mismatch.

It does **not** fix centerline angularity by itself.

### 4.7 Curve geometry is reconstructed repeatedly

The producer calls:

- `incremental_curve_unit_step_count`
- `incremental_curve_step_level_bounds`
- `apply_masked_incremental_curve_step`

for the same endpoint/step.

`vector_v2/src/tile_producer.cpp:430-486`  
`vector_v2/src/incremental_rasterizer.cpp:726-762`

Each helper reconstructs the midpoint quadratic unit. Replace this with a `PreparedCurveUnit` containing:

- transformed endpoint/control samples;
- 1/2/4 prepared segments;
- exact or conservative bounds;
- constant/conical classification;
- fixed-point edge state;
- deterministic work estimate.

The active producer cursor should hold that unit until all steps are consumed.

### 4.8 The current slice work estimate is a poor proxy

`vector_v2/src/tile_producer.cpp:454-493`

Raster work is charged as clipped bounding-box area. A skinny diagonal and a filled rectangle of the same bounding box receive the same cost, while finalized-mask saturation changes actual work dramatically.

The app itself documents that the per-step budget underpredicts masked replay by an order of magnitude and therefore loops against a wall-clock deadline:

`esp32/main/vector_v2/vector_v2_app.cpp:1202-1229`

Use deterministic work units closer to the kernel:

- rows visited;
- finalized-mask words examined;
- span pixels or span words considered;
- actual pixels written;
- prepared-segment setup.

Keep a cycle deadline as the final interaction guard.

### 4.9 Tile publication currently copies raw pixels twice

`vector_v2/src/tile_producer.cpp:585-611`:

1. copy a tile from the 128×128 supertask to packed scratch;
2. analyze packed scratch;
3. `MaterializedCanvas::publish_tile` copies packed scratch into the raw pool.

`vector_v2/src/materialized_canvas.cpp:884-935`

Add strided analysis and strided publication:

- analyze directly from the supertask;
- publish a uniform without packing;
- for raw data, reserve/select a slot and copy each row once into its final destination;
- commit metadata only after the infallible copy.

This also frees the 8 KiB packed buffer in the one-core producer and reduces the additional worker workspace.

### 4.10 Visible-completion bookkeeping rescans metadata

After each complete group, `render_active_batch` calls `visible_tiles_remaining`:

`vector_v2/src/tile_producer.cpp:562-569`

That path repeatedly asks the canvas about visible identities. Raw lookup is a linear scan of 448 slots. Initialize a missing-identity bitset/count once for the view, clear identities as they are reused or published, and complete when the count reaches zero.

---

## 5. Dual-core cold pipeline

### 5.1 The second core is available

- Main app task runs on CPU0 in the hardware log.
- Touch sampler is pinned to core 1, priority 5:
  `esp32/main/vector_v2/vector_v2_touch_sampler.cpp:33-46`
- Cold fill runs from the main app loop and is disabled while pressed:
  `esp32/main/vector_v2/vector_v2_app.cpp:1143-1153`
- Existing project history already used a low-priority second-core benchmark worker.

The ESP32-S3 has two LX7 application cores. A priority-1 raster worker on core 1 remains preemptible by the priority-5 1 kHz touch sampler.

### 5.2 Do not make `MaterializedCanvas` thread-safe

Locks around log, canvas, LRU metadata, and presentation would destroy the current clarity and create priority-inversion risks.

Use a pure job/result boundary:

```text
Main core / sole owner                  Core-1 raster worker
----------------------                  --------------------
capture replay snapshot
make center-first group queue
submit GroupRasterJob  ------------->   raster into private workspace
render another group                    check cancel token at safe boundaries
publish completed main result           return revision-stamped GroupResult
present region while worker runs  <---- block until result slot is consumed
validate worker result
publish on main only
```

A job/result must carry:

- operation-log epoch;
- destination revision;
- view fingerprint: zoom and viewport;
- group key and exact bounds;
- cancellation generation;
- raster statistics/hash in benchmark builds.

The main thread discards stale results.

### 5.3 Formalize an immutable replay snapshot

`OperationLog` explicitly says callers must serialize reads, appends, and clear operations:

`vector_v2/include/tinydraw/vector_v2/operation_log.h:65-68`

Do not let the worker call `OperationLog::operation()` on a concurrently mutable object.

Introduce a read-only snapshot containing:

- pointers/spans to the record and sample prefix;
- captured operation/sample counts;
- baseline/destination revisions;
- epoch;
- pre-synchronized replay-index view.

Appending writes beyond the captured prefix and can be made safe, but reset/undo must wait for worker cancellation acknowledgement. Put this contract in the type system rather than relying on current behavior.

### 5.4 Use one result slot first

A worker workspace needs approximately:

- 32 KiB: 128×128 RGB565 supertask;
- 2 KiB: one-bit finalized mask;
- small row/word summaries and state;
- task stack.

The existing 8 KiB packed scratch can disappear with strided publication. A single result slot means the worker blocks while the main publishes, so no second result framebuffer is needed.

The nominal memory fits the reported slack, but the real export-reserve gate and internal task-stack cost are authoritative.

### 5.5 Overlap matters as much as raw scaling

Perfect 2× compute scaling alone would produce approximately 583 ms compute; adding the current serial noncompute would still be about 687 ms.

The pipeline changes that arithmetic:

- while main waits for panel transfer or tear pacing, core 1 can rasterize the next group;
- while core 1 rasterizes, main may rasterize a separate group or publish;
- only startup, final drain, single-writer publication, and unavoidable contention stay serial.

Measure:

- one-core compute;
- dual-core total core cycles;
- wall;
- presentation overlap;
- worker idle/block time;
- PSRAM/cache contention;
- touch event age and queue overflow;
- cancellation latency at press.

### 5.6 Suggested go/no-go gate

After the conical/span kernel exists:

- **Go** if effective two-core compute scaling is at least 1.65×, touch metrics are unchanged, and no stale result is ever published.
- **Stop and redesign** if scaling is below 1.45× because PSRAM/cache contention dominates.
- Keep the worker completely idle during live ink initially.

---

## 6. The common span kernel

Once constant and tapered segments both produce exact row intervals, one kernel can serve most immediate raster work.

### 6.1 Use 32-bit finalized-mask words

The current tapered masked loop examines mask bytes in 8-pixel chunks:

`vector_v2/src/incremental_rasterizer.cpp:366-405`

For each span:

1. handle a misaligned head;
2. process 32-pixel words;
3. handle the tail.

Per mask word:

- `0xFFFFFFFF`: skip;
- `0`: fill 32 pixels and set all bits;
- mixed: iterate clear bits with `std::countr_zero`.

The mask is already in internal RAM. The destination is PSRAM.

### 6.2 Use paired RGB565 stores

For a fully clear run, replicate the 16-bit color into a 32-bit word and store two pixels at a time when alignment permits. Let the device A/B decide between:

- explicit 32-bit writes;
- `std::fill_n`;
- a small unrolled loop.

Do not write hand assembly until the C++ version is measured.

### 6.3 Prepare edges once; increment by row

All operation coordinates are quantized:

- position: quarter-world units;
- radius: 1/256 world units;
- zoom: 25/50/100/200/400%.

A fixed-point screen representation can preserve these inputs exactly. Compute conical tangent/arc setup once per segment, then advance edge intersections per row using fixed increments. One-time float/square-root setup is acceptable; per-pixel float projection is not.

The prior failed scanline recurrence experiment does not invalidate this experiment. It retained the legacy nonconvex predicate and paid setup without obtaining a universal one-span interior.

### 6.4 Keep exactness tests at pixel-boundary degeneracies

Add fixtures for:

- zero-length segment;
- equal radius;
- one disk containing the other;
- near-containing threshold;
- horizontal/vertical/diagonal;
- subpixel tangent touching a pixel center;
- extreme minimum and maximum radii;
- eraser over pen and pen over eraser;
- adjacent segments with shared endpoints;
- 25/50/100/200/400% zooms.

For the new authority version, compare against a slow mathematical reference, not the legacy raster.

---

## 7. Cache and metadata

### 7.1 Raw lookup is linear; uniform lookup is direct

`MaterializedCanvas::find_tile` scans all slots:

`vector_v2/src/materialized_canvas.cpp:822-830`

Uniform identities use direct indexing:

`vector_v2/src/materialized_canvas.cpp:832-839`

The raw scan appears in lookup, composition, publication, retention, pinning, mark-used, and completeness checks.

Add:

```cpp
std::array<std::uint16_t, kMaterializedTileIdentityCount> raw_slot_by_identity;
```

- Sentinel: `0xFFFF`
- Size: 13,692 × 2 = **27,384 bytes**
- Validate that the referenced slot is occupied and has the expected key/generation.
- Update on every raw publish, eviction, invalidation, and raw→uniform conversion.

Start in PSRAM if internal RAM is too valuable. One random indexed access is still preferable to 448 metadata probes.

### 7.2 Use a retained-identity bitset during commit

`commit_in_place_revision` scans `retained_keys` for every occupied slot:

`vector_v2/src/materialized_canvas.cpp:669-696`

Uniform invalidation also performs `std::find`:

`vector_v2/src/materialized_canvas.cpp:303-334`

A 13,692-bit set is only about **1.7 KiB**. Mark retained identities once, then test in O(1).

### 7.3 Maintain free-slot state, but do not overengineer LRU first

`choose_slot` scans 448 slots and computes protection rank:

`vector_v2/src/materialized_canvas.cpp:856-872`

A free list is easy. A perfect heap for protected LRU is less attractive because view-protection rank changes. After direct lookup and publication cleanup, profile whether this scan remains meaningful.

### 7.4 Do not resurrect the rejected chunk-bounds cache unchanged

The prior exact chunk-bounds experiment saved about 8.5% wall but cost roughly 200 KiB PSRAM. With the current export-reserve margin, that is not the next move. Revisit spatial metadata only if the conical/dual-core path changes the measured bottleneck.

---

## 8. Incremental append and lift behavior

### 8.1 The “complete commit budget” is not a hard wall-time bound

The comment says the deadline covers the complete commit:

`vector_v2/src/incremental_document.cpp:301-314`

But work before the budget check includes:

- overview copy and replay:
  `vector_v2/src/incremental_document.cpp:331-333`
- scanning resident materialized tiles:
  `vector_v2/src/incremental_document.cpp:334-335`
- all uniform retention/materialization:
  `vector_v2/src/incremental_document.cpp:228-259`
- visible raw retention, which is explicitly exempt:
  `vector_v2/src/incremental_document.cpp:262-290`
- final invalidation/revision commit:
  `vector_v2/src/incremental_document.cpp:351-366`

The deadline only cuts off offscreen raw retention. Rename and instrument it honestly before relying on it.

### 8.2 Lift still blocks the coordinator

The lift path:

- finishes and presents the preview cap;
- finishes the builder;
- commits final ready chunks synchronously;
- refreshes the entire affected region synchronously.

`esp32/main/vector_v2/vector_v2_app.cpp:1034-1113`

Owner receipts show:

- move event→submit averages around 1.8–1.9 ms;
- move event→DMA averages around 3.1–3.3 ms;
- no presentation failures, overflows, or authority mismatches;
- post-lift next-poll delay around 87–111 ms;
- final refresh wall around 72–80 ms.

The `append_us` field is cumulative across stroke chunks, so it should not be interpreted as all occurring after lift. The lift poll gap and refresh wall are the reliable evidence of the hitch.

### 8.3 Authority and materialization are over-coupled

The current append requires:

```text
operation_log.current_revision == canvas.current_revision
```

before and after every commit.

That is safe, but it makes a derived cache part of the authority transaction. A materialized cache should be allowed to trail the operation log if the lag is explicit.

Introduce:

- `authority_revision`;
- `materialized_revision`;
- `pending_operation_range`;
- a committed overlay for operations not yet absorbed into visible materialization.

At lift:

1. close the live ribbon and submit the final visual update;
2. publish operation authority quickly;
3. keep the finished ribbon in a committed overlay;
4. advance overview and affected tiles in bounded phases;
5. remove the overlay only after visible exact materialization reaches its revision.

Undo, export, and a new stroke must either include pending authority directly or serialize at explicit boundaries. This is more work than another local optimization, but it eliminates the architectural reason that final cache maintenance can block input.

### 8.4 Resumable commit phases

A practical state machine:

```text
PrepareLog
PrepareOverviewRows
EnumerateAffected
RetainVisibleUniforms
RetainVisibleRawTiles
RetainOffscreenWithinBudget
CommitRevisionMetadata
ScheduleVisibleRefresh
IdleRepair
```

Each phase reports time and work counters. Mutation must remain fail-safe after it begins.

---

## 9. Smoother ink

### 9.1 The latency lane is already good; the fidelity lane is not closed

The live owner log demonstrates excellent software latency during motion. The visible complaint is angularity and the lift hitch, not event delivery.

Do not spend the next performance round replacing `std::pow` or `std::hypot` in `InkStream` unless device phase telemetry says they matter.

### 9.2 The canonical trace corpus is not canonical yet

`docs/INK_TRACE_HARNESS.md:22-34` requires five recorded traces.

`docs/INK_TRACE_HARNESS.md:90-99` says:

- four current fixtures are synthetic;
- `fast-curve-dense-25.csv` is missing.

The four committed CSV headers explicitly identify themselves as synthetic placeholders. Record the owner corpus before choosing smoothing constants. Otherwise every “looks smoother” change remains anecdotal and hard to regress.

### 9.3 Input spacing is variable and mostly retained

`OperationBuilder` drops only identical quantized position/radius duplicates:

`vector_v2/src/operation_builder.cpp:106-140`

It does not resample by arc length. Slow motion can produce dense points; fast motion can produce long gaps. The midpoint curve then has uneven geometric support.

### 9.4 Add deterministic arc-length resampling after pressure estimation

The raw touch event should still feed `InkStream`, because pressure is speed-derived. Then resample adjusted `InkPoint`s spatially:

- carry residual distance between raw events;
- interpolate position, radius, pressure, and timestamp at fixed screen-space distances;
- emit all stable resampled points to both live geometry and operation authority;
- keep the newest raw endpoint as provisional so the tail reaches the finger;
- flush the final endpoint at lift.

Sweep a small set of brush-aware spacings against recorded traces rather than selecting one by intuition. This can:

- remove slow-stroke oversampling;
- bound fast-stroke gaps;
- stabilize curvature;
- make cold work more predictable.

Track generated-point count and operation storage; resampling can increase fast-stroke samples.

### 9.5 Selective 1/2/4 subdivision, not universal subpixel flatness

The current midpoint quadratic always emits two chord spans:

`core/src/ribbon_geometry.cpp:121-141`

A corpus analysis at 400% found:

- current maximum centerline deviation: 14.65 px;
- current combined segment count: 21,584;
- enforcing a 1 px bound would nearly double segment count;
- even a generous cap still raises work materially;
- near-straight hairline units can use one segment.

Use a bounded selector:

- 1 span for nearly straight/slow-radius-change units;
- 2 spans for ordinary units;
- 4 spans only for high curvature or pressure change;
- brush-aware tolerance, not a universal 0.25 or 1 px rule;
- hard cap at 4 initially.

The same prepared unit must feed live preview, committed authority, export, and cold replay.

### 9.6 Conical capsules help pressure smoothness

The current legacy tapered set can pinch inward between endpoint disks. The conical hull removes that dent and makes varying radii look like a continuous brush envelope. Test this before adding a separate radius-slope filter.

### 9.7 Do not silently increase live primitive capacity

`RibbonPrimitiveBatch` has a fixed capacity of eight:

`core/include/tinydraw/ink/ribbon_geometry.h:23-37`

`push_back` checks capacity only with `assert`, then writes:

`core/src/ribbon_geometry.cpp:152-155`

Adaptive subdivision can exceed the old bound. Make capacity failure explicit before changing geometry.

---

## 10. Antialiasing strategy

### 10.1 Keep AA out of the immediate cold gate

The existing full-frame 4-sample SSAA receipt around 808 ms is correctly rejected. AA should be a `kSettled` quality upgrade after exact hard-edge immediate tiles meet the latency target.

### 10.2 Boundary-only tile-local AA

For a 64×64 settled tile:

1. start from background or the older exact source;
2. replay operations forward in painter order;
3. for each operation, build a 4×4 subpixel occupancy mask:
   - 16 bits per pixel;
   - 8 KiB per tile;
   - fill interior pixels as `0xFFFF`;
   - sample only left/right boundary pixels and cap/arc boundaries;
4. convert occupancy popcount to alpha;
5. blend pen color or paper for eraser;
6. publish `kSettled`;
7. never downgrade an immediate/settled identity.

The conical one-span rasterizer is useful here because the interior is analytic and only the boundary needs fractional coverage.

### 10.3 Operation self-overlap must be unioned

Do not blend each segment independently; overlapping segments of one stroke would darken partial edges. The per-operation 16-bit occupancy mask provides exact sample union.

The existing coverage helper’s max-count behavior is not an exact sub-sample union and should not be reused as authoritative settled AA.

### 10.4 Forward painter order is simpler

Reverse first-writer-wins works beautifully for opaque hard coverage. Fractional alpha would require residual transmittance and color accumulation. Forward operation order with an operation-union mask is easier to reason about and test.

### 10.5 Define the RGB565 blend model

Current RGB565 work is in encoded channel space. Decide and freeze whether settled AA blends:

- directly in 5/6/5 encoded channels; or
- in a small linear-light lookup representation.

Encoded blending is faster and deterministic. Physical linearity is optional; consistency is mandatory.

---

## 11. Release correctness and API safety

### 11.1 `RibbonRenderer` validation disappears under `NDEBUG`

`core/src/ribbon_renderer.cpp:74-83`

Width, height, stride, and surface size are asserted only. In Release:

- invalid negative dimensions can underflow `height - 1`;
- `required` becomes unused under warnings-as-errors;
- an undersized surface can be written out of bounds.

Validate dimensions before computing `required`, then return an explicit failure status.

### 11.2 `RibbonPrimitiveBatch::push_back` can overflow in Release

`core/src/ribbon_geometry.cpp:152-155`

Replace with a checked fixed-capacity API. Good options:

- `bool try_push_back(...)`;
- `RibbonUpdate` carries `overflowed`;
- capacity derives from the maximum supported subdivision;
- fail closed and request a full refresh rather than corrupting memory.

### 11.3 `InkStream` lifecycle is assert-only

`core/src/ink_stream.cpp:45-47`

Calling update/finish without begin is only guarded by `assert`. Make misuse return a status or a safe inactive result in Release.

### 11.4 `OperationLog::ready` is too weak

`vector_v2/src/operation_log.cpp:78-82`

It checks only nonempty spans. It should also reject:

- record/sample storage overlap;
- capacities that cannot be represented by stored 32-bit indices;
- any alignment/size requirement relied upon by the implementation.

The LOD store already demonstrates a stronger readiness style.

### 11.5 Include hygiene and CI matrix

Several tests required direct standard-library includes in this toolchain. Add CI for:

- GCC Debug/Release;
- upstream Clang Debug/Release;
- AppleClang if supported;
- ASan/UBSan;
- `NDEBUG` with warnings-as-errors;
- self-contained public-header compilation.

---

## 12. Memory-accounting drift

`vector_v2/include/tinydraw/vector_v2/memory_layout.h:12-15` admits that the plan is not a complete `AppStorage` model, but the drift is now large enough to mislead design decisions.

The static plan includes:

- `OperationLodStore` storage, roughly 668 KiB;
- two renderer tasks.

The product app does not allocate/use the LOD store and currently allocates one producer workspace. Conversely, `AppStorage` allocates overview, snapshot, frame, overview scratch, region scratch, chrome cache, producer buffers, and other product-specific regions:

`esp32/main/vector_v2/vector_v2_app.cpp:183-263`

Keep the export-reserve gate as authority, but generate one memory report from the actual allocation table:

- name;
- capability: internal/external/DMA;
- count and bytes;
- lifetime;
- product vs harness-only;
- largest contiguous reserve after full product state.

Do not “spend” the phantom LOD budget without a real allocation receipt.

---

## 13. Hardware/compiler experiments that are worth bounding

Current defaults already enable:

- 240 MHz CPU;
- performance optimization (`-O2` class);
- QIO flash;
- 80 MHz PSRAM.

The sdkconfig does not pin/report the S3 cache geometry. The chip supports multiple instruction/data cache sizes, associativities, and line sizes.

After the algorithmic path is in place, A/B:

- 32 vs 64 KiB data cache;
- 32 vs 64-byte data-cache line;
- current instruction cache vs one alternative;
- `-O2` vs `-O3`;
- selected hot scan functions in IRAM.

Report the lost internal heap for larger cache and verify the export reserve and worker stack.

Avoid `-ffast-math` in authority code; boundary changes can alter exact pixels.

---

## 14. Experiments I would not repeat now

1. **Whole-viewport replay:** exact but 7% slower on tapered and 21% slower on combined host workload.
2. **Exact three-piece legacy taper decomposition:** fewer predicates, no useful wall improvement.
3. **Universal one-span assumption for legacy taper:** incorrect; split rows exist.
4. **Strict subpixel adaptive subdivision:** nearly doubles combined segment count at 1 px.
5. **Full-frame 4× SSAA:** already too expensive.
6. **Internal-RAM coverage as the main fix:** prior hardware result improved wall only ~1.7%.
7. **More publication batching/panel clock work:** compute is 92% of the current wall.
8. **512 raw tile slots:** already violated the contiguous export reserve.
9. **The prior ~200 KiB chunk-bounds cache unchanged:** 8.5% is not enough for its memory cost.
10. **Locks around a shared canvas:** use pure worker results and single-writer publication.
11. **Hand assembly before representation and layout changes:** the current inner predicate should be removed, not polished.
12. **AA before hard-edge immediate closure:** it would hide the real bottleneck and complicate exactness.

---

## 15. Ranked implementation campaign

### Stage 0 — measurement closure

- Record all five real owner ink traces.
- Add cycle/phase counters for:
  - curve preparation;
  - segment setup;
  - row edge generation;
  - mask-word scan;
  - pixel writes;
  - publication analysis/copy;
  - raw lookup;
  - presentation wait;
  - worker active/blocked/canceled.
- Produce 20-run device distributions only at closure; use 3–5 runs during stop/go.

### Stage 1 — conical authority + common span writer

Deliver:

- versioned conical capsule reference;
- one-span scan converter;
- word-oriented mask writer;
- exact fixtures;
- export/persistence version handling.

**Go:** combined device compute improves at least 20%, visual delta is accepted, no exactness mismatch under the new authority.  
**Conditional:** 12–20% improvement; continue only if the dual-core prototype is already promising.  
**Stop:** under 12% or unacceptable visual behavior.

### Stage 2 — dual-core group pipeline

Deliver:

- immutable replay snapshot;
- one low-priority core-1 worker;
- one result slot;
- cancellation generation;
- main-only publication;
- compute/presentation overlap telemetry.

**Go:** ≥1.65× effective compute scaling, unchanged touch latency/overflow, no stale publication.  
**Stop:** <1.45× scaling.

### Stage 3 — remove multiplicative overhead

In this order:

1. prepared curve unit;
2. direct raw-slot directory;
3. maintained visible-missing count;
4. strided analyze/publish, no double copy;
5. retained-key bitset;
6. work units based on rows/mask words;
7. cache-geometry A/B.

Target: bring the combined wall below 500 ms with margin, not one lucky run.

### Stage 4 — lift transaction

- phase instrumentation;
- committed overlay;
- authority/materialization revision split;
- bounded overview/cache update;
- owner trace and scribble-multistroke closure.

### Stage 5 — fidelity

- arc-length resampler sweep;
- selective 1/2/4 subdivision;
- conical pressure transition review;
- exact live/final path comparison;
- optical spot check.

### Stage 6 — settled AA

- one 64×64 tile;
- boundary-only 4×4 occupancy;
- forward painter order;
- publish `kSettled`;
- idle budget and cache-quality tests.

---

## 16. Concrete first patch set

A low-risk first patch can be reviewed independently of the authority experiment:

1. Runtime validation in `RibbonRenderer`.
2. Checked `RibbonPrimitiveBatch`.
3. Runtime `InkStream` lifecycle guard.
4. Stronger `OperationLog::ready`.
5. Test include fixes and Release/ASan CI.
6. Phase counters for incremental append and cold publication.
7. `PreparedCurveUnit` API with current two-segment behavior unchanged.
8. Strided tile analysis/publication.
9. Maintained visible-missing count.

Then build the conical rasterizer behind an authority-version feature flag and run the device A/B.

---

## 17. Bottom line

The project should not be abandoned or replaced with a generic graphics stack. The current design has already solved two of the hardest user-visible problems—fast raster panning and tearing—because it respects the hardware and keeps authority separate from presentation.

The remaining cold failure is also solvable in that style:

- change the primitive so the fast path is exact;
- turn the raster into interval generation plus bulk stores;
- pipeline independent groups across both cores;
- keep shared state single-writer;
- spend tens of kilobytes on direct metadata only where it removes repeated PSRAM scans;
- let derived materialization trail authority explicitly;
- defer AA to a tile-local boundary pass.

The 500 ms target is plausible but not yet proven. The first decisive receipts are the **device conical/span A/B** and the **dual-core scaling/overlap A/B**. Those two experiments should determine whether the rest of the campaign is closing a narrow gap or whether a deeper checkpoint/authority redesign is needed.
