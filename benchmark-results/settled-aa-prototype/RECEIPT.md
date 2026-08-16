# Settled-AA + arc-length resampling — host prototype receipts (2026-08-16)

Tool: `tinydraw_vector_v2_settled_aa_prototype` (host-release). Owner
decisions #3/#4: prototype approved; committed authority geometry frozen.

Pipeline is the production one: recorded trace → `InkStream` → (optional
arc-length resampling, review §9.4, inserted exactly where the review puts
it) → product 32-sample chunking → `prepare_incremental_curve_unit` chords.
The baseline renders through `apply_incremental_operation` (the exact
production rasterizer). The AA render is the analytic reference the device
boundary-pixel pass will approximate: per-operation UNION tapered-capsule
coverage (self-overlap never darkens), front-to-back newest-first
compositing across operations, erasers as opaque white.

**Frozen RGB565 blend model (proposal, owner review pending):** RGB565
expands to 8-bit by bit replication, compositing accumulates in float,
one final round to RGB565 over white.

## Renders (before/afters, ×4 crops for pixel-level judgment)

| Set | Files |
|---|---|
| Hairline fast curve, 400% | `fast-curve-400-{baseline,aa}[-x4].png` |
| + resample 2 px | `fast-curve-400-rs2-*` |
| + resample 4 px | `fast-curve-400-rs4-*` |
| Slow precise, 400% (±rs2) | `slow-precise-400*` |
| XL brush (20 px), 400% | `xl-400-*` |
| Scribble multistroke, 100% | `scribble-100-*` |

Reviewer visual check (agent): AA edges smooth at every brush size, no
double-darkening at self-crossings (union verified visually at the X
crossing in `fast-curve-400-aa-x4.png`), fat-brush staircase aliasing
eliminated in `xl-400-aa-x4.png`.

## Cost-model numbers (device-relevant)

`boundary_share` = partially-covered pixels / all covered pixels — the
population that needs analytic coverage on device (interiors keep today's
exact span fills):

| Content | interior px | boundary px | share |
|---|---|---|---|
| Hairline 400% (5 px brush) | 1,453 | 2,088 | 0.59 |
| Slow precise 400% | 964 | 855 | 0.47 |
| Scribble 100% | 4,243 | 5,508 | 0.57 |
| XL 400% (20 px brush) | 47,944 | 7,165 | **0.13** |

Hairlines are boundary-dominated (2 px radius → almost every covered pixel
is near an edge); fat strokes are interior-dominated. The review's
~100–150K boundary-pixel full-viewport estimate is consistent with these
densities.

## Resampling sample-count effect (the review's warned tradeoff)

| Spacing | fast-curve-400 ops / samples | slow-precise ops / samples |
|---|---|---|
| off | 13 / 397 | 18 / 575 |
| 2 px | 16 / 495 (+25% samples) | 6 / 192 (−67%) |
| 4 px | 8 / 248 (−37%) | — |

2 px spacing *adds* samples on fast strokes (gap filling) while slashing
slow-stroke oversampling; 4 px shrinks both. The spacing constant is a
real design choice; the renders let the owner judge the smoothness delta
per spacing before any constant freezes.

## Reproduce

```sh
cmake --build --preset host-release --target tinydraw_vector_v2_settled_aa_prototype
./out/build/host-release/vector_v2/tinydraw_vector_v2_settled_aa_prototype \
  testdata/ink-traces/fast-curve-400.csv --zoom 400 --size 5 [--resample 2] --out prefix
```
