# TinyDraw V2 roadmap

Last updated: 2026-08-16

Branch: `feat/v2-performance-followup`

V2 is the accepted architecture. Raster V1 remains the default and fallback
until every required item in [`SHIP_CONTRACT.md`](SHIP_CONTRACT.md) closes.
[`PROJECT_STATE.md`](PROJECT_STATE.md) contains current measurements; this file
is the only forward queue.

## Execution rules

- Land one measured hot-path hypothesis per revision with predicted savings,
  removed work, unchanged guards, and a falsifying observation.
- A result is provisional until measured with every normal product service that
  currently exists. Final pan/ink/cold closure requires autosave enabled.
- Glass-visible gates need optical evidence and a failing positive control.
- Preserve the 1.5 MiB export reserve and report free/largest PSRAM.
- Revert a change that pushes a closed metric outside its guard.
- Keep accepted receipts; rejected experiments remain in Git and dated evidence
  directories, not in this queue.

## Phase 0 — one source of truth

- [x] Restore [`PRODUCT_TENETS.md`](PRODUCT_TENETS.md) and make the ship
      contract authoritative for numeric gates.
- [x] Update the cold baseline to 663.829 ms and define closure as the maximum
      of 20 reset-separated fixed-corpus device runs.
- [x] Make firmware cold pass/fail use the ≤500 ms product threshold.
- [x] Report requested and effective panel clocks separately.
- [x] Record the authority decision: V2 is blank baseline plus ordered vector
      operations; raster drawings remain explicitly Raster V1.
- [x] Reconcile this queue with the implemented compositor and current receipts.

## Phase 1 — close pan

Current result: the chrome lifetime split reduced PANSEQ p95 from 50.934 ms to
33.939 ms at 100% and 33.934 ms at 400%, below the 38 ms guard. All 432 staged
strips stayed faster than wire and camera motion caused zero persistent chrome
redraws. The cadence change reopens optical correctness.

- [x] Use a toroidal canvas-only frame ring and compose only exposed canvas.
- [x] Patch fixed chrome in internal staging; never mutate reusable canvas.
- [x] Submit one row-zero ordered stream and drain once.
- [x] Keep every strip's staging time below its measured wire time.
- [x] Prove drawing beneath zoom/minimap/toolbar/battery does not corrupt
      authority or canvas pixels.
- [x] Split the existing 53,956-pixel chrome allocation by lifetime:
      toolbar state, battery state, zoom, minimap base/content, and dynamic
      old/new viewport rectangle lines.
- [x] Run the one-variable cache-split A/B with p50/p95/max, TE-period
      histogram, chrome prep, exposed compose, sweep, strip headroom, and
      reuse outcome.
- [x] Skip the balanced-strip A/B: p95 is below 38 ms.
- [x] Defer direct exposed-row composition and completion notification: the
      pacing guard is met.
- [ ] Exercise slow one-pixel motion and cached-pan deltas just below, at, and
      above the 96-pixel fallback boundary.
- [ ] Close optically in one session with product pan, under-overlay drawing,
      and the torn positive control. Tag the known-good revision.

Do not alter pacing and carry an old optical verdict forward. Pan tearing is a
rhythm property; each cadence/staging change reopens the optical gate.

## Phase 2 — close visual-first ink

Current result: authority work can run before visibility, provisional geometry
is omitted, lift drains authority synchronously, and timing often starts at loop
dispatch. The small-region presenter itself is fast enough.

- [ ] Complete the five canonical traces. At least one must be recorded from the
      owner's finger; inject through the production buffer `offer()` path.
- [ ] Carry sampler timestamps through consume, geometry-ready, first command,
      first payload, DMA-complete, and optical observation.
- [ ] Keep a latest-point visual mailbox while preserving the ordered authority
      FIFO and every Down/Up transition.
- [ ] Stage a transient old/new provisional tail over authoritative canvas;
      never bake it into the ring, document, or cache identity.
- [ ] Submit the newest visible tail before chunk commit/materialization.
- [ ] Convert authority commit, overview work, tile publication, and lift drain
      into resumable bounded slices. Lift closes visually and returns to input.
- [ ] Reconcile capacity rejection by erasing only the uncommitted transient
      tail; keep already committed chunks as physical ink.
- [ ] Report event→consume→geometry→payload→DMA distributions, optical p95/p99,
      coalescing, max time/space gap, lost transitions, lift backlog, and final
      authority equality.
- [ ] Meet optical p95 ≤45 ms / p99 ≤60 ms and the Raster V1 feel check; tag the
      known-good revision.

Live ink remains hard-edged. Settled AA does not enter this phase.

## Phase 3 — cold viability and rerender truth

Current frozen adversarial 400% result is 663.829 ms wall: 577.667 compute,
69.371 present, 15.618 pacing, and 1.173 touch. The operation block index stays:
it helps normal documents and active-prefix replay, though it rejects none of
the 1,038 genuinely intersecting adversarial candidates.

Run these as independent experiments:

1. [ ] Add exact conservative bounds for fixed-size segment chunks; report
       operation/chunk/segment counts, metadata bytes, pixel equality, and wall.
2. [ ] Optimize tapered-raster scanlines with incremental row/x terms and exact
       boundary fallback; retain exhaustive and fuzz equality oracles.
3. [ ] Batch adjacent exact publications while reporting first-exact-visible,
       viewport-exact, publication count, resent pixels, and total wall.

After experiments 1–2, require a cumulative trajectory of at least ~15% on
repeated device runs. After all three, every accepted run must be ≤500 ms and
continue toward the 450 ms guard. If the fixed corpus remains above 500 ms,
stop before generalized checkpoint caching and make an explicit owner decision
about corpus, target, hardware, or selective cached representation.

In the same phase, replace the current revision-only amplification metric:

- [ ] Exact-compute ledger keyed by generation/revision + zoom + group.
- [ ] Spatial-revisit ledger keyed by zoom + group and expected damage set.
- [ ] Cause every attempt: cold miss, expected damage, eviction, view abort,
      stale revision, repair, settled refinement, or unknown.
- [ ] Wire both ledgers into the product `TileProducer`; fail on dropped keys.
- [ ] Gate A→B→mutate outside A→return A, local mutation, Undo/Redo, settled AA,
      autosave, and another return.

Broad group checkpoints are not funded: one 128×128 RGB565 checkpoint is
32 KiB, while only ~306 KiB remains with the export reserve held.

## Phase 4 — authority spine

- [ ] Add a generation-pinned immutable read view over operation storage.
- [ ] Separate append/storage epoch, active operation-prefix cursor, and
      monotonic document generation.
- [ ] Undo/Redo changes the active prefix and advances generation; a new gesture
      after Undo truncates the redo branch or begins a new epoch.
- [ ] Compute whole-gesture damage as the union of its chunks and invalidate only
      intersecting overview cells and tile groups.
- [ ] Make New/Clear reset operation authority, overview, cache catalog, camera,
      history, and autosave state transactionally.
- [ ] Remove raster-only snapshot restore from product V2 load/Undo flows.

## Phase 5 — Undo, persistence, autosave

- [ ] Implement whole-gesture Undo/Redo with ≥10 guaranteed depth and exact
      mixed pen/eraser, branch-after-Undo, zoom, and localized-damage fixtures.
- [ ] Define a versioned append-only authority journal: operation records,
      gesture commit boundaries, active prefix, generation/epoch, and view/tool
      state, with sequence numbers, CRCs, and commit markers/superblocks.
- [ ] Save in bounded idle slices without entering the visual ink path.
- [ ] Recover to the last complete committed state after interruption at every
      record phase; lose at most the in-progress gesture plus the contract's
      reviewed committed-work window.
- [ ] Re-run pan, ink, cold, rerender, and memory gates with autosave enabled.

Derived overview, tile, chrome, and settled caches are never persisted.

## Phase 6 — finish product parity

- [ ] Wire the exact variable-width SVG core through a pinned authority snapshot
      and transactional temporary sink; promote output only after generation is
      rechecked. Preserve eraser fidelity.
- [ ] Implement settled analytic-coverage AA in bounded idle work. Publish a
      final cached identity and prove revisit retention; retain immediate
      hard-edged live ink. The rejected four-sample SSAA probe is not a path.
- [ ] Add minimap tap-to-jump using pan fallback/delta telemetry. Minimap drag
      remains post-ship.
- [ ] Integrate V1 power off/on, battery transitions, RTC, one-shot NTP, and
      autosave-before-risky-transition through narrow adapters.
- [ ] Add visible capacity, save, export, storage, and hardware failure states.
- [ ] Enlarge invisible tap targets, add pressed feedback, resolve overlaps, and
      run the physical missed-tap check.
- [ ] Capture a clean PNG/USB export receipt without watchdog failure.
- [ ] Revalidate Raster V1 on the current board as the fallback.

## Phase 7 — all-on release closure

- [ ] Characterize representative long documents and capacity limits.
- [ ] Exercise hairlines, XL strokes, dense overdraw, erasing, long gestures,
      every world edge, all zooms, and cache pressure.
- [ ] Soak repeated pan/draw/Undo/Redo/autosave/export/power cycles for hours.
- [ ] Interrupt power at every persistence boundary and verify exact recovery.
- [ ] Run host tests, sanitizers, formatting, static analysis, both firmware
      builds, hardware gates, optical checks, and export verification.
- [ ] Compare Raster V1 and Vector V2 feature parity explicitly.
- [ ] Tag each requirement's known-good revision, promote V2 to default, and
      retain a named legacy Raster V1 build.

## Definition of feature complete

- [ ] Every required pan, ink, cold, revisit, AA, Undo/Redo, SVG, autosave,
      platform, capacity, and recovery gate in the ship contract is closed.
- [ ] Normal services are enabled during final performance receipts.
- [ ] Current visible output never regresses from exact/settled to unexplained
      fallback on revisit within cache capacity.
- [ ] Input remains responsive during rendering, saving, recovery, and export.
- [ ] Long-session and restart tests show no corruption, leaks, or stale pixels.
- [ ] Raster V1 and Vector V2 build independently before and after promotion.

## Dependency reopen matrix

| Change | Reopens |
|---|---|
| Presenter, staging, TE cadence | Pan optical correctness; ink presentation latency |
| Touch buffering or coordinator order | Ink latency/fidelity; pan gesture behavior |
| Authority, generation, Undo/Redo | Cold exactness; rerender accounting; SVG; autosave |
| Cache eviction or settled AA | Cold; revisit retention; memory reserve |
| Autosave/storage scheduling | Pan; ink; cold interaction; memory; power-loss recovery |

## Guardrails and post-ship work

No rewrite, camera-aligned atlas, four stored stroke LODs, hidden V2 allocation,
V2 state in the Raster V1 loop, or speculative second-core scheduling. Cache
growth requires a measured reuse contract and must preserve the export reserve.

Post-ship: demo record/replay, minimap drag, semantic/editable SVG, optional
minimap visibility controls, and 800% zoom.

Completed foundation and historical measurements live in
[`vector_v2/README.md`](vector_v2/README.md),
[`vector_v2/hardware-receipts/`](vector_v2/hardware-receipts/),
[`benchmark-results/`](benchmark-results/), and [`docs/archive/`](docs/archive/).
