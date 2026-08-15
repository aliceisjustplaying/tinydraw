# TinyDraw V2 roadmap

Last updated: 2026-08-14
Current branch: `main`

Status: **Vector V2 foundation and bounded UI refinement accepted; measured performance round next**

This is the current worklist and source of truth for TinyDraw V2. It replaces the
prototype-era task order as the forward plan. Historical plans and receipts are
evidence, not an instruction to reopen settled architecture questions.

## Product decision

The Vector V2 architecture is accepted. This is the app now—not another
prototype, not yet the shipping/default firmware, and not a reason to start a
clean rewrite. The existing shipping code is **Raster V1**.

V2 stays in this repository beside the working raster app until it reaches
feature parity. Stable mechanisms may be shared through explicit dependencies,
but the V1 and V2 coordinators, state models, and build targets must remain
separate. Do not grow V2 inside `hardware_app.cpp`, and do not retrofit V2 tile
semantics into `WorldCanvas` or `FirmwareCanvas`.

## Completed milestone: production vector foundation

The following has been proven on the physical ESP32-S3 with the deterministic
seed-7 1,000-stroke workload and an aggressive manual glass test:

- [x] Vector operations are authoritative.
- [x] Pen and eraser operations preserve painter order and revision authority.
- [x] Perfect-Freehand-style live ribbons remain responsive at 400%.
- [x] The bounded world is 1472×1792 units.
- [x] Committed zoom levels are 25%, 50%, 100%, 200%, and 400%.
- [x] A complete 368×448 overview is always available at 25%.
- [x] World-aligned 64×64 materialized tiles provide sparse detail at tiled zooms.
- [x] Cache misses fall back to the current overview instead of checkerboards.
- [x] Revisions invalidate stale materialization safely.
- [x] Hard-edged immediate materialization never masquerades as settled AA.
- [x] The tile producer is bounded and aborts stale work.
- [x] Drawing while refinement is active works.
- [x] Paper occupancy and compact uniform identities avoid wasting raw slots.
- [x] The complete seed-7 100% world fits: 266 raw + 378 uniform, zero fallback.
- [x] The raw pool uses 384 slots; a mutation-free tour A/B measured zero
      return-trip refills at 384 versus 63 tiles and 409 ms at 320.
- [x] Framebuffer overlap is reused during ordinary warm pans.
- [x] Cached pan reaches first physical completion in about 30.6–30.7 ms.
- [x] A live, separate 1.5 MiB contiguous USB/export reserve still allocates.
- [x] Forty-five rapid manual strokes committed with zero touch errors,
      presentation failures, stale authorities, or corruption.
- [x] Fable high and Grok 4.6 high found no remaining blocker after fixes.
- [x] Host tests, ASan/UBSan, formatting, clang-tidy, and cppcheck pass.
- [x] Long strokes split into one logical gesture without losing boundary samples.
- [x] A transition-preserving touch FIFO retains Down/Up/final points across render stalls.
- [x] A nearly three-minute physical stroke test committed all 8,003 live samples;
      two intentional boundary overlaps yielded 8,005 stored samples.

Load-bearing evidence:

- `vector_v2/GATE_1_RECEIPT_2026_08_13.md`
- `vector_v2/GATE_1_CACHE_CLOSURE_2026_08_13.md`
- `vector_v2/hardware-receipts/CORRECTNESS_CLOSURE_2026_08_14.md`
- `vector_v2/hardware-receipts/gate1-paper-cache-scroller.log`
- `vector_v2/hardware-receipts/gate1-final-glass.log`
- `vector_v2/hardware-receipts/PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md`
- `vector_v2/hardware-receipts/live-ink-overlay-clipping-2026-08-14.md`

The overall rendering-quality verdict remains **YELLOW** only because settled
anti-aliasing is not implemented. The architecture itself is no longer under
referendum unless new contradictory hardware evidence appears.

## Definition of V2 feature complete

V2 is feature complete when it can replace the raster app for ordinary use:

- [ ] Drawing, erasing, panning, and every committed zoom work through the
      production document and materialization path.
- [ ] Input remains responsive while rendering, committing, saving, or exporting.
- [ ] Settled output has an accepted anti-aliasing path.
- [ ] Undo and redo work on vector authority.
- [ ] New/Clear is transactional and resets all derived state.
- [ ] A document survives restart through vector persistence and autosave.
- [ ] PNG/USB-C export works from the V2 document without violating memory gates.
- [ ] Battery, power, sleep/wake, time, and failure-recovery behavior reach V1
      parity where applicable.
- [ ] Capacity exhaustion is reported safely rather than corrupting authority.
- [ ] The production UI is usable on physical hardware.
- [ ] Long-session and restart tests show no corruption, leaks, or stale pixels.
- [ ] V1 and V2 build independently before V2 becomes the default.

Performance polish may continue after feature completeness, but input starvation,
wrong pixels, missing operations, stale revisions, and failed persistence are not
polish and must be closed first.

# Forward worklist

## Phase 1 — Interaction reliability and navigation

This is the immediate next batch because it addresses failures felt in the final
manual test.

### Input-first scheduling

- [x] Build a deterministic hardware repro for touch starvation during cold fill
      and stroke commit.
- [x] Make producer and display work yield to pending input at bounded intervals.
- [x] Prevent a pan gesture from being lost when refinement is in progress.
- [x] Add a tiny independent touch-sampling task; the renderer remains on its
      measured cooperative path rather than moving wholesale to the second core.
- [x] Give the touch sampler explicit ownership and shutdown, preserve transition
      edges and final points in a bounded FIFO, and treat transient read errors as
      hold rather than lift.
- [x] Separate input latency from append, compose, and transfer telemetry.
- [x] Gate maximum touch-poll gaps during deterministic cold replay; retain p95
      gesture sampling for the final interaction glass test.
- [x] Repeat the aggressive draw-while-pan glass test.

Current measured debt:

- ordinary warm-pan frames remain roughly 45–50 ms end-to-end; the final glass
  test reused the framebuffer on 18.3% of 400% frames and 2.9% of 200% frames;
- a warm multi-zoom cache makes lower-zoom commits unbounded: the final glass
  test reached 120.1 ms per chunk at 25% and 131.8 ms at 100%;
- one repeated-reset harness run lost TE synchronization at startup; later
  clean runs showed no logged tearing, but physical glass remains authoritative;
- complete PNG/USB export triggers the five-second CPU-0 task watchdog during
  encoding even though the generated image and mounted media are correct.

At `264b60e`, the controlled 400% long-stroke gate fell from about 70 ms to
11.1 ms. The final physical long gesture measured 13.3 ms and looked smooth.
That gate did not cover lower-zoom drawing against a warm multi-zoom cache. See
`vector_v2/hardware-receipts/PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md`.

### Camera and zoom behavior

- [x] Write and accept the zoom/navigation behavior document before implementation.
- [x] Preserve world-space focus when zooming in or out.
- [x] Remember the last useful camera position per zoom.
- [x] Define how per-zoom memory and center-preserving zoom interact.
- [x] Expose 50% and 200% in the production UI.
- [x] Remove the test-app behavior that always opens a zoom at `(0,0)`.
- [x] Add host tests for clamping at all world edges and zoom round trips.
- [x] Exercise repeated zoom-out/return cycles on physical glass.

### Cache policy

- [ ] Protect the recent viewport footprint at each zoom from ordinary global-LRU
      churn.
- [ ] Prefer evicting distant/unprotected raw tiles.
- [x] Preserve zero-fallback returns for protected views across a long 400%
      tour: permanent `TINYDRAW_GATE1_CACHE_TOUR` gate (protected 100% home
      returns sharp after a 16-viewport 400% tour at both 320 and 384 slots).
- [ ] Keep learned uniform identities cheap and revision-safe.
- [x] Measure 320 versus 384 raw slots only after policy improvements: 384
      adopted at `6abfa0f`, funded by the 449 KiB freed at `264b60e`.
      Retention and export-reserve gates green; largest free PSRAM block
      2,490,368 bytes (harness) / 2,949,120 bytes (product). The cache-tour
      A/B then proved the benefit directly: returning through a 16-viewport
      400% tour refills 63 tiles in 409 ms at 320 slots versus 0 tiles in
      40 ms at 384 (`cache-tour-320.log` / `cache-tour-384.log`).
- [ ] Do not adopt 448 slots under the current memory plan: it would consume an
      additional 1 MiB and leave too little margin beside the export reserve.
- [x] Remove the test-only seed corpus storage from the product app allocation;
      it remains available only to the exclusive Gate 1 harness.

The 384-slot pool remains adopted for the UI round because its mutation-free
revisit benefit and memory margin are measured. The next performance round must
repeat the 320-versus-384 comparison with drawing between visits. Drawing
latency wins if the two goals conflict. Each raw slot costs 8 KiB.

## Phase 2 — V2 cleanup and repository shape

This bounded cleanup completed before the interaction batch. The milestone
exposed and hardware-proven the real seams and was fast-forwarded to `main`; it
was not a rewrite or feature-parity promotion.

- [x] Rename the interactive `production-live-app` concept to the V2 application
      and remove its test-only startup workload.
- [x] Put V2 ESP adapters/coordinator/UI under a clear V2-specific directory or
      source group; do not mix them into legacy `hardware_app.cpp`.
- [x] Keep `vector_v2/` platform-independent and host-tested.
- [x] Keep truly shared mechanisms in `core/`; share by dependency, never copy.
- [x] Leave the raster coordinator behaviorally unchanged.
- [x] Add explicit build commands for both `raster-v1` and `vector-v2`.
- [x] Make the named validation commands build both firmware variants. Add
      hosted CI for ESP-IDF only when a CI workflow is introduced.
- [x] Remove test-only workload generation and automated gate orchestration from
      the V2 product coordinator; retain it in the exclusive
      `vector-v2-gate-harness` target.
- [ ] Audit remaining rejected or unused experiments only when a current seam
      makes their removal relevant; this is not an interaction-batch blocker.
- [x] Quarantine prototype-only renderer sources in the explicit
      `tinydraw::vector_prototype` test/benchmark target; normal host and product
      targets do not compile or link them.
- [x] Archive superseded root handoffs and interim Gate 1 plans.
- [x] Keep only `README.md`, `PROJECT_STATE.md`, this roadmap, and genuinely
      current design documents prominent at repository root.
- [x] Add an index for hardware receipts; retain intermediate captures without
      deleting load-bearing final receipts.
- [x] Re-run both full build/test matrices after every structural move.

### Coexistence rules

1. V1 remains runnable until V2 feature parity and migration acceptance.
2. V1 receives only regressions/security/hardware-preservation fixes.
3. New product behavior lands in V2.
4. Shared code must have a platform-neutral interface and tests.
5. No shared mutable application state between V1 and V2.
6. No V2 `#ifdef` branches inside the V1 interaction loop.
7. When V2 supersedes a responsibility, remove the duplicate V2 test adapter—not
   the still-runnable V1 path—until final promotion.
8. Switch the default firmware only after feature parity, migration proof, and a
   final V1/V2 comparison. Keep a named legacy V1 build afterward.

## Phase 3 — Settled anti-aliasing gate

Timebox this. Do not disappear into another renderer project.

- [ ] Implement the smallest analytic-coverage AA experiment for one bounded tile.
- [ ] Compare it visually against hard-edged immediate output and the rejected
      four-sample SSAA receipt.
- [ ] Measure tile compute, complete cold viewport, maximum cooperative slice,
      PSRAM traffic, and input latency.
- [ ] Verify painter order and eraser edges.
- [ ] Publish accepted AA output as `kSettled` or better.
- [ ] Keep hard-edged `kImmediate` as live/early feedback.
- [ ] Prove that settled output replaces immediate output one-directionally.
- [ ] If analytic AA misses the timebox or gates, record the result and choose a
      simpler coverage policy rather than reviving the rejected atlas renderer.
- [ ] Decide the named subpixel-stroke policy, including the current minimum
      visible screen radius.

The prior complete-viewport four-sample SSAA probe took about 808 ms and is not
the funded product path.

## Phase 4 — Production UI

### Toolbar

- [x] Bottom toolbar: **Undo | Redo | Tools | Colors | Sizes | Document**.
- [x] Make the toolbar a full-width rectangle with a subtle upper shadow so
      canvas pixels cannot creep through the curved lower screen corners.
- [x] Tools pop-up: **Draw | Erase | Pan**, using the V1 icons and reflecting the
      selected tool in the toolbar.
- [x] Document pop-up: **New | Export**.
- [x] Consume an outside tap while dismissing compact popups.
- [x] Disable Undo/Redo visibly when unavailable.
- [ ] Keep visual controls compact but later enlarge invisible hit regions and
      spacing; physical tap targets remain a final glass-polish item.

### Color palette

- [x] Replace current TinyDraw colors with the 16 standard PICO-8 colors.
- [x] Add the 16-color PICO-8 secret palette as a second page.
- [x] Use a white second-level 4×4 round-swatch popup above the toolbar, with a
      tall page-navigation row and a shadow.
- [x] Keep all 16 colors on each page; do not sacrifice a swatch for navigation.
- [x] Keep PICO-8 warm white as a drawable color; Erase remains a separate tool.
- [x] Convert and lock all 32 colors to deterministic RGB565 values in tests.
- [x] Show the current palette page and selected color clearly.

Palette references:

- <https://pico-8.fandom.com/wiki/Palette>
- <https://lospec.com/palette-list/pico-8-secret-palette>

### Zoom and navigation overlays

- [x] Add a right-side vertical zoom rail: plus, percentage, minus.
- [x] Levels: 25 → 50 → 100 → 200 → 400.
- [x] Disable controls at minimum/maximum zoom.
- [ ] Add small top/left/right/bottom canvas-extent indicators.
- [ ] Hide extent indicators at 25%.
- [x] Keep navigation controls, battery, and minimap as display overlays, never
      document pixels.
- [x] Show battery state and transient export progress/status above the canvas.
- [x] Refresh minimap authority after every completed stroke, including strokes
      whose final overview update has empty canvas presentation bounds.
- [x] Exclude fixed overlays from cached-pan reuse and live-ink partial updates;
      the hardware circle gate stays below 3 ms both on clear canvas and over
      the overlays.
- [ ] Reduce changing-minimap cost during pan; the current 100% pan-overlay gate
      remains red at about 40 ms first-submit.

### Tap-target polish — later

- [ ] Expand hit regions beyond visible icon bounds.
- [ ] Resolve overlaps deterministically at toolbar/pop-up boundaries.
- [ ] Add pressed/selected feedback before expensive actions.
- [ ] Test every target repeatedly with large hands on physical hardware.
- [ ] Record missed-tap telemetry during the UI glass test.

## Phase 5 — Vector document feature parity

### Undo and redo

- [ ] Define operation-history and snapshot/checkpoint semantics.
- [ ] Implement vector-aware Undo and Redo without restoring stale tile identities.
- [ ] Invalidate/rebuild overview and materialization transactionally.
- [ ] Bound history memory and define behavior at capacity.
- [ ] Test mixed pen/eraser operations, zoom changes, restart, and branch-after-undo.

### New/Clear

- [ ] Make New/Clear reset operation authority, overview, cache catalog, occupancy,
      camera state, undo/redo, and autosave state together.
- [x] Restore the V1-style modal confirmation/cancel behavior.
- [ ] Test failure atomicity.

### Persistence and recovery

- [ ] Design a versioned vector document format with explicit geometry, palette,
      operation/sample capacities, checksum, and compatibility policy.
- [ ] Save transactionally; interrupted writes must preserve the last valid file.
- [ ] Load operation authority and regenerate derived state safely.
- [ ] Implement autosave without starving input.
- [ ] Recover after reset/power loss during append and during save.
- [ ] Define migration behavior for existing raster drawings: preserve V1 access,
      import flattened raster where useful, or explicitly keep formats separate.
- [ ] Never silently reinterpret raster tile persistence as vector authority.

### PNG and USB-C export

- [x] Reuse the stable PNG encoder, FAT16 disk, USB transport, and export-store
      mechanisms through narrow adapters where their contracts fit. Landed at
      `7302963`; en route, `88123ee` fixed a latent pngenc corruption path
      that also affected Raster V1 exports of dense/wide content and added a
      full decode round-trip host gate.
- [x] Render/export the complete bounded V2 world with correct painter order
      (`WorldBandRenderer`: banded forward authority replay, host-proven
      equal to one-shot replay).
- [x] Keep export scratch within the proven 1.5 MiB reserve. Measured:
      51 KiB internal (deflate state; internal placement is what makes the
      encode take 5.7 s instead of minutes) plus ~390 KiB PSRAM, all
      transient; zero permanent live-storage growth.
- [x] Decide whether export uses settled AA or explicitly reports immediate
      quality: export replays exact hard-edged authority (identical to
      immediate rendering); revisit only after the settled-AA gate exists.
- [x] Test export while the cache is full: the harness export gate runs after
      the cache/full-world gates and the export-reserve gate re-verifies
      afterward. Long-session soak remains part of Phase 7.
- [x] Verify generated media on host and physical USB-C. The device artifact
      passes strict zlib/CRC/defilter decoding, and the final glass test mounted
      the "TinyDraw Export" drive and opened the correct 1472×1792 DRAWING.PNG.
      Activating USB ends the serial console until reset, exactly like V1.
- [x] Yield at each rendered-band progress boundary so CPU 0's idle task can run.
- [ ] Capture a clean hardware export receipt proving the former five-second
      task-watchdog warning is gone; implementation alone is not closure.
- [x] Show bounded export progress and a saved/error state before USB takes over
      the port.

### Device lifecycle parity

- [x] Integrate the live battery percentage/charging display.
- [ ] Integrate low-power, sleep/wake, and RTC/time behavior where V1 supports it.
- [ ] Preserve autosave before risky power transitions.
- [ ] Define visible errors for save, export, capacity, and hardware failures.
- [ ] Reuse stable V1 hardware services through adapters; do not copy their logic.

## Phase 6 — Performance campaign

Correctness and profiling come before cleverness. Optimize one measured bottleneck
at a time and retain before/after receipts.

### Warm pan

- [x] Raise warm-pan frame rate above the current roughly 20 FPS behavior
      (30 FPS floor with margin at `4022917`: 28.1 ms avg, p50 26.95 ms).
- [x] Avoid moving the complete frame per pan step: the toroidal ring makes
      scroll pointer math and composes only exposed strips; the full-panel
      transmit remains (the panel shows the whole moved viewport) but is
      beam-raced and DMA-bound.
- [x] Increase framebuffer-reuse coverage in the wild: fallback pixels and
      composition-epoch drift no longer break the cached-pan identity, and
      refinement region presents preserve reusability (the 2026-08-14 manual
      session measured reused=0 on all 386 real pan frames before this).
- [ ] Coalesce touch samples/view updates when the panel is already busy.
- [ ] Separate camera responsiveness from refinement presentation.
- [ ] Measure p50/p95/max event-to-first-complete and dropped/coalesced updates.

### Cold refinement

Across 20 reset-separated runs, the earlier overlapping-XL regression gate has
0.771–0.971 second p95 wall time across 50–400%, with bounded producer/input
slices. The earlier tapered seed-7 400% case has 0.683 second p95 wall time. See
`vector_v2/hardware-receipts/492f2ef-overlap-cold-p95-20-runs.log` and
`vector_v2/hardware-receipts/0560525-overlap-cold-baseline.log`.

The subsequent four-times-adversarial campaign reduced its 400% cold replay from
9.703 seconds at clean commit `a3ac4fc` to a 1.452-second p95 across 20 clean,
reset-separated runs at `26a05f5`; overlap and seed-7 400% p95 measured 0.416 and
0.343 seconds. Saturation-gated replay at `092f2a3`/`264b60e` then cut the
adversarial 400% p95 to 0.675 seconds (target: below one second) and the
overlap corpus by 14–30%, with maximum ticks under 11 ms; seed-7 regressed
6.4% to 0.365 seconds (attributed to producer restructure/code layout, receipt
retained). Deadline-sliced cold fill (`f20c201`) and the 384-slot pool
(`6abfa0f`) then settled the final distribution at 0.646 seconds for
adversarial 400% (−55.5% overall) with every producer tick under 12.7 ms. See
`vector_v2/hardware-receipts/6abfa0f-cold-p95-20-runs.log` and
`vector_v2/hardware-receipts/LONGSTROKE_COLDRENDER_INVESTIGATION_2026_08_14.md`.

The permanent validation comprises one exact census sweep and three CTest fuzz
invocations backed by two distinct fuzzer implementations. Exact painter results
remain the authority; raw timing receipts are retained without editorial cleanup.

- [x] Profile operation bounds filtering, replay, raster coverage, publication,
      composition, touch polling, loop pacing, and panel transfer independently.
- [ ] Batch paper/uniform publication and display updates where it reduces work.
- [ ] Prioritize newly exposed regions and current motion direction.
- [ ] Cancel obsolete views immediately when the camera moves.
- [ ] Prevent progressive display work from delaying touch polling.
- [x] Remove redundant segment subdivision, reject distant segments, hoist
      software division, skip finalized mask runs, and replay eligible collinear
      runs per source segment without weakening exactness or interaction gates.
- [x] Capture a clean-HEAD 20-run distribution before tightening the current
      two-second four-times-adversarial alarm. Keep the alarm until the intended
      product workload margin is explicitly chosen.

### Memory and CPU mechanical sympathy

- [ ] Profile PSRAM traffic and cache behavior before changing representations.
- [ ] Eliminate redundant full-frame and raw-tile reads.
- [ ] Evaluate packed/fixed-point loops, IRAM placement, and wider pixel operations
      only in measured hot paths.
- [ ] Re-evaluate 320 versus 384 raw slots with a mixed warm-cache drawing and
      revisit gate; the existing tour A/B contains no intervening mutation.
- [ ] Keep the 1.5 MiB export reserve and a meaningful fragmentation margin.
- [ ] Use the second core only for a measured independent workload with explicit
      ownership, cancellation, and revision publication; do not add locks around
      the current deep modules speculatively.
- [ ] Add long-session heap/fragmentation and thermal/power measurements.

## Phase 7 — Robustness and release acceptance

- [ ] Characterize representative long documents, not only seed-7 synthetic data.
- [ ] Revisit operation/sample capacities with captured aggregate evidence.
- [ ] Exercise maximum stroke length, XL strokes, dense overdraw, erasing, and all
      world edges.
- [ ] Test repeated zoom/pan/draw/save/export cycles for hours.
- [ ] Test restart and power interruption at every persistence boundary.
- [ ] Verify no stale pixels after Undo, Redo, New, load, or autosave completion.
- [ ] Run host tests, sanitizers, format, clang-tidy, cppcheck, both firmware builds,
      hardware automation, and the final glass checklist.
- [ ] Compare V1 and V2 feature parity explicitly.
- [ ] Promote V2 to the default firmware only after the comparison passes.
- [ ] Retain an explicit legacy raster build and its regression smoke test.

# Stretch goals

These are intentionally outside feature completeness unless promoted later.

- [x] **Minimap skeleton:** compact live overview in the bottom-right canvas
      corner, directly above the toolbar and below the zoom rail.
- [x] Show the current viewport rectangle on the minimap.
- [ ] Hide or simplify the minimap at 25%.
- [ ] Tap the minimap to jump.
- [ ] Drag the minimap viewport to navigate continuously.
- [ ] Toggle the minimap by tapping the zoom percentage.
- [ ] Ensure minimap interaction never edits the document.
- [ ] 800% zoom, only if memory, navigation, and quality gates still pass.
- [ ] Additional document-management conveniences after New/Export are solid.

The minimap is unusually affordable architecturally because V2 already owns a
complete overview. It still needs careful tap targeting and viewport math.

# Explicit non-goals and guardrails

- No new clean-room rewrite.
- No further camera-aligned 3×3 atlas development.
- No V2 state inside `WorldCanvas`, `FirmwareCanvas`, or `hardware_app.cpp`.
- No four separately stored simplified stroke copies.
- No camera-aligned cache identities.
- No hidden dynamic allocation in Vector V2 state modules.
- No speculative second-core concurrency.
- No broad V1 refactor merely to make files look symmetrical with V2.
- No deleting V1 before V2 feature parity and promotion.
- No endless source-of-truth documents; update this roadmap and `PROJECT_STATE.md`.

# Next concrete sequence

Phase 0 of the second performance round is complete at `205fefe`: the
mixed-zoom drawing gate, the reconciled warm-pan attribution gate, a fresh
cold 20-run distribution, and the settled 320-versus-384 A/B all live in
[`vector_v2/hardware-receipts/PERF_ROUND_2_BASELINES_2026_08_14.md`](vector_v2/hardware-receipts/PERF_ROUND_2_BASELINES_2026_08_14.md).

1. ~~Fix drawing latency~~ **Done at `1848cc6`**: active-zoom mutation policy
   plus a 10 ms commit budget; `TINYDRAW_GATE1_MIXED_DRAW` green at every
   zoom and both slot counts and now part of the battery's final verdict. See
   [`vector_v2/hardware-receipts/DRAWING_LATENCY_CLOSURE_2026_08_14.md`](vector_v2/hardware-receipts/DRAWING_LATENCY_CLOSURE_2026_08_14.md).
   Residual: the ~13.7 ms uninterruptible 25% overview band replay is the
   commit ceiling; round-end glass must confirm transient-fallback feel.
2. ~~Raise warm pan to a 30 FPS floor (frame ≤ 33.3 ms)~~ **Done at
   `4022917`**: toroidal frame ring (scroll 15 ms → 10 µs), beam-raced
   push sweep (tear wait 4.2 ms → ~0.1 ms, frame time unlocked from the
   tear-period quantum), exposed compose fused into the sweep's DMA idle,
   and wild-reuse fixes so product pans actually take the cached path.
   `TINYDRAW_GATE1_PANSEQ`: 28.1 ms avg, p50 26.95 ms, p95 32.95 ms, both
   slot counts. See
   [`vector_v2/hardware-receipts/PAN_FLOOR_CLOSURE_2026_08_15.md`](vector_v2/hardware-receipts/PAN_FLOOR_CLOSURE_2026_08_15.md).
   Residual: worst single frame 33.95 ms (2% over floor, accepted); beam
   racing awaits a glass tearing re-check.
3. ~~Idle cache repair~~ **Done at `24a9fe9`** (inserted from the 2026-08-14
   glass session's strongest complaint): quiet moments rebuild dropped
   tiles — cardinal neighbors, remembered zooms, the full 100% grid — in
   bounded idle slices with no presentation; gate
   `TINYDRAW_GATE1_IDLE_REPAIR` (588 damaged → 0 remaining, worst slice
   7.5 ms) joined the battery verdict. See
   [`vector_v2/hardware-receipts/IDLE_REPAIR_CLOSURE_2026_08_15.md`](vector_v2/hardware-receipts/IDLE_REPAIR_CLOSURE_2026_08_15.md).
   Residual: 200%/400% stay neighborhood-only; fast continuous 400% panning
   can outrun repair.
4. Cold −50% campaign from the fresh distribution (adversarial 400% p95
   638 ms → 319 ms target).
5. Capture a clean export-watchdog receipt; investigate SVG eraser/mask
   semantics with the SVG encoder budgeted inside the existing 1.5 MiB export
   reserve. The startup TE flake is fixed at the root (`4022917`: TEON
   re-issued after display-on, plus a rate-limited runtime self-heal that
   logs `TINYDRAW_PANEL_TE_HEAL`).
