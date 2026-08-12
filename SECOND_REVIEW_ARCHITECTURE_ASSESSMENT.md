# TinyDraw vector canvas: assessment of the architecture review

## Purpose

This document consolidates my response to the review in `review_findings_2026_08_12_noon/`, my reassessment after removing implementation time as a constraint, and my view of where hand-written assembly may help.

Please read the original review first, especially `review_findings_2026_08_12_noon/RESPONSE.md`. This document is commentary on that review, not a substitute for it.

## Current recommendation

Keep vector authority and replace the prototype cache architecture.

The intended production design should use:

- a compact ordered vector operation log;
- append-time bounds, sample reduction, and multiresolution geometry;
- one complete low-resolution overview;
- a world-aligned sparse raster tile cache for higher zoom levels;
- separate settled and exact renderers;
- incremental tile updates when operations are appended;
- a display scheduler that owns strip ordering, DMA completion, overlays, publication generations, and metrics.

I do not recommend raster authority, abandoning broad zoom, or continuing to evolve the camera-aligned double-buffered 3×3 atlas into the production cache.

The 3×3 prototype proved useful facts: direct raster pan can meet the interaction target, live drawing can remain fast with vector recording, and valid scaled output can hide cold vector reconstruction. It also exposed the wrong production behaviors: giant clears, camera-aligned rebasing, invalid edge management, repeated per-band geometry reconstruction, and poor memory headroom.

## Why the project still looks viable

The strongest evidence is physical:

- Live raster updates measured 5.1 ms p95 and 7.2 ms p99 in the latest workload. These measurements cover the inner raster update rather than the full pen event path, but they show substantial headroom.
- Warm direct pan measured about 33.8 ms p95 with valid cached pixels.
- An earlier prototype produced and displayed a complete nearest-neighbor preview in about 65 ms on the same hardware class.
- The latest zoom prototype eliminated failed zoom-out transitions and prevented known-invalid raster publication.
- Current 175–233 ms zoom feedback includes avoidable work, most notably a 2.97 MB inactive-atlas clear.
- Exact rendering takes seconds, but product requirements allow exact convergence to happen later. Interactive output needs a faster settled representation.

These results support further investment in vector authority. They do not support canonical vector replay as the only interactive renderer.

## What the external review changed

The review increased my confidence in the overall project while lowering my confidence in the current benchmark labels and cache coordinator.

### The 3 MB zoom clear is probably the largest immediate waste

Every zoom currently clears the full inactive 1104×1344 RGB565 atlas before writing only the visible region:

`esp32/main/interactive_pan_benchmark.cpp:877`

That clear writes 2,967,552 bytes. Using earlier measured PSRAM throughput, the review estimates about 82 ms. The estimate needs a direct hardware measurement, but the clear is unquestionably unnecessary work on the zoom critical path.

The combination of removing this clear, using nearest-neighbor preview, and submitting strips as they become ready gives a credible path to first feedback under 100 ms. The earlier 65 ms preview is stronger evidence for this than projections alone.

### Several benchmark labels overstate what was measured

The review correctly points out:

1. `first_valid_us` is recorded after the visible strips have been queued, not when the LCD transfer-completion callback fires. It is neither first physical pixel nor final physical completion.
2. Zoom timing starts after cancellation, hiding up to 43.7 ms from the main metric.
3. The 200% p95 uses only the first 256 retained samples from 599 frames.
4. Zero missing frames excludes pan requests rejected before presentation. It proves that known-invalid pixels were not displayed, but it does not prove smooth pan.
5. Settled timestamps have different meanings for zoom-in and zoom-out.
6. Center exact includes all 448 rows of the center cache cell even though only 372 rows are visible, and neighboring seam work may run first.
7. Drawing p95 measures only `canvas.raster().update()`. It excludes pen-down cancellation, vector setup, finalization, capture, commit, and invalidation.

The next benchmark should record input receipt, cancellation completion, strip ready, strip submitted, strip transfer completed, full visible completion, visible settled, visible exact, and pan refusal/fallback events.

### Verified implementation findings

I checked the important findings against the current source:

- Off-region primitives consume arena capacity before rejection in `core/src/viewport_renderer.cpp`. The supplied host-tested patch corrects this and adds a regression test.
- The interactive coordinator does not provide `ViewportRenderOptions::execute`, so the existing two-lane tile compositor is disabled there.
- Every 32-row exact job invokes a new renderer pass. This repeats candidate queries and raw-sample geometry work.
- Cache publication holds the cache mutex while staging and queueing display work.
- Each refined band posts an event into the touch queue and may trigger toolbar processing.
- One stroke outside the macrogrid permanently enables all-strokes fallback.
- The benchmark allocation provides 16,384 samples for 1,100 strokes, only 14.9 samples per stroke at full stroke capacity.
- The bilinear source-validity halo divides an already pixel-space halo by source zoom, under-checking the required neighboring sample at high source zoom.
- The large-stroke fallback reconstructs an entire stroke once per touched tile.
- Camera projection uses software-emulated `double` arithmetic per projected sample.

The supplied renderer patch passed host and sanitizer validation in the external review. The coordinator patch only passed a dry-run and should be reviewed, built, and tested incrementally rather than applied blindly.

## Where I agree with the architecture proposal

### Complete overview

A complete lowest-resolution overview gives the system a valid source for cold startup, zoom fallback, uncached pan regions, and zoom-out. It eliminates checkerboards and pan refusal without requiring high-resolution tiles to be ready.

The world bound must be chosen before fixing its representation. A 4096×4096 world at 12.5% needs a 512×512 overview, which costs 512 KiB in RGB565 or 256 KiB in RGB332. An 8192×8192 world changes the budget materially.

### World-aligned tile cache

Higher zoom levels should be sparse and world-aligned. Stable tile keys allow reuse across camera movement, avoid giant atlas clears and rebases, support incremental invalidation, and make relationships between power-of-two zoom levels predictable.

Only the overview should be complete. The tile cache should retain visible, nearby, recent, and possibly persisted tiles at higher levels.

### Separate settled and exact renderers

The settled renderer should prioritize recognizable clean output:

- simplified centerlines or capsules;
- zoom-specific LOD;
- fixed-point transforms;
- 2×2 coverage or analytic edge coverage;
- solid interior spans;
- geometry reuse across multiple tiles.

The canonical curved-ribbon renderer should remain available for exact idle refinement, export, and reference validation.

Trying to make canonical replay satisfy the 500 ms settled contract is the wrong optimization target.

### Incremental append updates

When operation 1,001 is committed, resident raster tiles should apply operation 1,001 in document order rather than replaying the preceding 1,000 operations. The overview and visible active-level tiles receive immediate updates; adjacent zoom levels update while idle; nonresident affected tiles become stale.

Append-only pen operations and the current opaque-white eraser semantics support this. Undoing or editing an older operation needs checkpointed replay for affected tiles.

### Decouple rendering from publication size

Coverage microtiles, cache tiles, geometry supertasks, and panel strips solve different problems and should not share one forced dimension.

A reasonable starting hypothesis is:

| Role | Starting size |
|---|---:|
| Coverage microtile | 32×32 |
| Cache/publication tile | 64×32 or 64×64 |
| Geometry supertask | about 128×96 or 128×128 |
| Panel strip | based on measured transfer-buffer capacity |

The renderer should generate geometry once for a supertask, bin it into several microtiles, and publish smaller cache or panel units as they complete.

### Dedicated display scheduler

Display publication deserves a deep module with a small interface. Its implementation should hide DMA staging, transfer completion, strip ordering, overlay composition, cache-slot pinning, publication generations, and metrics.

The cache mutex should not remain held while waiting for panel queue capacity. The scheduler needs slot/version pinning so it can release shared state before staging and submission.

## Where I qualify the review

The recommendation to avoid ordinary pan refusal is correct for production. I would solve this through the complete overview and tile cache rather than spending much effort adding overview behavior to the disposable atlas coordinator.

The proposed 64×32 tile and 128-slot ring are useful initial values, not final decisions. The bounded-world size, vector encoding, renderer scratch, overview format, and reserve requirements should determine the final budget.

The stated 85% confidence and future latency ranges are engineering estimates. The strong evidence is narrower:

- the current zoom clear is avoidable;
- the hardware has already shown about 65 ms nearest-preview presentation;
- cached pan and live raster update meet their current gates;
- canonical replay cannot serve as the interactive settled path.

## Reassessment without a time constraint

Removing engineering time as a constraint makes me more willing to commit to the proper architecture now. It does not change the fixed hardware limits:

- 8 MiB PSRAM;
- finite panel and PSRAM bandwidth;
- arbitrary documents can force seconds of exact ordered rendering;
- a complete raster pyramid through 800% cannot fit;
- old-operation Undo or edits require replay or checkpoints.

Additional engineering can remove major categories of repeated work:

- compact fixed-point vector encoding and delta compression;
- append-time sample coalescing and LOD generation;
- complete overview maintenance;
- sparse world-aligned tile levels;
- incremental resident-tile updates;
- checkpointed replay for Undo;
- a specialized settled renderer;
- geometry supertasks and two-core rasterization;
- persistent settled or exact tiles;
- fixed-point, SIMD, and selected assembly kernels.

I would still run the instrumented no-clear/nearest/settled experiment. Its role is to measure mechanisms and calibrate the new architecture, not to decide whether a few more days are justified.

## Recommended work sequence

### Evidence and correctness

- Integrate the safe renderer capacity/off-region fixes.
- Correct the bilinear source halo and coordinator failure paths.
- Add LCD submission and completion sequence numbers.
- Start interaction timing before cancellation.
- Record requested, accepted, refused, and fallback pan frames.
- Define visible validity, settled, and exact over the actual physical region.
- Capture realistic stroke sample distributions and near-capacity behavior.

### Critical-path preview

- Remove full-atlas clearing from interaction.
- Specialize power-of-two nearest preview.
- Generate focal or center-out strips and submit each immediately.
- Measure first and last transfer completion.
- Remove per-band touch-queue refinement events.
- Reduce cache-lock scope around display work.

### Settled rendering

- Restore two-core tile compositing with touch-safe priorities.
- Add a 2×2 settled coverage mode.
- Prototype simplified centerline/capsule rendering.
- Generate multiresolution centerline LOD at append time.
- Render larger geometry supertasks and publish smaller tiles.
- Measure visible settled output on captured realistic documents.

### Production storage and cache

- Fix the bounded world size and overview format.
- Design compact operation and LOD storage.
- Build the complete overview.
- Replace the camera-aligned atlas with a world-aligned tile ring.
- Add directional prefetch and overview-derived misses.
- Incrementally update resident levels on commit.
- Add checkpointed tile replay for Undo.
- Persist expensive materializations where flash behavior permits it.

### Low-level optimization

- Profile CPU cycles, PSRAM waits, lock waits, display staging, and DMA queue delays.
- Replace software `double` transforms with bounded fixed-point or local `float` where safe.
- Inspect compiler output for packed pixel loops.
- Use Xtensa intrinsics before raw assembly.
- Keep assembly only when it improves a user-visible path by at least about 10% and has a portable tested reference.

## Assembly opportunities

Assembly can improve selected kernels after the architecture stabilizes. It cannot compensate for full-atlas clearing, repeated geometry reconstruction, or poor scheduling.

### Power-of-two preview resampling

This is the best early low-level target. Fixed zoom levels allow specialized kernels:

- 100% to 200% duplicates pixels and rows;
- 200% to 100% selects or filters fixed positions;
- 100% to 50% performs a fixed 2×2 reduction;
- larger transitions compose these operations.

The generic nearest loop in `core/src/raster_materializer.cpp` performs fixed-point mapping and bounds checks for each pixel. Specialized integer C++ should remove most of that work. Xtensa intrinsics or assembly are justified only if generated code remains inefficient.

A 2–4× kernel gain is plausible. This path directly affects first zoom feedback.

### Display staging and RGB565 byte swapping

`esp32/main/hardware_app.cpp` reads raster pixels, applies overlays, byte-swaps RGB565, writes internal DMA buffers, and queues transfers. The no-overlay path already packs two pixels into a 32-bit operation.

Before writing assembly, test architectural alternatives:

- store hot tiles in panel byte order;
- keep overlays in panel byte order;
- separate ordinary strips from overlay strips;
- test direct DMA from suitable buffers.

If staging remains necessary, a packed copy/swap kernel may gain about 1.3–2×. The end-to-end benefit depends on PSRAM and queue wait time.

### RGB565 compositing

`core/src/coverage_tile.cpp` performs three channel blends per partially covered pixel. Add the zero and fully opaque fast paths first. The external patch adds direct assignment for alpha 255.

Packed arithmetic or Xtensa DSP instructions may process several narrow integer operations efficiently. Carry isolation between RGB565 channels requires careful implementation and randomized equivalence tests. A 1.5–3× kernel gain is plausible.

### Settled capsule and scanline kernels

The future settled renderer is likely the most important assembly target. Candidate loops include:

- fixed-point segment transforms;
- capsule distance tests;
- scanline extent calculation;
- solid interior span writes;
- 2×2 edge coverage;
- partial RGB565 blending.

A 1.5–3× gain in these kernels could materially reduce visible-settled latency. The renderer needs to exist and be profiled before choosing instructions or data layout.

### Bilinear resampling

The current bilinear path reads four RGB565 samples and interpolates three channels per output pixel. Precomputed horizontal maps, interior/edge loop separation, packed arithmetic, and intrinsics can help.

Bilinear should not remain on the first-valid path. Optimize it only if filtered intermediate output remains valuable after nearest preview.

### Canonical coverage rendering

The current 4×4 circle and convex coverage loops are expensive. Possible low-level improvements include fixed-point edge equations, packed sample tests, and optimized blending.

Algorithmic changes come first: analytic interiors, scanline spans, difference arrays, geometry reuse, and avoiding per-band reconstruction. Canonical rendering is idle work, so its assembly priority is lower than preview and settled rendering.

### Areas where assembly is the wrong fix

- Delete the interaction-time 3 MB clear instead of accelerating it.
- Replace or repair macrogrid overflow behavior instead of vectorizing all-stroke scans.
- Generate long-stroke geometry once instead of accelerating samples-times-tiles reconstruction.
- Specialize bounded camera transforms in C++ instead of hand-writing trivial arithmetic.
- Remove copies before replacing standard `memcpy` or `memset`.

## Assembly acceptance criteria

Each low-level kernel should have:

- a portable reference implementation;
- deterministic and randomized equivalence tests;
- edge-width, alignment, and out-of-bounds tests;
- documented RAM, alignment, and byte-order requirements;
- physical benchmarks for internal RAM and PSRAM;
- end-to-end measurements for the relevant interaction.

I would keep hand-written assembly only when it provides at least a 1.5× kernel gain and about a 10% improvement in the user-visible path.

## Expected product ceiling

With the recommended architecture and enough engineering, these ordinary-case targets look credible:

| Interaction | Credible range |
|---|---:|
| Live drawing | 5–10 ms |
| Warm cached pan | 25–35 ms submit path |
| First completed valid zoom strip | 40–90 ms |
| Complete visible fallback | 70–150 ms |
| Visible settled viewport | 150–500 ms |
| Warm cached zoom | approximately one display transfer |
| Ordinary exact convergence | 1–3 seconds |
| Pathological exact convergence | 5–15 seconds or more |

These are targets and engineering estimates. The next hardware instrumentation should replace them with distributions from real captured documents.

## Conditions that would change the recommendation

I would reconsider the design if any of these remain true after implementing the corresponding architecture rather than testing the current atlas:

1. Append-time LOD plus a simplified settled renderer cannot settle a captured realistic 1,000-stroke viewport within 500 ms at 200% and 400%.
2. The overview, compact document, renderer, and useful tile cache cannot coexist while retaining about 1–1.5 MiB of PSRAM reserve.
3. Incremental resident-tile updates push full pen event-to-submit latency above 10 ms.
4. Overview-derived pan misses or publication still cause frequent stalls during ordinary human swipes.
5. Users reject the visual transition between overview, settled, and exact output.
6. Real captured documents require more compact storage than the hardware can support.
7. Only 800% fails its product gate. In that case, cap zoom at 400% rather than abandoning vector authority.

Several-second exact convergence by itself is not a stop condition.

## Summary for the second reviewer

My recommendation is to proceed with vector authority and move to a complete overview plus world-aligned sparse tile cache. Interactive rendering should use incrementally maintained raster tiles and a purpose-built settled renderer. Canonical replay should run during idle time.

The immediate experiment should correct the measurement endpoints, remove the 3 MB zoom clear, use nearest progressive strips, restore two-core rendering, and test a 2×2 or simplified settled path on real captured documents. Regardless of implementation duration, this experiment is useful for measuring mechanisms before the production cache migration.

Assembly is worth considering for specialized power-of-two resampling, display staging, RGB565 compositing, and the future settled renderer. It should follow algorithm and data-layout changes, not precede them.
