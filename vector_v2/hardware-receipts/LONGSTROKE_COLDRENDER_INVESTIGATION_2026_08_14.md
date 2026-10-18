# Long-stroke and cold-render investigation — 2026-08-14

Working notes and evidence for the two funded defects at HEAD `4a1aada`:

1. Interactive long strokes stall the coordinator ~70 ms at every 64-sample
   chunk commit (`append_max_us=70214/72144` in
   `/tmp/v2-correctness-closure/4a1aada-manual.log`), producing bursty ink and
   the tester's "cold rendering of marks just drawn" impression.
2. The 4× adversarial tapered 400% cold replay p95 is 1.452 s; the product
   target is now below 1.0 s with producer ticks under 15 ms.

This document records measured root causes first, then design decisions, then
before/after receipts. Raw logs referenced here are retained unedited.

## Measured baseline (clean 26a05f5 receipts, reconfirmed at 4a1aada)

From `26a05f5-cold-p95-20-runs.log`, the paced 400% adversarial line:

```
steps=4018 tiles=42 compute_us=1110536 present_us=60717 touch_us=9680
pacing_us=267967 wall_us=1448900 max_tick_us=7785
```

Wall-time attribution:

| Term | us | share |
|---|---:|---:|
| producer compute | 1,110,536 | 76.6% |
| pacing (idle delay + loop) | 267,967 | 18.5% |
| presentation | 60,717 | 4.2% |
| touch polling | 9,680 | 0.7% |

Two independent problems fall out immediately:

- **Pacing is pure overhead proportional to step count.** The harness (and the
  product loop) takes one `produce_next` slice per tick and sleeps 1 ms every
  8 ticks. 4,018 steps at 400% → ~502 sleeps ≈ 268 ms of wall doing nothing.
  Average compute per step is 276 us against a 15 ms tick bound — the
  work-unit budget (`kTileProducerRasterWorkBatch = 16000` bbox pixels)
  under-fills ticks by ~30×, because the bbox-pixel work estimate
  over-predicts the real cost of masked replay by that factor once the
  finalized mask saturates.
- **Producer compute itself is dominated by re-scanning finalized rows.**

## Host census (tool `tinydraw_vector_v2_raster_census`, release, counters on)

400% sweep at HEAD:

```
zoom=400 steps=4018 tiles=42 wall_ms=10.8 exact=1
  ops_rejected=0 segs_painted=16868 segs_rejected=30748
  rows_scanned=35164 rows_prefinal=1155562 rows_empty=4619 span_px=1501115
  mask_skips=1210171 covers_calls=290944 covers_hits=124070
```

Reading: 47,616 segment visits (128 ops × 31 segments × 12 view groups) paint
only 124,070 pixels, but walk **1,155,562 bbox rows that are already fully
finalized**, each paying a `mask_range_all_set` byte scan (≤16 bytes internal
RAM) plus loop overhead. Only 35,164 rows do real span work. 97% of visited
rows are dead work re-proving the same saturation, segment after segment,
operation after operation. The producer keeps iterating ops/segments even when
the whole 128×128 group mask is saturated; there is no early exit.

At 200% the same shape holds (452k prefinalized rows, steps=1214).

Historical context: the word-skip optimization took device compute from 21.8 s
to 1.09 s by making finalized work cheap per row. The remaining 1.1 s is the
*number of row visits*, not the per-visit cost. The next structural step is to
not visit dead rows/segments/ops at all.

## Root causes (cold render)

1. **No saturation summary.** The finalized mask can only answer "is this
   byte range all set" by scanning; nothing caches "this row is done",
   "these rows are done", or "the whole group is done". Consequences, in
   decreasing cost order at 400%:
   - 1.16M redundant per-row byte scans;
   - 30,748 segment visits (bbox reject) + 16,868 painted-segment visits that
     could be skipped in O(1) once their row ranges saturate;
   - full 128-op walks per group continue after the group is provably final.
2. **Work-budget units mispredict masked cost ~30×**, so slices are tiny and
   the paced loop pays fixed per-tick overhead (poll, scheduling, 1 ms sleeps)
   ~4,000 times instead of ~150.

## Root causes (long stroke)

`append_incrementally` per 64-sample chunk at 400% (from code reading; the
70 ms `append_max_us` receipts):

- For every affected resident tile in the priority view (up to 56):
  `copy_resident_tile` copies 8 KiB PSRAM→PSRAM into scratch, replays all ~64
  chunk segments against the tile, then `commit_incremental_revision` copies
  8 KiB back, and `analyze_tile_payload` scans the 8 KiB payload **twice**
  (once in validation, once in commit). ≈40 KiB of PSRAM traffic per tile
  ≈ >1 MiB per chunk commit, all synchronous inside one input-poll slice.
- `invalidate_uniforms` walks all 13,692 uniform identities computing world
  rectangles per entry, per commit.
- The transactional scratch design exists so a failed commit leaves the canvas
  untouched — but every failure mode is detectable *before* painting
  (log capacity, revision mismatch, workspace validity). The copies buy
  nothing that up-front validation cannot.

The visible effect: ink freezes ~70 ms at every boundary while core 1
coalesces moves, then a burst of path appears at once — perceived as the app
re-rendering the stroke. Post-lift fill telemetry (revision 45: 1 ms, zero
tiles) already proved the background producer was not the cause.

## Design decisions

### Cold render

1. `MaskedSurfaceSummary` (new, host-tested): per-row unset-pixel counts plus
   a saturated-row bitmap over the producer group, maintained exactly by the
   masked paint paths (a bit is set once per newly finalized pixel, so the
   count update is O(1) amortized). Queries:
   - row saturated → skip the row without scanning mask bytes;
   - inclusive row range saturated → skip a whole segment or operation in O(1);
   - all rows saturated → group is final, stop replay and publish.
   Exactness argument: a skipped row/segment/op writes only pixels whose
   finalized bits are already set, which the masked painter would skip
   pixel-by-pixel anyway; skipping produces bit-identical surfaces.
2. Work accounting: charge saturation-skipped segments/ops at O(1) cost so
   slices fill with useful work; step counts (and therefore pacing overhead)
   collapse proportionally.

### Long stroke

Replace the copy-out/copy-back chunk commit with **validate-first in-place
commit**:

1. Validate everything fallible up front (log prepare, revision equality,
   workspace checks, slot availability for uniform conversions).
2. Paint the chunk's segments directly into resident view tiles (only covered
   spans are written), paint the affected overview region through the existing
   small scratch, then publish log + revision + sweeps atomically.
3. Affected-but-not-updated identities (other zooms, out-of-view) are
   invalidated exactly as today.
4. `invalidate_uniforms` switches from a full catalog walk to per-zoom index
   ranges computed from the affected world bounds.

Failure after validation is impossible by construction (painting into owned
storage cannot fail), so transactionality is preserved without the 4× PSRAM
copy traffic. Chunk boundaries drop from ~70 ms to a bounded few ms; the
64-sample chunk stays (it also keeps per-operation bboxes tight, which the
cold path's op-level rejection depends on).

## Phase A result: saturation-gated cold replay

Implementation (one commit):

- `MaskedRowSummary` (incremental_rasterizer): exact per-row unfinalized
  counts + saturated-row bitmap over the producer group, fed by the masked
  painters (`note_finalized` per painted row; popcount on chunk fills).
- `TileProducer` consults it at three levels: whole-group early completion
  (`all_saturated`), whole-operation skip, and whole-segment skip via
  O(words) `rows_saturated` range queries. Skips are provably no-ops: every
  pixel the skipped unit could write is already finalized.
- Producer restructure: operation-level gates (log fetch, bbox reject,
  saturation) now run once per operation via a cached fetch
  (`gate_active_operation`) instead of once per segment visit, and the
  work-unit estimate reuses the already-computed clipped segment bounds
  instead of recomputing them through `incremental_segment_step_work`. This
  made the summary net-negative-cost on corpora where it never fires.
- Two rejected sub-designs, kept out deliberately:
  - per-row bitmap probe inside the painters (measured +8% seed-7 compute for
    no exactness benefit; row-level saturation stays on the mask byte scan);
  - the initial per-segment-visit op gates (+13..39 ms at 100/200%).

Device paced-gate confirmation (single clean runs, `pass=1` everywhere):

| Corpus/zoom | 26a05f5 p95 | Phase A run | delta |
|---|---:|---:|---:|
| adversarial 50% | 164.0 ms | 154.0 ms | −6% |
| adversarial 100% | 244.0 ms | 234.0 ms | −4% |
| adversarial 200% | 603.0 ms | 591.0 ms | −2% |
| **adversarial 400%** | **1448.9 ms** | **670.9 ms** | **−54%** |
| overlap 50% | 541.3 ms | 465.9 ms | −14% |
| overlap 100% | 406.0 ms | 315.0 ms | −22% |
| overlap 200% | 416.0 ms | 310.0 ms | −25% |
| overlap 400% | 416.0 ms | 287.0 ms | −31% |
| seed-7 400% | 343.0 ms | 363.0 ms | +6% |

400% adversarial detail: steps 4018→904, compute 1110.5→540.2 ms, pacing
268.0→64.6 ms, max tick 7.9 ms (bound 15 ms). Host census: painted segments
16,868→4,781; prefinalized-row scans 1,155,562→227,515; 53 ops and 2,543
segments skipped in O(1); 4 groups complete early; `exact=1` and both fuzzers
green.

The seed-7 +6% was isolated experimentally: feeding the painters a null
summary (disabling all bookkeeping and skips) did not recover it, so it is
attributable to the producer restructure / code layout, not to summary
maintenance. It remains far below every alarm; the same restructure buys
−22 ms at adversarial 200%. Raw logs: `/tmp/v2-inv/satsum-gate-run*.log`,
`/tmp/v2-inv/exp1-gate.log` (isolation experiment).

New tests: `MaskedRowSummary` unit coverage (word-boundary ranges, reset
rearm, painter-fed exactness vs the mask bit-for-bit) and a behavioral
producer test that a newest opaque cover completes a group in ≤6 slices over
80 buried multi-sample operations while staying bit-exact.

## Phase B result: in-place interactive chunk commit

Measured root cause confirmed by temporary phase instrumentation on the
deterministic long-gesture gate (worst-case: maximum-speed XL zigzag at 400%,
1,600 samples, 26 chunks of 64):

```
PROFILE_INPLACE valid=~50 prep=~100 overview=~570 enum=~150 canedit=~45
                paint=9800..12900 commit=~430  (us, per 64-sample chunk)
```

Everything except painting totals ~1.4 ms; the reference path's additional
~20 ms per chunk was pure copy-out/copy-back/analyze staging of affected
tiles. Painting cost itself is dominated by per-(tile,segment) row span
searches, which scale with samples per chunk — not by pixel writes (masking
out the ~7× fat-capsule overdraw changed nothing measurable).

Design landed:

1. `MaterializedCanvas` in-place revision protocol:
   `can_edit_in_place_revision` → `edit_resident_tile` /
   `materialize_uniform_as_raw` → `commit_in_place_revision(retained_keys)`,
   plus `invalidate_identity` as the universal per-tile recovery. Failure
   after validation is impossible by construction; a tile that cannot be
   updated is simply not retained and becomes correct overview fallback.
2. `append_incrementally_in_place`: validate-first sibling of the reference
   append. Affected resident raw tiles at every zoom are painted in place
   (segments prefiltered per tile by exact painter bounds, replayed
   newest-first through a 512-byte chunk mask — single tool+color makes that
   bit-identical to forward order); resident uniforms whose color equals the
   painted color (eraser over paper) are retained untouched; view uniforms
   are converted to raw; everything else is invalidated exactly like the
   reference path. Uniform conversions run before raw edits so slot eviction
   cannot cannibalize a tile edited in the same commit.
3. `invalidate_uniforms` walks a conservative per-zoom tile-index window
   (typically tens of candidates) instead of all 13,692 identities, with the
   exact intersection predicate unchanged inside the window.
4. Interactive chunk limit 64 → 48 samples (`kInteractiveChunkSampleLimit`,
   now shared between app and harness). Device sweep on the worst-case
   gesture: 64 → 14.9 ms max, 48 → 11.5 ms max (+7% total work),
   32 → 9.1 ms max (+42% total). 48 buys a 26% margin under the 15 ms slice
   without meaningfully growing total work; worst-case-session record count
   is 80,000/47 ≈ 1,702 of 4,000.
5. The product app no longer allocates the 56-tile staging workspace: live
   storage 5,301,792 → 4,842,144 bytes (−449 KiB); free PSRAM 3,502,096 with
   a 3,473,408-byte largest block (1.5 MiB export reserve untouched);
   product main-task stack margin 4,232 bytes. The reference path and its
   workspace remain harness-only for corpus construction and A/B evidence.

New permanent evidence: `TINYDRAW_GATE1_LONG_GESTURE` harness gate streams the
deterministic gesture through both commit implementations every run:

| Path | chunks | append total | append max | append avg | fallback px |
|---|---:|---:|---:|---:|---:|
| reference (64) | 26 | 687.9 ms | 33.2 ms | 26.5 ms | 0 |
| in-place (48) | 35 | 325.0 ms | **11.1 ms** | 9.3 ms | 0 |

Gate bound: every intermediate in-place commit under 15 ms, zero fallback
pixels in the stroke-region refresh (the "detail stays current" property),
balanced authority, all chunks committed. Compare the 4a1aada glass numbers:
`append_max_us=70214/72144` and 43 submits over 16 ms.

Host equivalence gates: dual-rig differential test (reference vs in-place,
8 randomized multi-chunk gestures, pen+eraser, tapered+constant, view compose
pixel-identical with zero fallback after every gesture, both equal to direct
forward replay; in-place additionally keeps resident non-view tiles exact),
eraser-over-paper uniform retention, atomic failure tests, and in-place
primitive contract tests. Full battery green: host tests, release, ASan/UBSan,
clang-tidy, cppcheck, format; census sweep `exact=1`; both fuzzers; complete
device harness `pass=1` including the cold gates and export reserve.

## Final clean 20-reset distribution (264b60e)

Source: [`264b60e-cold-p95-20-runs.log`](264b60e-cold-p95-20-runs.log),
captured reset-separated on the committed firmware, `pass=1` on all 180 gate
lines.

| Corpus | Zoom | 26a05f5 p95 | 264b60e p95 | delta |
|---|---:|---:|---:|---:|
| 4× adversarial tapered | 50% | 164,971 us | 157,977 us | −4.2% |
| 4× adversarial tapered | 100% | 243,956 us | 237,958 us | −2.5% |
| 4× adversarial tapered | 200% | 602,969 us | 597,977 us | −0.8% |
| **4× adversarial tapered** | **400%** | **1,451,905 us** | **674,901 us** | **−53.5%** |
| Overlapping XL | 50% | 541,331 us | 466,937 us | −13.7% |
| Overlapping XL | 100% | 405,954 us | 314,978 us | −22.4% |
| Overlapping XL | 200% | 415,975 us | 311,957 us | −25.0% |
| Overlapping XL | 400% | 415,966 us | 289,979 us | −30.3% |
| Seed-7 realistic | 400% | 342,970 us | 364,978 us | +6.4% |

The 400% adversarial maximum across the 20 runs was 675,889 us; the maximum
producer tick anywhere was 10,756 us (seed-7), well under the 15 ms alarm.
The product target — 4× adversarial 400% p95 below one second with bounded
ticks — is met with a 32.5% margin. The two-second alarm remains in place.

The seed-7 +6.4% regression is characterized above (producer restructure /
code layout, not summary bookkeeping; the same restructure buys −22 ms at
adversarial 200%). It remains 3× under its own historical alarm and is the
only negative delta in the matrix.

## Follow-up slice: export, deadline slicing, and the 384-slot pool

Three further changes landed the same day after the closure above; final
numbers below supersede the 264b60e table as the current distribution.

1. **PNG/USB export** (`88123ee`, `7302963`): while lifting the V1 export
   mechanisms, decode-validating the output caught a vendored pngenc path
   that silently corrupts streams (scanline-buffer overrun for worlds wider
   than 1104 plus a duplicated deflate loop that drops data); both affected
   Raster V1 exports of dense content too. Fixed with a decode round-trip
   host gate. The deflate workspace must live in internal RAM: in PSRAM the
   sound encoder path ran minutes-slow; internal, the full 1472x1792 world
   encodes in 5.7 s within 51 KiB internal + ~390 KiB PSRAM, all transient.
2. **Cold-fill deadline slicing** (`f20c201`): producer slices now run to a
   shared 2.5 ms deadline instead of one bounded step per tick, in the
   product loop and the paced gate. The worst single resumable step measures
   ~11.2 ms (seed-7 publication), so 2.5 ms holds the worst tick at 12.6 ms
   under the 15 ms alarm; a 6 ms deadline measured 15.2 ms and was rejected.
3. **384 raw slots** (`6abfa0f`): the 449 KiB freed by in-place commits funds
   64 more slots (104 spare beyond the five protected footprints). Cache
   retention passes at 384; the 1.5 MiB export reserve still allocates after
   an export encode; largest free PSRAM block 2,490,368 bytes in the harness
   build and 2,949,120 in the product build.

### Final clean 20-reset distribution (6abfa0f)

Source: [`6abfa0f-cold-p95-20-runs.log`](6abfa0f-cold-p95-20-runs.log); all
180 gate lines pass, maximum tick anywhere 12,640 us.

| Corpus | Zoom | 26a05f5 p95 | 6abfa0f p95 | delta |
|---|---:|---:|---:|---:|
| 4× adversarial tapered | 50% | 164,971 us | 160,973 us | −2.4% |
| 4× adversarial tapered | 100% | 243,956 us | 229,972 us | −5.7% |
| 4× adversarial tapered | 200% | 602,969 us | 538,972 us | −10.6% |
| **4× adversarial tapered** | **400%** | **1,451,905 us** | **646,305 us** | **−55.5%** |
| Overlapping XL | 50% | 541,331 us | 468,051 us | −13.5% |
| Overlapping XL | 100% | 405,954 us | 315,976 us | −22.2% |
| Overlapping XL | 200% | 415,975 us | 314,973 us | −24.3% |
| Overlapping XL | 400% | 415,966 us | 299,973 us | −27.9% |
| Seed-7 realistic | 400% | 342,970 us | 361,974 us | +5.5% |

The 400% target (p95 under one second) is met with a 35% margin. The overlap
corpus gave back 0.2–3.4% relative to the pre-384 measurement (larger slot
directory scans); it remains 13–28% below the closure baseline. Seed-7's
+5.5% is the Phase A producer-restructure/code-layout residue measured
earlier, unchanged by the later work.

## Residual risks and open items

1. The final human test is recorded in
   `PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md`. A 3,751-sample XL 400%
   gesture flowed continuously with a 13.3 ms worst append, confirming the
   intended improvement. The same session exposed a missing gate: warm-cache
   commits reached 120.1 ms at 25% and 131.8 ms at 100%. The deterministic
   400% result is therefore not a product-wide latency bound. Physical PNG/USB
   export also worked, but both the manual capture and existing export gates
   contain a CPU-0 task-watchdog warning during encoding.
2. Between accepting a sample mid-gesture and the next chunk boundary,
   resident tiles briefly contain painted-but-uncommitted chunk pixels. No
   composition path observes them in the product loop (fill and chrome are
   gated off while pressed; lift refresh happens after the boundary commit),
   and a boundary failure invalidates the footprint. A future concurrent
   consumer of the canvas would need to respect the in-place protocol notes
   in `materialized_canvas.h`.
3. Harness-build main-task stack margin dropped from 2,472 to 2,104 bytes
   (the new gate adds one nesting level); the product build measures 4,232
   bytes free. Worth rechecking if more harness gates are added.
4. Canvas-exit/re-entry phantom: code review of `clip_canvas_segment` and
   the coordinator identified a concrete mechanism, recorded here without a
   behavior change. When a live stroke dips into the chrome region, samples
   are dropped (exit segment clipped to the canvas bottom, fully-outside
   segments discarded, and the re-entry crossing segment also discarded
   because its `previous` point lies in chrome). The ink stream and builder
   stay active throughout, so the first fully-inside sample after re-entry
   connects by one straight segment from the clamped exit point — a
   "phantom" line hugging the toolbar across the horizontal distance the
   finger traveled while over chrome. This is deterministic, not a
   corruption; whether a gesture should bridge, split, or end at canvas exit
   is a product decision. The clipping math itself is correct (the
   `vertical_distance <= 0` branch is unreachable given the guards).

## Work log

- [x] Read roadmap, receipts, all listed sources.
- [x] Host census baseline captured (above).
- [x] Phase A: saturation summary + producer early-exit; host census + device
      paced gate before/after.
- [x] Phase B: in-place chunk commit + uniform range invalidation; host
      equivalence tests; device long-stroke measurement.
- [x] Full validation battery on final tree; 20-reset distribution; roadmap
      update.
