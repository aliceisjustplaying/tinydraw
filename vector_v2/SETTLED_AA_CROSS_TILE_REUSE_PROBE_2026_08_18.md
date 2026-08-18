# Settled AA cross-tile reuse probe — 2026-08-18

## Verdict

**NO-GO.** One viewport-wide spatial query can reuse the existing 2,000-byte
candidate buffer with no additional memory, but it is not a material whole-view
win. The useful 400% cases save only 0.29–0.44% of measured whole-render host
time, while a distributed 50% viewport regresses the preparation phase by more
than 3x. No production code changed.

## Method

A throwaway host probe replayed the settled renderer's exact query, newest-first
operation scan, bounds rejection, and `prepare_incremental_curve_unit` work over
42 aligned or 56 maximally crossed adjacent tiles at 50%, 100%, 200%, and 400%.
It used the five frozen `settled_aa_benchmark` corpora. The treatment queried the
viewport union once into the existing candidate span and reused that superset
for each tile. If the union query failed the renderer's 25% usefulness gate, it
ran the unchanged per-tile path. Every tile retained the exact operation order,
intersection set, prepared-unit checksum, and fallback decision.

Each entry below is the median of three process runs. Within a process, timing
is the median of 11 rounds of 50 iterations.

| Corpus / zoom | Tiles | Per-tile query | Shared query | Change | Scans before → after |
|---|---:|---:|---:|---:|---:|
| Distributed 50% | 42 | 20.439 us | 65.880 us | +222.3% | 2,367 → 18,522 |
| Distributed 50% | 56 | 27.701 us | 105.490 us | +280.8% | 3,196 → 30,688 |
| Distributed 400% | 42 | 4.133 us | 3.804 us | -8.0% | 440 → 1,050 |
| Distributed 400% | 56 | 5.317 us | 5.082 us | -4.4% | 555 → 1,400 |
| Sparse 400% | 42 | 8.787 us | 7.702 us | -12.3% | 1,086 → 1,680 |
| Sparse 400% | 56 | 10.213 us | 9.481 us | -7.2% | 1,232 → 2,240 |

The long-crossing, hairline/eraser, and dense union queries failed the usefulness
gate at every zoom, so the adaptive treatment selected the exact existing path.
All 40 corpus/zoom/tile-count comparisons passed the exactness oracle. The
candidate-batching treatment adds zero bytes.

## Whole-render attribution

The matching frozen 42-window benchmark measures 74 us for distributed 400%
and 373 us for sparse 400%. The query/preparation savings are 0.329 us and
1.085 us respectively: 0.44% and 0.29% of whole rendering. At distributed 50%,
the added 45.441 us is 16.5% of its 276 us whole render. This treatment misses
the 10% representative threshold and carries a material low-zoom regression.

## Curve preparation reuse

Curve preparation repeats heavily across adjacent tiles, but retaining every
unique prepared unit does not fit the current workspace. A `PreparedCurveUnit`
is 120 bytes before identity/validity metadata:

| Corpus / zoom | Tiles | Preparations | Unique operations | Cache lower bound |
|---|---:|---:|---:|---:|
| Long crossing 50% | 56 | 6,370 | 256 | 30,720 bytes |
| Hairline + eraser 400% | 56 | 6,726 | 600 | 72,000 bytes |
| Dense 400% | 56 | 4,905 | 1,000 | 120,000 bytes |

The renderer owns one tile's five accumulation planes, so operation-major
rendering across several tiles would also require several tile workspaces.
Cross-tile curve reuse therefore needs substantial new memory or a deeper
render-order redesign. Neither is justified by this probe. The throwaway probe
was removed after capture.
