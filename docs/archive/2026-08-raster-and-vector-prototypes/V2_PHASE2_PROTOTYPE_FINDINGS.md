# Vector Infinite Canvas — Phase 2 Prototype Findings (ESP32-S3)

Status: physical prototype completed. Companion to `V2_PHASE1_FINDINGS.md` and
`V2_PHASE2_PROTOTYPE_PLAN.md`.

## Verdict: PASS for incremental cached rendering

The Phase 2 system hypothesis works on the physical ESP32-S3:

- A cached viewport can be shifted and only its exposed strip rebuilt.
- A zoom can display an approximate preview in about 65 ms and then replace it
  with final, bit-exact bands.
- The real priority-5 touch polling task continues to run at approximately
  1 kHz while both cores render.
- Free PSRAM and the largest free block remain constant through the matrix.

This authorizes production design and implementation of a vector-backed tile
cache. It does **not** authorize deleting `WorldCanvas` yet. Production cached
pan/zoom, vector undo, and persistence still need to land beside the current
raster path before that destructive migration.

## Prototype shape

`ViewportRenderer::render_region()` rebuilds a half-open screen rectangle and
leaves every pixel outside it untouched. Full render delegates to the same
implementation using the full 368×448 rectangle.

The physical prototype uses the app's existing two viewport buffers:

1. Render the old camera into the cache.
2. Shift retained RGB565 pixels in place by 32 screen pixels.
3. Rebuild only the newly exposed strip at the new camera.
4. Compare against a clean full rebuild in the second viewport buffer.
5. Push the result to the real AMOLED.

For zoom:

1. Affinely resample the old cache into an approximate nearest-neighbor preview.
2. Push that preview to the AMOLED.
3. Rebuild and push final 32-row bands from top to bottom.
4. Compare the completed bands against a clean full rebuild.

The production canvas, raster undo, persistence, and drawing path remain
unchanged. The normal firmware was restored after measurement.

## Corrected physical results

All times are wall clock on the 240 MHz ESP32-S3 with octal 80 MHz PSRAM.
"Shown" includes the 45–46 ms full AMOLED transfer after strip rendering.

### Cached 32-pixel pan

| document | direction | strip render | shown | full rebuild | reduction | changed pixels vs full |
|---|---:|---:|---:|---:|---:|---:|
| visible short, 1,000 | left | 59.5 ms | **105.2 ms** | 286.7 ms | 4.8× render | 0 |
| visible short, 1,000 | up | 49.7 ms | **95.2 ms** | 293.1 ms | 5.9× render | 0 |
| handwriting, 1,000 | left | 161.9 ms | **207.6 ms** | 1,994.9 ms | 12.3× render | 2 |
| handwriting, 1,000 | up | 151.8 ms | **197.4 ms** | 1,991.1 ms | 13.1× render | 0 |
| dense curves, 100 | left | 477.4 ms | **523.1 ms** | 2,923.8 ms | 6.1× render | 0 |
| dense curves, 100 | up | 24.6 ms | 70.0 ms | 2,809.7 ms | n/a | 0 |

The dense vertical strip contained no ink and is not evidence of dense strip
performance; the populated horizontal strip is the valid pathological case.

Typical and handwriting cases meet the written <300 ms heavy-strip criterion.
The intentionally pathological dense horizontal strip does not. LOD or
progressive strip subdivision remains required for that content class.

The prototype pushes the complete shifted viewport after each pan. If future
panel support can scroll retained pixels or accept only changed regions, the
45–46 ms transfer can be reduced; this prototype does not assume that feature.

### Progressive zoom

| document | zoom | preview generated | preview shown | first final band | complete refinement |
|---|---:|---:|---:|---:|---:|
| visible short, 1,000 | 50% | 20.0 ms | **65.7 ms** | 7.8 ms | 226.6 ms |
| visible short, 1,000 | 200% | 19.0 ms | **64.7 ms** | 18.6 ms | 257.2 ms |
| handwriting, 1,000 | 50% | 20.0 ms | **65.7 ms** | 7.8 ms | 1,403.4 ms |
| handwriting, 1,000 | 200% | 19.0 ms | **64.7 ms** | 87.7 ms | 1,202.0 ms |
| dense curves, 100 | 50% | 20.0 ms | **65.7 ms** | 6.8 ms | 2,712.5 ms |
| dense curves, 100 | 200% | 19.1 ms | **64.8 ms** | 370.8 ms | 5,223.5 ms |

Every completed progressive result matched a clean full rebuild exactly.

The first implementation used software double division and rounding per pixel
and took about 550 ms to build the preview. Replacing that with one 16.16 affine
start/step per axis reduced preview generation to 19–20 ms. The real display
transfer then dominates immediate feedback, yielding a stable 65 ms preview
regardless of document complexity.

The preview gate passes. Typical final bands pass. The dense 200% first band
fails the 300 ms heavy criterion and confirms the need for LOD/subdivision in
that pathological case. Importantly, the user sees a valid preview during the
long refinement rather than a frozen old camera or blank screen.

## Touch coexistence

The actual CST820 polling task ran on core 1 at priority 5 while the renderer's
second lane ran on core 1 at priority 1. The probe records each real hardware
poll after its I2C read.

- Average poll interval: approximately 0.95–1.01 ms.
- Worst corrected-run interval: **2.028 ms**.
- Written maximum: 20 ms.

This directly validates scheduler coexistence for polling. It does not yet
measure end-to-end pen latency while the application consumes and rasterizes a
real stroke concurrently; that remains a production-integration test.

## Memory

At prototype entry:

- Free PSRAM: 678,256 bytes.
- Largest PSRAM block: 671,744 bytes.

After allocating document/renderer scratch:

- Free PSRAM: 334,692 bytes.
- Largest PSRAM block: 327,680 bytes.

After every document case and at completion those values were unchanged:

- Free PSRAM: 334,692 bytes.
- Largest PSRAM block: 327,680 bytes.

This proves no net leak and no observed largest-block degradation during the
matrix. It does not claim fragmentation is impossible. The prototype reuses
`committed` and `visible`; an attempted third 330 KB viewport did not fit in the
current pre-migration memory budget. Removing `WorldCanvas`/raster undo later
would release the previously measured ~6.1 MiB, but the production cache must
reserve room explicitly rather than count theoretical whole viewports.

## Correctness caveat

All progressive zooms and five of six valid pan comparisons were bit-exact.
The 1,000-stroke handwriting horizontal pan differed at **2 of 164,864 pixels**
(0.0012%); the same direction differs by one pixel on the host. Region rendering
itself matches full rendering exactly. The discrepancy comes from replaying
curved floating-point geometry at a translated camera versus retaining its
already-rasterized pixels.

This is visually negligible but violates a literal exactness requirement.
Production has three choices:

1. Accept retained cache pixels and let later full refinement replace them.
2. Quantize camera-local geometry so integer camera translations are exactly
   translation invariant.
3. Cache world-aligned raster tiles rather than a camera-aligned whole viewport.

The third option best matches the intended tile-cache architecture and should
be tested first.

## Architecture decision

Proceed with a deep cached-viewport module whose interface owns:

- current camera and zoom generation,
- world-aligned raster tile keys,
- retained-tile lookup,
- exposed-region scheduling,
- preview generation,
- progressive final-tile publication,
- cancellation when a newer camera supersedes an older refinement.

Keep `ViewportRenderer::render_region()` as the rendering seam. Do not spread
strip calculations, cache invalidation, or refinement generation checks across
toolbar/input callers.

Recommended production order:

1. Add the cache module beside `WorldCanvas`.
2. Integrate pan with retained world-aligned tiles and generation cancellation.
3. Integrate zoom preview + progressive final tiles.
4. Measure real drawing while background refinement runs.
5. Add vector operation undo and persistence.
6. Only then remove `WorldCanvas` and raster undo.

## What remains unproven

- Real pen strokes consumed/rasterized while background refinement runs.
- Cancellation latency when the camera changes repeatedly.
- World-aligned tile key precision at very large/negative coordinates.
- Cache eviction under production memory pressure.
- Display behavior during rapid successive pan frames.
- LOD quality and timing for pathological dense content.
- Multi-run median/p95 distributions; this small prototype records one corrected
  physical run per case.

## Durable evidence

- Corrected report: `benchmark-results/phase2/esp32s3-phase2-v1-corrected.txt`
- Corrected raw 8 KiB flash image:
  `benchmark-results/phase2/esp32s3-phase2-v1-corrected.bin`
- First report (retained because it exposed the invalid empty-strip workload and
  slow floating preview): `benchmark-results/phase2/esp32s3-phase2-v1.txt`
- Prototype plan and gates: `V2_PHASE2_PROTOTYPE_PLAN.md`

The report was read from the final 8 KiB of the `export` partition at flash
offset `0x90e000`. No serial monitor was opened.
