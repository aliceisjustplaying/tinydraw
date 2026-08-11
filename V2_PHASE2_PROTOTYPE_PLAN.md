# Phase 2 Prototype Plan

Status: completed. Results and verdict: `V2_PHASE2_PROTOTYPE_FINDINGS.md`.

## Question

Can one cached 368×448 RGB565 viewport produce an exact new camera view fast
enough by shifting its pixels and rebuilding only exposed regions, and can a
zoom operation show useful pixels quickly while final regions refine—without
starving the real touch polling task?

This is a reversible feasibility prototype. It does not replace `WorldCanvas`,
raster undo, persistence, or the live drawing path.

## Prototype seam

Add half-open rectangular region rendering to `ViewportRenderer`. Full render
remains the existing interface and delegates to the full viewport region.
The benchmark adapter uses this seam to:

1. Render and cache a complete viewport.
2. Move the camera by an integer screen delta.
3. Shift retained RGB565 pixels in place.
4. Rebuild only the newly exposed horizontal/vertical rectangles.
5. Compare the result bit-for-bit with an independent full rebuild checksum.
6. Produce an immediate nearest-neighbor zoom preview from the prior cache.
7. Refine the zoomed view in 32-pixel horizontal bands and record time to the
   first final band plus total refinement time.

The benchmark runs alongside the actual core-1 touch polling task. Poll count,
average interval, and maximum interval are persisted with the report. Touch
keeps priority 5; the render helper stays priority 1 on core 1.

## Physical cases

- 1,000 short visible strokes at 100% zoom (typical/high-count case).
- 1,000 handwriting strokes at 100% zoom (heavy realistic case).
- Optional 100 dense strokes at 100% zoom (pathological raster case) if report
  and runtime budgets allow.

Use a 32-pixel horizontal pan and 32-pixel vertical pan. Zoom from 100% to 50%
and 200% using the same cached starting viewport.

## Pass/fail criteria

- Incremental pan output equals a clean full rebuild exactly.
- Typical strip rebuild: <100 ms preferred, <300 ms maximum.
- Heavy strip rebuild: <300 ms.
- Immediate scaled preview generation: <30 ms preferred, <100 ms maximum.
- First final zoom band: <100 ms typical, <300 ms heavy.
- Touch polling continues during rendering; maximum poll interval remains below
  20 ms (the existing pen-up debounce interval) and no queue/input regressions
  are observed.
- No net PSRAM leak; largest free block is reported before and after repeated
  cases. Do not claim zero fragmentation from free-byte totals alone.

## Explicit non-goals

- No production pan/zoom controls.
- No general multi-viewport cache replacement policy.
- No deletion or migration of raster state.
- No vector persistence or operation history.
- No LOD, spatial index, or additional rasterizer optimization.
