# Interactive vector-cache pan benchmark findings

Status: first physical interaction captured on the ESP32-S3. Companion to
`INTERACTIVE_PAN_BENCHMARK.md`.

## Verdict

**PASS for fixed-zoom pan latency. FAIL for renderer-only cold-cache prefetch.**

The unchanged raster display path remained responsive while the low-priority
vector renderer ran. However, immediate panning after the 200% cache reset
outran refinement almost completely and exposed the magenta/yellow miss pattern.
A production design cannot depend on final vector rendering alone to populate a
new zoom cache before the user pans.

This is not a project-level rejection of vector authority. It requires a valid
stale/scaled raster fallback, faster coarse cache initialization, or temporarily
bounded movement after a zoom. Checkerboard is intentionally harsher than such
a production fallback.

## Physical results

240 MHz ESP32-S3, octal PSRAM at 80 MHz, 60 MHz AMOLED bus. The synthetic
periodic workload presents 1,000 intersecting handwriting strokes in every
cache cell with screen-space geometry normalized across zoom levels.

| zoom | gestures | frames | direct median | direct p95 | event p95 | miss frames | full cache |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 50% | 0 | 0 | — | — | — | — | — |
| 100% | 26 | 108 | 26.0 ms | 31.1 ms | 41.1 ms | 0/108 | 27.9 s |
| 200% | 10 | 185 | 30.3 ms | 33.4 ms | 45.5 ms | 184/185 | incomplete |

Additional observations:

- 100% direct presentation was about 38 FPS at the median and 32 FPS at p95.
- 200% direct presentation was about 33 FPS at the median and 30 FPS at p95.
- The maximum missing 200% area was 136,896 pixels: the entire presented
  368×372 canvas area.
- The 200% cache missed on 99.46% of recorded frames.
- Center-cache preparation took about 3.18 seconds at both tested zooms.
- The 100% full 3×3 cache took 27.91 seconds to refine.

## Important interpretation caveat

The 200% miss does **not** show that 200% rendering is intrinsically slower in
this prototype. Its synthetic world geometry is normalized so each zoom renders
the same screen-space workload. The difference is cache age: interaction at
100% occurred after more neighboring data had been prefetched, while the 200%
drag began against a newly reset cache.

The load-bearing conclusion is therefore:

> A freshly reset cache has insufficient prefetch runway for immediate panning
> under a 1,000-intersecting-stroke workload, regardless of fixed zoom level.

## What this settles

- Direct raster panning remains fast enough under concurrent rendering.
- Zoom level does not inherently change display-copy cost.
- A 3×3 allocation does not solve cold-cache availability by itself.
- Filling all nine final-quality viewports before interaction is unacceptable
  at roughly 28 seconds for this workload.
- The next prototype, if pursued, should test whole-cache scaled/stale preview
  publication before final bands—not a larger cache or more isolated renderer
  optimization.

## Durable evidence

- Text report: `benchmark-results/interactive-pan/esp32s3-interactive-pan-v1.txt`
- Raw 8 KiB flash image:
  `benchmark-results/interactive-pan/esp32s3-interactive-pan-v1.bin`
- SHA-256 (text):
  `46e7ebfa1190e1f7645c98d09015f51be84079622e41f5b3cebe1835645216d3`
- SHA-256 (binary):
  `27433a21896dac2f713943b572994cead16c116e7409eb9ed769db53b088c14f`

The report was read from flash offset `0x90e000`. Reading reset the device.
