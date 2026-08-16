# Cold exact-publication batching experiment — rejected

Date: 2026-08-16

Corpus: tapered adversarial only, curved authority, 400%, origin `(0,0)`

The first candidate batched arbitrary groups in threes. It resent 286,720
pixels, raised presentation to 190.880 ms, produced a 68.701 ms interaction
tick, and raised wall to 1,163.443 ms. It was rejected immediately.

The second candidate batched only a gap-free rectangular prefix. It preserved
the first exact publication and resent zero pixels.

| Measurement | Unbatched median | Candidate | Change |
|---|---:|---:|---:|
| Exact compute | 961.073 ms | 943.247 ms | -1.9% |
| Presentation | 70.884 ms | 69.497 ms | -2.0% |
| Wall | 1,056.871 ms | 1,039.960 ms | -1.6% |
| Publications | 12 | 10 | -2 |

The tiny compute movement is run variance; batching does not change raster
work. Presentation stayed near 69–71 ms because most groups were not adjacent.
At lower zooms, larger batches produced interaction ticks around 22 ms and
violated the 15 ms limit. The batching code was removed.

This experiment predates the combined tapered + evil hairline corpus. Its
failure mode is independent of corpus size: the visible groups do not form
enough adjacent rectangles to remove meaningful panel work.
