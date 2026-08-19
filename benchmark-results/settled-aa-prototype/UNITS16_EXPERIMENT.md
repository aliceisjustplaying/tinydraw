# Sixteenth-world sample units — experiment receipt (2026-08-16)

Author: "let's try it and see if it causes regressions and if yes how much."
Change: `kSampleUnitsPerWorldUnit` 4 → 16 (`operation.h`); every
encode/decode site converted through the constant or geometry-preserving
×4 on generators/fixtures (field names unchanged pending experiment
verdict). Centerline resolution at 400%: 1.0 px → 0.25 px. Storage cost:
zero (23,552 / 28,672 max coordinates fit the existing uint16).

## Wins (measured)

Angularity at 400% (recorded recorded traces, host tool):

| trace | joint_p95 | joint_max | joints >20° |
|---|---|---|---|
| fast-curve-400 | 22.8° → **14.0°** | 90.0° → **53.1°** | 69 → **27** |
| slow-precise-100 | 53.1° → **36.9°** | 90.0° | 141 → 129 |

Optical: `slow-precise-400-units16-aa-x4.png` now matches the
float-geometry reference (`slow-precise-400-ref-aa-x4.png`) — the
thickness pulsing and centerline wobble of the quarter-unit render
(`slow-precise-400-aa-x4.png`) are gone. The residual joint tail is input
jitter (arc-length resampling's territory), per the stack model.

## Regressions (measured)

- **Cold walls: none.** Same-build before/after: 50% 448.0→439.8,
  100% 431.0→424.0, 200% 486.2→482.2, 400% 506.0→512.2 ms — the 400%
  +6.2 ms is inside the documented ±10 ms between-build icache dice, and
  the three zooms that got *faster* in the same build confirm dice, not
  systematics. All walls inside their ceilings; frozen-corpus world
  geometry is bit-identical by construction (generators emit whole
  quarter-unit multiples).
- **Ink latency: none.** INKTRACE e2d_p95 within ±34 µs on all five
  traces; drain maxima unchanged; identical revision counts (identical
  chunking).
- **Sample storage: +3–10% on recorded traces** (fewer quantized-duplicate
  drops: fast-curve units 381→392, slow-precise 525→579). Well inside
  capacity; visible in `operations=`/`samples=` on future receipts.
- **Verdict vector: identical** (green except requested
  `overlap_cold`); ledger amplification=1.000; five INKTRACE pass=1;
  mixed_draw green (worst append 171 µs); host suites 229/229 + ASan.

## Reopened-gate status

- Cold exactness: host exactness/fuzz suites green (the census oracle is
  self-consistent; fixtures converted geometry-preserving ×4).
- SVG parity: svg_export tests green (reference converter updated).
- Frozen corpus statistics: geometry identical; walls re-measured above.
- 20-run closure statistic: still deferred to the autosave-enabled build.

Logs: `/tmp/units16-1.log` archived as
`../committed-overlay/units16-battery-1.log` (same battery format).

## Addendum — streamline 0.4 (author follow-up, same day)

Glass verdict on units16: "much better," wants it even smoother;
suggested raising the perfect-freehand streamline (0.445 was an STT
artifact; corrected to 0.4). Mechanism: `InkConfig.streamline` scales the
exponential trailing filter α = 0.15 + (1−s)·0.85 (`ink_stream.cpp:64`);
0.35→0.4 lowers α 0.70→0.66.

Sweep at 400% (angularity tool, `--streamline`):

| s | fast-curve p95 / j>20 | slow-precise p95 / j>20 |
|---|---|---|
| 0.35 | 14.0° / 27 | 36.9° / 129 |
| **0.4** | **13.1° / 19** | **22.8° / 132** |
| 0.445 | 13.4° / 16 | 26.6° / 133 |
| 0.55 | 9.3° / 7 | 22.8° / 109 |

Landed: `streamline = 0.4` scoped to the V2 app + harness mirror (Raster
V1 fallback keeps the 0.35 default). Renders: `*-sl40-*.png`. Battery
verdict identical (`../committed-overlay/streamline40-battery-1.log`).

Also landed from the same glass session: the "lift spike" was attributed
to the battery chrome refresh (full-frame present, 60.7 ms compose,
`kind=power` in the lift window — not the ink path); the power redraw now
defers until no lift/stroke report or pending drain is outstanding.
