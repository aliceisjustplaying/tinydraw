# Settled-AA + arc-length resampling — host prototype receipts (2026-08-16)

Tool: `tinydraw_vector_v2_settled_aa_prototype` (host-release). Author
decisions #3/#4: prototype approved; committed authority geometry frozen.

Pipeline is the production one: recorded trace → `InkStream` → (optional
arc-length resampling, review §9.4, inserted exactly where the review puts
it) → product 32-sample chunking → `prepare_incremental_curve_unit` chords.
The baseline renders through `apply_incremental_operation` (the exact
production rasterizer). The AA render is the analytic reference the device
boundary-pixel pass will approximate: per-operation UNION tapered-capsule
coverage (self-overlap never darkens), front-to-back newest-first
compositing across operations, erasers as opaque white.

**Frozen RGB565 blend model (proposal, author review pending):** RGB565
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
real design choice; the renders let the author judge the smoothness delta
per spacing before any constant freezes.

## Addendum — why Raster V1 is not jagged and Vector V2 is (author question)

Author observation: AA looks nice but does not fix the optical jaggedness,
and Raster V1 has no such jaggedness. Answer, with receipts:

1. **V1's historical jaggedness fix was shape smoothing** — commit
   `d7ec88f` ("smooth sparse hardware strokes") introduced
   `CurvedRibbonStream`'s midpoint quadratics. V2 inherited exactly that
   model, live and committed. Smoothing is not the difference.
2. **The difference is what gets rendered.** V1 rasterizes float geometry
   directly at screen resolution (plus 4×4 coverage AA). V2's committed
   authority quantizes sample centers to quarter-world units
   (`operation_builder.cpp:122`), and at 400% zoom a quarter-world unit is
   **one full screen pixel** (0.5 px at 200%). Slow, careful strokes emit
   chords about as long as the quantization step, so the stored centerline
   itself zigzags — the angularity baseline's joint_max=90° at 400% is
   this mechanism. Hard edges amplify it; AA renders the zigzag smoothly
   but faithfully.
3. **Four-quadrant proof** (`slow-precise-400*-x4.png`):
   {quantized, float-reference} × {raw, resampled-2px} through the same AA
   compositor. The float reference (the V1-equivalent path,
   `-ref-aa-x4.png`) shows a cleaner centerline than the committed render
   at equal smoothing; resampling cleans the jitter component; the
   combination (float + resampled) is the cleanest — the V1 look.

**Fix candidate (author decision needed): sixteenth-world sample units.**
Max coordinate 1472×16 = 23,552 fits the existing `uint16` — zero storage
cost — and gives 0.25 px centerline resolution at 400%. It is an
authority-format change: per the dependency matrix it reopens cold
exactness, SVG parity, and the frozen corpus statistics (same reopen class
as the declined Stage C, but with a verified, visible justification).
Author has already ruled backwards compatibility out of scope.

## Reproduce

```sh
cmake --build --preset host-release --target tinydraw_vector_v2_settled_aa_prototype
./out/build/host-release/vector_v2/tinydraw_vector_v2_settled_aa_prototype \
  testdata/ink-traces/fast-curve-400.csv --zoom 400 --size 5 [--resample 2] --out prefix
```
