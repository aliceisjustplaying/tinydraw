# F13 settled-AA loop-alignment probe — 2026-08-18

## Question and gate

Could 32-byte loop alignment in `settled_tile.cpp` improve whole-view settled
rendering by at least 5% at every zoom, with frozen RGB565 output and no heavy
corpus regression above 2%? The treatment changed generated code only, added
no persistent memory, and left all rendering semantics unchanged.

## Code generation and attribution

The final-HEAD baseline already lowers operation-alpha clearing to the platform
`memset`; the final RGB565 fold auto-vectorizes at width 16. The branchy
operation composite remains scalar and the compiler reports a missed
vectorization. A line-resolved host sample attributed the identified render
loop samples as follows:

| Loop | Samples | Share of identified loop samples |
|---|---:|---:|
| Initialize | 6 | 0.9% |
| Operation clear | 10 | 1.5% |
| Coverage raster | 624 | 93.0% |
| Operation composite | 28 | 4.2% |
| Final fold | 3 | 0.4% |

Compiling only `settled_tile.cpp` with 32-byte loop alignment left those
vectorization decisions unchanged and increased object text by 40 bytes.

## Exact A/B

The existing five-corpus, five-zoom settled-AA benchmark ran seven interleaved
baseline/treatment pairs. Each result retained the benchmark's warmup plus five
timed renders, and every render matched its frozen checksum oracle across all
25 corpus/zoom combinations.

| Zoom | Baseline aggregate | Treatment aggregate | Improvement |
|---:|---:|---:|---:|
| 25% | 62.931 ms | 63.540 ms | -0.97% |
| 50% | 67.035 ms | 66.662 ms | +0.56% |
| 100% | 68.299 ms | 67.035 ms | +1.85% |
| 200% | 69.043 ms | 67.445 ms | +2.31% |
| 400% | 55.600 ms | 56.243 ms | -1.16% |

Heavy-corpus regressions exceeded the guard: hairline/eraser at 25% regressed
2.71%; dense at 50%, 100%, and 400% regressed 3.52%, 2.98%, and 3.79%.

## Verdict

**NO-GO.** The treatment missed the 5% threshold at every zoom, regressed two
zoom aggregates, and violated the heavy-regression guard. The probe code and
artifacts were removed; no product, test, build-system, or memory-shape change
was retained.
