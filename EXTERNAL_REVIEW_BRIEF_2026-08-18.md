# TinyDraw Vector V2 external review brief

Date: 2026-08-18

This review concerns the remaining performance and release work in TinyDraw's
Vector V2 firmware. The architecture is accepted, the main features work on
physical hardware, and the current task is to remove the last visible stalls
without weakening exactness or reopening closed interaction problems.

The source snapshot in this packet is authoritative. Historical receipts
explain how the current design was reached, including experiments that were
measured and removed.

## Requested review

Recommend the smallest measured sequence of changes that can close the work
listed below. Concentrate on mechanisms that fit the existing authority,
memory, cache, and scheduling model. Do not propose a rewrite, a broad
camera-aligned atlas, approximate painter order, hidden allocations, or
speculative second-core concurrency unless the supplied evidence proves that a
current requirement cannot otherwise be met.

The most important result is a concrete plan for high-zoom Undo/Redo. Faster
settled-AA progression and shorter one-shot refresh stalls are next. The review
should also check the residual rerender and cold-closure evidence for missing
confounders.

## Short reading order

1. `PRODUCT_TENETS.md`, `SHIP_CONTRACT.md`, and `PROJECT_STATE.md` for product
   priorities, binding thresholds, and the current scorecard.
2. `V2_ROADMAP.md` for the forward queue and dependency reopen matrix.
3. `docs/PERFORMANCE_CHRONICLE.md` for the measured optimization history,
   including failed experiments.
4. `VECTOR_V2_AUTHORITY_UNDO_DESIGN.md` and
   `VECTOR_V2_COMMITTED_OVERLAY_DESIGN.md` for the two authority transitions
   most relevant to the remaining stalls.
5. `benchmark-results/overlap-cold-fix-2026-08-17/RECEIPT.md`,
   `benchmark-results/committed-overlay/RECEIPT.md`,
   `benchmark-results/committed-overlay/DEJAVU_FIX_RECEIPT.md`,
   `benchmark-results/settled-aa-prototype/RECEIPT.md`, and
   `benchmark-results/v2-cleanup-final-2026-08-17/RECEIPT.md`.
6. The code paths named in each open item below.

## Product and hardware

The target is an ESP32-S3 at 240 MHz with 8 MiB octal PSRAM and a 368 by 448
RGB565 CO5300 panel. Requested 40, 50, and 60 MHz panel clocks all resolve to a
40 MHz effective clock because of the GPSPI divider. The effective transport is
10 Mpixel/s or 20 MB/s. The TE period is 16.773 ms. Register reads do not expose
a usable scanline oracle.

A full 448-row stream sustains about 29.4 FPS. A stream of at most 368 rows can
sustain 58.8 FPS. The product pan sweep covers rows 0 through 371 and has a
13.69 ms panel payload.

The vector world is 1472 by 1792 world units at 25%, 50%, 100%, 200%, and 400%
zoom. The final cache has 448 raw 64 by 64 RGB565 tile slots plus compact
paper/uniform identities. The 1.5 MiB export reserve is binding. In the latest
gate, holding that reserve left 709,256 bytes free and a 704,512-byte largest
PSRAM block. Broad viewport checkpoints and a second full detail cache are not
funded.

The product main task has a 16 KiB stack. The latest normal-product boot left
8,712 bytes of stack margin. The diagnostic gate uses a 20 KiB stack and left
6,248 bytes after the latest battery. Instrumentation and product behavior must
be assessed under their own layouts.

## Current architecture

Durable drawing authority is a blank baseline plus an ordered operation log.
Each physical Stroke may contain adjacent operation chunks with one nonzero
`gesture_id`. The active operation prefix is the Undo/Redo cursor; the retained
tail is Redo authority. Epoch and generation values prevent stale work from
crossing a history transition.

All pixels are derived:

```text
ordered vector operations
        |
        +-- complete hard-edged 368x448 overview at 25%
        |
        +-- sparse world-aligned detail at 50% to 400%
              +-- paper/uniform identities
              +-- 448 raw 64x64 tiles
                    |
             canvas-only toroidal frame ring
                    |
             bounded internal DMA staging
                    +-- transient live ink
                    +-- transient chrome
                    |
                 CO5300 panel
```

Interactive chunks publish operation authority first. The canvas absorbs the
pending range during bounded idle slices while a committed-operation overlay
keeps the visible result exact. A high-water fallback caps the pending range at
24 operations. Detail production is resumable, newest-first, cancellation-safe,
and constrained by fixed workspaces. Settled AA is another derived quality tier
under the same revision identity; it may replace immediate pixels but may not
change authority.

Autosave journals retained operations, active prefix, generation, and epoch.
Navigation, chrome state, caches, settled pixels, and export buffers are not
durable authority.

## Verified baseline and closed work

These are the latest accepted measurements in the supplied receipts. They are
not substitutes for the final same-candidate release run.

### Pan and input

- Pan is owner-accepted tear-free at 50%, 100%, 200%, and 400%, including dense
  hairlines.
- PANSEQ p95 is 33.939 ms at 100% and 33.934 ms at 400%, about 29.5 FPS and
  below the 38 ms guard.
- The canonical five-trace input corpus loses no Down or Up transitions.
- A controlled physical Stroke observed a 12 to 14 ms touch-controller cadence.
  Product submission averaged 1.527 ms and DMA completion averaged 2.353 ms,
  with a 3.810 ms maximum.

### Interactive publication

- Worst mixed-draw append fell from 19,324 us to 173 us after the
  authority-revision split.
- The old 87 to 199 ms lift hitch fell to 10 to 34 ms, then to about 4 to 5 ms
  typical after idle drains were restricted to empty input polls.
- Five INKTRACE replays, pending-overlay exactness, and the full device verdict
  remained green. Visible fallback and drop counters were zero in the accepted
  receipt.

### Cold rendering

- The frozen general corpus contains 910 operations and 12,157 samples.
- Cold Stage B measured 437.9, 428.4, 488.0, and 507.0 ms maxima at 50%, 100%,
  200%, and 400%. Later IRAM pinning removed a flash-layout regression and
  measured 496.693 ms at 400%.
- The latest 448-slot battery measured 421.787, 399.498, 464.071, and 515.123
  ms at 50%, 100%, 200%, and 400%. The autosave store was initialized, but the
  harness issued no journal writes. The permanent contract remains at most 500
  ms over 20 reset-separated normal-product runs. The firmware's 520 ms number
  is a development hold-the-line guard, not the release threshold.
- The separate overlap corpus at 50% fell from 585.821 ms to 476.969 ms after
  each overlapping chord began refreshing its finalized-pixel window inside
  that chord's narrower bounds. Compute fell from 496.256 to 384.393 ms and
  producer steps from 235 to 90. Every firmware verdict in that device battery
  passed, including the 520 ms development guard at general 400%.

### Revisit retention

- After drawing across a warm multi-zoom cache, the 50%, 100%, and 200% revisit
  gate previously missed 4, 9, and 16 tiles and spent 188 to 326 ms refilling.
  Idle all-zoom retention and remembered-view uniform materialization reduced
  each to zero missing tiles and about 0.38 ms.
- Pure revisit amplification is 1.000 with zero unexplained renders in the
  automated gate.
- Owner glass feedback was strongly positive. A few stray rerenders were still
  seen, so attribution remains open even though the main defect is fixed.

### Authority, features, and quality

- Whole-Stroke Undo/Redo is exact on host and accepted on glass. It retains at
  least ten levels, preserves Redo until replacement ink publishes, and restores
  mixed pen/eraser states byte-for-byte. Its high-zoom presentation cost is not
  accepted.
- Autosave and recovery preserve Redo, active prefix, generation, and epoch.
  Deterministic truncation and corruption fixtures retain the previous valid
  recovery point. Owner reboot tests passed on a real drawing.
- Settled analytic-coverage AA is accepted on glass. Earlier tiled measurements
  improved from 5 to 11 ms mean and 17 ms maximum to 1.7 to 5.4 ms mean and
  9.3 ms maximum. Those numbers are not a universal bound. A fresh gate
  verification on 2026-08-18 reported the 25% overview settling 42 windows in
  152.945 ms with a 76.416 ms maximum window, while the harness verdict still
  printed `ssaa_receipt=yellow`. Progression under normal interaction and a
  passing same-candidate device receipt remain open.
- SVG emits one painter-ordered path per physical Stroke. PNG streams the
  production settled-AA derivative. Physical open/appearance checks passed.
- The packet snapshot passed 31 of 31 host debug tests, 31 of 31 host
  release tests, and 13 of 13 ASan/UBSan tests. Vector V2 product, Raster V1,
  the 448-slot gate, QEMU, and the macOS host built; QEMU replay and the physical
  gate passed. Static-analysis failures and the remaining product-level physical
  checks are listed below.

## Ranked open work

### 1. Remove the high-zoom Undo/Redo cold rebuild

This is the binding user-visible performance problem.

`move_history_incrementally()` prepares the history transition, reconstructs
the affected 25% overview rectangle by replaying the target prefix, commits a
new canvas revision, invalidates intersecting detail identities, and publishes
the prepared operation-log change. The ESP controller then calls
`refresh_region()` for the affected bounds at the current zoom. At high zoom,
the exact detail is cold, so the user sees a visibly brutal rebuild.

Relevant code:

- `vector_v2/src/incremental_document.cpp`, especially
  `move_history_incrementally()`
- `vector_v2/include/tinydraw/vector_v2/incremental_document.h`
- `vector_v2/src/operation_log.cpp` and `operation_log.h`, especially
  `prepare_undo()`, `prepare_redo()`, `publish_history()`, epoch handling, and
  active/retained prefixes
- `vector_v2/src/materialized_canvas.cpp` and `materialized_canvas.h`, especially
  `commit_incremental_revision()` and damage invalidation
- `esp32/main/vector_v2/vector_v2_chrome_controller.cpp`, history actions and
  `refresh_region()`
- `esp32/main/vector_v2/vector_v2_presenter.cpp`
- `vector_v2/src/tile_producer.cpp`
- `vector_v2/tests/operation_log_test.cpp`,
  `incremental_document_test.cpp`, `materialized_canvas_revision_test.cpp`, and
  `tile_producer_test.cpp`

Assess whether affected resident detail can transition incrementally, whether
an Undo-specific reversible tile operation is possible without storing raster
authority, and whether visible-first production can avoid a full cold path.
Any proposal must handle Undo's subtractive nature, Redo, erasers, overlapping
Strokes, multi-chunk Stroke boundaries, branch replacement, partial failure,
and cancellation. Unaffected identities must remain resident and byte-exact.

The review should define a device gate for Undo and Redo at every zoom. Report
separately: authority transition time, first exact visible pixels, complete
affected-region time, maximum input-poll gap, tiles invalidated, tiles retained,
tiles rendered, and rerender-ledger causes.

### 2. Measure and improve settled-AA progression

Functional AA is accepted. The question is whether useful visible tiles settle
soon enough during ordinary draw, pan, zoom, and idle sequences, and whether the
same mechanism remains practical for PNG export.

`render_settled_window()` scans operations newest-first, unions coverage within
each operation, performs analytic coverage in the one-pixel boundary annulus,
and composites front-to-back into RGB565. Interior and exterior classification
uses squared distance; boundary pixels pay `sqrt`. Background scheduling gives
cold fill and repair priority over settlement.

The scheduler's nominal settle-slice budget is 8 ms, but `run_settle()` checks
that deadline only between complete windows. The fresh 25% gate result therefore
contains one 76.416 ms uninterruptible window despite the 8 ms nominal budget.
This is both a progression problem and a potential input-age problem. A proposal
must either make one window safely bounded or introduce an exact resumable seam
inside it; changing only the outer batch size cannot enforce the budget.

Relevant code:

- `vector_v2/src/settled_tile.cpp` and `settled_tile.h`
- `esp32/main/vector_v2/vector_v2_background_pipeline.cpp`, especially
  `run_settle()` and `run_slice()`
- `vector_v2/src/materialized_canvas.cpp`, quality promotion and downgrade rules
- `esp32/main/vector_v2/vector_v2_app_storage.cpp`, fixed AA workspaces
- `vector_v2/src/world_export.cpp` and
  `esp32/main/vector_v2/vector_v2_export.cpp`
- `vector_v2/tests/settled_tile_test.cpp`, materialized-canvas quality tests,
  and `world_export_test.cpp`

Preserve per-operation coverage union, newest-first saturation, the frozen
RGB565 blend model, exact painter order across operations, hard-edged 25%
overview authority, revision-safe publication, revisit retention, and PNG/device
pixel parity. Consider operation culling, ordering, reusable bounds, cheaper
boundary math, visible-tile priority, and batch sizing only with explicit byte
and gate costs.

The closure receipt should measure time to first settled tile, time until the
visible viewport is settled, total CPU time, worst uninterruptible window and
slice, poll-gap distribution, tiles promoted, repeated work after
pan/zoom/revisit, and export throughput on the normal product allocation. It
must replace the current `ssaa_receipt=yellow` verdict with a numeric pass gate.

### 3. Slice one-shot refresh stalls

The remaining 166 to 184 ms `poll_max` class was attributed to idle-time
one-shot full-frame refreshes on dense content: zoom transitions, pan fallback,
power/chrome, and drain swaps. Most happened with no touch waiting, but a Down
arriving during a stall measured event ages up to 66 ms. The largest known
sliceable contributor was a drain swap refresh of up to 79.5 ms. The battery
refresh has already been reduced to its 124 by 44 region.

Relevant code:

- `esp32/main/vector_v2/vector_v2_presenter.cpp`, `refresh()`,
  `refresh_region()`, frame-region presentation, and panel submit boundaries
- `esp32/main/vector_v2/vector_v2_background_pipeline.cpp`, drain swap, fill,
  repair, settle, and pending region state
- `esp32/main/vector_v2/vector_v2_chrome_controller.cpp`, chrome-triggered
  refreshes
- `esp32/main/vector_v2/vector_v2_app.cpp`, input-loop ordering
- `vector_v2/include/tinydraw/vector_v2/panel_staging.h`
- `esp32/main/co5300_panel_transport.cpp`

Assess band-sliced presentation with an input poll between bands. Preserve one
ordered visual transaction, TE and panel-window correctness, chrome lifetime,
exact composed pixels, bounded internal scratch, and cancellation of stale
work. A cadence change reopens pan optical correctness and ink presentation
latency.

### 4. Attribute residual revisit renders

Do not change cache capacity or eviction policy until every observed stray
render has a ledger cause. Current expected residuals are slot eviction under
pressure, XL-Stroke off-view retention that exceeds the 25 ms idle budget, and
the rare pending-range high-water fallback. `TINYDRAW_LIVE_LEDGER` prints cold,
damage, eviction, stale, and unexplained deltas at zoom, pan end, and drain in
the gate build. The normal product does not currently compile this ledger, so
an ordinary glass session cannot produce the attribution promised by older
documentation.

Relevant code:

- `vector_v2/src/rerender_ledger.cpp` and `rerender_ledger.h`
- `vector_v2/src/incremental_document.cpp`, all-zoom raw retention and
  remembered-view uniform materialization
- `vector_v2/src/idle_repair.cpp`
- `vector_v2/src/tile_producer.cpp`
- `vector_v2/src/materialized_canvas.cpp`, recent views and eviction ownership
- `esp32/main/vector_v2/vector_v2_background_pipeline.cpp`

The output should propose a compact reproduction tour and the minimum new
counters needed to separate legitimate damage from avoidable cold work. Keep
render amplification at or below 1.25, with 1.15 as the guard band, and require
zero unexplained renders.

### 5. Close the final normal-product cold distribution

The final release statistic has not been recorded. Run 20 reset-separated
physical-device trials of the frozen general 400% corpus with autosave and all
normal product services enabled. Measure from discarded detail through exact
visible viewport publication and DMA completion. The maximum, not p95, must be
at most 500 ms.

Record requested and effective panel clocks, compute, presentation, pacing,
touch service, free and largest internal/PSRAM blocks, export reserve, stack
margin, firmware size, and the complete verdict vector. A run is invalid if it
has a crash, allocation failure, authority mismatch, touch-service violation,
telemetry overflow, presentation failure, or skipped downstream gate. Keep raw
serial artifacts outside Git and commit a concise receipt.

Relevant code:

- `esp32/main/vector_v2/vector_v2_gate_harness.cpp`
- `vector_v2/src/tile_producer.cpp`
- `vector_v2/src/incremental_rasterizer.cpp`
- `vector_v2/tools/raster_census.cpp`
- `esp32/main/vector_v2/vector_v2_presenter.cpp`
- `esp32/main/vector_v2/vector_v2_ship_contract.h`

### 6. Finish correctness, failure, and release polish

Rank these separately from the measured hot paths:

- SVG eraser Strokes are white paths, not transparent cutouts, so they are
  semantically correct only over white. Preserve one painter-ordered path per
  physical Stroke.
- Capacity, save, export, storage, and hardware failures need visible UI.
- USB helper text needs measured centering.
- Every physical touch target and overlap needs pressed feedback and a glass
  missed-tap check.
- The SVG+PNG volume needs one physical mount, read, eject, Return to Drawing,
  remount receipt with no watchdog, stale medium, or USB-mode wedge.
- Representative long documents, operation/sample capacity, journal-full
  behavior, cache pressure, and 25% overview paths need characterization.
- Release closure needs multi-hour draw, pan, Undo/Redo, autosave, export,
  power, and restart soak coverage.

The main code areas are `vector_v2/src/svg_export.cpp`,
`esp32/main/vector_v2/svg_export_store.cpp`,
`esp32/main/vector_v2/vector_v2_export.cpp`,
`esp32/main/usb_export.cpp`, `vector_v2/src/chrome.cpp`, and
`esp32/main/vector_v2/vector_v2_chrome_controller.cpp`.

## Invariants and stop conditions

- Ordered vector operations and the active prefix remain the only V2 drawing
  authority. Raster tiles, overview pixels, AA, previews, and exports are
  derivatives.
- Pen and eraser painter order is exact. Undo, Redo, SVG, autosave, recovery,
  cold replay, and materialized pixels must agree on the same authority.
- A prepared authority or history change is transactional. Failure or
  cancellation leaves the published state unchanged.
- Unaffected tile identities survive local damage. A local Undo may not become
  a whole-cache invalidation.
- Settled quality may replace immediate quality for the same revision. It may
  not downgrade, outlive its revision, or cause sharp-to-blurry cycling on
  revisit.
- Input sampling remains independent of renderer progress. Down and Up are
  never lost. The interaction alarm is 15 ms; long work must yield at safe,
  exact boundaries.
- Fixed workspaces, allocation order, 448 raw slots, export reserve,
  cancellation checks, and toroidal staging stay unchanged unless one device
  measurement justifies reopening them.
- Every cache or index proposal must name its byte budget, identity,
  invalidation owner, exactness oracle, reuse receipt, and removal condition.
- Glass is authoritative for visible correctness and feel. Counters explain the
  result but do not overrule it.
- Change and measure one hot-path hypothesis at a time. Revert a change that
  pushes a closed metric beyond its guard.

Relevant dependency reopen rules:

| Changed area | Required reopened gates |
|---|---|
| Presenter, staging, or TE cadence | Pan optical correctness; ink presentation latency |
| Touch buffering or Stroke coordination | Ink latency and fidelity; pan and overlay gestures |
| Authority, generation, or history | Cold exactness; damage; SVG; autosave; recovery |
| Cache eviction or settled AA | Cold; revisit retention; memory reserve; export pixels |
| Autosave or storage scheduling | Pan; ink; cold; memory; recovery; export flush |

## Known current static-analysis findings

These findings were reproduced against the packet working tree on 2026-08-18.
They are not proof of runtime failure. They must be triaged before claiming the
static-analysis gate is green.

`clang-tidy` stops in `vector_v2/src/authority_journal.cpp`:

- `validate_payload()` has cognitive complexity 40 against a threshold of 20,
  nesting level 5 against 4, and 21 variables against 20.
- `copy_payload()` has seven parameters against a threshold of six.
- `recover_authority_journal()` has 127 lines, 85 statements, 16 branches, and
  cognitive complexity 50. The configured thresholds are 100 lines, 80
  statements, 15 branches, and complexity 20.
- Because the script stops at the first failing translation unit, this is a
  lower bound on the full `clang-tidy` result.

`cppcheck` reports:

- Possible uninitialized aggregate members:
  `InPlaceCommitScope::cross_zoom_invalidated` in `materialized_canvas.h`, and
  `InPlaceRetainScope::{painted_color, priority_view, deadline_us}` in
  `incremental_document.cpp`. Confirm whether every aggregate construction
  initializes them before dismissing these as false positives.
- The epoch, revision, and operation-count stability checks in
  `svg_export.cpp` are reported as always true or always false because the log
  is passed as `const` and the snapshots are compared within one synchronous
  call. Decide whether these checks document a future concurrency boundary or
  provide no current protection.
- `RerenderLedger::totals()` returns a stored aggregate by value and is flagged
  as a possible return-by-reference performance issue.
- Lower-priority style findings include raw loops in `panel_staging.h`,
  `incremental_document.cpp`, and `ink_trace.cpp`; a non-const reference in
  `materialized_canvas.cpp`; and shadowed names in `operation_log.cpp`.

Land safety-relevant initialization and ineffective-oracle findings as focused
changes with their own tests. Keep measured renderer patches limited to one
pre-registered performance hypothesis.

The whole-tree format check has one existing failure at
`esp32/main/firmware_canvas.h:28`. The changed Vector V2 files pass the targeted
format check. `git diff --check` passes.

## Required reviewer output

Return a ranked report with a separate section for each proposed change. For
every change include:

1. The root cause, with exact file and symbol references.
2. The smallest mechanism that addresses it and the invariant that makes the
   mechanism safe.
3. Predicted savings in wall time, compute time, first-useful-pixel latency, or
   poll gap, as applicable. Give a range and explain the measured input used to
   derive it.
4. Persistent and transient byte costs, split into internal RAM, PSRAM, flash,
   stack, and exported file size where relevant. State zero explicitly.
5. Exact host tests, fuzz cases, counters, device gates, and glass checks to add
   or rerun.
6. The observation that would falsify the mechanism, plus a numeric stop/go
   threshold.
7. Failure modes and the dependency-matrix gates reopened by the change.
8. A minimal patch sequence. Keep refactors separate from performance changes
   and order the patches so each measured hypothesis can be reverted alone.

Distinguish conclusions supported by static code inspection from conclusions
that require physical hardware. If two proposals compete for the same problem,
rank them by expected benefit per byte and per reopened gate. End with the
first patch you would implement, its pre-registered prediction, and the exact
command or device receipt that would decide whether to continue.
