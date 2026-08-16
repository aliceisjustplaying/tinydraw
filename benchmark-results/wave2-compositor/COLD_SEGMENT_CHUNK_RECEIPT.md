# Cold segment-chunk experiment — removed

Date: 2026-08-16

Corpus: frozen `adversarial_tapered_4x`, 400%, origin `(0,0)`

Candidate: exact conservative bounds for four curve endpoints (eight quadratic
segments), stored in caller-funded PSRAM

The candidate preserved pixel equality and reduced per-segment bounding-box
rejections from 59,101 to 46,022. It considered 8,795 chunks, rejected 1,774,
and skipped 6,354 curve endpoints. Worst-case metadata was 200,002 bytes.

| Measurement | Baseline | Candidate | Change |
|---|---:|---:|---:|
| Host debug median, five runs | 52.745 ms | 49.257 ms | -6.6% |
| Device exact compute | 961.073 ms | 865.461 ms | -9.9% |
| Device wall | 1,056.871 ms | 966.609 ms | -8.5% |

The original comparison to the 577.667 ms straight-authority receipt was
invalid: curved authority had already changed the product replay cost. The
correct current curved-authority baseline is the median of three unindexed
device runs.

The candidate improved the old tapered-only corpus, but missed the campaign's
15% trajectory and permanently consumed 200,002 bytes from roughly 306 KiB of
post-export-reserve slack. It was removed. The later combined tapered + evil
hairline corpus was not measured with this index, so this receipt makes no
claim about its combined-corpus effect.
