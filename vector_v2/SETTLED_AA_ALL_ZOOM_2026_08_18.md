# Settled AA all-zoom local-span receipt — 2026-08-18

## Verdict

**HOST GO; PHYSICAL NOT ACCEPTED.** Settled analytic AA now clears and
composites only the alpha spans touched by the current operation. Exact output,
painter order, self-overlap union, erasers, resumability, and the existing
settle/export workspace are unchanged. The host treatment is a measured exact
optimization, but the first product capture did not meet the whole-view
latency target at 50–400%.

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

## Gates

- Rendering ASan: 125/125 tests, 63,090 assertions.
- Authority/export ASan: 79/79 tests, 25,609 assertions.
- Benchmark under ASan: all 25 frozen pixel oracles pass.
- Focused sliced rendering: 8/8 tests, 197 assertions.
- Focused world export: 3/3 tests, 83 assertions.

## Initial product result

The normal-product captures used revision 156 and the centered zoom sequence.
The telemetry does not confirm the presumed 109-operation document identity.
They recorded:

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

During subsequent 400% panning, the owner saw transient blue dots. This
receipt records the observation only; the capture does not identify its cause
or connect it to the local-span treatment.

Physical status remains yellow. Acceptance needs a same-tree device A/B, or a
further treatment that brings the measured 50–400% whole-view latency within
the requested bound while preserving the green slice and correctness results.
