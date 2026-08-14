# TinyDraw V2 roadmap

Last updated: 2026-08-13  
Current branch: `feat/v2-navigation-interaction`

Status: **Vector V2 foundation validated; interaction integration in progress**

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
- [x] The raw pool remains 320 slots.
- [x] Framebuffer overlap is reused during ordinary warm pans.
- [x] Cached pan reaches first physical completion in about 30.6–30.7 ms.
- [x] A live, separate 1.5 MiB contiguous USB/export reserve still allocates.
- [x] Forty-five rapid manual strokes committed with zero touch errors,
      presentation failures, stale authorities, or corruption.
- [x] Fable high and Grok 4.6 high found no remaining blocker after fixes.
- [x] Host tests, ASan/UBSan, formatting, clang-tidy, and cppcheck pass.

Load-bearing evidence:

- `vector_v2/GATE_1_RECEIPT_2026_08_13.md`
- `vector_v2/GATE_1_CACHE_CLOSURE_2026_08_13.md`
- `vector_v2/hardware-receipts/gate1-paper-cache-scroller.log`
- `vector_v2/hardware-receipts/gate1-final-glass.log`

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

- [ ] Build a deterministic hardware repro for touch starvation during cold fill
      and stroke commit.
- [ ] Make producer and display work yield to pending input at bounded intervals.
- [ ] Prevent a pan gesture from being lost when refinement is in progress.
- [ ] Decide whether a tiny independent touch-sampling task is needed; do not move
      the whole renderer to the second core without measured need.
- [ ] Separate input latency from append, compose, and transfer telemetry.
- [ ] Gate maximum/p95 touch-poll gaps under stress.
- [ ] Repeat the aggressive draw-while-pan glass test.

Current debt from the manual trace:

- ordinary pan updates occupy roughly 46–50 ms end-to-end;
- stroke completion blocked touch polling for about 127 ms median, 175 ms p95,
  and 195 ms maximum;
- live ink itself remained fast at roughly 2–3 ms typical and 5.35 ms maximum.

### Camera and zoom behavior

- [x] Write and accept the zoom/navigation behavior document before implementation.
- [ ] Preserve world-space focus when zooming in or out.
- [ ] Remember the last useful camera position per zoom.
- [ ] Define how per-zoom memory and center-preserving zoom interact.
- [ ] Expose 50% and 200% in the production UI.
- [ ] Remove the test-app behavior that always opens a zoom at `(0,0)`.
- [ ] Add host tests for clamping at all world edges and zoom round trips.
- [ ] Add a hardware glass test that zooms out and returns to the same drawing.

### Cache policy

- [ ] Protect the recent viewport footprint at each zoom from ordinary global-LRU
      churn.
- [ ] Prefer evicting distant/unprotected raw tiles.
- [ ] Preserve zero-fallback returns for protected views across a long 400% tour.
- [ ] Keep learned uniform identities cheap and revision-safe.
- [ ] Measure 320 versus 384 raw slots only after policy improvements.
- [ ] Do not adopt 448 slots under the current memory plan: it would consume an
      additional 1 MiB and leave too little margin beside the export reserve.
- [x] Remove the test-only seed corpus storage from the product app allocation;
      it remains available only to the exclusive Gate 1 harness.

More raw slots can still help long 400% excursions, but policy comes first.
Each additional raw slot costs 8 KiB. A 384-slot experiment costs 512 KiB and
may be viable; it must re-prove the live export reserve and long-session margin.

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
- [x] Tools pop-up: **Draw | Erase | Pan**.
- [x] Document pop-up: **New | Export**.
- [x] Disable Undo/Redo visibly when unavailable.
- [ ] Keep visual controls compact but later enlarge invisible hit regions and
      spacing; physical tap targets are currently too easy to miss.

### Color palette

- [x] Replace current TinyDraw colors with the 16 standard PICO-8 colors.
- [x] Add the 16-color PICO-8 secret palette as a second page.
- [x] Use a full-screen 4×4 swatch grid with a small page header/navigation row.
- [x] Keep all 16 colors on each page; do not sacrifice a swatch for navigation.
- [x] Keep PICO-8 warm white as a drawable color; Erase remains a separate tool.
- [x] Convert and lock all 32 colors to deterministic RGB565 values in tests.
- [x] Show the current palette page and selected color clearly.

Palette references:

- <https://pico-8.fandom.com/wiki/Palette>
- <https://lospec.com/palette-list/pico-8-secret-palette>

### Zoom and navigation overlays

- [ ] Add a right-side vertical zoom rail: plus, percentage, minus.
- [ ] Levels: 25 → 50 → 100 → 200 → 400.
- [ ] Disable controls at minimum/maximum zoom.
- [ ] Add small top/left/right/bottom canvas-extent indicators.
- [ ] Hide extent indicators at 25%.
- [ ] Keep indicators and controls as display overlays, never document pixels.
- [ ] Reserve the top area for battery and transient toast/status messaging.

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
- [ ] Preserve confirmation/cancel behavior from V1 where desired.
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

- [ ] Reuse the stable PNG encoder, FAT16 disk, USB transport, and export-store
      mechanisms through narrow adapters where their contracts fit.
- [ ] Render/export the complete bounded V2 world with correct painter order.
- [ ] Keep export scratch within the proven 1.5 MiB reserve.
- [ ] Decide whether export uses settled AA or explicitly reports immediate quality.
- [ ] Test export while the cache is full and after long drawing sessions.
- [ ] Verify generated media on host and physical USB-C.

### Device lifecycle parity

- [ ] Integrate battery display and low-power behavior.
- [ ] Integrate sleep/wake and RTC/time behavior where V1 supports it.
- [ ] Preserve autosave before risky power transitions.
- [ ] Define visible errors for save, export, capacity, and hardware failures.
- [ ] Reuse stable V1 hardware services through adapters; do not copy their logic.

## Phase 6 — Performance campaign

Correctness and profiling come before cleverness. Optimize one measured bottleneck
at a time and retain before/after receipts.

### Warm pan

- [ ] Raise warm-pan frame rate above the current roughly 20 FPS behavior.
- [ ] Avoid transmitting the complete panel when controller/window semantics let
      us retain and update only newly exposed strips.
- [ ] Increase framebuffer-reuse coverage for large touch deltas without unbounded
      scratch.
- [ ] Coalesce touch samples/view updates when the panel is already busy.
- [ ] Separate camera responsiveness from refinement presentation.
- [ ] Measure p50/p95/max event-to-first-complete and dropped/coalesced updates.

### Cold refinement

- [ ] Profile operation bounds filtering, replay, raster coverage, publication,
      composition, and panel transfer independently.
- [ ] Batch paper/uniform publication and display updates where it reduces work.
- [ ] Prioritize newly exposed regions and current motion direction.
- [ ] Cancel obsolete views immediately when the camera moves.
- [ ] Prevent progressive display work from delaying touch polling.
- [ ] Improve the accepted 0.64–0.75 second cold-compute range without weakening
      correctness or interaction gates.

### Memory and CPU mechanical sympathy

- [ ] Profile PSRAM traffic and cache behavior before changing representations.
- [ ] Eliminate redundant full-frame and raw-tile reads.
- [ ] Evaluate packed/fixed-point loops, IRAM placement, and wider pixel operations
      only in measured hot paths.
- [ ] Re-evaluate 384 raw slots after cache policy and test-corpus removal.
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

- [ ] **Minimap:** compact overview in the bottom-right canvas corner, directly
      above the toolbar and below the zoom rail.
- [ ] Show the current viewport rectangle on the minimap.
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

1. Capture an unchanged-behavior hardware latency baseline with corrected
   same-event telemetry.
2. Implement the accepted navigation behavior and expose all five zooms without
   coupling camera authority to presentation or cache state.
3. Close input latency on hardware with bounded lift and refinement scheduling.
4. Protect one recent footprint per tiled zoom with soft eviction preference,
   then add minimal zoom controls and close the combined glass test.
5. Run the timeboxed analytic anti-aliasing gate.
6. Build the remaining product UI shell.
7. Implement vector persistence, Undo/Redo, New, and real export.
8. Integrate device lifecycle behavior and run the performance campaign alongside
   feature work, always from measured traces.
