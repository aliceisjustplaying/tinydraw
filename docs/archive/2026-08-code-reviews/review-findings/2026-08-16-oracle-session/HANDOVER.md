# Session handover — 2026-08-16 oracle session

Written for the next session, which starts **Cold Stage B** (owner-approved).
Read this alongside the wave-3 handover
([`HANDOVER.md`](../2026-08-16-cold-campaign/HANDOVER.md))
— its §3 A/B loop recipe and §6 device-physics cheat sheet remain THE
operating manual and are not repeated here. The external-review adoption plan
is [`EXTERNAL_REVIEW_SYNTHESIS_2026-08-16.md`](../../../../reviews/EXTERNAL_REVIEW_SYNTHESIS_2026-08-16.md).

## 1. What this session did (all pushed to `feat/v2-performance-followup`)

| Commit | Content |
|---|---|
| `02790e5` | fix: four release-build safety holes (RibbonRenderer runtime validation; batch fail-closed — true worst case is 9 primitives, capacity now 10 with `overflowed()`; InkStream inactive guard; stronger `OperationLog::ready`) |
| `af9809a` | feat: ink-trace capture firmware (`./scripts/esp32 vector-v2-ink-capture PORT`) + host recorder (`tools/ink-trace-record.py`), upstream of coalescing |
| `8712ac6` | feat: in-place append phase attribution + honest rename `InPlaceCommitBudget` → `InPlaceRetentionBudget` (it only bounds offscreen raw retention) |
| `9e67220` | feat: recorded owner corpus (5 traces) + two capture bug fixes (WDT interleave on long dumps; missing header-columns line) |
| `7513aa1` | feat: under-overlay trace (9,284 events — proves the dump fixes) |
| `16d223a` | feat: `tinydraw_vector_v2_ink_angularity` tool + baseline receipt |
| `7a78dd7` | feat: re-render ledger (déjà-vu oracle) wired into product canvas/producer + harness receipts |
| `f3beb5c` | feat: ink-trace replay gate through production `offer()` + restore-hook fix + first device baselines |
| `0c6ffe1` | docs: PROJECT_STATE scorecard updates + external-review synthesis |

## 2. Machine and tree state

- Device `/dev/cu.usbmodem101` is flashed with the **gate harness at HEAD**.
- Build dirs: `esp32-vector-v2-gate-harness` (current), `esp32-vector-v2-ink-capture`
  (capture firmware, kept), `esp32-raster-v1` (verified), `host-debug/release/asan`
  (all green: 29/29, 29/29, 11/11), host-release has the angularity tool.
- Untracked leftovers predate the session (review zip, wave1a dir, datasheet PDF,
  `LATEST_tinydraw-review-report.md`).
- Full gate log of the final run was at `/tmp/gate-a2-a4-v2.log` (tmp — key lines
  are preserved in `benchmark-results/ink-trace-replay-baseline/BASELINE.md`).

## 3. NEXT TASK: Cold Stage B — serial, authority-neutral wins

Goal: compute **582 → ~410 ms** (wall ≤500 ms; preferred ≤450). Ranked queue
(estimates from the wave-3 receipt; code receipts re-verified this session at HEAD):

1. **Strided publish** (est. 10–20 ms, small): `tile_producer.cpp:629–647`
   copies supertask→packed, then `MaterializedCanvas::publish_tile`
   (`materialized_canvas.cpp:~898+`) copies packed→slot pool. Add strided
   analysis + strided publication; skip `packed` (which is internal SRAM —
   freeing it also returns 8 KiB internal).
2. **Metadata stack** (small, double-duty — also raises offscreen retention
   throughput inside the append budget → fewer budget-dropped tiles):
   - raw-slot directory `raw_slot_by_identity` (27,384 B; `find_tile` linear
     scan at `materialized_canvas.cpp:822`, ~10 call sites);
   - retained-identity bitset (~1.7 KiB; `std::find` scans at `:329, :683`);
   - maintained visible-missing count (`visible_tiles_remaining` rescans at
     `tile_producer.cpp:201,234,601`);
   - free-slot list only (no LRU heap) for `choose_slot` (`:856`).
3. **H7 op-level chord sweep** (est. 40–60 ms, medium): plan in wave-3
   handover §4.1 — per-(op × group) chord table, one y-sorted row sweep,
   budget by rows.
4. **Aligned word-mask retest** (est. 20–40 ms): only with
   `__builtin_assume_aligned` + painter-entry alignment check, and
   **disassembly-verified `l32i` before flashing** (wave-3 rejected the naive
   version: GCC-Xtensa emitted `callx8` memcpy per load).

Discipline: one hypothesis per flash; A and B in the same session; boot spread
is 1–3 ms so 10 ms deltas are real. **New standing guards this session added:**
every A/B run's log must keep
`TINYDRAW_RERENDER_LEDGER site=cache_tour … amplification=1.000 stale=0 unexplained=0`
and all five `TINYDRAW_INKTRACE … pass=1`. Speed must not buy amplification.

Expected residual gap after B: ~70–170 ms. Then the **owner gate C decision**:
the bundled authority change (conical capsule + flatness-adaptive 1/2/4
subdivision). IMPORTANT INVERSION: subdivision is now justified by **cold +
mixed_draw speed** (flat units on real input can drop to 1 chord at ≤0.35 px
error), NOT smoothness — the smoothness case was falsified (§5).

## 4. Queue after Stage B (owner-specified order)

1. **AA stuff**: settled-AA host prototype — internal design in
   `../review_findings_2026_08_16_cold_campaign/REVIEW.md` (8-bit accumulated
   alpha over the existing newest-first replay, boundary-only analytic
   coverage), amended with two external-review constraints: within-operation
   self-overlap must UNION coverage, never composite twice (§10.3), and the
   RGB565 blend model must be frozen before implementation (§10.5). Output =
   rendered before/afters for the owner. The **arc-length resampling sweep**
   pairs with this (both are the real smoothness levers; grade with the
   angularity tool's joint metrics on the recorded traces; resampling does
   NOT reopen the frozen cold corpus but re-measures `mixed_draw`).
2. **Panning/redrawing (déjà-vu campaign)**: ledger-guided. Add gate
   scenarios beyond the pure tour: draw-then-return-A, and an
   eviction-pressure tour (448 slots vs 10,304-tile 400% world). Fix what the
   cause histogram says: budget-dropped retention → authority/materialization
   revision split + committed overlay (external review §8.3–8.4, which is
   also the lift-hitch fix); eviction → protection-rank tuning; abort churn →
   producer scheduling.

## 5. Key facts established this session (do not re-derive)

- **Angularity (falsification)**: on real 1 kHz owner input, 2-chord
  deviation ≤0.11 px at 400%; 1-chord ≤0.35 px; extra subdivision does not
  reduce the joint-angle tail (input jitter + quarter-px quantization +
  32-sample chunk boundaries). Four-span-for-smoothness is dead; resampling +
  AA are the smoothness levers.
  Receipt: `benchmark-results/ink-angularity-baseline/BASELINE.md`.
  Tool: `tinydraw_vector_v2_ink_angularity` (`--chords 1|2|4 --zoom --size`).
- **mixed_draw attribution** (from the phase counters, 50% pen, 19.6 ms max):
  raw tile painting 12.3 ms, overview replay 3.9 ms, uniform
  materialize+paint 5.4 ms (pen only), commit 1.8 ms. It's the curved-path
  tile painting. Owner glass check deferred until after the subdivision
  experiment (which targets exactly that term).
- **Replay gate**: five traces embedded (~95 KiB, partition 44% free);
  event→DMA p95 2.3–5.3 ms, zero lost Down/Up, zero overflows.
  `under-overlay` is NOT embedded (190 KiB — needs streamed delivery).
  Receipt: `benchmark-results/ink-trace-replay-baseline/BASELINE.md`.
- **Ledger semantics**: causes = cold/damage/evict/stale/unexplained;
  `restore_snapshot` resets it (a restore replaces authority); harness
  resets + marks before the cache tour so that receipt is tour-scoped.
  Pure-revisit tour = amplification exactly 1.000.
- **Capture firmware**: dump yields every 256 lines (task-WDT + USB-drain);
  recorder validates per-line and rejects <100-event stray taps.

## 6. Owner decisions pending (with what each needs)

1. **Stage C authority bundle** — decide after B, with the residual cold gap
   + the angularity numbers (already in hand) + a conical pixel-delta A/B.
2. **mixed_draw budget vs fix** — deferred; re-attribute after subdivision.
3. **Dual-core guardrail relaxation** — only if B+C fall short of ≤500 ms.
4. **Settled-AA go** — owner explicitly wants to see the prototype (queue §4.1).

## 7. Gotchas for the next session

- The ctest `tinydraw_ink_trace_check_recorded` pins the recorded scribble
  trace (848 events / 7 strokes) — do not casually regenerate trace files.
- The DONE line now carries `ink_trace=%u`; the return conjunction is
  unchanged (cold + mixed_draw reds are pre-existing and expected).
- `TouchTraceReplayer` polls consumption tighter than the product loop
  (e2c_p50 ≈ 0.5 ms vs ~8.5 ms app tick) — noted in the baseline receipt;
  do not compare consumed counts against product sessions.
- Subagents were unavailable in this environment (no herdr); plan for
  sequential work unless that changes.
- `vector_v2_app.cpp` structural split remains deferred until after the
  performance gates close (standing guardrail).
