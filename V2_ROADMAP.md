# TinyDraw V2 roadmap

Last updated: 2026-08-16 (post Cold Stage B)

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
- [x] Freeze the combined tapered-adversarial + evil-hairline cold corpus and
      define closure as the maximum of 20 reset-separated device runs. Current
      three-run development maximum is 1,269.157 ms.
- [x] Make firmware cold pass/fail use the ≤500 ms product threshold.
- [x] Report requested and effective panel clocks separately.
- [x] Record the authority decision: V2 is blank baseline plus ordered vector
      operations; raster drawings remain explicitly Raster V1.
- [x] Reconcile this queue with the implemented compositor and current receipts.

## Phase 1 — close pan

Current result: the chrome lifetime split reduced PANSEQ p95 from 50.934 ms to
33.939 ms at 100% and 33.934 ms at 400%, below the 38 ms guard. All 432 staged
strips stayed faster than wire and camera motion caused zero persistent chrome
redraws. Owner glass acceptance is clean at 50%, 100%, 200%, and 400%.

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
- [x] Exercise slow one-pixel motion and cached-pan deltas just below, at, and
      above the 96-pixel fallback boundary.
- [x] Accept product pan on glass at 50%, 100%, 200%, and 400%, including dense
      hairline content and drawing beneath fixed chrome.
- [ ] Archive the same-session torn positive control and tag the known-good
      revision for formal release evidence.

Do not alter pacing and carry an old optical verdict forward. Pan tearing is a
rhythm property; each cadence/staging change reopens the optical gate.

## Phase 2 — close visual-first ink

Current result: the visual lane is provisionally closed. The transient
provisional tail uses the original sampler timestamp and reaches DMA before
authority work. The owner trace measured 767 samples at 3.22 ms average and
12.40 ms maximum event→DMA with exact final authority. Lift still drains
authority synchronously, and formal trace/optical closure remains open.

- [x] Complete the five canonical traces — all five are recorded owner
      finger input, embedded in the harness, and replayed through the
      production buffer `offer()` path with zero lost transitions
      (`benchmark-results/ink-trace-replay-baseline/BASELINE.md`).
- [ ] Carry sampler timestamps through consume, geometry-ready, first command,
      first payload, DMA-complete, and optical observation.
- [ ] Keep a latest-point visual mailbox while preserving the ordered authority
      FIFO and every Down/Up transition.
- [x] Stage a transient old/new provisional tail over authoritative canvas;
      never bake it into the ring, document, or cache identity.
- [x] Submit the newest visible tail before chunk commit/materialization.
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

Owner-gated follow-up: the four-span (or flatness-adaptive 1-4 span) curved
authority subdivision was rejected on cost, but the wave-3 unit-merged row
sweep changed its cost model — extra chords in a unit are nearly free on the
row axis and subdivision is paid once per endpoint. A rematch directly
targets the residual live-curve angularity, but it changes committed
authority geometry and therefore reopens cold exactness, SVG parity, and the
frozen corpus statistics. Needs an explicit owner go and a re-baseline.

Deferred structural debt: `esp32/main/vector_v2/vector_v2_app.cpp` is over 1,300
lines. Extract interaction, authority, and lifecycle coordinators after the
performance gates are closed; keep that refactor out of measured hot-path
changes.

## Phase 3 — cold viability and rerender truth

The frozen cold corpus combines tapered adversarial content with Sarah's evil
hairlines in one 910-operation, 12,157-sample document. After Cold Stage B
(2026-08-16) the three-run maxima are: 50% **437.9 ms**, 100% **428.4 ms**,
200% **488.0 ms** — all under the ≤500 ms line — and 400% **507.0 ms**
(431.9 compute + 61.3 present + 12.1 pacing; was 668.980, originally
1,269.157). The operation block index stays because it helps normal
documents and active-prefix replay.

Completed experiments (receipts in `benchmark-results/wave2-compositor/` and
`benchmark-results/wave3-cold-compute/`):

1. [x] Segment-chunk bounds removed: exact and 8.5% faster on the tapered-only
       curved-authority device run, but below trajectory and cost 200,002 bytes
       of scarce PSRAM. See `COLD_SEGMENT_CHUNK_RECEIPT.md`.
2. [x] Tapered scanline recurrence rejected: exact and storage-free, but 3.3%
       slower on device. See `COLD_RASTER_RECURRENCE_RECEIPT.md`.
3. [x] Adjacent exact-publication batching rejected: zero resent pixels, but
       only 1.6% wall movement and lower-zoom interaction ticks around 22 ms.
       See `COLD_PUBLICATION_BATCH_RECEIPT.md`.
4. [x] Wave-3 accepted: stateless windowed span search with conservative chord
       seeds, device-native arithmetic (no per-row float libcalls),
       internal-SRAM producer scratch, once-per-endpoint prepared curve units,
       unit-merged masked row sweeps, and caller-split painters (warm append /
       windowed producer). Combined -47.3%. See
       `COLD_COMPUTE_CAMPAIGN_RECEIPT.md`.
5. [x] Wave-3 rejected with mechanisms recorded: summary-bitmap row
       saturation, plain-memcpy word masks, 6x2-tile bands without block
       saturation, hybrid warm/seeded shared search.

Cold Stage B results (receipts in
`benchmark-results/cold-stage-b-2026-08-16/RECEIPT.md`):

6. [x] Stage B accepted: strided publication straight from the supertask
       surface (no packed staging), O(1) raw-slot metadata (per-identity
       directory + occupied count, host-parity-asserted), the H7 op-level
       chord sweep (y-sorted scanline over whole-operation chord batches,
       work-budgeted by window-clipped span pixels), and IRAM-pinned
       presentation strip loops (flash-icache layout was consuming the pan
       wire-budget margin on unrelated builds). Combined −26% from the
       wave-3 endpoint.
7. [x] Stage B rejected with mechanisms recorded: word-mask window scans
       even with proven `l32i` inlining (`may_alias` loads; post-H7 windows
       too short — net +4–5 ms), flat row-count slice budgets (blew the
       idle-repair step contract), per-publish cache flush hooks, and
       `Cache_WriteBack_All` before the sweep (interrupt-WDT panic).

Remaining candidates for the last ~7 ms of 400% wall (only if the owner
wants the line closed before autosave re-measurement):

- [ ] Block-granular `MaskedRowSummary` saturation, then re-run the band-unit
      experiment on top of it.
- [ ] PIE fixed-point probe pre-filter and presentation/compute overlap only
      if the above fall short (the latter reopens pan optical gates).
- [ ] The 20-run reset-separated closure statistic has still never been run
      on post-wave-3 numbers; development characterization is 3 runs.

The stop/go gate stands: no generalized checkpoint system without an explicit
owner decision. The current trajectory has not required one.

In the same phase, replace the current revision-only amplification metric:

- [ ] Exact-compute ledger keyed by generation/revision + zoom + group.
- [x] Spatial-revisit ledger keyed by zoom + group, wired into the product
      canvas/producer, causing renders as cold / damage / evict / stale /
      unexplained; pure-revisit tour receipt is amplification 1.000
      (view-abort / repair / settled-refinement causes and the fail-on-
      dropped-keys check remain open).
- [ ] Gate A→B→mutate outside A→return A, local mutation, Undo/Redo, settled AA,
      autosave, and another return. Owner glass session 2026-08-16 confirmed
      revisit re-rendering after multi-zoom drawing (cross-zoom damage +
      eviction); first step is printing the ledger cause histogram during
      live glass sessions so feel maps to counts.

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
      Candidate design (2026-08-16, unmeasured): re-run the newest-first
      masked replay with the 1-bit mask widened to 8-bit accumulated alpha;
      interiors keep today's exact span fills, only span-boundary pixels get
      analytic capsule coverage composited front-to-back; publish as
      higher-quality cached tiles. First step is a host prototype on one
      group. See `review_findings_2026_08_16_cold_campaign/REVIEW.md`.
- [ ] Fix zoom-cycle return position: cycling back to a recently explored
      zoom must restore its saved local position per
      `VECTOR_V2_ZOOM_NAVIGATION.md` (owner glass report 2026-08-16; the
      cycle currently loses the 400% viewport). Mechanical fix in the
      button-cycle path against per-zoom navigation origins.
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

- [ ] Characterize representative long documents and capacity limits,
      including the never-gated 25% paths (overview present cost, append
      feel at 25%, zoom-out-to-25 transition — 25% has no cold path by
      design; the overview is the authority).
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
