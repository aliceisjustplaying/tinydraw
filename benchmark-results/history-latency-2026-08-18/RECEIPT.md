# Deterministic high-zoom Undo/Redo latency baseline — 2026-08-18

## Scope

First device end-to-end whole-Stroke Undo/Redo timing, closing the
V2_ROADMAP §2 baseline gap ("no device end-to-end dense-history timing was
recorded"). ESP32-S3 at 240 MHz, 8 MiB octal PSRAM, CO5300 at 40 MHz
effective, 448-slot gate build on the post-cleanup head (`8ad5cfc` plus this
gate). Full battery remained all-ones with the new `history_latency=1` gate;
`ssaa_receipt=yellow` unchanged. Raw log: `/tmp/gate-verify.log`
(20:30 run), transcribed below.

## Corpus

`run_history_latency_gate` (`esp32/main/vector_v2/vector_v2_gate_harness_history.cpp`):
20 whole Strokes inside the 400% viewport at level origin (0,0). Base layer
is the owner-named "evil hairlines" — twelve minimum-radius (0.6 world px)
billiard-path strokes at varied angles, densely crossing. On top, a scribble
stack: two erasers, one XL band, medium/thin scribbles, one broad closing
scribble (~82 operations, ~2,700 samples). Strokes commit through
`ChainedOperationBuilder` + `append_authority_only` + full absorption, so
every history move starts from equal log/canvas revisions, exactly like the
product. Eight undos then eight redos per zoom; the first undos measure
broad damage over the dense crossing field — the worst replay case.

## Measured phases per move

- `move_us` — `move_history_incrementally`: authority transition, damaged
  overview patch replay (spatially indexed), may-ink rebuild.
- `first_us` — `refresh_region(affected_level_bounds)`: the product's
  immediate post-undo presentation, showing overview fallback for the
  invalidated detail (the visible "blurry flash").
- `repair_us` — producer refill of the affected tiles with one
  `refresh_region` per publication, mirroring the product's background fill
  (the visible block-by-block repair).

## Results (worst / range across 16 moves per zoom)

| Zoom | move_us | first_us | repair_us | total | max repair tick |
|---:|---:|---:|---:|---:|---:|
| 400% | 15,409–31,314 | 51,369–73,573 | 196,494–335,986 | 263,272–**433,851** | 14,762 µs |
| 200% | 15,389–31,282 | 24,025–32,817 | 101,988–140,989 | 145,863–203,678 | 11,907 µs |

All 32 moves passed the 520 ms development guard (`over_guard=0`). Undo and
redo are symmetric (redo of the same stroke costs within a few percent of
its undo). `move_us` grows with the active prefix (15.4 ms at the shortest
prefix, 31.3 ms with all ~82 operations active).

Same-log paced cold context: adversarial 400% wall 489.772 ms; overlap 50%
478.390 ms. A worst-case broad undo (433.9 ms) therefore costs about 89% of
a full adversarial cold render, but is presented as roughly 44 visible
transitions: one full-region fallback flash plus ~11–16 per-publication
block presentations across ~300 ms.

## Attribution

1. The dominant cost is exact re-rasterization of the affected tiles
   (`repair_us` ≈ 75% of total at 400%); its magnitude is bounded by the
   region's cold compute and is already spatially indexed
   (`scanned` ≈ 1,000 for 42 tiles).
2. The perceived brutality is presentation policy, not compute: the damage
   region degrades to fallback in one 51–74 ms present, then improves
   block-by-block. Nothing in the pipeline batches history repair into a
   single exact transition.
3. `first_us` at 400% (51–74 ms) is the F16/F17 composition class (compose
   into scratch, copy, stage) applied to a nearly full-frame region.

## Treatment: history damage hold-back — measured same-image A/B

Implemented in the same tree and measured in one gate image against the
identical corpus (`policy=per_publication` vs `policy=holdback`, 16 moves
per policy per zoom):

| Zoom | Policy | repair_max_us | total_max_us | presents/move | max repair tick |
|---:|---|---:|---:|---:|---:|
| 400% | per_publication | 341,672 | 440,848 | 12 | 13,059–14,8xx µs |
| 400% | holdback | 338,998 | 438,288 | **1** | **6,256 µs** |
| 200% | per_publication | 144,999 | 207,685 | ~5–12 | ~11,9xx µs |
| 200% | holdback | 142,999 | 206,376 | **1** | ≈ 6–7 ms class |

The wall-time hypothesis is **falsified**: intermediate presentation
overhead was not a meaningful wall component (≈1% delta, within dice).
The retained wins are perceptual and scheduling: visible transitions per
move drop from ~13 (fallback flash + ~12 block presentations) to 2 (one
exact fallback flash + one exact union swap), and the repair loop's worst
atomic tick halves from ~13–15 ms to ~6.3 ms because producer ticks no
longer carry a presentation — input polls run about twice as often during
history repair.

Same-log paced cold stayed green (adversarial 400% wall 497.793 ms ≤ 500;
layout dice against the 20:30 run's 489.772 ms). Full battery all-ones
with `history_latency=1`.

### Product wiring

`VectorV2ChromeController` records the level-space damage of a successful
Undo/Redo (`take_history_damage()`); the app forwards it to
`VectorV2BackgroundPipeline::hold_history_damage()` after
`reset_document_state()`. `run_fill` suppresses per-publication presents
intersecting the held region, accumulates the published union, and
converts it into one pending exact presentation when the refill completes
(`TINYDRAW_LIVE_HISTORY_HOLD suppressed=… union=…`). The hold is dropped
on any view or revision change, so pan/zoom/new ink mid-repair fall back
to ordinary per-publication behavior. Exactness, damage bounds, and
eviction policy are untouched; this is presentation policy only.

### Open

- Owner glass verdict on the new undo/redo feel (two transitions instead
  of block churn) — the binding acceptance for this scorecard row.
- `first_us` (51–74 ms fallback present at 400%) is now the largest
  single visible latency component; it belongs to the F16/F17 composition
  class (compose→copy→stage double-copy).

## COW preserved-tile swap — device accepted (same day, second round)

Whole-Stroke history now swaps preserved pre-images instead of rebuilding.
`MaterializedCanvas::commit_history_revision` retags affected current raw
tiles as preserved pre-images of the departing active prefix and swaps in
preserved tiles of the arriving prefix; preserved slots leave the
directory, are invisible to composition and residency, and form the first
eviction class in `choose_slot` (the "expendable" 604-slot policy). The
validity key is the new `OperationLog::history_timeline()` — unlike epoch
it survives pure Undo/Redo and changes only on redo-replacing append,
reset, and restore. This closes the aliasing trap that rejected the first
adjacent-generation prototype (epoch changes on every history publish).

Exactness: host 31/31 release + 13/13 ASan, including a dedicated oracle
(swap == cold rebuild byte-for-byte; branch replacement with a matching
prefix number must not swap — timeline guard proven; quality restored
exactly as preserved).

Device (604-slot gate, all-ones battery, evil-hairline history corpus):

| Metric, 400% | rebuild path | preserved swap |
|---|---:|---:|
| repair max | 338,998 µs | **229 µs** |
| total max | 440,301 µs | **116,253 µs** |
| producer work | 74 steps / 1,140 scans | 1 step / 0 scans |
| 200% total max | 208,390 µs | **66,334 µs** |

Round structure proves both directions: first-descent undos rebuild once;
redos swap; second-round undos swap. Remaining per-move cost is the
authority/overview patch replay (15–31 ms) and one exact presentation
(62–87 ms at 400%) — F16/F17 composition work and a future preserved
overview patch are the follow-on targets. Same-log adversarial paced cold:
391.8/382.5/457.3/492.8 ms — within dice, ≤500 held.

## Settled-AA round 1 — deterministic all-zoom gate + two treatments

`settle_timing` now rides the evil-hairline document in the battery,
closing the 50–400% automated coverage gap. Baseline (PSRAM planes):
107.2 / 138.8 / 276.9 / 661.0 / 1,765.3 ms at 25–400%; the 50% figure is
one 64×64 window containing the whole corpus (138.8 ms for a single tile).

Treatment 1 — no_ink white fast path (kept): windows the conservative
index proves empty complete as one atomic white fill and skip
publication/staging/presentation. 25% on this document: 41/42 windows
skipped, 166.8→107.1 ms, slices 959→438. Frozen AA checksums
byte-identical; the win scales with the ink-free fraction of real
drawings and does not move the dense window.

Treatment 2 — settle planes internal (kept): all six planes allocate
internal-first (40,960 B = exactly five 8 KiB dcache ways, so PSRAM
cache-set placement is preserved). Same-work A/B: −1.9/−1.9/−2.8/−3.4/
−4.1% at 25–400%. Cost: steady internal free 223→182 KiB; export dip
48,804 B free internal (thin, green — watch NTP/export margins).
The PSRAM-plane hypothesis for the host/device divergence is therefore
mostly falsified.

Attribution from the slice ledger: 8,812 slices × 512 work-px ≈ 4.5 M
work-pixels for 172 K settled output pixels at 400% — ~26× overdraw from
dense overlapping operations re-rasterized per window. The next credible
lever is edge-span recording (immediate rasterization persists its span
boundaries per tile; settle touches only the one-pixel annulus), which
attacks the overdraw itself rather than its memory placement.
