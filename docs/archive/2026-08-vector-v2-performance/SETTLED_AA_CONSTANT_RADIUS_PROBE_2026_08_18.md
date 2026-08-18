# Settled AA constant-radius predicate probe — 2026-08-18

## Verdict

**NO-GO; rejected without product changes.** An exact zero-memory special case
for constant-radius chords slowed the complete 25-case settled-AA corpus in
all seven alternating process pairs. It fails the required 5% all-zoom gain
and the 2% maximum heavy-corpus regression guard.

## Treatment

The existing raster loop computes each sample radius as
`first_radius + radius_delta * t`. The throwaway treatment returned
`first_radius` directly when `radius_delta == 0.0F`, retaining the existing
variable-radius expression otherwise. All distance, fringe, alpha, union,
composition, and work-accounting operations were unchanged. The treatment
added no state or workspace.

The existing `tinydraw_vector_v2_settled_aa_benchmark` supplied five corpora at
25%, 50%, 100%, 200%, and 400%. Each process result is the benchmark's median
of five timed whole-render passes after warmup. Seven process pairs alternated
baseline/treatment execution order.

## Whole-census A/B

Each row sums the 25 reported whole-viewport render medians in that pair.

| Pair | Baseline | Treatment | Change |
|---:|---:|---:|---:|
| 1 | 354.421 ms | 369.544 ms | +4.27% |
| 2 | 414.243 ms | 574.421 ms | +38.67% |
| 3 | 327.859 ms | 349.465 ms | +6.59% |
| 4 | 317.921 ms | 343.584 ms | +8.07% |
| 5 | 315.869 ms | 341.509 ms | +8.12% |
| 6 | 318.005 ms | 340.238 ms | +6.99% |
| 7 | 324.618 ms | 335.103 ms | +3.23% |

Every pair regressed. The paired regression median is 6.99%; the mean of the
per-case seven-run means is 338.991 → 379.123 ms (+11.84%).

| Zoom | Baseline corpus sum | Treatment corpus sum | Change |
|---:|---:|---:|---:|
| 25% | 68.398 ms | 84.796 ms | +23.97% |
| 50% | 72.375 ms | 78.413 ms | +8.34% |
| 100% | 70.134 ms | 74.463 ms | +6.17% |
| 200% | 69.512 ms | 78.640 ms | +13.13% |
| 400% | 58.571 ms | 62.812 ms | +7.24% |

Representative heavy regressions include hairline/eraser at 25% (+46.26%),
50% (+13.99%), and 200% (+11.02%), plus long-crossing at 100% (+9.23%), 200%
(+19.20%), and 400% (+14.39%). Dense 400% improved 1.11%, but the other dense
zooms ranged from +0.68% to +13.75%.

All 25 treatment RGB565 checksums matched baseline. The exactness and
zero-memory conditions pass; the performance conditions fail decisively. The
throwaway treatment and benchmark build products were not retained.
