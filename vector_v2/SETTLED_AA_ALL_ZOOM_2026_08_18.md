# Settled AA all-zoom local-span receipt — 2026-08-18

## Verdict

**LOCAL SPANS HOST GO; SETTLED-TILE IRAM PHYSICAL GO; SATURATED SKIP PHYSICAL
NO-GO AND REVERTED.** Settled analytic AA retains the exact local-span treatment
that clears and composites only alpha touched by the current operation. Its
settled-tile implementation is retained in internal RAM after a same-revision
device A/B improved every zoom by 6.5–7.4%. A follow-up that omitted raster math
behind fully opaque newer pixels passed the host gate but did not produce a
persuasive device gain or meet the 500 ms whole-view target at 50–400%; its
production, diagnostic, benchmark, and test changes were removed.

## Reproduction

Build `tinydraw_vector_v2_settled_aa_benchmark` with the host-release preset and
run it directly. The benchmark renders a 368×448 viewport as 42 resumable
windows at 25%, 50%, 100%, 200%, and 400% for five deterministic corpora:
distributed short strokes, 256 long crossing strokes, 400 hairlines plus 200
erasers, a sparse 1,000-operation document, and a dense 1,000-operation
document. Each of its 25 cases checks a frozen whole-viewport RGB565 checksum.

The A/B baseline is `60ec561`. Each timing below is the mean of three
alternating process runs; each process reports the median of five timed renders
after one warmup. Timings are host evidence and exclude publication copies.

## A/B by zoom

| Zoom | Baseline mean per corpus | Local spans | Change |
|---:|---:|---:|---:|
| 25% | 15.541 ms | 12.810 ms | -17.6% |
| 50% | 15.965 ms | 13.892 ms | -13.0% |
| 100% | 15.785 ms | 13.603 ms | -13.8% |
| 200% | 15.671 ms | 13.615 ms | -13.1% |
| 400% | 12.594 ms | 11.247 ms | -10.7% |

## A/B by corpus

| Corpus | Baseline mean across zooms | Local spans | Change |
|---|---:|---:|---:|
| Distributed | 0.624 ms | 0.233 ms | -62.7% |
| Long crossing | 33.379 ms | 30.587 ms | -8.4% |
| Hairline + eraser | 34.758 ms | 29.829 ms | -14.2% |
| Sparse | 0.560 ms | 0.212 ms | -62.2% |
| Dense | 6.235 ms | 4.308 ms | -30.9% |

All 25 treatment checksums equal the frozen baseline. Across one complete
25-case census, alpha clear work falls from 249,284,608 to 7,830,946 pixels
(-96.9%) and composite work from 140,571,648 to 8,018,084 pixels (-94.3%).
Initialization and final fold remain one pass over the exact 164,864 viewport
pixels per case. Publication is an unchanged row copy and is not part of the
treatment.

## Attribution

The operation-local corpora were dominated by clearing and compositing full
windows for small strokes. Two byte-sized x bounds per tile row plus a y range
make those passes proportional to touched coverage. The cursor owns the bounds;
the five existing 4,096-pixel workspace planes and candidate buffer do not
grow, and slice progress remains caller-owned and allocation-free.

Long crossing and hairline cases remain raster-bound, so their gains are
smaller. Spatial discovery is already effective for distributed and sparse
cases; dense and 25% long-crossing queries correctly retain the authority scan
when the index cannot reject enough candidates. No cross-window cache or band
workspace was added.

## Raster phase follow-up

Host-only timers attributed the local-span renderer before the follow-up
treatment. Raster math consumed 87.3–91.1% of measured phase time at every
zoom. Mean discovery (`query + scan`) was 0.124–0.184 ms, clear was
0.194–0.297 ms, prepare was 0.094–0.126 ms, composite was 0.695–0.892 ms,
fold was 0.048–0.050 ms, and publication was 0.021–0.029 ms. Raster was the
only remaining phase large enough to move the product result materially.

Newest-first compositing makes coverage math unnecessary when accumulated
alpha is already 255: every older contribution is exactly zero. The experiment
used the existing accumulated-alpha plane and cursor saturation count. At 50%
window saturation it switched to a raster loop that skipped those pixels;
below the threshold the original loop remained intact. It added no product
state or workspace and retained the original conservative raster work charge.

The follow-up A/B used five alternating baseline/treatment process pairs; each
process result is already a median of five renders after warmup. The table
reports the median process result summed across all five corpora:

| Zoom | Full raster | Saturated skip | Change |
|---:|---:|---:|---:|
| 25% | 98.254 ms | 98.442 ms | +0.2% |
| 50% | 102.207 ms | 96.434 ms | -5.6% |
| 100% | 102.164 ms | 78.605 ms | -23.1% |
| 200% | 103.174 ms | 59.827 ms | -42.0% |
| 400% | 84.936 ms | 40.181 ms | -52.7% |

All 25 frozen RGB565 checksums remain exact. At 50%, long crossing improved
11.4%, dense improved 2.1%, and hairline/eraser changed by +0.4%. At
100–400%, every long-crossing, hairline/eraser, and dense case improved by
10.8–54.7%. The 25% heavy-corpus range was -0.5% to +0.8%. Sub-millisecond
distributed and sparse cases remained within measurement noise. This cleared
the host gate: exact pixels, no product memory growth, at least 10% in every
100–400% heavy case, and no material heavy-corpus regression. The physical
result below overruled that host-only result, and the experiment was reverted.

## Gates

- Rendering ASan: 125/125 tests, 63,090 assertions.
- Authority/export ASan: 79/79 tests, 25,609 assertions.
- Benchmark under ASan: all 25 frozen pixel oracles pass.
- Focused sliced rendering: 8/8 tests, 197 assertions.
- Focused world export: 3/3 tests, 83 assertions.

## Initial product result

The normal-product captures used revision 156 and the centered zoom sequence.
They include local spans but predate the saturated-destination treatment. The
telemetry does not confirm the presumed 109-operation document identity. They
recorded:

| Zoom | Origin | Tiles | Slices | Total | Maximum slice | Work | Failures |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 25% | overview | 42 | 1,607 | 396.111 ms | 2.308 ms | 806,067 | 0 |
| 50% | 184,262 | 47 | 2,494 | 603.894 ms | 2.341 ms | 1,253,784 | 0 |
| 100% | 552,710 | 51 | 3,895 | 902.751 ms | 2.133 ms | 1,957,497 | 0 |
| 200% | 1288,1606 | 48 | 4,540 | 1,026.000 ms | 2.226 ms | 2,281,122 | 0 |
| 400% | 2760,3398 | 48 | 3,584 | 788.944 ms | 2.065 ms | 1,792,223 | 0 |

Every captured slice stayed well below the 15 ms interaction guard, and every
render completed with zero transient and permanent failures. The 25% result
passes the 500 ms bound at 396.111 ms; 50–400% all exceed it. The review’s
older 152.945 ms 25% result is not like-for-like because this capture only
confirms revision 156, not the same document identity. There is no same-tree
physical full-alpha baseline, so these captures do not establish the device
speedup attributable to local spans.

## Saturated-skip product result

The follow-up cold capture used revision 160:

| Zoom | Tiles | Slices | Total | Maximum slice | Work | Failures |
|---:|---:|---:|---:|---:|---:|---:|
| 25% | 42 | 1,650 | 418.266 ms | 2.263 ms | 828,914 | 0 |
| 50% | 47 | 2,502 | 624.740 ms | 2.309 ms | 1,257,852 | 0 |
| 100% | 51 | 3,863 | 915.430 ms | 2.119 ms | 1,941,287 | 0 |
| 200% | 48 | 4,399 | 1,003.380 ms | 2.095 ms | 2,209,606 | 0 |
| 400% | 48 | 3,388 | 735.274 ms | 2.097 ms | 1,693,172 | 0 |

Every slice remained below 15 ms and every render completed without transient
or permanent failures. The 50–400% totals remained above 500 ms. Revision 160
is not like-for-like with revision 156; its raw totals were slower at 25–100%
and only 2.2%/6.8% lower at 200%/400%, far below the host projection. The run
therefore establishes no persuasive device win. The saturated-skip treatment
is physically rejected and fully reverted; local spans remain.

## Settled-tile IRAM product A/B

A cold same-revision-162 A/B mapped the settled-tile implementation into
internal RAM. Work was identical and every render completed with zero failures:

| Zoom | Flash text | Internal RAM | Change |
|---:|---:|---:|---:|
| 25% | 407.426 ms | 377.439 ms | -7.36% |
| 50% | 606.535 ms | 561.363 ms | -7.45% |
| 100% | 903.449 ms | 840.370 ms | -6.98% |
| 200% | 1,027.491 ms | 958.249 ms | -6.74% |
| 400% | 790.251 ms | 738.756 ms | -6.52% |

Maximum slices improved or remained in the 1.97–2.35 ms range. The accepted
mapping costs 4,528 bytes of text and 4,608 bytes of internal heap. It is
retained because the physical gain is consistent at all five zooms with exact
work and failure counters. The 25% treatment total passes 500 ms; 50–400%
remain above the whole-view bound, so settled AA remains yellow.

The owner first saw transient blue dots during later 400% panning, then
reproduced one during a 50→100% zoom while cache/refinement was incomplete. It
moved or disappeared as rendering progressed and did not recur on the immediate
repeat. This rules out a pan-only trigger but does not identify a cause or tie
the artifact to either F13 treatment.

Physical optimization status is green for the retained IRAM mapping. Overall
settled AA remains yellow because the exact renderer still exceeds the
whole-view target at 50–400%.
