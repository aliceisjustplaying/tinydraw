# Decisive vector-cache prototype plan

## Question

Can one unchanged, realistic vector document remain pleasant to draw, pan, and zoom on the ESP32-S3 while exact output converges later?

This is a throwaway hardware prototype. It is not the production cache interface.

## Product decisions

- Continue toward vector authority plus materialized raster output.
- Production Undo/New history is deferred.
- Durable vector/tile persistence and export redesign are deferred.
- Slight rendering differences are acceptable when they buy meaningful interaction performance, but cold output must always be valid and refinement must converge to the defined canonical renderer.
- Preserve raster presentation and approximately 25–35 ms panning.

## Quality states

- **Valid:** every visible pixel comes from known document output or exact white proven by the document/index. Never checkerboard or uninitialized.
- **Derived:** scaled or downsampled materialized pixels. May differ slightly from canonical low-zoom LOD.
- **Settled:** the designated inexpensive coarse output is present over the visible viewport. It need not be canonical.
- **Exact:** completed output from the canonical renderer for the current document revision, camera level, and renderer version.

## Gates

- Event to first valid presentation: under 100 ms.
- Event to settled viewport for coherent 1,000-stroke handwriting: under 500 ms.
- Direct pan p95: under 35 ms.
- Live drawing update p95: under 10 ms.
- Zero invalid/checkerboard pixels.
- Queue converges after repeated input stops.
- Exact tiles eventually match the canonical reference output.

## Milestones

### 1. Safe canonical renderer

Public seam: `ViewportRenderer::render` / `render_region`.

- Complete strokes larger than the fixed primitive arena without fragment blending.
- Bounded cancellation inside geometry work.
- Conservative low-zoom query halo.
- Incomplete/canceled jobs are never published ready.

### 2. Materialized interaction pipeline

Prototype seam: one coordinator owns document revision, camera generation, quality state, and publication into the existing 3x3 `WorldCanvas` atlas.

- One unchanged coherent world document.
- Current-level world-aligned active 3x3 atlas.
- Immediate scaled valid fallback.
- One 100% -> 50% dirty-rect downsample path, marked derived rather than exact.
- One 100% -> 200% scaled fallback followed by a cheap coarse renderer.
- Minimal world macrogrid plus sequence-preserving candidate bitset.
- New camera generations cancel stale jobs.

### 3. Live drawing policy

- Refinement cancellation/suspension begins on pen-down.
- Live stroke stays separate until vector commit succeeds.
- Completed stroke becomes a new document revision on pen-up.
- Resume materialization afterward.
- Prototype refuses an unrepresentable stroke rather than acknowledging divergent raster ink.

### 4. Physical benchmark

Workloads:

- coherent 1,000-stroke handwriting concentrated centrally;
- sparse surrounding content;
- one over-capacity long stroke;
- self-crossing pen and white-eraser operations;
- immediate 100% -> 50% and 100% -> 200% zoom after a real stroke;
- repeated edge-to-edge swipes;
- one representative 4 KiB flash erase/write burst.

Record first-valid, settled, exact, pan/draw p50/p95/p99, invalid area, cancellation latency, discarded work, queue depth, convergence, downsample time, index on/off timing, free/minimum PSRAM, and largest block.

## Explicit non-goals

- Production Undo/New semantics or branching history.
- Persistent vector or raster checkpoint format.
- Production eviction and final cache interface.
- Final export behavior.
- Every proposed zoom level.
- Guaranteed cold bit-exact output under 500 ms.
