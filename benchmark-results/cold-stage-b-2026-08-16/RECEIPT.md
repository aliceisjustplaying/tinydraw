# Cold Stage B — session receipt (2026-08-16)

Machine: ESP32-S3 `/dev/cu.usbmodem101`, gate harness, three-run captures per
variant (within-build spread ≤3 µs on strip staging, ≤1.5 ms on cold walls).
Logs in `logs/`. Corpus: frozen `adversarial_tapered_4x+evil_hairlines`.

## Landed

**B1 — strided publish** (commit `f2f6da7`): publish straight from the
supertask surface; `packed_tile_pixels` staging removed from the producer
workspace (harness keeps a private census/edge-ink scratch). All gates held,
verdict vector identical to A.

**B2 — O(1) slot metadata** (candidate, NOT committed; full diff in
`cold-stage-b2-metadata-directory.patch`): per-identity raw-slot directory
(27,384 B, optional caller span, empty = legacy scans, host-debug parity
assert), occupied-slot count (O(1) `resident_raw_tiles`, full-pool
`choose_slot` skip), all occupancy flips centralized in
`release_slot`/`claim_slot`.

## Cold compute, three-run max (µs)

| zoom | A (HEAD e76b98e) | B1 | B1+B2 best | Δ total |
|---|---|---|---|---|
| 50  | 434,462 | 422,719 | 396,296 | −38.2 ms |
| 100 | 468,006 | 455,411 | 429,529 | −38.5 ms |
| 200 | 598,516 | 588,045 | 561,984 | −36.5 ms |
| 400 | 586,606 | 577,454 | 547,928 | −38.7 ms |

Walls at B1+B2: 50% **482.7 ms** (under the ≤500 contract line), 100%
513.1, 200% 651.4, 400% 635.5. Standing guards held on every run of every
variant: cache-tour ledger `amplification=1.000 stale=0 unexplained=0`, all
five `TINYDRAW_INKTRACE pass=1`.

## The B2 blocker: pan_seq strip-8 wire budget

`TINYDRAW_PANSEQ_STRIP` enforces per-strip CPU staging <
`rows × width / 10` µs (20 bytes/µs QSPI payload rate,
`co5300_panel_transport.cpp:730`). It is a **pipelining invariant** (staging
must outrun the wire so queued DMA hides it), not a directly demonstrated
glass tear. Strip 8 (20 rows, budget 736 µs) has the least absolute
headroom; whole-frame pacing (`pacing_pass`) stayed green in every variant.

z100 strip-8 staging (mean / max, µs) across builds:

| variant | mean | max | pan_seq |
|---|---|---|---|
| A baseline | 660 | 694 | green |
| B1 | 661–663 | 699–704 | green |
| B2 internal-mid + device assert | 716–721 | 779–782 | red |
| B2 internal-mid | 707–710 | 745–751 | red |
| B2 PSRAM-mid | 702 | 741–742 | red |
| B2 PSRAM-mid, directory span DISABLED (probe) | 694–695 | 732–735 | green (margin 1–4 µs) |
| B2 PSRAM-tail | 701–703 | 734–738 | 1 red / 3 |
| B2 + per-publish `esp_cache_msync` hook | 705–711 | 750–762 | red (and cost ~12 ms cold) |
| B2 directory-only (count/skip reverted to scans) | 712–716 | 754–762 | red |
| B2 PSRAM-tail padded to 32 KiB | 695–700 | 732–739 | 1 red / 3 |

One additional data point: a raw `Cache_WriteBack_All()` before the strip
sweep dropped the mean to **644** (below baseline) but hit interrupt-WDT
panics — unsafe, reverted. It confirms dirty-line writeback participates,
but source-flushing publishes did not recover it, and neither did restoring
the slot-scan "cache rinse", moving the allocation (internal / PSRAM-mid /
PSRAM-tail), nor 32 KiB way-size padding.

**Empirical law after 8 variants:** every B2-family build pays a uniform
+35–55 µs per-strip staging elevation across ALL strips (~4%); the two
non-B2 builds sit at 660±3. B1's strip-8 headroom was only ~35–40 µs, so B2
turns the gate into a coin flip or worse. Device-assert removal, placement,
and padding each shaved a few µs; nothing recovered the baseline.

## Open decision (owner) — RESOLVED later this session

Owner chose "park B2, proceed to B3". B3 then hit the same strip razor,
which forced the mechanism to a conclusion (below); the razor fix un-shelved
B2 the same day.

---

# Session 2 addendum — Stage B closed

## The strip-staging mystery, resolved

B3 (with zero canvas changes and no producer work inside the timed pan
frames — the gate prewarms every footprint) reproduced the identical
elevation, killing the "faster producer contends with staging" theory.
Further falsification probes on pure B1 code: reversed tile→slot
assignment (`choose_slot` from the end) measured 654–658 µs — fine; ~1 KB
of never-called dead code — 659 µs — fine. The phase receipts then showed
the smoking gun: `byte_swap`, a pure internal-memory CPU loop, ran 11 µs on
every baseline-layout image and 17–18 µs on every producer-change image,
with all prepare subphases up ~2–3% uniformly — **flash instruction-cache
layout luck**, larger than the strip-8 wire margin.

**Fix (landed, `11edbd7`):** a linker fragment pins the panel transport's
code in internal RAM (`esp32/main/linker.lf`, noflash_text; IRAM_ATTR on
the in-class methods trips Xtensa l32r literal errors). Receipt on B1 code:
strip-8 z100 staging mean 660→633 µs, max ~700→647 µs, max−mean jitter
40→13 µs. The optical contract's tightest margin no longer moves with
unrelated code changes.

## Landed experiments (all pushed)

| Commit | Experiment | 400% cold compute (3-run max) |
|---|---|---|
| `f2f6da7` | B1 strided publish | 586.6 → 577.5 ms |
| `11edbd7` | IRAM transport pin (razor fix) | — (presentation) |
| `bdd95e7` | B3 H7 op-level chord sweep + honest work-budget slices + index sort | 577.5 → 458.2 ms |
| `75c9145` | B2 un-shelved: O(1) slot metadata | 458.2 → 431.9 ms |

B3 notes: a flat 128-row slice budget red-flagged idle_repair
(worst_step 18.1 ms); budgeting by window-clipped span pixels actually
visited restored the step contract (6.1 ms, better than baseline 6.5).
Sorting 1-byte chord indices instead of 128-byte plans recovered ~40 ms at
50% zoom.

## Final Stage B numbers (device, 3-run max + confirmation run)

| zoom | session A compute | final compute | final wall |
|---|---|---|---|
| 50  | 434,462 | **356,150** (−78 ms) | **437.9 ms ✓ ≤500** |
| 100 | 468,006 | **348,620** (−119 ms) | **428.4 ms ✓** |
| 200 | 598,516 | **410,133** (−188 ms) | **488.0 ms ✓** |
| 400 | 586,606 | **431,908** (−155 ms) | 507.0 ms — 7 ms over |

Every run of every landed variant: cache-tour ledger
`amplification=1.000 stale=0 unexplained=0`, five `INKTRACE pass=1`,
verdict vector identical to the session baseline (pre-existing reds only).

## B4 — word-mask window scan: REJECTED (again, with new receipts)

1. `__builtin_assume_aligned` + fixed-size `memcpy` STILL emits a `callx8`
   memcpy libcall on GCC-Xtensa (caught by the objdump gate BEFORE
   flashing; the wave-3 prescription was insufficient). A
   `__attribute__((may_alias))` typed load does compile to a bare `l32i`
   (0 callx8 in both scan functions, verified).
2. Even with correct `l32i` loads, the device measured a net LOSS:
   z50 +4.3 ms, z400 +5.1 ms, z100/z200 −1 ms. Post-H7 the unfinalized
   windows are the per-row active-chord unions — typically a few bytes — so
   alignment stepping and extra branches outweigh the word loads. Do not
   retry without a structurally longer scan.

Also observed once (B4 run 1): a boot where the panel TE signal never
arrived (`tear_edge_observed=0 tear_edge_timeout=1 tear_heal_attempted=1`
from startup), cascading every gate red. Panel bring-up flake, healed by
the next reset; unrelated to any Stage B change.

## Owner glass session (2026-08-16 18:42–18:50, log: `logs/glass-session-2026-08-16.log`)

Drawing at 25→400% (long strokes, evil hairlines, fat lines, multiple
colors), stress panning at every tiled zoom, draw-then-zoom-return.

- **Tearing: none observed on glass**, and zero missed presentation
  bounds in the receipts (`submit_over_16ms=0 complete_over_33ms=0` on
  all 18 strokes). Cold redraw content correct at every zoom. Owner
  verdict: cold render closable barring regressions.
- **400% drawing lag (felt, slight)**: attributed to the known mixed_draw
  overrun — worst chunk commits 17–21.6 ms with `ph_uniform_max` up to
  18.3 ms (fresh paper tiles materialized+painted mid-stroke) and
  `ph_raw_max` up to 12 ms. At 25% the single worst commit was 42 ms
  (`ph_overview` 19.1 + `ph_commit` 22.2) but too rare to feel.
- **Zoom-cycle return position bug** (new): cycling back to 400% does not
  restore the previous viewport, violating VECTOR_V2_ZOOM_NAVIGATION.md's
  documented return behavior. Queued as a mechanical fix.
- **Déjà-vu confirmed open**: pan-around-then-revisit at 100% (after
  drawing at several zooms) visibly re-renders. Pure-revisit amplification
  is exactly 1.000 in the tour gate, so the observed causes are
  damage/eviction — cross-zoom invalidation by design (commits drop
  affected tiles at non-active zooms) plus slot pressure. Campaign step 1:
  live ledger cause-histogram receipts during glass sessions.

## Remaining to ≤500 ms at 400%

~7 ms of wall. Candidates: presentation/compute overlap (wave-3 §4.4,
owner-gated), PIE fixed-point probing (§4.3), block-granular saturation
(§4.5), or accept after the 20-run closure statistic. 25%-zoom gates
(present cost, append feel, zoom-out transition) are a noted future
addition — 25% has no cold path by design (the overview IS the authority).

## Addendum — owner ruling + hold-the-line guard (2026-08-16, later session)

Owner accepted the 7 ms residual until autosave exists ("ignore as long as
it does not regress further"); micro-candidates parked. Encoded as
`kColdViewport400HoldTheLineUs`, applied only to the frozen-corpus 400%
gate. First calibrated at 510'000 (507.0 ms measured max + twice the
≤1.5 ms within-build spread); recalibrated to 520'000 the same day after
four builds walked the wall 499.95/507.98/508.98/512.27 ms — between-build
flash-icache layout dice on the unpinned producer loops (this receipt's
±2–3% law), including +6 ms between builds with no cold-path diff.
IRAM-pinning the producer hot loops is queued to let the ceiling tighten.
Device verification (`logs/holdline-verify-1.log`): 400% wall 499,950 µs
against budget 510,000, pass=1; verdict vector `general_cold=1` with only
the pre-existing reds remaining (`overlap_cold=0` from the 50% overlap
workload — 621.9 ms, red since wave-3, surfaced for owner triage — and
`mixed_draw=0`); ledger amplification=1.000 stale=0 unexplained=0; all
five INKTRACE pass=1. Full rulings in SHIP_CONTRACT.md "Owner decisions —
2026-08-16".
