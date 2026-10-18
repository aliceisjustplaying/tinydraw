# Synthesized correctness review: 2026-08-16

## Executive summary

This document merges and deduplicates two reviews of snapshot `e76b98e`, plus the first review's follow-up check of concurrent commit `f2f6da7`:

- Review A: `CORRECTNESS_REVIEW_2026-08-16.md` (32 findings, host tests and targeted reproducers).
- Review B: `review_findings_2026_08_16_correctness/REVIEW.md` (49 findings, source inspection only).

The synthesis retains **50 actionable correctness findings: 14 High, 31 Medium, and 5 Low**. Fourteen Review B findings overlap Review A and appear once here. Eighteen Review B findings add distinct issues. The remaining 17 Review B labels are recorded in the disposition table because they describe documented feature gaps, performance or maintenance work, unreachable defensive states, or claims that still need a contract/hardware check.

The highest-risk work remains visible drawing divergence and data loss (CR-002 through CR-006 and CR-031), false-green firmware gates (CR-010 through CR-017), and unsafe public raster descriptors (CR-027 through CR-030, CR-042, CR-043, CR-048, and CR-050). Review B also adds panel synchronization failures, replay/product-policy divergence, and normal-operation V2 touch overflow (CR-033 through CR-040 and CR-045 through CR-046).

This file is the consolidated output. It preserves source-qualified evidence, proposed fixes, and regression tests from both inputs while resolving duplicates and noting disagreements.

## Scope and review boundary

The last-24-hour review was pinned so concurrent performance work could not change the evidence underneath the review:

| Boundary | Commit | Timestamp / evidence |
|---|---|---|
| Parent immediately before the 24-hour range | `f66c808d79b0539f2eda3d54622fef09b4c3dc8f` | `2026-08-15 12:45:31 +0100`, `docs: bind the 20-run cold p95 distribution for the review-round baseline` (`git show -s`) |
| Main review snapshot | `e76b98eb3084533912b18e9307a4f375a76a77e4` | `2026-08-16 15:29:14 +0100`, `docs: hand over the oracle session and point the queue at Cold Stage B` (`git show -s`) |
| Concurrent commit reviewed separately | `f2f6da7e97265626ace1b96e363d92dbbe09c7df` | `2026-08-16 15:52:39 +0100`, `perf: publish tiles straight from the supertask surface` (`git show -s`) |

The pinned recent range `f66c808d..e76b98e` contains **143 changed files, +38,134/-1,792 lines** (`git diff --shortstat` and `git diff --name-only | wc -l`). The broader pass covered `core`, `vector_v2`, `esp32`, `rp2350`, `host`, `tools`, `scripts`, and `second_review_hardware_ab`; those trees contain **44,563 lines of C/C++ headers/sources, Python, and shell at `f2f6da7`** (`find ... | xargs wc -l`).

At `2026-08-16 16:16:40 +0100`, the live worktree remained at `f2f6da7` with uncommitted edits in:

```text
esp32/main/vector_v2/vector_v2_app.cpp
vector_v2/include/tinydraw/vector_v2/materialized_canvas.h
vector_v2/src/materialized_canvas.cpp
vector_v2/tests/incremental_document_test.cpp
vector_v2/tests/tile_producer_test.cpp
vector_v2/tools/raster_census.cpp
```

Those uncommitted edits were inspected for obvious correctness regressions but deliberately excluded from stable line-number findings because the user warned that another agent was actively changing them. CR-030 covers the last committed concurrent change, `f2f6da7`. By `2026-08-16 16:43:16 +0100`, `esp32/main/vector_v2/vector_v2_presenter.cpp` had also become modified; that later uncommitted edit remains outside the stable findings (`git status --short` receipt captured during synthesis).

### Severity model

- **High:** reachable user-visible corruption/data loss, a false-green mandatory gate, a hang, or a memory-safety defect on a supported path.
- **Medium:** narrower state divergence, diagnostic/tool false results, unsafe public API behavior, or a hardware-dependent correctness risk.
- **Low:** latent or narrow defects outside current product call paths.

## Deduplication and provenance

Review A's CR-001 through CR-032 remain CR-001 through CR-032. Review B maps as follows:

| Review B label | Consolidated result | Reason |
|---|---|---|
| H1 | CR-001 | Same open-gesture hardware zoom defect |
| H2 | CR-002 | Same zero-length unequal-radius defect |
| H3 | CR-020 | Same capture-dump starvation; CR-020 also retains Review A's in-flight append race |
| H4 | CR-013 | Same replay-versus-product chrome routing split |
| H5 | CR-014 | Same exhausted-while-pressed hang |
| H6 | CR-004 plus contract gap G1 | Same SVG/glass geometry split; PNG-only device delivery remains a documented gap |
| H7 | CR-018 | Same 2×2 ledger granularity defect |
| M1 | CR-033 | Unique: replay reject policy differs from product |
| M2 | CR-034 | Unique: capture and product derive events through different buffers |
| M3 | CR-003 | Same live-versus-authority geometry split |
| M4 | CR-009 | Same post-scroll failure/state divergence |
| M5 | CR-035 | Unique: partial presentations skip TE synchronization |
| M6 | CR-036 | Unique: modal chrome is painted during streaming |
| M7 | D1 | Current legal primitive bound is 9 and capacity is 10; retain as hardening, not a reachable defect |
| M8 | CR-008 | Same failed-present state divergence |
| M9 | CR-037 | Unique: neighbor repair bypasses saturation guard |
| M10 | G2 | Undo/Redo is already documented as incomplete |
| M11 | CR-019 | Same missing ledger eviction notification |
| M12 | D2 | `InkStream::ingest` explicitly returns the previous point when inactive; contract decision needed |
| M13 | N1 | Performance-budget result, not a correctness defect |
| M14 | CR-006 | Same blocking-export/drop-oldest V1 queue defect |
| M15 | CR-038 | Unique: gate build enters the product loop with gate-created state |
| M16 | CR-015 | Same replay timestamp/fidelity defect |
| M17 | CR-016 | Same 4,096-versus-12,288 capacity mismatch |
| M18 | CR-039 | Unique: normal V2 loop can refuse a Down during long lift work |
| M19 | CR-040 | Unique: failed stream does not drain submitted DMA |
| M20 | VFY-1 | IDF bus serialization was not verified; keep as an explicit verification item |
| M21 | CR-041 | Unique, but only under the off-by-default beam-race experiment |
| M22 | N2 | Comment/test hardening for a load-bearing radius constant, not a present defect |
| M23 | D3 | Validation appears to make `choose_slot()` failure unreachable; retain as transactional hardening |
| M24 | N3 | Delayed cold-fill scheduling is a performance/settling policy issue |
| L1 | CR-042 | Unique: overlapping append source invokes `std::copy` undefined behavior |
| L2 | D4 | Dump requires idle/non-touching state; no incorrect post-reset event was established |
| L3 | VFY-2 | Raw-Up versus last-ink finish semantics need a product contract decision |
| L4 | CR-043 | Unique: odd-width stream can overread staging source |
| L5 | CR-044 | Unique latent exposed-ring byte-order defect |
| L6 | D5 | Current app makes live ink and pan mutually exclusive; no reachable trigger established |
| L7 | D6 | Current app does not pan with modal chrome; no reachable trigger established |
| L8 | CR-045 | Unique TE edge-classification hardware risk |
| L9 | CR-046 | Unique completion-loss follow-on hang |
| L10 | CR-047 | Unique non-atomic group publication behavior |
| L11 | CR-048 | Unique unsafe `PixelPainter` surface contract |
| L12 | CR-049 | Unique startup-allocation leak |
| L13 | D7 | `can_reset()` is checked before restore; no reachable second-step failure established |
| L14 | CR-050, narrowed | `CoverageTile::reset` lacks runtime guards; `StrokeRaster` does set `valid_` and returns safely |
| L15 | N4 | Unwired/dead subsystem, not a correctness defect |
| L16 | N5 | Public seam with no current product caller, not a defect by itself |
| L17 | N6 | Redundant one-step abstraction, not a correctness defect |
| L18 | N7 | File size/maintainability observation |

This map accounts for all 49 Review B labels. Review A's statement that the strided surface check was still overflow-prone also corrects Review B's “02790e5 closed” note: the runtime check exists, but its unchecked multiplication wraps on 32-bit targets (CR-029).

## Validation receipts

All commands below ran in immutable snapshots rather than the changing worktree.

| Snapshot | Command | Result |
|---|---|---|
| `e76b98e` | `./scripts/dev test` | **29/29 passed** in approximately 36 seconds |
| `e76b98e` | `./scripts/dev asan` | **11/11 passed** in approximately 100 seconds |
| `e76b98e` | `./scripts/dev release` | **29/29 passed** in approximately 2.6 seconds |
| `e76b98e` | `./scripts/dev format-check` | **Passed** |
| `e76b98e` | `python3 -m compileall -q tools second_review_hardware_ab` | **Passed** |
| `e76b98e` | `./scripts/dev tidy` | Failed only on the configured function-size check for `append_incrementally_in_place` (25 variables versus threshold 20); no compiler correctness diagnostic was emitted |
| `f2f6da7` | `./scripts/dev test` | **29/29 passed** in 35.72 seconds |
| `f2f6da7` | `./scripts/dev asan` | **11/11 passed** in 72.63 seconds |
| `f2f6da7` | `./scripts/dev release` | **29/29 passed** in 2.82 seconds |
| `f2f6da7` | `python3 -m compileall -q tools second_review_hardware_ab` | **Passed** |
| `f2f6da7` | `./scripts/dev format-check` | **Failed** on formatting in `esp32/main/vector_v2/vector_v2_app.cpp`, `esp32/main/vector_v2/vector_v2_gate_harness.cpp`, `vector_v2/src/tile_producer.cpp`, and `vector_v2/tests/tile_producer_test.cpp` |

Targeted sanitizer and behavioral reproducers were also run. Their outputs are quoted in the corresponding findings. No ESP32 or RP2350 hardware run was available, so findings that depend on electrical/PIO timing are labeled as hardware risks rather than claimed device observations.

---

## Detailed findings

### CR-001: High: A physical zoom-button event can remap an open stroke or pan halfway through the gesture

**Applies to:** `e76b98e` and `f2f6da7`; the relevant app path is inside the recent changed-file range, although the problematic button handling predates the final snapshot.

**Trigger.** Press the separate physical mode/zoom button while a touch-driven draw or pan is still active, then release the button before lifting the touch.

**Impact.** The active gesture began under one zoom/origin transform and continues under another. A stroke can split or jump in world space; a pan can jump; and the immediate refresh can erase the in-progress preview before the gesture is finalized.

**Evidence.** The physical button release calls `presenter.set_zoom(...)` without checking `pressed`, `panning`, or active ink at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:974-983`. Draw samples are mapped at `:1032`, `:1071`, and `:1144`, while pan samples use the same evolving navigation state at `:1060`. The coordinate transform depends on the presenter's current zoom/origin at `e76b98e:esp32/main/vector_v2/vector_v2_presenter.cpp:145-154`; `set_zoom` mutates navigation and refreshes at `:400-408`.

**Proposed fix.** Defer physical zoom changes until no gesture is open, or explicitly cancel/finalize the current gesture before changing navigation. Keep the zoom level captured at gesture start if zoom must be allowed concurrently.

**Regression test.** Start a three-sample stroke and a pan, inject a physical zoom-button release between samples two and three, and verify either (a) zoom is deferred and all samples map through one transform or (b) the gesture is explicitly terminated before zoom.

### CR-002: High: A zero-length authority segment with unequal radii ignores the larger radius

**Applies to:** recent-range incremental geometry at `e76b98e` and `f2f6da7`.

**Trigger.** Commit two samples at exactly the same coordinate with different radii, such as radius 1 followed by radius 10.

**Impact.** The committed authority paints only the first radius. The larger pressure/radius sample disappears even though the operation builder preserves it, producing a visibly undersized dot after commit or replay.

**Evidence.** The operation builder retains coincident points when their radii differ at `e76b98e:vector_v2/src/operation_builder.cpp:121-139`. For a zero-length segment, the incremental rasterizer sets the inverse length to zero at `e76b98e:vector_v2/src/incremental_rasterizer.cpp:93-103`; coverage then uses interpolation amount zero and therefore only the first radius at `:149-160`. The settled renderer handles the same geometry by choosing the maximum radius at `e76b98e:core/src/settled_renderer.cpp:54` and `:81-82`.

A targeted repro linked against the pinned rasterizer produced:

```text
center=0000 distance5=ffff distance9=ffff
```

`center=0000` is painted RGB565 black, while pixels five and nine units away remain white even though the second radius is 10.

**Proposed fix.** Special-case zero-length segments as a disk whose radius is `max(first_radius, second_radius)`; use the same rule in every live, authority, and export geometry producer.

**Regression test.** Cover increasing and decreasing radii at one coordinate, masked and unmasked rendering, every supported zoom, and comparison against the settled renderer.

### CR-003: High: Live preview and committed authority use different stroke geometry, causing a lift-time visual pop

**Applies to:** V2 at `e76b98e` and `f2f6da7`; the live geometry and authority rasterizer were changed in the recent range.

**Trigger.** Draw at least three non-collinear samples, especially a sharp turn or changing-radius curve, and lift.

**Impact.** Pixels visible during live ink can disappear, new pixels can appear, and shared pixels can change color when the provisional stroke is replaced by committed authority. This violates visual continuity and makes the display state depend on whether the finger is currently down.

**Evidence.** Live geometry emits circles and curved convex paths through `CurvedRibbonStream` at `e76b98e:core/src/ribbon_geometry.cpp:247-319`; the presenter bakes those primitives at `e76b98e:esp32/main/vector_v2/vector_v2_presenter.cpp:353-397`. Committed authority instead constructs midpoint-based capsule segments at `e76b98e:vector_v2/src/incremental_rasterizer.cpp:746-775` and paints them at `:1019-1037`.

A pixel-for-pixel repro for the same sharp three-point operation reported:

```text
live_only=284 authority_only=63 shared_color_diffs=153
```

The counts prove both coverage-mask and color disagreement; they are not merely different internal representations.

**Proposed fix.** Define one canonical prepared stroke geometry and make live preview, authority rasterization, and export consume it. If incremental authority needs a lower-level representation, derive both paths from the same segment/join semantics and rounding rules.

**Regression test.** For straight, curved, sharp-turn, repeated-point, and tapered strokes, compare the complete live and committed RGB565 pixel surfaces at all supported zooms before accepting the operation.

### CR-004: High: SVG tests establish parity with the preview renderer, not with the committed glass image

**Applies to:** recent-range SVG export at `e76b98e` and `f2f6da7`.

**Trigger.** Export an operation with at least three non-collinear or tapered samples.

**Impact.** The SVG can differ from the document users see after lift. This breaks the explicit export requirement even if the existing SVG test remains green.

**Evidence.** SVG export rebuilds `CurvedRibbonStream` circles/convex paths at `e76b98e:vector_v2/src/svg_export.cpp:106-121`. Committed rendering uses the distinct midpoint-capsule authority path at `e76b98e:vector_v2/src/incremental_rasterizer.cpp:746-775` and `:1019-1037`. The parity test compares SVG-derived geometry with `RibbonRenderer`, not `apply_incremental_operation`, at `e76b98e:vector_v2/tests/svg_export_test.cpp:258-278`. The product contract requires an SVG “visually identical to glass” at `e76b98e:SHIP_CONTRACT.md:93-99`. CR-003's repro demonstrates that the preview geometry used by SVG is not pixel-equivalent to authority.

**Proposed fix.** Export the same canonical geometry used by committed authority, or formally change authority to the geometry already exported. Do not maintain two independent join/cap implementations.

**Regression test.** Rasterize exported SVG geometry and compare its complete mask against `apply_incremental_operation` for sharp joins, repeated samples, variable radius, clipping, and all supported zooms.

### CR-005: High: V1 autosave can treat torn sector data as a valid drawing after power loss

**Applies to:** existing ESP32 V1 persistence at `e76b98e` and `f2f6da7`; README identifies V1 as the default operational app (`e76b98e:README.md:16-19`).

**Trigger.** Lose power after an existing saved drawing's data-sector erase or partial rewrite but before the full in-place save completes.

**Impact.** Boot can accept the old header and restore a mixture of old, new, and erased raster sectors as if it were a valid drawing. The failure is silent because no payload checksum or committed generation distinguishes complete from torn data.

**Evidence.** The header checksum covers header metadata, not drawing bytes, at `e76b98e:esp32/main/drawing_store.cpp:42-62`. A valid header is sufficient to mark a saved drawing at `:81-88`. Save erases and overwrites payload sectors in place at `:166-178`; the header is rewritten only when metadata is pending at `:193-203`. Restore trusts that header and reads payload sectors without validating a data checksum at `:241-268`.

**Proposed fix.** Use two banks or copy-on-write generations. Write payload sectors with per-sector or whole-image checksums, then atomically publish a generation/commit record last. Never erase the only committed generation first.

**Regression test.** Add fault injection after every erase/write step and reboot from the resulting flash image. Recovery must choose either the complete previous generation or the complete new generation, never mixed data.

### CR-006: High: V1's drop-oldest shared event queue can discard touch edges and control commands

**Applies to:** existing ESP32 V1 input/replay path at `e76b98e` and `f2f6da7`.

**Trigger.** Fill the 32-entry queue while the app consumer is blocked or slow, for example during export, or generate a dense replay/demo burst.

**Impact.** Dropping an `Up` leaves the app in a pressed state, so a later `Down` is processed as continuation and can connect separate strokes. The same policy can discard replay start/reset controls, changing the meaning of all following samples.

**Evidence.** A full queue unconditionally drops its oldest `AppEvent` at `e76b98e:esp32/main/hardware_app.cpp:104-110`. Controls use that queue at `:113-115`, replay start/reset at `:151-167`, and touch samples at `:118-122`; capacity is 32 at `:743`. Export blocks the main consumer at `:600-625`. Gesture state relies on Down/Up ordering at `:1065-1213`.

**Proposed fix.** Separate control and touch queues, reserve slots for transitions, and coalesce/drop only replaceable Move samples. Never discard control, Down, or Up events; expose overflow as a recoverable cancellation if conservation becomes impossible.

**Regression test.** Hold the consumer behind a slow export, enqueue more than 32 mixed events, and prove all controls and touch edges arrive in order while only Moves may be coalesced.

### CR-007: Medium: A failed V1 export can leave the toolbar stuck on “SAVING”

**Applies to:** existing ESP32 V1 display path at `e76b98e` and `f2f6da7`.

**Trigger.** Start export, keep the export toast visible, and complete with failure so `exporting` changes from true to false while ready remains false.

**Impact.** The screen can continue to show “SAVING” instead of the error state until another unrelated change dirties the toast region.

**Evidence.** Toast invalidation does not include the `exporting` bit at `e76b98e:esp32/main/physical_display.cpp:82-84`; palette invalidation at `:85-89` does not cover this state-only transition. The toolbar chooses “SAVING” versus “SAVED”/“ERROR” from `exporting` at `e76b98e:core/src/toolbar.cpp:270-285`. Refresh only transmits the toast when `toast_dirty_` is set at `e76b98e:esp32/main/physical_display.cpp:216-238`.

**Proposed fix.** Include `exporting` in toast-change detection whenever the export toast is visible.

**Regression test.** Render exporting=true, transition to exporting=false/ready=false without changing any other state, and require the toast pixels to be dirtied and updated to ERROR.

### CR-008: Medium: Failed V2 presentation still advances visual and authority state

**Applies to:** V2 at `e76b98e` and `f2f6da7`.

**Trigger.** Make `present_unobscured` fail during a live-ink update.

**Impact.** The presenter's in-memory frame/provisional state and the committed document can advance even though those pixels never reached the panel. Later damage calculations can assume an old provisional region was erased when it remains visible, leaving stale ink until a full refresh.

**Evidence.** `show_update` mutates `frame_`, committed/provisional bookkeeping, and baked geometry before calling `present_unobscured` at `e76b98e:esp32/main/vector_v2/vector_v2_presenter.cpp:353-397`. `LiveInkCoordinator` records a visual failure but still calls builder add/commit at `e76b98e:vector_v2/include/tinydraw/vector_v2/live_ink_coordinator.h:19-36`. The app proceeds based on add/commit status and does not roll back `move.visual_passed` at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:1071-1101`.

**Proposed fix.** Make presentation transactional or set an explicit “panel state unknown/full refresh required” latch before authority can continue. Clear provisional bookkeeping only after a successful transfer.

**Regression test.** Inject one failed update transfer followed by a successful move/lift and verify the panel model converges exactly to a full compose without stale provisional pixels.

### CR-009: Medium: A failed V2 pan transfer can leave navigation, ring state, and the panel split

**Applies to:** V2 at `e76b98e` and `f2f6da7`.

**Trigger.** Fail composition, chrome, TE, or transfer after a pan has already advanced navigation/ring bookkeeping, especially on the final move before lift.

**Impact.** The presenter's logical origin can be new while the panel still displays the old origin. The lift branch does not force a repair, so the mismatch can remain indefinitely until another refresh-triggering event.

**Evidence.** `pan_from` mutates navigation before `refresh_pan` succeeds at `e76b98e:esp32/main/vector_v2/vector_v2_presenter.cpp:421-454`. `refresh_pan` advances/invalidate ring state and has multiple post-mutation failure returns at `:508-555`. The app's panning lift branch reports completion without forcing a final full refresh at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:1124-1138`.

**Proposed fix.** Roll back navigation/ring state on failure, or immediately fall back to a full compose/transfer and keep a repair latch until that succeeds.

**Regression test.** Inject each failure point on the last pan move, lift, and compare logical navigation, ring contents, and modeled panel pixels to a clean full refresh.

### CR-010: High: The overall firmware gate ignores the entire ink-trace replay result

**Applies to:** recent-range gate harness at `e76b98e` and `f2f6da7`.

**Trigger.** Make any trace parse, conservation, allocation, overflow, presentation, or authority-commit check return false while the other top-level gates pass.

**Impact.** The firmware harness reports overall success despite a failed mandatory ink-trace gate.

**Evidence.** `ink_trace_replay` is computed at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:3181-3183` and printed at `:3203-3209`, but the final return conjunction at `:3211-3212` omits it.

**Proposed fix.** Include `ink_trace_replay` in the final verdict and factor the final conjunction into a host-testable pure helper.

**Regression test.** Set every top-level input true except `ink_trace_replay=false`; the helper and harness must fail.

### CR-011: High: The trace gate computes latency acceptance but omits it, and zero samples summarize as passing latency

**Applies to:** recent-range gate harness at `e76b98e` and `f2f6da7`.

**Trigger.** Produce event-to-DMA p95 over 28,000 µs, or collect no successful latency samples, while conservation/presentation/commit counters pass.

**Impact.** A trace can print `pass=1` despite violating the documented finger-to-glass threshold or measuring no latency at all.

**Evidence.** Empty latency data returns a zero-valued summary at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2770-2780`. `latency_pass` is calculated at `:2997`, but `trace_pass` omits it at `:2998-2999`. The documented threshold is 28 ms at `e76b98e:docs/INK_TRACE_HARNESS.md:66-70`.

**Proposed fix.** Require a defined nonzero/expected sample population and include `latency_pass` in `trace_pass`.

**Regression test.** Test otherwise-green summaries with p95 `28,001` and with zero samples; both must fail.

### CR-012: High: First-contact presentation is absent from the latency population

**Applies to:** recent-range trace replay at `e76b98e` and `f2f6da7`.

**Trigger.** Make the initial Down/show-start transfer slow while subsequent Move transfers remain fast.

**Impact.** First-visible-ink lag can exceed the gate while reported p95 remains green, even though first contact is the latency users most directly perceive.

**Evidence.** The Down branch calls `show_start()` but only increments a presentation-failure counter at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2913-2929`. Full-chain samples are appended only in the Move branch at `:2967-2985`. The specification asks for a chain for every consumed sample at `e76b98e:docs/INK_TRACE_HARNESS.md:46-54`. The committed baseline records 371 consumed events but only 33 latency samples at `e76b98e:benchmark-results/ink-trace-replay-baseline/BASELINE.md:17`.

**Proposed fix.** Record geometry, submit, and DMA completion timestamps for successful `show_start()` calls; explicitly document and count any stationary/no-op events excluded from the sample population.

**Regression test.** Replay one stroke with Down over 28 ms and fast Moves; the latency verdict must fail.

### CR-013: High: Trace replay bypasses product UI routing and grades chrome taps as ink

**Applies to:** recent-range V2 app/gate integration at `e76b98e` and `f2f6da7`.

**Trigger.** Replay a captured Down inside a chrome control or overlay hit region.

**Impact.** The harness draws ink where production activates UI, so event conservation, latency, and final authority no longer describe the product path the trace came from.

**Evidence.** Production checks `chrome_contains` before opening ink at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:998-1048`. The gate treats every Down as ink at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2913-2929`. The canonical `scribble-multistroke.csv` has a Down at `(355,71)` on line 359 (`e76b98e:testdata/ink-traces/scribble-multistroke.csv:359`); the zoom rail is defined at `e76b98e:vector_v2/src/chrome.cpp:37` and its expanded hit test at `:638-656`. The harness document claims production downstream routing at `e76b98e:docs/INK_TRACE_HARNESS.md:35-43`.

**Proposed fix.** Share the product event router with replay, or capture/replay events after routing with explicit event kinds (ink, pan, chrome action).

**Regression test.** Include overlay/zoom-rail taps in a trace and verify they generate the same action and zero ink in app and harness.

### CR-014: High: Trace replay can hang forever if the final Up cannot enter the touch buffer

**Applies to:** recent-range gate replay at `e76b98e` and `f2f6da7`.

**Trigger.** Fill the 16-slot touch buffer with transition-heavy events so the final two no-touch samples emitted for Up overflow.

**Impact.** The replayer becomes exhausted while the consumer remains pressed; the loop's only termination condition is never met, hanging the firmware gate.

**Evidence.** The loop breaks only when `!pressed && replayer.exhausted()` at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2886-2893`. Up attempts only two no-touch offers and then marks itself done at `:2720-2731`. On overflow, `TouchEventBuffer` refuses the event while preserving its prior touching state at `e76b98e:vector_v2/src/touch_event_buffer.cpp:43-50`. A burst of short valid strokes can therefore consume capacity with non-coalescible edges.

**Proposed fix.** Treat `exhausted && pressed` as an explicit unclosed-stroke failure and add a replay deadline/watchdog. Better, reserve transition capacity so a final Up cannot be lost.

**Regression test.** Replay enough one-move strokes to overflow an edge-only buffer and require bounded failure rather than a hang.

### CR-015: High: Trace fidelity counters and original timing do not affect acceptance

**Applies to:** recent-range trace tooling/gate at `e76b98e` and `f2f6da7`.

**Trigger.** Increase coalescing/max gaps, replay a burst late, or produce a final authority different from the capture while basic consumed/commit/presentation counters remain acceptable.

**Impact.** The gate can pass a replay that did not preserve capture timing or drawing outcome, so it cannot establish the fidelity claimed by its specification.

**Evidence.** `trace_pass` uses only event conservation, overflow, authority commit, and presentation at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2996-2999`. `InkStrokeCounters::valid()` defines stroke-counter invariants at `e76b98e:vector_v2/include/tinydraw/vector_v2/ink_trace.h:133-147` and is implemented at `e76b98e:vector_v2/src/ink_trace.cpp:379-384`, but the gate never calls it. The docs say coalescing/max-gap regressions and authority mismatches fail at `e76b98e:docs/INK_TRACE_HARNESS.md:56-73`. Although traces store relative timestamps (`:39-40`), replay stamps offered samples with current `esp_timer_get_time()` at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2700-2728`, so a late burst changes `InkStream`'s timing inputs instead of retaining recorded time.

**Proposed fix.** Enforce `InkStrokeCounters::valid()`, captured/baseline gap bounds, and an exact expected authority digest. Pass recorded target timestamps into the ink path while separately measuring real delivery/transfer lateness.

**Regression test.** Deliberately delay a burst, perturb one final operation, and exceed each fidelity bound independently; every mutation must fail the gate.

### CR-016: Medium: The capture capacity and canonical corpus exceed what the gate parser can replay

**Applies to:** V2 trace capture/replay at `e76b98e` and `f2f6da7`.

**Trigger.** Dump a valid capture containing more than 4,096 events, including the committed 9,284-event under-overlay corpus.

**Impact.** The capture facility can produce traces the product gate cannot load, and the documented canonical corpus is not the corpus actually embedded in the firmware gate.

**Evidence.** Capture capacity is 12,288 at `e76b98e:esp32/main/vector_v2/vector_v2_ink_trace_capture.h:26`; the gate's parse storage is capped at 4,096 at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2804-2812`. The under-overlay baseline records 9,284 events and notes it is not embedded at `e76b98e:benchmark-results/ink-trace-replay-baseline/BASELINE.md:34-40`. The docs list under-overlay as canonical at `e76b98e:docs/INK_TRACE_HARNESS.md:23-30`, while the harness embeds a different fast-curve asset at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2792-2802`.

**Proposed fix.** Stream parse/replay from external storage, allocate matching capacity, or lower capture capacity to an explicitly supported limit. Make the embedded/required corpus list one source of truth.

**Regression test.** Capture and replay the maximum documented event count, and assert every canonical corpus is either embedded or loaded by the gate job.

### CR-017: High: The déjà-vu rerender oracle is diagnostic only and cannot fail the gate

**Applies to:** recent-range V2 gate and ledger at `e76b98e` and `f2f6da7`.

**Trigger.** Produce same-revision unexplained rerenders, stale-revision rerenders, or amplification over the contract bound while the home-view sharpness check passes.

**Impact.** The firmware can report success despite violating the contract's bounded-work/no-unexplained-rerender requirements.

**Evidence.** The cache-tour result is based only on `home_sharp` at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:1253-1271`. Ledger totals are only reset/printed at `:3141-3166`; no ledger condition appears in the final return at `:3211-3212`. A same-revision duplicate increments `unexplained` at `e76b98e:vector_v2/src/rerender_ledger.cpp:119-127`. The required amplification ≤1.25 and zero stale/unexplained counts are stated at `e76b98e:SHIP_CONTRACT.md:38-64`.

**Proposed fix.** Calculate a tour-scoped ledger verdict requiring zero stale/unexplained counts and the configured amplification bound, then propagate it into the top-level result.

**Regression test.** Inject each forbidden ledger count and a 1.2501 amplification independently; each must fail cache-tour and overall verdict.

### CR-018: Medium: Publishing one visible tile marks its whole 2×2 group rendered in the ledger

**Applies to:** recent-range materialization/ledger path at `e76b98e` and `f2f6da7`.

**Trigger.** Render a view intersecting only one tile of a 2×2 producer group, then expose a sibling tile at the same document revision.

**Impact.** The sibling's legitimate first render is classified as an unexplained rerender, making ledger metrics false even after CR-017 starts enforcing them.

**Evidence.** The producer records the group rendered whenever any tile was published at `e76b98e:vector_v2/src/tile_producer.cpp:591-603`. `publish_group` renders only the visible intersection at `:657-681`. The ledger stores one rendered flag per group rather than per tile at `e76b98e:vector_v2/src/rerender_ledger.cpp:104-137`.

**Proposed fix.** Track rendered bits per tile/revision, or mark a group rendered only when every member has actually been published for that revision.

**Regression test.** Render a one-tile view and then a sibling-only view at the same revision; both must classify as first renders, not unexplained rerenders.

### CR-019: Medium: Explicit cache discard is misclassified as an unexplained rerender

**Applies to:** recent-range materialized canvas at `e76b98e` and `f2f6da7`.

**Trigger.** Render a group, call `discard_tiles()`, and render that group again at the same revision.

**Impact.** The ledger reports an unexplained rerender even though an explicit capacity action removed the cache entry.

**Evidence.** `discard_tiles()` clears raw and uniform cache entries without notifying the ledger at `e76b98e:vector_v2/src/materialized_canvas.cpp:1186-1201`. Ordinary slot replacement does call `mark_evicted` at `:934-935`. Without that flag, a same-revision refill reaches the unexplained branch at `e76b98e:vector_v2/src/rerender_ledger.cpp:119-127`.

**Proposed fix.** Before clearing, mark each distinct occupied raw/uniform group evicted once.

**Regression test.** Render, discard, then rerender. The sequence should produce exactly one eviction classification and zero unexplained classifications.

### CR-020: Medium: Dumping a large trace can both overflow production touch input and race an in-flight capture write

**Applies to:** V2 ESP32 capture path at `e76b98e` and `f2f6da7`.

**Trigger.** Request a near-capacity trace dump while new touch samples continue arriving, especially with a producer preempted after observing capture enabled but before publishing its storage write.

**Impact.** The main app stops consuming product touch events long enough to overflow its 16-slot queue. Separately, a fixed two-tick delay is not a synchronization handshake: an in-flight producer can write after the dump snapshots/resets storage, corrupting the next capture or omitting an event from the dump.

**Evidence.** The app performs the dump in the main consumer at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:1218-1223`. Dump prints up to 12,288 events and yields only every 256 at `e76b98e:esp32/main/vector_v2/vector_v2_ink_trace_capture.cpp:92-131`. The sampler continues offering into the production queue at `e76b98e:esp32/main/vector_v2/vector_v2_touch_sampler.cpp:104-134`, whose capacity is 16 at `e76b98e:esp32/main/vector_v2/vector_v2_touch_sampler.h:18`. Capture disables with a relaxed store and merely delays two ticks at `e76b98e:esp32/main/vector_v2/vector_v2_ink_trace_capture.cpp:93-96`; producer enable load and later storage/count publication are separate at `:46` and `:69-75`.

**Proposed fix.** Pause/acknowledge the sampler before snapshotting, or use an epoch/producer handshake or critical section that proves no write is in flight. Dump from a non-consumer task or first drain/cancel product touch state safely.

**Regression test.** Preempt the producer at every point around its enable load/storage/count update while dumping a maximum trace, and simultaneously inject a new gesture; require exact capture conservation and no production touch overflow.

### CR-021: Medium: Angularity reconstruction double-processes chunk-boundary samples and can omit the terminal chunk

**Applies to:** recent-range host angularity tool at `e76b98e` and `f2f6da7`.

**Trigger.** Analyze a trace crossing the 32-sample operation boundary, especially one whose `finish()` first returns `kChunkReady`.

**Impact.** Reported angularity is measured over operations that differ from production: boundary points are duplicated after a second stateful filter update, and the final pending chunk can be acknowledged without ever being copied into the measured operations.

**Evidence.** `capture_chunk()` copies pending data and acknowledges internally at `e76b98e:vector_v2/tools/ink_angularity.cpp:141-146`. On a chunk-ready Move, the caller then calls stateful `ink.update(touch)` and `builder.add()` again at `:176-180`; builder acknowledgment has already reoffered the rejected boundary point at `e76b98e:vector_v2/src/chained_operation_builder.cpp:89-109`. `InkStream::update()` mutates filter, pressure, length, and timestamp state at `e76b98e:core/src/ink_stream.cpp:88-95`. Finish acknowledges once in `capture_chunk()` and a second time at `e76b98e:vector_v2/tools/ink_angularity.cpp:184-188`, allowing the final chunk to complete without capture.

**Proposed fix.** Compute each `InkPoint` once, offer it once, copy every pending append before exactly one acknowledgment, and continue solely from the status returned by that acknowledgment.

**Regression test.** Compare reconstructed operations sample-for-sample with the production coordinator for traces spanning several boundaries and a boundary-triggered finish.

### CR-022: Medium: Angularity metrics omit physical joints at operation-chunk boundaries

**Applies to:** recent-range host angularity tool at `e76b98e` and `f2f6da7`.

**Trigger.** Place a sharp turn exactly across a 32-sample chunk boundary.

**Impact.** The turn is absent from joint p95, maximum, and threshold counts, understating the tail the tool is intended to grade.

**Evidence.** Each operation starts a fresh local chord chain at `e76b98e:vector_v2/tools/ink_angularity.cpp:201-207`; angles are computed only between chords in that chain at `:278-290`, and `run_trace()` measures operations independently at `:319-324`. Production chunks deliberately overlap while retaining one gesture identity at `e76b98e:vector_v2/include/tinydraw/vector_v2/chained_operation_builder.h:21-25`.

**Proposed fix.** Preserve stroke identity and carry the prior operation's terminal nondegenerate chord into the next operation's first joint calculation; reset only between distinct strokes.

**Regression test.** Chunked and unchunked forms of a stroke whose sole sharp turn lands on the boundary must report the same physical joint metrics.

### CR-023: Medium: Raster setup census excludes work for rejected and saturation-skipped units

**Applies to:** recent-range tile producer metrics at `e76b98e` and `f2f6da7`.

**Trigger.** Benchmark many clipped/bbox-empty units or units skipped because coverage is already saturated.

**Impact.** Bounds, curve preparation, clipping, and decision work is reported as zero setup time for those units, understating setup cost and distorting optimization decisions.

**Evidence.** Setup timing starts at `e76b98e:vector_v2/src/tile_producer.cpp:431-433`. Bbox-empty and saturation paths return at `:483-496`; `setup_ticks` is accumulated only later at `:499-502`.

**Proposed fix.** Use a scoped setup timer or accumulate before every early return.

**Regression test.** Census-enabled groups containing only bbox-rejected units and only saturation-skipped units must contribute setup ticks and no paint ticks.

### CR-024: Medium: Host “cold” census includes an untimed warm revisit

**Applies to:** recent-range raster census at `e76b98e` and `f2f6da7`.

**Trigger.** Run the general cold census path with its immediate complete-view revisit enabled.

**Impact.** Printed cold remaining-scan counts/time include a warm reuse scan that is excluded from cold wall time, so phase counters no longer reconcile with the labeled timing.

**Evidence.** Cold wall timing ends at `e76b98e:vector_v2/tools/raster_census.cpp:146-147`; an immediate revisit calls `produce_next()` at `:149-153`. `produce_next()` increments `remaining_scans`/`remaining_scan_ns` before detecting complete-view reuse at `e76b98e:vector_v2/src/tile_producer.cpp:232-251`. General census snapshots occur afterward at `e76b98e:vector_v2/tools/raster_census.cpp:621` and `:656-657`.

**Proposed fix.** Snapshot cold counters before the revisit, or subtract/restore the revisit counters.

**Regression test.** Toggling the reuse-accounting revisit must not change captured cold `RasterCensus` values.

### CR-025: Medium: The Python trace validator accepts numeric syntax rejected by production

**Applies to:** recent-range trace checker/parser at `e76b98e` and `f2f6da7`.

**Trigger.** Put a leading plus, whitespace, Unicode digits, or another Python-accepted/non-`from_chars` representation in a numeric field.

**Impact.** `tools/ink-trace-check` can certify a file that firmware/production C++ refuses to replay.

**Evidence.** Python parses fields using `int()` at `e76b98e:tools/ink-trace-check:35-40` and `:53-56`. Production requires `std::from_chars` to consume the exact field at `e76b98e:vector_v2/src/ink_trace.cpp:54-61`.

A trace using `+1` numeric fields produced divergent verdicts:

```text
$ ./tools/ink-trace-check /tmp/ink-trace-plus.csv
... valid ...

$ /tmp/ink_trace_parser_repro /tmp/ink-trace-plus.csv
ok=0 status=1 line=2
```

**Proposed fix.** Match the C++ ASCII decimal grammar and integer-width bounds exactly, or expose one shared parser to both tools.

**Regression test.** Cross-parser fixtures for leading `+`, leading/trailing spaces, Unicode digits, overflow, empty values, negatives where allowed, and boundary values must return identical status/line results.

### CR-026: Low: Invalid metadata is reported on the first event line instead of the metadata line

**Applies to:** recent-range C++ trace parser at `e76b98e` and `f2f6da7`.

**Trigger.** Parse syntactically valid CSV with invalid magic, empty name, or an empty sample-rate note on metadata line 2.

**Impact.** Capture diagnostics point at line 4, slowing investigation and potentially causing the wrong line to be edited.

**Evidence.** Header validation returns the default `event_index=0` at `e76b98e:vector_v2/src/ink_trace.cpp:201-206`; all validation failures are translated to `4 + event_index` at `:317-324`.

**Proposed fix.** Map `kInvalidHeader` to line 2 and retain event-index mapping only for event validation failures.

**Regression test.** One fixture per malformed metadata field must return `kInvalidTrace`, `kInvalidHeader`, and `line==2`.

### CR-027: Medium: `CoverageTile` accepts arbitrary polygon lengths but writes them into a four-edge stack array

**Applies to:** public core API at `e76b98e` and `f2f6da7`.

**Trigger.** Call `CoverageTile::paint_convex_polygon` with a convex polygon containing five or more vertices.

**Impact.** The function writes past a fixed stack array, causing memory corruption. Current `RibbonPrimitive` callers appear limited to at most four vertices, but the public span API does not enforce that invariant.

**Evidence.** The public declaration accepts an arbitrary point span at `e76b98e:core/include/tinydraw/graphics/coverage_tile.h:20-30`. Implementation allocates `edges[4]` and appends once per input edge without a maximum check at `e76b98e:core/src/coverage_tile.cpp:239-247`.

The five-edge ASan repro reports:

```text
ERROR: AddressSanitizer: stack-buffer-overflow
... core/src/coverage_tile.cpp:247 ...
```

**Proposed fix.** Reject `points.size() > 4` with an explicit status/assert if four is the intended contract, or allocate/iterate storage sized for the supported polygon maximum. Make the limit part of the type/API.

**Regression test.** Exercise 0-5 vertices under ASan; five vertices must be safely rejected or correctly rendered.

### CR-028: Medium: Public prepared-curve APIs trust a mutable `step_count` larger than their fixed arrays

**Applies to:** V2 rasterizer API at `e76b98e` and `f2f6da7`.

**Trigger.** Pass a public `PreparedCurveUnit` with `step_count=4` while `steps` and related local plan storage hold only three elements.

**Impact.** Public paint/coverage helpers index beyond fixed arrays and corrupt/read stack memory.

**Evidence.** `PreparedCurveUnit` is a public aggregate with externally mutable `step_count` at `e76b98e:vector_v2/include/tinydraw/vector_v2/incremental_rasterizer.h:141-144`. Index validation compares only against that untrusted count before indexing `steps` at `e76b98e:vector_v2/src/incremental_rasterizer.cpp:1099-1129`; the unit painter similarly loops to `step_count` into three-element plan storage at `:1190-1227`.

The ASan repro with `step_count=4` reports:

```text
ERROR: AddressSanitizer: stack-buffer-overflow
... vector_v2/src/incremental_rasterizer.cpp:1099/1129 ...
```

**Proposed fix.** Make prepared units opaque/private and construct only validated values, or reject `step_count > steps.size()` in every public entry point before indexing any related array.

**Regression test.** Fuzz malformed prepared units, including counts 4 and 255, through every public query/paint API under ASan/UBSan.

### CR-029: Medium: Strided raster-surface extent checks wrap on 32-bit `size_t`

**Applies to:** ESP32-targeted raster APIs at `e76b98e` and `f2f6da7`.

**Trigger.** Supply dimensions/stride whose `(height-1)*stride + width` exceeds `SIZE_MAX` but wraps to a value no larger than the provided span.

**Impact.** Validation succeeds and later row indexing writes outside the span on 32-bit ESP32 builds.

**Evidence.** `RibbonRenderer::render_surface` computes the required extent with unchecked `size_t` multiplication/addition at `e76b98e:core/src/ribbon_renderer.cpp:85-90`, then writes using `row * stride` at `:136-151`. `RasterSurface` repeats the pattern at `e76b98e:vector_v2/src/incremental_rasterizer.cpp:117-129` before downstream writes. An exact 32-bit `RibbonRenderer` trigger is `width=1, height=65,537, stride=65,536`: mathematically required is `2^32+1`, but the check wraps to `1`. A within-V2-maximum-height trigger is `height=5,376, width=1, stride=1,943,322,877`, which wraps the required extent to `4`.

**Proposed fix.** Before multiplication, reject when `height - 1 > (SIZE_MAX - width) / stride`; centralize this in a checked strided-extent helper and use it for every surface API.

**Regression test.** Unit-test the helper with the exact values above and run a 32-bit sanitizer/emulator build that verifies rejection with no writes.

### CR-030: Medium: The post-baseline direct-publish commit adds two more unchecked strided-extent paths

**Applies to:** concurrent commit `f2f6da7e97265626ace1b96e363d92dbbe09c7df`, not the pinned `e76b98e` baseline.

**Trigger.** Call the new payload-analysis/direct-publish API with `width=2`, `height=2`, `stride=SIZE_MAX`, and a one-pixel span.

**Impact.** The extent expression wraps to one even on 64-bit, validation accepts the span, and the next-row access reads out of bounds. The same arithmetic appears in direct publication, so an unsafe descriptor can bypass both layers.

**Evidence.** New analysis validation computes `(height - 1) * stride + width` without overflow checking at `f2f6da7:vector_v2/src/tile_payload_analysis.cpp:19-39`. Direct publish repeats unchecked strided bounds arithmetic at `f2f6da7:vector_v2/src/materialized_canvas.cpp:922-956`.

The exact ASan repro reports:

```text
ERROR: AddressSanitizer: stack-buffer-overflow
... analyze_tile_payload ...
width=2 height=2 stride=SIZE_MAX span_size=1
```

**Proposed fix.** Use the same checked extent helper proposed in CR-029 in analysis, publication, rendering, and raster-surface construction.

**Regression test.** Add `SIZE_MAX` and near-`SIZE_MAX` stride cases to both APIs; every overflowing descriptor must return failure without reading or publishing.

### CR-031: High: RP2350's drop-oldest touch queue can lose lift and connect separate strokes

**Applies to:** existing RP2350 app at `e76b98e` and `f2f6da7`.

**Trigger.** Accumulate more than 32 touch events while the main loop is blocked in partial display submission.

**Impact.** If an Up is evicted before a following Down, `touch_down` remains true and the next physical gesture continues the prior operation, drawing a connector between separate strokes.

**Evidence.** A full queue drops the oldest event indiscriminately at `e76b98e:rp2350/src/main.cpp:84-90`; capacity is 32 at `:513`. Partial display submission is synchronous from the main loop at `:445-447`. Gesture state depends on conserved Down/Up ordering at `:571-606`.

**Proposed fix.** Reserve transition capacity and coalesce/drop only Move events, matching a transition-preserving touch buffer. If an edge truly cannot be retained, cancel the active stroke explicitly rather than silently continuing.

**Regression test.** Block display completion, enqueue more than 32 events spanning two gestures, and prove that operations remain separate and every Down has one matching Up/cancel.

### CR-032: Medium: RP2350 partial DMA deselects the display before the PIO FIFO is known to drain

**Applies to:** existing RP2350 AMOLED driver at `e76b98e` and `f2f6da7`.

**Trigger.** Complete DMA for a partial update while the PIO state machine still has bytes queued in its FIFO/shift register.

**Impact.** Chip select can rise before the panel receives the final bytes, risking truncated/corrupt right- or bottom-edge pixels. This is a source-confirmed sequencing mismatch but remains a hardware risk because no RP2350 panel run was available.

**Evidence.** The full-display path explicitly delays after DMA before deselect and documents that DMA completion only means bytes reached the PIO FIFO at `e76b98e:rp2350/vendor/amoled/AMOLED_1in8.c:224-228`. The partial path waits for DMA and immediately deselects without that drain allowance at `:234-251`. The drawing app uses partial submission for strokes at `e76b98e:rp2350/src/main.cpp:445-447`.

**Proposed fix.** Apply the same proven drain delay to partial writes or, preferably, wait on a PIO-empty/shift-complete condition before deasserting CS.

**Regression test.** On hardware, repeatedly paint high-contrast patterns ending at the right and bottom boundaries while varying DMA length; verify the final bytes with a logic analyzer and panel readback/photographic comparison.

### CR-033: Medium: Replay does not follow the product's rejection/cancellation policy

**Provenance:** Review B M1.

**Trigger.** Make `process_live_ink_move` return a rejected add or failed chunk commit.

**Impact.** Product discards the uncommitted tail, resets ink/ribbon state, and refreshes authority. Replay records a commit counter but keeps feeding its state machines. The gate therefore evaluates a different tail and recovery policy than the product.

**Evidence.** The gate calls `process_live_ink_move`, increments only `move.commit_failed`, and continues at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2967-2986`. Product checks every non-accepted status, cancels the builder, resets the ribbon, ends ink, and refreshes at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:1087-1104`.

**Proposed fix.** Extract one rejection handler used by product and replay. Record the rejection reason, then apply the same tail cancellation and authority refresh.

**Regression test.** Inject add rejection and commit rejection into app and harness; compare builder, ribbon, ink-active state, operation log, and presented authority after recovery.

### CR-034: Medium: Capture and production derive touch events through different buffer states

**Provenance:** Review B M2.

**Trigger.** Overflow or coalesce the production 16-slot buffer while capture is enabled.

**Impact.** The CSV can contain a Down or Move sequence that production rejected or coalesced. Capture then records what the sampler observed, not what the app consumed, undermining replay fidelity.

**Evidence.** The sampler offers to production first, then independently calls `record_contact_read` regardless of the production result at `e76b98e:esp32/main/vector_v2/vector_v2_touch_sampler.cpp:120-132`. Capture feeds a private buffer and immediately drains it at `e76b98e:esp32/main/vector_v2/vector_v2_ink_trace_capture.cpp:43-55`, so it rarely coalesces or overflows. Production changes `touching_` only when a Down is accepted at `e76b98e:vector_v2/src/touch_event_buffer.cpp:26-50`.

**Proposed fix.** Capture accepted production `TouchEvent`s and their `TouchOfferResult`, or derive events once and fork only accepted events to consumer and recorder.

**Regression test.** Force production overflow/coalescing and verify the capture exactly matches the sequence returned by `read_next()`.

### CR-035: Medium: Partial V2 presentations skip tear-edge synchronization

**Provenance:** Review B M5. Hardware-risk classification; no panel run was available.

**Trigger.** Present a large lift, fill, minimap-expanded region, or multi-strip refresh that is not exactly 368×448.

**Impact.** RAM write can begin mid-scanout, producing a stale band or horizontal tear even though full-frame and pan paths wait for TE.

**Evidence.** `present_pixels` waits for a tear edge only when bounds equal the full panel at `e76b98e:esp32/main/vector_v2/vector_v2_presenter.cpp:995-1006`. `refresh_region` can submit partial or sequential strip windows at `:239-267`. Pan separately waits before its sweep at `:525-554`.

**Proposed fix.** Define a TE policy by window height/position and wait before every presentation large enough to race scanout, including each independently programmed strip sequence.

**Regression test.** On hardware, present high-contrast partial windows at randomized scan phases and compare camera/logic-analyzer evidence with and without TE waiting.

### CR-036: Medium: Modal chrome is rasterized inside the active stream instead of prepared before it

**Provenance:** Review B M6. Hardware timing risk.

**Trigger.** Present popup, confirmation, or export-toast chrome through the strip callback.

**Impact.** CPU-heavy circles/filled regions are drawn while prior strips may already be transferring. If preparation exceeds the scan/wire margin, the panel can show a torn or partly stale modal band.

**Evidence.** `prepare_for` returns without caching when canvas overlays are hidden by a modal at `e76b98e:vector_v2/src/chrome.cpp:905-907`. `paint_prepared` then calls `draw_fixed_chrome`, `draw_export_toast`, and strip overlays for each DMA strip at `:996-1002`. The presenter invokes that paint callback from the active stream at `e76b98e:esp32/main/vector_v2/vector_v2_presenter.cpp:986-1024`.

**Proposed fix.** Rasterize modal chrome into cache/scratch before TE wait and stream start; the strip callback should only blit prepared pixels.

**Regression test.** Measure worst-case modal preparation before the first submit and verify strip callbacks perform bounded copies only; confirm tear-free panel output under the heaviest popup.

### CR-037: Medium: Idle-repair neighbor views bypass the cache-saturation guard

**Provenance:** Review B M9.

**Trigger.** Finish a visible fill when the raw tile pool is near capacity, then start the repair plan's neighbor phase.

**Impact.** Neighbor repair can evict warmer active-view tiles before the guard applies, causing later refreshes to fall back or rematerialize the view the user is looking at.

**Evidence.** The saturation check runs only when `repair_cursor >= repair_plan.grid_start` at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:1330-1352`. The plan places cardinal neighbors before the grid sweep at `e76b98e:vector_v2/src/idle_repair.cpp:42-57`.

**Proposed fix.** Apply saturation/headroom checks to every repair view, or pin the active view until all optional repair work stops.

**Regression test.** Fill the pool to the headroom threshold, run neighbor repair, and require the active view's resident keys to remain resident.

### CR-038: Medium: A passing gate build enters the interactive loop with gate-created authority and export data

**Provenance:** Review B M15.

**Trigger.** Boot firmware compiled with `TINYDRAW_VECTOR_V2_GATE_HARNESS` and let all gates pass.

**Impact.** The interactive app starts from the harness's long-gesture document and an already-written export partition rather than a clean product state.

**Evidence.** The app returns on gate failure but falls through into the normal loop on success at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:860-870`. The final gate sequence runs long-gesture commit and export encode, then only resets the camera with `set_view` at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:3191-3211`.

**Proposed fix.** Either terminate after printing the gate receipt or restore a blank snapshot, reset producer/cache state, clear export state, and refresh before entering product interaction.

**Regression test.** Run a passing harness boot and assert operation count, overview digest, cache revision, and export-store metadata match the intended post-gate policy.

### CR-039: Medium: Normal V2 processing can refuse a Down while the loop handles a long lift

**Provenance:** Review B M18.

**Trigger.** Generate rapid edge-heavy gestures while one loop iteration performs finish-preview, chunk commits, and region refresh for a prior Up.

**Impact.** The sampler can fill all 16 slots with non-coalescible edges. A new Down is refused and not retried as a Down, losing an entire gesture.

**Evidence.** The app pops at most one touch event per loop at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:988-996`; lift performs synchronous finish/commit/presentation work at `:1124-1208`. `TouchEventBuffer` coalesces/removes Moves but returns overflow when a full queue contains no Move at `e76b98e:vector_v2/src/touch_event_buffer.cpp:83-96`.

**Proposed fix.** Reserve transition slots, drain multiple events after long work, or separate edge and Move storage. If a Down cannot be preserved, cancel/reset the derivation state explicitly.

**Regression test.** Delay every lift phase while injecting rapid taps and prove that every physical Down reaches the app or produces an explicit cancellation receipt.

### CR-040: Medium: A failed linear panel stream returns without draining previously submitted DMA

**Provenance:** Review B M19.

**Trigger.** Let `stream_rect` submit one or more strips, then fail a later stage-patch or `tx_color` call.

**Impact.** The next presentation can program CASET/RASET while the prior RAMWR/RAMWRC queue is still draining, mixing windows and corrupting panel output.

**Evidence.** `present_pixels` aborts the scheduler and returns immediately when `stream_rect` fails at `e76b98e:esp32/main/vector_v2/vector_v2_presenter.cpp:1022-1028`. `stream_rect` can return false after earlier submissions when a later patch or `esp_lcd_panel_io_tx_color` fails at `e76b98e:esp32/main/co5300_panel_transport.cpp:610-636`.

**Proposed fix.** Track whether any strip was submitted and always call bounded `wait_for_all` or hard-reset the transport before another window is programmed.

**Regression test.** Fail each strip after the first successful submit, invoke another presentation, and assert the first queue drains or the transport resets before new window commands.

### CR-041: Low: The optional beam-race experiment can program a second window while the first is still in flight

**Provenance:** Review B M21. This applies only when `TINYDRAW_VECTOR_V2_PRESENTATION_BEAM_RACE_CONTROL` is enabled; the default path uses one sweep.

**Trigger.** Enable the experiment and pan with `start_row > 0`.

**Impact.** The second `present_ring` can send new CASET/RASET commands while queued color DMA still belongs to the first band.

**Evidence.** The experiment calls `present_ring(start_row…bottom)` and then `present_ring(0…start_row)` before the surrounding completion wait at `e76b98e:esp32/main/vector_v2/vector_v2_presenter.cpp:612-649`.

**Proposed fix.** Drain after the first band, combine both bands into one window, or remove the disabled experiment.

**Regression test.** Under the experimental define, assert no second window command occurs until the first band's submit/complete counters match.

### CR-042: Medium: `OperationLog::prepare` permits overlapping source and destination ranges

**Provenance:** Review B L1; severity raised because the public API can invoke undefined behavior.

**Trigger.** Pass `OperationAppend.samples` as a tail-overlapping view into the log's own sample storage.

**Impact.** `std::copy` on overlapping ranges is undefined when the destination starts inside the source, potentially corrupting operation authority.

**Evidence.** `prepare` validates the append, then copies directly into `samples_.subspan(sample_count_)` without a storage-overlap check at `e76b98e:vector_v2/src/operation_log.cpp:116-125`. The class already has `workspace_overlaps_storage`, showing storage aliasing is an established invariant at `:108-114`.

**Proposed fix.** Reject any source/destination overlap unless exact identity is explicitly supported with a safe operation; otherwise use non-overlapping caller workspace.

**Regression test.** Pass forward- and backward-overlapping subspans of the log arena under ASan/UBSan and require rejection with unchanged counts/revision.

### CR-043: Medium: Odd-width stream windows overread staging rows

**Provenance:** Review B L4; latent in current aligned product calls but unsafe in the transport API.

**Trigger.** Call `stream_rect` or `stream_rect_ring` with an odd width.

**Impact.** `stage_pixels_swapped` reads `source[column + 1]` on the last odd pixel, causing an out-of-bounds read and writing one extra destination element.

**Evidence.** The staging helper advances by two and unconditionally accesses the pair at `e76b98e:vector_v2/include/tinydraw/vector_v2/panel_staging.h:35-42`. `push_rect` rejects odd windows, but streaming validation checks bounds/stride without enforcing even width at `e76b98e:esp32/main/co5300_panel_transport.cpp:546-555` and `:643-650`.

**Proposed fix.** Apply the even-window validation to every stream entry point or implement a safe final-pixel path.

**Regression test.** Submit widths 1, 3, and 367 under ASan and require rejection or exact staged bytes without overread.

### CR-044: Low: The latent exposed-ring patch copies host-order pixels into a byte-swapped staging surface

**Provenance:** Review B L5. Current production pan precomposes exposed rows and passes an empty exposed span, so this is an API-path defect rather than a current app trigger.

**Trigger.** Call `present_ring` with a nonempty `exposed` span while `accepts_byte_swapped=true`.

**Impact.** Recomputed canvas pixels in the exposed patch have reversed RGB565 byte order while surrounding ring/chrome pixels are correct.

**Evidence.** `paint_stage_surface` calls `copy_ring_to_stage` for exposed areas before chrome at `e76b98e:esp32/main/vector_v2/vector_v2_presenter.cpp:849-860`. `present_ring` declares that the patch accepts a byte-swapped surface at `:931-935`; the copy routine writes host-order ring values.

**Proposed fix.** Swap copied exposed pixels when `surface.byte_swapped`, or remove this unused in-patch compose path.

**Regression test.** Present a nonempty exposed rectangle with asymmetric RGB565 colors and compare staged wire bytes with the normal ring path.

### CR-045: Medium: The TE ISR infers edge direction by sampling the pin after an ANYEDGE interrupt

**Provenance:** Review B L8; hardware-risk severity raised because the result controls presentation timing.

**Trigger.** A short TE pulse, bounce, or task/ISR latency allows the pin level to change again before `gpio_get_level` runs.

**Impact.** A rise can be counted as a fall or vice versa, causing a missed selected edge or starting a sweep at the wrong scan phase.

**Evidence.** One ANYEDGE ISR samples the current GPIO level to classify the edge at `e76b98e:esp32/main/co5300_panel_transport.cpp:799-815`.

**Proposed fix.** Use separate hardware edge events if supported, capture transition state at interrupt time, or use one configured edge required by the active presentation policy.

**Regression test.** Feed controlled TE pulses/bounce from a signal generator and compare ISR counters/timestamps with a logic analyzer.

### CR-046: Medium: A lost color-completion callback can hang the next presentation

**Provenance:** Review B L9; severity raised because the next acquire uses an infinite wait.

**Trigger.** Lose enough completion callbacks that `wait_for_all` times out while transfer semaphore slots remain held.

**Impact.** The next stream waits forever in `xSemaphoreTake(..., portMAX_DELAY)` once all three slots are exhausted.

**Evidence.** `wait_for_all` returns false after a bounded counter wait but does not recover semaphore/counters at `e76b98e:esp32/main/co5300_panel_transport.cpp:415-426`. Stream slot acquisition uses `portMAX_DELAY` at `:583-584` and `:678-679`.

**Proposed fix.** Use a bounded acquire and hard transport reset after completion timeout; reset counters/semaphores only after quiescing the peripheral.

**Regression test.** Suppress one through three completion callbacks, verify timeout, then require the next present to fail/recover within a bound rather than hang.

### CR-047: Low: Group publication can report failure after publishing a prefix of tiles

**Provenance:** Review B L10.

**Trigger.** Fail `publish_uniform` or `publish_surface_tile` after one earlier tile in the same group succeeded.

**Impact.** The caller receives `nullopt` even though derived cache state changed. A retry is usually safe, but completion/ledger accounting no longer describes an atomic group step.

**Evidence.** Paper publication writes each tile and returns immediately on a later failure at `e76b98e:vector_v2/src/tile_producer.cpp:185-205`. Raster group publication does the same at `:657-681`.

**Proposed fix.** Prevalidate/reserve all target slots before publishing, or return explicit partial progress and make ledger/caller semantics resumable by contract.

**Regression test.** Inject failure at every tile index and assert the returned progress and ledger exactly match the cache prefix that became live.

### CR-048: Medium: `PixelPainter` trusts an undersized packed span and has no stride contract

**Provenance:** Review B L11; latent in current packed callers but unsafe as a public utility.

**Trigger.** Construct `PixelPainter` with positive width/height and a span shorter than `width * height`, or with a strided surface represented as packed.

**Impact.** Pixel and rectangle methods write beyond the supplied span or into wrong rows.

**Evidence.** Constructors store the span and dimensions without validation, and writes index `y * width + x` at `e76b98e:core/include/tinydraw/ui/pixel_painter.h:20-47`.

**Proposed fix.** Accept an explicit stride, validate dimensions with checked extent arithmetic, and make invalid painters fail closed.

**Regression test.** Exercise undersized, overflowing, packed, and padded surfaces under ASan; invalid descriptors must perform no writes.

### CR-049: Low: Partial `AppStorage::allocate` failure leaks all earlier allocations

**Provenance:** Review B L12.

**Trigger.** Fail any later heap allocation after earlier external/internal blocks succeeded.

**Impact.** The startup failure path leaves PSRAM/internal blocks allocated. The current app stops after failure, but retries or future recovery code would inherit reduced memory.

**Evidence.** `AppStorage` owns many raw `heap_caps_malloc` pointers, returns false when any is null, and defines no destructor or rollback at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:188-309`.

**Proposed fix.** Add RAII cleanup or a single rollback routine invoked on every allocation failure; make ownership non-copyable.

**Regression test.** Fail each allocation ordinal and verify heap free-size and every pointer return to the pre-allocation state.

### CR-050: Medium: `CoverageTile::reset` relies on debug assertions before writing fixed storage

**Provenance:** Review B L14, narrowed to `CoverageTile`. Review B's `StrokeRaster` part is not retained because constructors set `valid_` and update/finish return safely at `e76b98e:core/src/stroke_raster.cpp:78-106`.

**Trigger.** Construct or reset a public `CoverageTile` with nonpositive or greater-than-tile dimensions in a release build.

**Impact.** Assertions disappear, dimensions are stored, and `fill_n(width * height)` can underflow/overflow the fixed coverage array.

**Evidence.** `CoverageTile::reset` uses assertions as its only dimension guard before storing dimensions and filling at `e76b98e:core/src/coverage_tile.cpp:43-55`.

**Proposed fix.** Return a status or maintain a `valid_` state and perform no write for invalid dimensions. Keep assertions as diagnostics, not the safety boundary.

**Regression test.** Exercise width/height `-1`, `0`, `kTileSize+1`, and multiplication-overflow pairs in a release ASan build; all must fail without writes.

---

## Known contract gaps and non-retained review notes

These items remain outside the 50-count because the repository already records them as incomplete requirements or because they are not correctness defects:

1. G1: V2 firmware export still writes PNG at `e76b98e:esp32/main/vector_v2/vector_v2_export.cpp:11` and `:70-104`; the shipment contract requires SVG through USB at `e76b98e:SHIP_CONTRACT.md:93-99`. CR-004 covers the separate SVG-versus-glass geometry defect.
2. G2: Undo/Redo actions are visible but authority support remains incomplete. `apply_chrome_action` does not mutate authority for Undo/Redo at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:743-746`, and the README records missing V2/RP2350 features at `e76b98e:README.md:16-19` and `:64-65`.
3. N1/N3: mixed-draw budget and cold-fill scheduling observations are performance results, not correctness findings.
4. N2/N4-N7: comments, unused modules/APIs, redundant abstractions, and file size are maintenance work.
5. D1-D7 and VFY-1/VFY-2: the provenance table preserves defensive or unresolved claims without presenting them as confirmed defects. The most important unresolved item is VFY-1, cross-core I2C access: the source shows touch and PMIC use the same bus from different tasks at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:779-780` and `:962-971`, but the ESP-IDF driver's internal serialization was not verified.

## Areas checked without another retained finding

Both inputs also examined RAMWRC continuation at `y=0`, replay block indexing, image-export metadata invalidation, USB globals, and `InkStream::end()` semantics. Source tracing did not establish another actionable defect in those paths. Image export invalidates metadata before payload rewrite, and the inactive `InkStream` behavior explicitly returns the prior safe point at `e76b98e:core/src/ink_stream.cpp:45-51` and `:99`.

## Recommended repair order

1. Fix gate truthfulness and replay parity: CR-010 through CR-017, CR-033, CR-034, and CR-038.
2. Unify stroke geometry: CR-002 through CR-004, with pixel and SVG parity oracles.
3. Protect data and gesture edges: CR-005, CR-006, CR-020, CR-031, and CR-039.
4. Repair presentation failure/synchronization: CR-008, CR-009, CR-032, CR-035, CR-036, CR-040, CR-045, and CR-046.
5. Centralize checked surface/geometry contracts: CR-027 through CR-030, CR-042, CR-043, CR-048, and CR-050.
6. Make cache and metric oracles truthful: CR-018, CR-019, CR-021 through CR-026, CR-037, and CR-047.
7. Address low-risk configuration/startup paths after product paths: CR-041, CR-044, and CR-049.

## Review limitations

- Review A ran the host debug, release, sanitizer, format, and Python checks listed above. Review B states that it did not rerun tests (`review_findings_2026_08_16_correctness/REVIEW.md:14-16`).
- Neither review built or executed ESP-IDF/RP2350 firmware on physical devices. CR-032, CR-035, CR-036, and CR-045 therefore describe source-supported hardware risks, not observed panel traces.
- No power-cut fixture tested CR-005, and no logic analyzer tested CR-032/CR-045.
- The worktree was active. Stable evidence is commit-qualified to `e76b98e` or `f2f6da7`; uncommitted performance edits were excluded from stable findings.
- The committed host suites do not exercise the malformed public descriptors demonstrated by targeted reproducers in CR-027, CR-028, and CR-030 or the latent descriptors in CR-042, CR-043, CR-048, and CR-050.
