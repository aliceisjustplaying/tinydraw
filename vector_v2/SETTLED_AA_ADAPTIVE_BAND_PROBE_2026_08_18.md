# Settled AA adaptive band/window probe — 2026-08-18

## Verdict

**NO-GO.** Full-width 11-row windows produced a narrow 2.64–3.60% win for the
long-crossing corpus at 25–100%, then regressed the same corpus by 11.94% at
200% and 57.61% at 400%. A hybrid that retained the first 64-row tile slab as
both useful rendering and its grouping signal recovered only 2.23–3.37% in the
three winning cases and still recorded three representative regressions over
1%. No production code changed.

## Treatment and exactness

The host probe used the accepted local-alpha-span renderer math and compared
the current 64x64 window grid with full-width 368x8 and 368x11 windows. The
11-row form holds 4,048 pixels, so it fits the existing 4,096-pixel planes. Its
two `uint16_t[11]` row-span arrays need 44 bytes; they can share storage with
the current two `uint8_t[64]` arrays, which need 128 bytes. The treatment
therefore requires zero persistent bytes and no workspace growth.

The adaptive form rendered the first 64-row slab as six ordinary tiles. When
that useful work reported more than 1.6 intersecting-operation visits per
authority operation, it rendered the remaining rows as 11-row full-width
bands, but only at 25–100%. Otherwise it completed the existing tiled grid.
This selects bands only for the constructed long-crossing corpus and avoids
their known high-zoom reversal without a preflight query.

All tile, 8-row, 11-row, and adaptive outputs matched the 25 frozen viewport
checksums across distributed, sparse, dense, long-crossing, and
hairline-plus-eraser corpora at 25%, 50%, 100%, 200%, and 400%. Results are
medians of seven rotated-order host-release measurements. Lower is better.

## Fixed 11-row A/B

| Corpus | 25% | 50% | 100% | 200% | 400% |
|---|---:|---:|---:|---:|---:|
| Distributed | +3.83% | +6.58% | +5.54% | +4.81% | -0.46% |
| Long crossing | -2.64% | -3.60% | -3.32% | +11.94% | +57.61% |
| Hairline + eraser | -1.61% | -0.62% | -0.70% | -1.42% | +17.16% |
| Sparse | +18.64% | +2.37% | +1.46% | +2.35% | +2.52% |
| Dense | +0.81% | -3.94% | -2.81% | +11.37% | +83.09% |

The 8-row form had the same decision shape and did not improve the worst cases:
at 400% it regressed long crossing by 60.92%, hairline plus eraser by 18.21%,
and dense by 82.16%.

## Adaptive result

The hybrid selected bands for long crossing at 25%, 50%, and 100%. It reduced
wall time by 2.48%, 2.23%, and 3.37%, with query count falling from 42 to 41
and intersecting-operation visits falling by 20.7–21.3%. Raster coverage still
dominated the renderer, so the lower replay duplication did not become a
material wall-time gain.

All other cases selected the tiled path. Distributed 25%, hairline plus eraser
100%, and sparse 25% nevertheless measured +2.91%, +1.22%, and +3.52% versus
their paired tiled baselines, breaching the zero-representative-regression
gate. The adaptive control flow is not justified by its sub-4% niche gain. The
disposable probe was removed after capture.
