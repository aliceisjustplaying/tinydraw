# F2 history may-ink rebuild — 2026-08-18

## Verdict

**ACCEPTED for completed history changes only.** Undo and Redo now rebuild the
1,288-byte tiled may-ink proof from the complete active pen authority after the
overview and authority transition commits. Branch replacement inherits the
rebuild performed by its required preceding Undo; the new branch operation then
marks its own conservative bounds normally. Ordinary pen/eraser absorption is
unchanged, so there is no per-chunk scan and no persistent-memory growth.

## Reproduction and gate

Build `tinydraw_vector_v2_occupancy_history_benchmark` with the host-release
preset and run it directly. It constructs overlap-erase, drifting-erase, Undo,
Redo, branch-replacement, and adversarial full-Undo documents. At 25%, 50%,
100%, 200%, and 400% it compares the current monotonic map with the existing
authority-derived `build_tiled_may_ink` result. At 50–400% it drives both
through the real `TileProducer`, checks composed pixels for exact equality, and
renders every raw-tile difference through settled AA. The six 25% rows record
map counts and the direct-overview bypass; they do not run tiled composition.

The predeclared GO gate required representative Undo/branch to remove at least
20% of producer scans or settled work at two tiled zooms, with the map rebuild
below 5% of avoided host work and no more than 1,288 bytes of caller scratch.
The result clears that gate at 100–400%.

## Occupancy recovery

| Corpus state | Current occupied cells | Active-authority cells | 25% optical cells |
|---|---:|---:|---:|
| Exact overlap erase, 300 pen/eraser pairs | 618 | 618 | 0 |
| Drifting eraser, 300 pens + 200 erasers | 1,469 | 506 | 357 |
| Undo 750 of 1,000 pens | 1,686 | 422 | 298 |
| Redo 500 after that Undo | 1,686 | 1,265 | 891 |
| Replace the Redo tail with 100 new pens | 1,843 | 585 | 416 |
| Adversarial Undo of all 1,000 pens | 5,735 | 0 | 0 |

The optical column is a lower bound only. A blank 25% overview cannot prove a
high-zoom hairline absent, so the accepted rebuild uses active pen bounds. This
explains the overlap-erase limit: output is paper, but the still-active pen
authority requires all 618 cells to remain may-ink. Drifting eraser-only bounds
are safely removed because erasers cannot introduce ink.

## Producer A/B

Median host production time from five alternating runs:

| Corpus | 100% | 200% | 400% |
|---|---:|---:|---:|
| Drifting erase | 201.958 → 170.667 us (-15.5%) | 78.667 → 54.875 us (-30.2%) | 46.125 → 23.333 us (-49.4%) |
| Undo | 200.750 → 136.166 us (-32.2%) | 109.708 → 46.459 us (-57.7%) | 43.875 → 24.208 us (-44.8%) |
| Redo | 219.000 → 219.500 us (+0.2%) | 118.417 → 93.166 us (-21.3%) | 45.416 → 35.500 us (-21.8%) |
| Branch replacement | 203.042 → 162.541 us (-19.9%) | 110.875 → 56.208 us (-49.3%) | 46.458 → 27.291 us (-41.3%) |
| Adversarial full Undo | 170.209 → 13.917 us (-91.8%) | 126.917 → 9.000 us (-92.9%) | 93.083 → 11.625 us (-87.5%) |

Undo scans fall from 90 to 44 at 200% and 26 to 10 at 400%; branch scans fall
from 126 to 84 and 38 to 14. The authority rebuild costs 1.125 us for Undo,
1.542 us for branch replacement, and 3.375 us for the 750-operation Redo. At
50%, representative scan/raster counters are unchanged because one coarse tile
still intersects active ink; 25% uses the overview directly and never consults
the tiled occupancy map.

False-positive immediate replay adds **zero settled tiles and zero settled
work** in all cases. Tile payload analysis recognizes the replayed paper and
publishes a uniform, which the background settle pass already skips. F2
therefore improves cold/immediate repair only; it does not address the settled
AA latency bound.

## Accepted scope and correctness

`MaterializedCanvas::replace_tiled_may_ink` validates the current revision,
exact map size, and external non-aliasing storage before copying only the map.
History reuses the first 1,288 bytes of its existing caller-owned overview
scratch after commit. A short scratch span simply retains the conservative map;
correctness never depends on the optimization. A focused integration test
completes Undo with 256 bytes of scratch and confirms the conservative bit is
retained.

- Host ASan authority: 82/82 tests, 25,650 assertions.
- Host ASan rendering: 126/126 tests, 63,098 assertions.
- Focused may-ink coverage: 7/7 tests, 10,381 assertions.
- Benchmark: all 24 tiled map/view cases compose exact pixels. The six 25%
  cases record the direct-overview bypass and map counts; no product state or
  persistent workspace was added.
