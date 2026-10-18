# Real-touch characterization

Date: 2026-08-13

This document contains aggregate results only. The user's raw drawing coordinates
remain untracked under `out/captures/private/2026-08-13-real-touch/`.

## Capture

An exclusive, temporary firmware captured raw CST820 touch paths for two minutes
while drawing simple immediate feedback. The firmware and its build wiring were
discarded after capture rather than retained as a second application.

- 70 strokes
- 1,189 source points
- 16.99 points per stroke on average
- 6 points at the median
- 19 points at the 90th percentile
- 64 points at the 95th percentile
- 216-point maximum stroke
- zero rejected strokes
- zero touch-read errors
- zero display-window rejects
- five strokes at least 1,200 world units wide
- five strokes at least 1,500 world units tall

The current controller polling and duplicate-coordinate removal produced about
67,942 source points when linearly projected to the provisional 4,000-operation
limit, or 84.9% of the 80,000-point source capacity. This is useful sizing
pressure, not a representative full-document receipt: the capture deliberately
included many taps and several long test strokes, contained no eraser operations,
and covered only 16.9 seconds of contact during a two-minute session.

## Timing

The temporary immediate path measured 329 microseconds average and 338
microseconds maximum from a first touch read to display submission. Its reported
transfer-completion values were not valid physical latency measurements because
the existing 64-entry completion history was queried only after each complete
stroke and could already have wrapped. They are excluded from evidence.

True touch-to-photon latency still needs a camera or optical sensor. Product-path
first-feedback timing also remains open because this capture rendered a small
fixed-width segment directly rather than running the complete production append
and display scheduler.

## LOD sizing result

The private paths were characterized with the same strict, balanced, and loose
screen-space policies used by `LOD_CHARACTERIZATION.md`. Raw paths and paths
processed through the raster app's existing medium-brush `InkStream` were both
checked. The capture used fixed input radius, so the raw path is evidence for
centerline density but not representative pressure/radius behavior.

| Input | Policy | Four-zoom points | Projected at 4,000 strokes | 90k capacity |
|---|---|---:|---:|---:|
| raw touch | strict | 4,135 | 236,285 | 262.5% |
| raw touch | balanced | 4,038 | 230,742 | 256.4% |
| raw touch | loose | 3,948 | 225,600 | 250.7% |
| raster-style shaped | strict | 4,446 | 254,057 | 282.3% |
| raster-style shaped | balanced | 4,283 | 244,742 | 271.9% |
| raster-style shaped | loose | 4,171 | 238,342 | 264.8% |

The four independent zoom copies retain too much high-zoom data for the
provisional 90,000-point slab. This capture therefore rejects the current
capacity/model combination; it does **not** justify choosing the loose policy or
simply increasing the allocation.

## Decision and next experiment

Do not approve a production simplification policy yet. First replace the
assumption that every operation owns four independent, nearly complete sample
copies. Compare at least:

1. nested/shared detail where finer zooms add points to coarser geometry;
2. one canonical shaped centerline with bounded on-demand simplification; and
3. fewer stored zoom levels with intermediate generation when needed.

Then render representative captured curves at 50–400% and compare both measured
screen-space error and visual character. Any checked-in visual fixture must be
synthetic or explicitly approved; the private raw drawing must not be committed.
