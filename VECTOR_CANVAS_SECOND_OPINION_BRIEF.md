# TinyDraw vector canvas: second-opinion brief

Status: 2026-08-11, branch `feat/vector-rebuild-prototype`, commit `35d38e7`
plus uncommitted raster-pan and interactive-pan prototypes.

## Review request

TinyDraw runs on an ESP32-S3 with 8 MiB of PSRAM and a 368×448 AMOLED. The
question is whether a vector-authoritative drawing document can support a
large, bounded canvas with fast drawing, panning, and a useful zoom range. A
range around 12.5% to 800% would be more useful than three levels, although
fixed power-of-two steps are acceptable.

The desired interaction contract is:

- drawing should feel as responsive as the existing raster firmware;
- panning should remain near the measured 25-33 ms display path;
- zoom should show valid pixels in under 100 ms;
- any visibly pixelated transition should normally settle in under 500 ms;
- exact rendering may take longer only in rare cold or pathological cases;
- the canvas may be large and bounded rather than mathematically infinite.

Please assess whether the evidence supports further investment, whether the
proposed architecture is appropriate, and which experiment would most reduce
remaining uncertainty.

## Hardware and current product

The ESP32-S3 has two 240 MHz cores, 8 MiB octal PSRAM at 80 MHz, and a CO5300
AMOLED driven over 60 MHz QSPI. Touch polling runs on core 1 at priority 5.

Production firmware currently uses:

- two 368×448 RGB565 viewport buffers;
- a 1104×1344 RGB565 `WorldCanvas`, exactly 3×3 screens, using 2.83 MiB;
- ten raster Undo slots using 3.28 MiB;
- a coverage buffer and renderer state;
- raster persistence and full-world PNG export.

`WorldCanvas` is currently both drawing storage and the source for direct pan
presentation. The vector branch records processed `InkStream` samples beside
the raster path, but raster state remains authoritative. Vector persistence,
vector Undo, production zoom controls, and a production cache do not exist yet.

## Work completed

The branch contains a bounded `VectorDocument`, world-space camera transforms,
live recording of processed stroke samples, and `ViewportRenderer`. The renderer
culls by stroke bounds, reconstructs ribbons, and composites 32×32 tiles into a
368×448 RGB565 destination.

Optimization work already completed:

1. tile-major compositing;
2. hoisted software floating-point division and square root;
3. dirty-rectangle coverage processing;
4. omission of provisional live geometry during offline replay;
5. two-lane tile compositing across both cores.

All optimizations preserved the existing golden raster snapshots.

## Verified physical measurements

All timings below come from the physical ESP32-S3 unless marked as a manual
observation.

### Full viewport reconstruction

The following table shows the optimized dual-core renderer. Times are for one
complete 368×448 viewport.

| workload | zoom | exact render |
|---|---:|---:|
| 100 handwriting strokes | 25% | 87 ms |
| 100 handwriting strokes | 100% | 191 ms |
| 1,000 handwriting strokes | 25% | 776 ms |
| 1,000 handwriting strokes | 100% | 1,777 ms |
| 1,000 short visible strokes | 25% | 111 ms |
| 1,000 short visible strokes | 100% | 292 ms |
| 5,000 short visible strokes | 25% | 516 ms |
| 5,000 short visible strokes | 100% | 1,428 ms |
| 100 pathological dense curves | 25% | 731 ms |
| 100 pathological dense curves | 100% | 2,675 ms |
| 5,000 offscreen strokes | any | 12.3 ms |

Measured unit costs:

- linear culling costs about 2.5 microseconds per stroke;
- clearing a viewport in PSRAM costs about 9.2 ms;
- rasterization consumes 60-80% of current render time;
- geometry usually consumes 10-25%;
- one unpacked 5,000-stroke, 15,000-sample document uses about 320 KiB.

These measurements rule out exact vector reconstruction on every interaction
frame. They do not rule out caching or incremental maintenance.

### Region rendering and progressive zoom

A second physical prototype shifted a cached viewport, rendered only the
exposed 32-pixel strip, and compared the result with a clean full rebuild.

| workload | 32-pixel strip | strip plus full-screen presentation |
|---|---:|---:|
| 1,000 short visible strokes | 50-60 ms | 95-105 ms |
| 1,000 handwriting strokes | 152-162 ms | 197-208 ms |
| 100 dense curves, populated strip | 477 ms | 523 ms |

The same prototype generated a nearest-neighbor zoom preview in 19-20 ms and
showed it after the physical display transfer in about 65 ms.

| workload | zoom | first exact 32-row band | complete exact viewport |
|---|---:|---:|---:|
| 1,000 short visible strokes | 50% | 7.8 ms | 227 ms |
| 1,000 short visible strokes | 200% | 18.6 ms | 257 ms |
| 1,000 handwriting strokes | 50% | 7.8 ms | 1,403 ms |
| 1,000 handwriting strokes | 200% | 87.7 ms | 1,202 ms |
| 100 dense curves | 50% | 6.8 ms | 2,713 ms |
| 100 dense curves | 200% | 371 ms | 5,224 ms |

Completed progressive zooms matched clean full rebuilds exactly. Touch polling
continued at roughly 1 kHz, with a worst measured interval of 2.028 ms. This
proved scheduler coexistence for polling. It did not test real stroke rendering
while background refinement occupied the machine.

### Production raster pan

A later benchmark corrected an important mistaken assumption. Production pan
does not shift a framebuffer or render a vector strip. It changes the origin
inside `WorldCanvas` and streams the strided raster window directly to the
panel.

Thirty physical frames measured:

```text
minimum: 25.445 ms
median:  25.452 ms
average: 25.451 ms
maximum: 25.458 ms
```

This is about 39 frames per second and matches the user's experience. It is
4-8 times faster than the vector strip-pan prototype. Any replacement must keep
pan as a raster presentation operation.

This result invalidated the earlier plan to recover all 6.1 MiB by deleting
both raster Undo and `WorldCanvas`. Deleting raster Undo still recovers 3.28
MiB. Some raster cache remains necessary, although it need not retain the old
meaning of a fixed 3×3 world.

### Interactive cold-cache benchmark

The latest prototype retained the direct raster pan path while a priority-1
renderer filled 32-row cache bands. Missing bands contained a magenta/yellow
checkerboard. The size controls selected 50%, 100%, and 200%; XL persisted the
report.

The synthetic workload was intentionally severe. Each of the nine screen cells
contained its own 1,000-intersecting-stroke handwriting workload, normalized to
have the same screen-space complexity at every zoom. This is closer to nine
dense viewports than to one coherent 1,000-stroke document.

| zoom | gestures | frames | direct median | direct p95 | event p95 | misses |
|---:|---:|---:|---:|---:|---:|---:|
| 100% | 26 | 108 | 26.0 ms | 31.1 ms | 41.1 ms | 0/108 |
| 200% | 10 | 185 | 30.3 ms | 33.4 ms | 45.5 ms | 184/185 |

The full 3×3 cache at 100% took 27.91 seconds. Preparing the exact center screen
took about 3.18 seconds in this single-core-background configuration. At 200%,
the largest miss covered all 368×372 presented canvas pixels.

The user also observed many checkerboard misses at 50%. No 50% frame metrics
were persisted, so that result is a manual observation rather than a recorded
timing distribution.

This prototype proved two separate points:

- direct raster pan remains responsive while vector rendering runs;
- final-quality renderer-only prefetch cannot initialize a cold cache before
  immediate human panning under this severe workload.

It did not show that 200% is inherently slower. The synthetic geometry was
normalized across zoom levels. Cache age explains the difference between the
100% and 200% recorded runs.

## Exactness

Region rendering is bit-exact inside the rendered region. Progressive zoom was
bit-exact after completion.

One camera-aligned handwriting pan differed from a clean rebuild at 2 of
164,864 pixels. Replaying floating-point curved geometry at a translated camera
caused the mismatch; retained raster pixels themselves were stable. World-aligned
raster tiles are the leading fix if literal translation invariance remains a
requirement.

The practical cold-render latency for exact output is currently:

| content in one uncached viewport | time to exact |
|---|---:|
| 1,000 short visible strokes | about 0.23-0.30 s |
| 1,000 handwriting strokes | about 1.2-2.0 s dual-core |
| pathological dense content | about 2.7-5.2 s |
| severe interactive workload, single background core | about 3.18 s |

A cheap scaled preview does not change these final-render costs.

## Drawing status

Production raster drawing averages roughly 2.5-3.4 ms per recent long-stroke
update. The vector branch already appends processed points alongside that path.
Adding stroke 1,001 does not replay the previous 1,000 strokes; the cost of the
new stroke should depend mostly on its own geometry.

This supports, but does not prove, the expectation that live drawing can stay
fast under vector authority. The missing test is a real pen stroke while the
background renderer competes for PSRAM and CPU time. Touch polling alone is not
enough evidence.

Undo is a separate cost. Appending a pen or ordered white eraser stroke can
update raster tiles incrementally. Removing an old operation requires replaying
at least the affected tiles.

## Memory

Current large allocations:

| allocation | size |
|---|---:|
| fixed 3×3 `WorldCanvas` | 2.83 MiB |
| ten-entry raster Undo arena | 3.28 MiB |
| two viewport buffers | 644 KiB total |
| 5,000-stroke/15,000-sample vector document | 320 KiB |
| renderer arenas and scratch during rebuild | about 490 KiB |

The Phase 2 prototype had 678,256 free PSRAM bytes before allocating its
benchmark document and renderer, then 334,692 free bytes during the matrix. No
net leak or largest-block degradation appeared in that run.

Keeping one 3×3 raster cache while deleting raster Undo frees 3.28 MiB rather
than 6.1 MiB. A second full 3×3 RGB565 cache would cost another 2.83 MiB and
might fit only after the raster Undo migration, with little margin. A 128×128
RGB565 tile costs 32 KiB; a 2 MiB cache holds 64 such tiles.

Compression may retain more mostly-white drawing tiles, but the design cannot
rely on a favorable compression ratio for correctness.

## Human pan geometry

A 3×3 raster cache centered on the viewport provides one full screen of runway
in every direction. A single continuous finger gesture cannot move farther
than roughly one screen because both endpoints remain on the display. This
makes 3×3 a useful prototype cache size even though it should no longer define
the document bounds.

Repeated immediate swipes can still outrun background refill. A sliding or
ring-based cache must rebase after movement, retain raster pixels, prioritize
newly exposed tiles, and provide valid preview pixels for regions that are not
exact yet.

The interactive workload exaggerated realistic refill cost by placing 1,000
intersecting strokes in every cell. A coherent document with 1,000 overlapping
strokes concentrated in the center would make surrounding cells much cheaper
to cull and clear. That scenario has not been measured. It should be the next
workload.

## Zoom range

A broad fixed range is compatible with a vector document and on-demand raster
tiles. A plausible sequence is:

```text
12.5%  25%  50%  100%  200%  400%  800%
```

At low zoom, LOD must simplify or omit subpixel strokes. At high zoom, the
viewport covers less world area, which usually reduces the number of
intersecting strokes. The implementation cannot allocate a full raster of the
world at 800%; it must materialize only nearby or previously drawn tiles.

If a 3×3 cache at 10% covered the bounded world, that world would span about
11,040×13,440 coordinates at 100%. Higher levels would remain sparse and
camera-local.

## Architecture options

### 1. Vector authority plus materialized multiresolution raster tiles

This is the current leading option.

`VectorDocument` remains authoritative. Exact raster tiles at fixed zoom levels
act as materialized views. The firmware updates the active level immediately
when a stroke arrives, prepares adjacent levels while idle, retains nearby pan
tiles, and optionally persists completed tiles. Zoom usually selects already
materialized tiles; scaled tiles cover any cold gaps until exact rendering
finishes.

This approach moves work away from camera changes. It makes warm zoom exact and
fast, while preserving vectors for reconstruction, future editing, export, and
cache eviction. Undo must replay affected tiles because an old operation cannot
always be subtracted from composited raster state.

The main risks are tile invalidation complexity, flash traffic, memory pressure,
and maintaining exact operation order across pen, eraser, clear, and Undo.

### 2. Complete low-resolution overview plus one active high-resolution cache

This is a simpler special case. The lowest zoom level stores a complete bounded
world overview. Higher zooms use one sliding high-resolution cache initialized
from that overview. It guarantees valid pixels, but large zoom ratios can look
very coarse. Supporting 12.5%-800% probably requires intermediate cached levels,
which naturally becomes the tile-pyramid design above.

### 3. Vector reconstruction on camera demand

The completed benchmarks reject this as the sole interaction strategy. It is
useful for cold-tile reconstruction and background refinement, but exact
rebuilds cannot sit behind every pan or zoom.

### 4. Sparse raster authority

A sparse raster tile pyramid could replace vector authority. Pan and zoom might
be simpler, and raster Undo could remain tile-oriented. It would lose
resolution-independent reconstruction unless the firmware stores a high base
resolution, and memory would scale with rasterized drawn area. Drawing at low
zoom would also need a policy for higher-resolution levels. This option has not
been prototyped and would discard much of the completed vector work.

## Remaining rendering levers

The Phase 1 estimate for known final-renderer improvements is roughly another
2× in combination, not a guarantee. Candidates include scanline difference
arrays for filled interiors, fixed-point work, 2×2 rather than 4×4 coverage for
rebuilds, geometry caching at a fixed zoom, and pipelining geometry against the
second core.

A deliberately cheaper intermediate renderer could gain more by drawing
simplified centerlines, using lower supersampling, and applying zoom-specific
LOD. That output would not be bit-identical to the current renderer. No physical
benchmark yet shows that it can make a heavy viewport visually settled within
500 ms.

## Current assessment

The hardware cannot guarantee that every arbitrary, uncached, dense viewport
becomes bit-exact within 500 ms. Current measurements put that case at 1-5
seconds depending on content.

A realistic session may perform much better because drawing incrementally
maintains the active raster, human activity creates idle time for neighboring
zoom levels, and completed tiles can survive pan, zoom, or reload. The severe
interactive test did not model those advantages.

Further investment is justified only through another bounded prototype. The
team should not implement vector persistence, production Undo, or the final
cache interface until that prototype passes.

## Proposed decisive prototype

Estimated implementation time is 6-10 focused hours because the interactive
harness and renderer already exist.

Use one coherent 1,000-stroke handwriting document concentrated in the center
of a larger bounded world. Do not repeat the same 1,000-stroke workload in every
cache cell.

The prototype should:

1. keep the current viewport exact;
2. seed the surrounding pan runway with scaled valid pixels rather than a
   checkerboard;
3. prepare adjacent pan regions and representative zoom levels while idle;
4. accept and display real pen strokes while refinement continues;
5. zoom immediately after a stroke;
6. exercise repeated edge-to-edge swipes;
7. record direct presentation, event-to-present latency, missing-preview area,
   time to visually settled output, time to bit-exact output, and drawing-update
   latency.

Suggested gates:

| measurement | gate |
|---|---:|
| first valid zoom image | under 100 ms |
| blank/checkerboard pixels during ordinary interaction | zero |
| visually settled output in coherent 1,000-stroke case | under 500 ms |
| direct pan p95 | under 35 ms |
| live drawing update p95 during refinement | under 10 ms |
| final tile correctness | matches existing renderer |

The prototype should distinguish warm, partially cached, and cold cases. A
warm-cache pass does not imply a cold-cache pass.

## Questions for a second reviewer

1. Does vector authority plus incrementally materialized raster tiles fit the
   evidence better than on-demand vector reconstruction?
2. Can exact zoom-level tiles be updated cheaply when operations are appended,
   while preserving ordered eraser behavior?
3. Is one complete low-level overview plus a 1-2 MiB hot tile cache feasible
   with the measured PSRAM budget, or is another cache layout preferable?
4. Which persistent tile format and invalidation strategy would avoid flash
   stalls on this ESP32-S3?
5. Is the under-500-ms visually-settled target realistic for a coherent
   1,000-stroke document after applying LOD and cache reuse?
6. What evidence would justify stopping rather than building the next
   prototype?
7. What other tricks/moves/etc may we be missing?

## Evidence and code references

Primary physical reports:

- `benchmark-results/phase2/esp32s3-phase2-v1-corrected.txt`
- `benchmark-results/phase2/esp32s3-phase2-v1-corrected.bin`
- `benchmark-results/raster-pan/esp32s3-raster-pan-v1.txt`
- `benchmark-results/raster-pan/esp32s3-raster-pan-v1.bin`
- `benchmark-results/interactive-pan/esp32s3-interactive-pan-v1.txt`
- `benchmark-results/interactive-pan/esp32s3-interactive-pan-v1.bin`

Interpretation and plans:

- `V2_INITIAL_SPEC.md`
- `V2_PHASE1_FINDINGS.md`
- `V2_PHASE2_PROTOTYPE_PLAN.md`
- `V2_PHASE2_PROTOTYPE_FINDINGS.md`
- `INTERACTIVE_PAN_BENCHMARK.md`
- `INTERACTIVE_PAN_BENCHMARK_FINDINGS.md`

Relevant code:

- `core/include/tinydraw/document/vector_document.h`
- `core/include/tinydraw/graphics/viewport_renderer.h`
- `core/include/tinydraw/graphics/world_canvas.h`
- `core/src/viewport_renderer.cpp`
- `esp32/main/phase2_prototype_runner.cpp`
- `esp32/main/raster_pan_benchmark.cpp`
- `esp32/main/interactive_pan_benchmark.cpp`
- `esp32/main/hardware_app.cpp`
