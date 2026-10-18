# Correctness review — 2026-08-16

## Executive summary

This review found **32 actionable correctness issues: 14 High, 17 Medium, and 1 Low**. The highest-risk themes are:

1. **Visible drawing divergence and data loss:** live ink, committed ink, and SVG export do not share one geometry model; V1 autosave can accept torn raster data after a power loss; and both ESP32 V1 and RP2350 input queues can discard gesture edges (findings CR-002 through CR-006 and CR-031).
2. **False-green shipment gates:** the firmware's final verdict ignores the entire ink-trace replay result, the trace verdict ignores its latency result, first-contact latency is not sampled, UI taps are replayed as ink, and the déjà-vu ledger never affects acceptance (CR-010 through CR-017).
3. **Memory-safety/extent validation:** three public raster APIs trust invalid edge/step counts or wrap strided-surface extent calculations; the post-baseline direct-publish commit repeats the extent bug in two new APIs (CR-027 through CR-030).
4. **Moving-target regression risk:** commit `f2f6da7e97265626ace1b96e363d92dbbe09c7df` passes host tests and sanitizers, but its format check fails and its new strided APIs have a reproducible out-of-bounds read for an overflowing stride (validation table and CR-030).

No source changes were made as part of the review. This Markdown file is the only review artifact added to the repository.

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

Those uncommitted edits were inspected for obvious correctness regressions but deliberately excluded from stable line-number findings because the user warned that another agent was actively changing them. CR-030 covers the last committed concurrent change, `f2f6da7`.

### Severity model

- **High:** reachable user-visible corruption/data loss, a false-green mandatory gate, a hang, or a memory-safety defect on a supported path.
- **Medium:** narrower state divergence, diagnostic/tool false results, unsafe public API behavior, or a hardware-dependent correctness risk.
- **Low:** incorrect diagnostics that do not alter drawing output or acceptance.

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

### CR-001 — High — A physical zoom-button event can remap an open stroke or pan halfway through the gesture

**Applies to:** `e76b98e` and `f2f6da7`; the relevant app path is inside the recent changed-file range, although the problematic button handling predates the final snapshot.

**Trigger.** Press the separate physical mode/zoom button while a touch-driven draw or pan is still active, then release the button before lifting the touch.

**Impact.** The active gesture began under one zoom/origin transform and continues under another. A stroke can split or jump in world space; a pan can jump; and the immediate refresh can erase the in-progress preview before the gesture is finalized.

**Evidence.** The physical button release calls `presenter.set_zoom(...)` without checking `pressed`, `panning`, or active ink at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:974-983`. Draw samples are mapped at `:1032`, `:1071`, and `:1144`, while pan samples use the same evolving navigation state at `:1060`. The coordinate transform depends on the presenter's current zoom/origin at `e76b98e:esp32/main/vector_v2/vector_v2_presenter.cpp:145-154`; `set_zoom` mutates navigation and refreshes at `:400-408`.

**Proposed fix.** Defer physical zoom changes until no gesture is open, or explicitly cancel/finalize the current gesture before changing navigation. Keep the zoom level captured at gesture start if zoom must be allowed concurrently.

**Regression test.** Start a three-sample stroke and a pan, inject a physical zoom-button release between samples two and three, and verify either (a) zoom is deferred and all samples map through one transform or (b) the gesture is explicitly terminated before zoom.

### CR-002 — High — A zero-length authority segment with unequal radii ignores the larger radius

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

### CR-003 — High — Live preview and committed authority use different stroke geometry, causing a lift-time visual pop

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

### CR-004 — High — SVG tests establish parity with the preview renderer, not with the committed glass image

**Applies to:** recent-range SVG export at `e76b98e` and `f2f6da7`.

**Trigger.** Export an operation with at least three non-collinear or tapered samples.

**Impact.** The SVG can differ from the document users see after lift. This breaks the explicit export requirement even if the existing SVG test remains green.

**Evidence.** SVG export rebuilds `CurvedRibbonStream` circles/convex paths at `e76b98e:vector_v2/src/svg_export.cpp:106-121`. Committed rendering uses the distinct midpoint-capsule authority path at `e76b98e:vector_v2/src/incremental_rasterizer.cpp:746-775` and `:1019-1037`. The parity test compares SVG-derived geometry with `RibbonRenderer`, not `apply_incremental_operation`, at `e76b98e:vector_v2/tests/svg_export_test.cpp:258-278`. The product contract requires an SVG “visually identical to glass” at `e76b98e:SHIP_CONTRACT.md:93-99`. CR-003's repro demonstrates that the preview geometry used by SVG is not pixel-equivalent to authority.

**Proposed fix.** Export the same canonical geometry used by committed authority, or formally change authority to the geometry already exported. Do not maintain two independent join/cap implementations.

**Regression test.** Rasterize exported SVG geometry and compare its complete mask against `apply_incremental_operation` for sharp joins, repeated samples, variable radius, clipping, and all supported zooms.

### CR-005 — High — V1 autosave can treat torn sector data as a valid drawing after power loss

**Applies to:** existing ESP32 V1 persistence at `e76b98e` and `f2f6da7`; README identifies V1 as the default operational app (`e76b98e:README.md:16-19`).

**Trigger.** Lose power after an existing saved drawing's data-sector erase or partial rewrite but before the full in-place save completes.

**Impact.** Boot can accept the old header and restore a mixture of old, new, and erased raster sectors as if it were a valid drawing. The failure is silent because no payload checksum or committed generation distinguishes complete from torn data.

**Evidence.** The header checksum covers header metadata, not drawing bytes, at `e76b98e:esp32/main/drawing_store.cpp:42-62`. A valid header is sufficient to mark a saved drawing at `:81-88`. Save erases and overwrites payload sectors in place at `:166-178`; the header is rewritten only when metadata is pending at `:193-203`. Restore trusts that header and reads payload sectors without validating a data checksum at `:241-268`.

**Proposed fix.** Use two banks or copy-on-write generations. Write payload sectors with per-sector or whole-image checksums, then atomically publish a generation/commit record last. Never erase the only committed generation first.

**Regression test.** Add fault injection after every erase/write step and reboot from the resulting flash image. Recovery must choose either the complete previous generation or the complete new generation, never mixed data.

### CR-006 — High — V1's drop-oldest shared event queue can discard touch edges and control commands

**Applies to:** existing ESP32 V1 input/replay path at `e76b98e` and `f2f6da7`.

**Trigger.** Fill the 32-entry queue while the app consumer is blocked or slow—for example during export—or generate a dense replay/demo burst.

**Impact.** Dropping an `Up` leaves the app in a pressed state, so a later `Down` is processed as continuation and can connect separate strokes. The same policy can discard replay start/reset controls, changing the meaning of all following samples.

**Evidence.** A full queue unconditionally drops its oldest `AppEvent` at `e76b98e:esp32/main/hardware_app.cpp:104-110`. Controls use that queue at `:113-115`, replay start/reset at `:151-167`, and touch samples at `:118-122`; capacity is 32 at `:743`. Export blocks the main consumer at `:600-625`. Gesture state relies on Down/Up ordering at `:1065-1213`.

**Proposed fix.** Separate control and touch queues, reserve slots for transitions, and coalesce/drop only replaceable Move samples. Never discard control, Down, or Up events; expose overflow as a recoverable cancellation if conservation becomes impossible.

**Regression test.** Hold the consumer behind a slow export, enqueue more than 32 mixed events, and prove all controls and touch edges arrive in order while only Moves may be coalesced.

### CR-007 — Medium — A failed V1 export can leave the toolbar stuck on “SAVING”

**Applies to:** existing ESP32 V1 display path at `e76b98e` and `f2f6da7`.

**Trigger.** Start export, keep the export toast visible, and complete with failure so `exporting` changes from true to false while ready remains false.

**Impact.** The screen can continue to show “SAVING” instead of the error state until another unrelated change dirties the toast region.

**Evidence.** Toast invalidation does not include the `exporting` bit at `e76b98e:esp32/main/physical_display.cpp:82-84`; palette invalidation at `:85-89` does not cover this state-only transition. The toolbar chooses “SAVING” versus “SAVED”/“ERROR” from `exporting` at `e76b98e:core/src/toolbar.cpp:270-285`. Refresh only transmits the toast when `toast_dirty_` is set at `e76b98e:esp32/main/physical_display.cpp:216-238`.

**Proposed fix.** Include `exporting` in toast-change detection whenever the export toast is visible.

**Regression test.** Render exporting=true, transition to exporting=false/ready=false without changing any other state, and require the toast pixels to be dirtied and updated to ERROR.

### CR-008 — Medium — Failed V2 presentation still advances visual and authority state

**Applies to:** V2 at `e76b98e` and `f2f6da7`.

**Trigger.** Make `present_unobscured` fail during a live-ink update.

**Impact.** The presenter's in-memory frame/provisional state and the committed document can advance even though those pixels never reached the panel. Later damage calculations can assume an old provisional region was erased when it remains visible, leaving stale ink until a full refresh.

**Evidence.** `show_update` mutates `frame_`, committed/provisional bookkeeping, and baked geometry before calling `present_unobscured` at `e76b98e:esp32/main/vector_v2/vector_v2_presenter.cpp:353-397`. `LiveInkCoordinator` records a visual failure but still calls builder add/commit at `e76b98e:vector_v2/include/tinydraw/vector_v2/live_ink_coordinator.h:19-36`. The app proceeds based on add/commit status and does not roll back `move.visual_passed` at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:1071-1101`.

**Proposed fix.** Make presentation transactional or set an explicit “panel state unknown/full refresh required” latch before authority can continue. Clear provisional bookkeeping only after a successful transfer.

**Regression test.** Inject one failed update transfer followed by a successful move/lift and verify the panel model converges exactly to a full compose without stale provisional pixels.

### CR-009 — Medium — A failed V2 pan transfer can leave navigation, ring state, and the panel split

**Applies to:** V2 at `e76b98e` and `f2f6da7`.

**Trigger.** Fail composition, chrome, TE, or transfer after a pan has already advanced navigation/ring bookkeeping, especially on the final move before lift.

**Impact.** The presenter's logical origin can be new while the panel still displays the old origin. The lift branch does not force a repair, so the mismatch can remain indefinitely until another refresh-triggering event.

**Evidence.** `pan_from` mutates navigation before `refresh_pan` succeeds at `e76b98e:esp32/main/vector_v2/vector_v2_presenter.cpp:421-454`. `refresh_pan` advances/invalidate ring state and has multiple post-mutation failure returns at `:508-555`. The app's panning lift branch reports completion without forcing a final full refresh at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:1124-1138`.

**Proposed fix.** Roll back navigation/ring state on failure, or immediately fall back to a full compose/transfer and keep a repair latch until that succeeds.

**Regression test.** Inject each failure point on the last pan move, lift, and compare logical navigation, ring contents, and modeled panel pixels to a clean full refresh.

### CR-010 — High — The overall firmware gate ignores the entire ink-trace replay result

**Applies to:** recent-range gate harness at `e76b98e` and `f2f6da7`.

**Trigger.** Make any trace parse, conservation, allocation, overflow, presentation, or authority-commit check return false while the other top-level gates pass.

**Impact.** The firmware harness reports overall success despite a failed mandatory ink-trace gate.

**Evidence.** `ink_trace_replay` is computed at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:3181-3183` and printed at `:3203-3209`, but the final return conjunction at `:3211-3212` omits it.

**Proposed fix.** Include `ink_trace_replay` in the final verdict and factor the final conjunction into a host-testable pure helper.

**Regression test.** Set every top-level input true except `ink_trace_replay=false`; the helper and harness must fail.

### CR-011 — High — The trace gate computes latency acceptance but omits it, and zero samples summarize as passing latency

**Applies to:** recent-range gate harness at `e76b98e` and `f2f6da7`.

**Trigger.** Produce event-to-DMA p95 over 28,000 µs, or collect no successful latency samples, while conservation/presentation/commit counters pass.

**Impact.** A trace can print `pass=1` despite violating the documented finger-to-glass threshold or measuring no latency at all.

**Evidence.** Empty latency data returns a zero-valued summary at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2770-2780`. `latency_pass` is calculated at `:2997`, but `trace_pass` omits it at `:2998-2999`. The documented threshold is 28 ms at `e76b98e:docs/INK_TRACE_HARNESS.md:66-70`.

**Proposed fix.** Require a defined nonzero/expected sample population and include `latency_pass` in `trace_pass`.

**Regression test.** Test otherwise-green summaries with p95 `28,001` and with zero samples; both must fail.

### CR-012 — High — First-contact presentation is absent from the latency population

**Applies to:** recent-range trace replay at `e76b98e` and `f2f6da7`.

**Trigger.** Make the initial Down/show-start transfer slow while subsequent Move transfers remain fast.

**Impact.** First-visible-ink lag can exceed the gate while reported p95 remains green, even though first contact is the latency users most directly perceive.

**Evidence.** The Down branch calls `show_start()` but only increments a presentation-failure counter at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2913-2929`. Full-chain samples are appended only in the Move branch at `:2967-2985`. The specification asks for a chain for every consumed sample at `e76b98e:docs/INK_TRACE_HARNESS.md:46-54`. The committed baseline records 371 consumed events but only 33 latency samples at `e76b98e:benchmark-results/ink-trace-replay-baseline/BASELINE.md:17`.

**Proposed fix.** Record geometry, submit, and DMA completion timestamps for successful `show_start()` calls; explicitly document and count any stationary/no-op events excluded from the sample population.

**Regression test.** Replay one stroke with Down over 28 ms and fast Moves; the latency verdict must fail.

### CR-013 — High — Trace replay bypasses product UI routing and grades chrome taps as ink

**Applies to:** recent-range V2 app/gate integration at `e76b98e` and `f2f6da7`.

**Trigger.** Replay a captured Down inside a chrome control or overlay hit region.

**Impact.** The harness draws ink where production activates UI, so event conservation, latency, and final authority no longer describe the product path the trace came from.

**Evidence.** Production checks `chrome_contains` before opening ink at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:998-1048`. The gate treats every Down as ink at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2913-2929`. The canonical `scribble-multistroke.csv` has a Down at `(355,71)` on line 359 (`e76b98e:testdata/ink-traces/scribble-multistroke.csv:359`); the zoom rail is defined at `e76b98e:vector_v2/src/chrome.cpp:37` and its expanded hit test at `:638-656`. The harness document claims production downstream routing at `e76b98e:docs/INK_TRACE_HARNESS.md:35-43`.

**Proposed fix.** Share the product event router with replay, or capture/replay events after routing with explicit event kinds (ink, pan, chrome action).

**Regression test.** Include overlay/zoom-rail taps in a trace and verify they generate the same action and zero ink in app and harness.

### CR-014 — High — Trace replay can hang forever if the final Up cannot enter the touch buffer

**Applies to:** recent-range gate replay at `e76b98e` and `f2f6da7`.

**Trigger.** Fill the 16-slot touch buffer with transition-heavy events so the final two no-touch samples emitted for Up overflow.

**Impact.** The replayer becomes exhausted while the consumer remains pressed; the loop's only termination condition is never met, hanging the firmware gate.

**Evidence.** The loop breaks only when `!pressed && replayer.exhausted()` at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2886-2893`. Up attempts only two no-touch offers and then marks itself done at `:2720-2731`. On overflow, `TouchEventBuffer` refuses the event while preserving its prior touching state at `e76b98e:vector_v2/src/touch_event_buffer.cpp:43-50`. A burst of short valid strokes can therefore consume capacity with non-coalescible edges.

**Proposed fix.** Treat `exhausted && pressed` as an explicit unclosed-stroke failure and add a replay deadline/watchdog. Better, reserve transition capacity so a final Up cannot be lost.

**Regression test.** Replay enough one-move strokes to overflow an edge-only buffer and require bounded failure rather than a hang.

### CR-015 — High — Trace fidelity counters and original timing do not affect acceptance

**Applies to:** recent-range trace tooling/gate at `e76b98e` and `f2f6da7`.

**Trigger.** Increase coalescing/max gaps, replay a burst late, or produce a final authority different from the capture while basic consumed/commit/presentation counters remain acceptable.

**Impact.** The gate can pass a replay that did not preserve capture timing or drawing outcome, so it cannot establish the fidelity claimed by its specification.

**Evidence.** `trace_pass` uses only event conservation, overflow, authority commit, and presentation at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2996-2999`. `InkStrokeCounters::valid()` defines stroke-counter invariants at `e76b98e:vector_v2/include/tinydraw/vector_v2/ink_trace.h:133-147` and is implemented at `e76b98e:vector_v2/src/ink_trace.cpp:379-384`, but the gate never calls it. The docs say coalescing/max-gap regressions and authority mismatches fail at `e76b98e:docs/INK_TRACE_HARNESS.md:56-73`. Although traces store relative timestamps (`:39-40`), replay stamps offered samples with current `esp_timer_get_time()` at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2700-2728`, so a late burst changes `InkStream`'s timing inputs instead of retaining recorded time.

**Proposed fix.** Enforce `InkStrokeCounters::valid()`, captured/baseline gap bounds, and an exact expected authority digest. Pass recorded target timestamps into the ink path while separately measuring real delivery/transfer lateness.

**Regression test.** Deliberately delay a burst, perturb one final operation, and exceed each fidelity bound independently; every mutation must fail the gate.

### CR-016 — Medium — The capture capacity and canonical corpus exceed what the gate parser can replay

**Applies to:** V2 trace capture/replay at `e76b98e` and `f2f6da7`.

**Trigger.** Dump a valid capture containing more than 4,096 events, including the committed 9,284-event under-overlay corpus.

**Impact.** The capture facility can produce traces the product gate cannot load, and the documented canonical corpus is not the corpus actually embedded in the firmware gate.

**Evidence.** Capture capacity is 12,288 at `e76b98e:esp32/main/vector_v2/vector_v2_ink_trace_capture.h:26`; the gate's parse storage is capped at 4,096 at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2804-2812`. The under-overlay baseline records 9,284 events and notes it is not embedded at `e76b98e:benchmark-results/ink-trace-replay-baseline/BASELINE.md:34-40`. The docs list under-overlay as canonical at `e76b98e:docs/INK_TRACE_HARNESS.md:23-30`, while the harness embeds a different fast-curve asset at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:2792-2802`.

**Proposed fix.** Stream parse/replay from external storage, allocate matching capacity, or lower capture capacity to an explicitly supported limit. Make the embedded/required corpus list one source of truth.

**Regression test.** Capture and replay the maximum documented event count, and assert every canonical corpus is either embedded or loaded by the gate job.

### CR-017 — High — The déjà-vu rerender oracle is diagnostic only and cannot fail the gate

**Applies to:** recent-range V2 gate and ledger at `e76b98e` and `f2f6da7`.

**Trigger.** Produce same-revision unexplained rerenders, stale-revision rerenders, or amplification over the contract bound while the home-view sharpness check passes.

**Impact.** The firmware can report success despite violating the contract's bounded-work/no-unexplained-rerender requirements.

**Evidence.** The cache-tour result is based only on `home_sharp` at `e76b98e:esp32/main/vector_v2/vector_v2_gate_harness.cpp:1253-1271`. Ledger totals are only reset/printed at `:3141-3166`; no ledger condition appears in the final return at `:3211-3212`. A same-revision duplicate increments `unexplained` at `e76b98e:vector_v2/src/rerender_ledger.cpp:119-127`. The required amplification ≤1.25 and zero stale/unexplained counts are stated at `e76b98e:SHIP_CONTRACT.md:38-64`.

**Proposed fix.** Calculate a tour-scoped ledger verdict requiring zero stale/unexplained counts and the configured amplification bound, then propagate it into the top-level result.

**Regression test.** Inject each forbidden ledger count and a 1.2501 amplification independently; each must fail cache-tour and overall verdict.

### CR-018 — Medium — Publishing one visible tile marks its whole 2×2 group rendered in the ledger

**Applies to:** recent-range materialization/ledger path at `e76b98e` and `f2f6da7`.

**Trigger.** Render a view intersecting only one tile of a 2×2 producer group, then expose a sibling tile at the same document revision.

**Impact.** The sibling's legitimate first render is classified as an unexplained rerender, making ledger metrics false even after CR-017 starts enforcing them.

**Evidence.** The producer records the group rendered whenever any tile was published at `e76b98e:vector_v2/src/tile_producer.cpp:591-603`. `publish_group` renders only the visible intersection at `:657-681`. The ledger stores one rendered flag per group rather than per tile at `e76b98e:vector_v2/src/rerender_ledger.cpp:104-137`.

**Proposed fix.** Track rendered bits per tile/revision, or mark a group rendered only when every member has actually been published for that revision.

**Regression test.** Render a one-tile view and then a sibling-only view at the same revision; both must classify as first renders, not unexplained rerenders.

### CR-019 — Medium — Explicit cache discard is misclassified as an unexplained rerender

**Applies to:** recent-range materialized canvas at `e76b98e` and `f2f6da7`.

**Trigger.** Render a group, call `discard_tiles()`, and render that group again at the same revision.

**Impact.** The ledger reports an unexplained rerender even though an explicit capacity action removed the cache entry.

**Evidence.** `discard_tiles()` clears raw and uniform cache entries without notifying the ledger at `e76b98e:vector_v2/src/materialized_canvas.cpp:1186-1201`. Ordinary slot replacement does call `mark_evicted` at `:934-935`. Without that flag, a same-revision refill reaches the unexplained branch at `e76b98e:vector_v2/src/rerender_ledger.cpp:119-127`.

**Proposed fix.** Before clearing, mark each distinct occupied raw/uniform group evicted once.

**Regression test.** Render → discard → rerender should produce exactly one eviction classification and zero unexplained classifications.

### CR-020 — Medium — Dumping a large trace can both overflow production touch input and race an in-flight capture write

**Applies to:** V2 ESP32 capture path at `e76b98e` and `f2f6da7`.

**Trigger.** Request a near-capacity trace dump while new touch samples continue arriving, especially with a producer preempted after observing capture enabled but before publishing its storage write.

**Impact.** The main app stops consuming product touch events long enough to overflow its 16-slot queue. Separately, a fixed two-tick delay is not a synchronization handshake: an in-flight producer can write after the dump snapshots/resets storage, corrupting the next capture or omitting an event from the dump.

**Evidence.** The app performs the dump in the main consumer at `e76b98e:esp32/main/vector_v2/vector_v2_app.cpp:1218-1223`. Dump prints up to 12,288 events and yields only every 256 at `e76b98e:esp32/main/vector_v2/vector_v2_ink_trace_capture.cpp:92-131`. The sampler continues offering into the production queue at `e76b98e:esp32/main/vector_v2/vector_v2_touch_sampler.cpp:104-134`, whose capacity is 16 at `e76b98e:esp32/main/vector_v2/vector_v2_touch_sampler.h:18`. Capture disables with a relaxed store and merely delays two ticks at `e76b98e:esp32/main/vector_v2/vector_v2_ink_trace_capture.cpp:93-96`; producer enable load and later storage/count publication are separate at `:46` and `:69-75`.

**Proposed fix.** Pause/acknowledge the sampler before snapshotting, or use an epoch/producer handshake or critical section that proves no write is in flight. Dump from a non-consumer task or first drain/cancel product touch state safely.

**Regression test.** Preempt the producer at every point around its enable load/storage/count update while dumping a maximum trace, and simultaneously inject a new gesture; require exact capture conservation and no production touch overflow.

### CR-021 — Medium — Angularity reconstruction double-processes chunk-boundary samples and can omit the terminal chunk

**Applies to:** recent-range host angularity tool at `e76b98e` and `f2f6da7`.

**Trigger.** Analyze a trace crossing the 32-sample operation boundary, especially one whose `finish()` first returns `kChunkReady`.

**Impact.** Reported angularity is measured over operations that differ from production: boundary points are duplicated after a second stateful filter update, and the final pending chunk can be acknowledged without ever being copied into the measured operations.

**Evidence.** `capture_chunk()` copies pending data and acknowledges internally at `e76b98e:vector_v2/tools/ink_angularity.cpp:141-146`. On a chunk-ready Move, the caller then calls stateful `ink.update(touch)` and `builder.add()` again at `:176-180`; builder acknowledgment has already reoffered the rejected boundary point at `e76b98e:vector_v2/src/chained_operation_builder.cpp:89-109`. `InkStream::update()` mutates filter, pressure, length, and timestamp state at `e76b98e:core/src/ink_stream.cpp:88-95`. Finish acknowledges once in `capture_chunk()` and a second time at `e76b98e:vector_v2/tools/ink_angularity.cpp:184-188`, allowing the final chunk to complete without capture.

**Proposed fix.** Compute each `InkPoint` once, offer it once, copy every pending append before exactly one acknowledgment, and continue solely from the status returned by that acknowledgment.

**Regression test.** Compare reconstructed operations sample-for-sample with the production coordinator for traces spanning several boundaries and a boundary-triggered finish.

### CR-022 — Medium — Angularity metrics omit physical joints at operation-chunk boundaries

**Applies to:** recent-range host angularity tool at `e76b98e` and `f2f6da7`.

**Trigger.** Place a sharp turn exactly across a 32-sample chunk boundary.

**Impact.** The turn is absent from joint p95, maximum, and threshold counts, understating the tail the tool is intended to grade.

**Evidence.** Each operation starts a fresh local chord chain at `e76b98e:vector_v2/tools/ink_angularity.cpp:201-207`; angles are computed only between chords in that chain at `:278-290`, and `run_trace()` measures operations independently at `:319-324`. Production chunks deliberately overlap while retaining one gesture identity at `e76b98e:vector_v2/include/tinydraw/vector_v2/chained_operation_builder.h:21-25`.

**Proposed fix.** Preserve stroke identity and carry the prior operation's terminal nondegenerate chord into the next operation's first joint calculation; reset only between distinct strokes.

**Regression test.** Chunked and unchunked forms of a stroke whose sole sharp turn lands on the boundary must report the same physical joint metrics.

### CR-023 — Medium — Raster setup census excludes work for rejected and saturation-skipped units

**Applies to:** recent-range tile producer metrics at `e76b98e` and `f2f6da7`.

**Trigger.** Benchmark many clipped/bbox-empty units or units skipped because coverage is already saturated.

**Impact.** Bounds, curve preparation, clipping, and decision work is reported as zero setup time for those units, understating setup cost and distorting optimization decisions.

**Evidence.** Setup timing starts at `e76b98e:vector_v2/src/tile_producer.cpp:431-433`. Bbox-empty and saturation paths return at `:483-496`; `setup_ticks` is accumulated only later at `:499-502`.

**Proposed fix.** Use a scoped setup timer or accumulate before every early return.

**Regression test.** Census-enabled groups containing only bbox-rejected units and only saturation-skipped units must contribute setup ticks and no paint ticks.

### CR-024 — Medium — Host “cold” census includes an untimed warm revisit

**Applies to:** recent-range raster census at `e76b98e` and `f2f6da7`.

**Trigger.** Run the general cold census path with its immediate complete-view revisit enabled.

**Impact.** Printed cold remaining-scan counts/time include a warm reuse scan that is excluded from cold wall time, so phase counters no longer reconcile with the labeled timing.

**Evidence.** Cold wall timing ends at `e76b98e:vector_v2/tools/raster_census.cpp:146-147`; an immediate revisit calls `produce_next()` at `:149-153`. `produce_next()` increments `remaining_scans`/`remaining_scan_ns` before detecting complete-view reuse at `e76b98e:vector_v2/src/tile_producer.cpp:232-251`. General census snapshots occur afterward at `e76b98e:vector_v2/tools/raster_census.cpp:621` and `:656-657`.

**Proposed fix.** Snapshot cold counters before the revisit, or subtract/restore the revisit counters.

**Regression test.** Toggling the reuse-accounting revisit must not change captured cold `RasterCensus` values.

### CR-025 — Medium — The Python trace validator accepts numeric syntax rejected by production

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

### CR-026 — Low — Invalid metadata is reported on the first event line instead of the metadata line

**Applies to:** recent-range C++ trace parser at `e76b98e` and `f2f6da7`.

**Trigger.** Parse syntactically valid CSV with invalid magic, empty name, or an empty sample-rate note on metadata line 2.

**Impact.** Capture diagnostics point at line 4, slowing investigation and potentially causing the wrong line to be edited.

**Evidence.** Header validation returns the default `event_index=0` at `e76b98e:vector_v2/src/ink_trace.cpp:201-206`; all validation failures are translated to `4 + event_index` at `:317-324`.

**Proposed fix.** Map `kInvalidHeader` to line 2 and retain event-index mapping only for event validation failures.

**Regression test.** One fixture per malformed metadata field must return `kInvalidTrace`, `kInvalidHeader`, and `line==2`.

### CR-027 — Medium — `CoverageTile` accepts arbitrary polygon lengths but writes them into a four-edge stack array

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

**Regression test.** Exercise 0–5 vertices under ASan; five vertices must be safely rejected or correctly rendered.

### CR-028 — Medium — Public prepared-curve APIs trust a mutable `step_count` larger than their fixed arrays

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

### CR-029 — Medium — Strided raster-surface extent checks wrap on 32-bit `size_t`

**Applies to:** ESP32-targeted raster APIs at `e76b98e` and `f2f6da7`.

**Trigger.** Supply dimensions/stride whose `(height-1)*stride + width` exceeds `SIZE_MAX` but wraps to a value no larger than the provided span.

**Impact.** Validation succeeds and later row indexing writes outside the span on 32-bit ESP32 builds.

**Evidence.** `RibbonRenderer::render_surface` computes the required extent with unchecked `size_t` multiplication/addition at `e76b98e:core/src/ribbon_renderer.cpp:85-90`, then writes using `row * stride` at `:136-151`. `RasterSurface` repeats the pattern at `e76b98e:vector_v2/src/incremental_rasterizer.cpp:117-129` before downstream writes. An exact 32-bit `RibbonRenderer` trigger is `width=1, height=65,537, stride=65,536`: mathematically required is `2^32+1`, but the check wraps to `1`. A within-V2-maximum-height trigger is `height=5,376, width=1, stride=1,943,322,877`, which wraps the required extent to `4`.

**Proposed fix.** Before multiplication, reject when `height - 1 > (SIZE_MAX - width) / stride`; centralize this in a checked strided-extent helper and use it for every surface API.

**Regression test.** Unit-test the helper with the exact values above and run a 32-bit sanitizer/emulator build that verifies rejection with no writes.

### CR-030 — Medium — The post-baseline direct-publish commit adds two more unchecked strided-extent paths

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

### CR-031 — High — RP2350's drop-oldest touch queue can lose lift and connect separate strokes

**Applies to:** existing RP2350 app at `e76b98e` and `f2f6da7`.

**Trigger.** Accumulate more than 32 touch events while the main loop is blocked in partial display submission.

**Impact.** If an Up is evicted before a following Down, `touch_down` remains true and the next physical gesture continues the prior operation, drawing a connector between separate strokes.

**Evidence.** A full queue drops the oldest event indiscriminately at `e76b98e:rp2350/src/main.cpp:84-90`; capacity is 32 at `:513`. Partial display submission is synchronous from the main loop at `:445-447`. Gesture state depends on conserved Down/Up ordering at `:571-606`.

**Proposed fix.** Reserve transition capacity and coalesce/drop only Move events, matching a transition-preserving touch buffer. If an edge truly cannot be retained, cancel the active stroke explicitly rather than silently continuing.

**Regression test.** Block display completion, enqueue more than 32 events spanning two gestures, and prove that operations remain separate and every Down has one matching Up/cancel.

### CR-032 — Medium — RP2350 partial DMA deselects the display before the PIO FIFO is known to drain

**Applies to:** existing RP2350 AMOLED driver at `e76b98e` and `f2f6da7`.

**Trigger.** Complete DMA for a partial update while the PIO state machine still has bytes queued in its FIFO/shift register.

**Impact.** Chip select can rise before the panel receives the final bytes, risking truncated/corrupt right- or bottom-edge pixels. This is a source-confirmed sequencing mismatch but remains a hardware risk because no RP2350 panel run was available.

**Evidence.** The full-display path explicitly delays after DMA before deselect and documents that DMA completion only means bytes reached the PIO FIFO at `e76b98e:rp2350/vendor/amoled/AMOLED_1in8.c:224-228`. The partial path waits for DMA and immediately deselects without that drain allowance at `:234-251`. The drawing app uses partial submission for strokes at `e76b98e:rp2350/src/main.cpp:445-447`.

**Proposed fix.** Apply the same proven drain delay to partial writes or, preferably, wait on a PIO-empty/shift-complete condition before deasserting CS.

**Regression test.** On hardware, repeatedly paint high-contrast patterns ending at the right and bottom boundaries while varying DMA length; verify the final bytes with a logic analyzer and panel readback/photographic comparison.

---

## Known contract gaps (not counted as newly found defects)

These are already documented project limitations, so they are separated from the 32 findings:

1. **V2 firmware export is still PNG rather than the required SVG-to-USB flow.** The V2 export implementation writes PNG at `e76b98e:esp32/main/vector_v2/vector_v2_export.cpp:11` and `:70-104`, while the shipment contract requires SVG at `e76b98e:SHIP_CONTRACT.md:93-99`.
2. **V2/RP2350 feature completeness remains explicitly unfinished.** The project README says V1 is the default operational app and V2 is incomplete at `e76b98e:README.md:16-19`; it separately lists RP2350 limitations including missing pan/Undo/persistence at `:64-65`.

## Areas checked without a reportable correctness finding

The review also examined panel exposed-region byte ordering, RAMWRC stripe continuation at `y=0`, replay block indexing, image-export metadata invalidation, USB global access, idle-repair eviction behavior, and `InkStream::end()` semantics. No issue from those leads met the evidence threshold for this report. In particular, existing panel call sites precompose the exposed area before the ring presentation path, and image export invalidates metadata before payload rewrite, so the initial corruption hypotheses did not survive source-path tracing.

## Recommended repair order

1. **Make gates truthful first:** CR-010, CR-011, CR-012, CR-013, CR-014, CR-015, and CR-017. These changes prevent the harness from certifying later fixes falsely.
2. **Unify stroke geometry:** CR-002, CR-003, and CR-004 under one canonical geometry representation, then lock it with pixel/SVG parity tests.
3. **Protect user data and gesture edges:** CR-005, CR-006, CR-031, then CR-007 through CR-009 and CR-020.
4. **Centralize checked descriptors:** one checked strided-extent helper for CR-029/CR-030, plus opaque/validated polygon and curve-preparation APIs for CR-027/CR-028.
5. **Repair the oracle/tooling model:** CR-016, CR-018, CR-019, and CR-021 through CR-026 so performance/correctness evidence describes production behavior.
6. **Validate RP hardware sequencing:** CR-032 with a logic analyzer or an equivalent PIO completion signal.

## Review limitations

- Host debug, release, and sanitizer suites passed as recorded above, but they do not build or execute the ESP-IDF/RP2350 firmware paths on physical devices.
- No power-cut flash fixture was available for CR-005 and no logic-analyzer trace was available for CR-032; both findings are derived from source ordering and are worded accordingly.
- The worktree was actively changing. Stable findings are commit-qualified to `e76b98e` or `f2f6da7`; the six uncommitted performance files listed in the scope section were not frozen into this report.
- A passing host suite does not cover the demonstrated malformed public descriptors because the targeted ASan reproducers exercise inputs absent from the committed tests (CR-027, CR-028, and CR-030).
