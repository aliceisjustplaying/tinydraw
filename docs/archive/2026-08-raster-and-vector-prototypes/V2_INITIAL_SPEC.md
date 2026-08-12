# TinyDraw ESP32-S3 Infinite Canvas — Phase 1 Engineering Brief

We want to investigate and prototype an infinite-canvas architecture for the ESP32-S3 version of TinyDraw.

Repository:
https://github.com/aliceisjustplaying/tinydraw

Focus only on the ESP32-S3 implementation and shared core code needed for it. Do not change the RP2350 behavior unless a shared-core change requires keeping it building.

## Goal

Move toward a drawing architecture where:

* The authoritative document is stored as vector-like stroke data.
* The display remains a fixed 368×448 RGB565 raster viewport.
* Pan and zoom operate through a camera transform over world-space strokes.
* Canvas size is effectively unbounded by framebuffer dimensions.
* RAM usage scales primarily with drawing complexity rather than canvas area.
* Existing low-latency live drawing behavior is preserved.

However, do **not** replace `WorldCanvas`, raster undo, persistence, or the current drawing path yet.

The first milestone is to prove that the ESP32 can reconstruct a viewport from stored vector strokes fast enough to make this architecture practical.

## Current architecture facts

The current ESP32 implementation allocates:

* `committed_`: 368×448 RGB565
* `visible_`: 368×448 RGB565
* `active_coverage_`
* raster undo storage
* a 3×3 `WorldCanvas`, 1104×1344 RGB565

The 3×3 world is about 2.83 MiB.

The ten-entry raster undo arena is about 3.28 MiB.

Those two allocations alone consume about 6.11 MiB of PSRAM.

The current physical build reports roughly 889 KB free PSRAM at boot.

Relevant files include:

* `esp32/main/firmware_canvas.cpp`
* `core/include/tinydraw/graphics/world_canvas.h`
* `core/include/tinydraw/graphics/stroke_raster.h`
* `core/include/tinydraw/graphics/tile_undo_history.h`
* `core/include/tinydraw/ink/ink_stream.h`
* `core/include/tinydraw/ink/ribbon_geometry.h`
* `core/src/ink_stream.cpp`
* `core/src/ribbon_geometry.cpp`
* `core/src/stroke_raster.cpp`
* `PROJECT_STATE.md`

Read those before changing anything.

## Important architectural decision

The persistent stroke representation should store the processed centerline output of `InkStream`, not generated ribbon polygons.

Store the visual information required to reconstruct a stroke:

* world-space position
* radius
* stroke color
* tool/type
* stroke bounds

Do not persist `RibbonPrimitive`s.

Do not use raw `TouchPoint`s as the main document representation.

Raw touch plus timestamps could technically reproduce the current pressure algorithm, but processed position/radius is preferable because old drawings should remain visually stable if `InkStream`, pressure simulation, streamline, or brush behavior changes later.

## Phase 1 scope

Implement a vector-document prototype alongside the existing raster implementation.

The existing raster canvas remains authoritative.

### 1. Add a compact stroke document model

Introduce something like:

```cpp
struct StrokeSample {
    // Prototype representation can be simple first.
    // Optimize packing only after functionality is measured.
    float x;
    float y;
    float radius;
};

struct VectorStroke {
    std::vector<StrokeSample> samples;
    std::uint16_t color;
    RectF bounds;
};

class VectorDocument {
public:
    void begin_stroke(...);
    void append(...);
    void finish_stroke(...);

    std::span<const VectorStroke> strokes() const;
};
```

For the first benchmark implementation, clarity is more important than the final 6-byte packed representation.

Do not prematurely optimize serialization or fixed-point encoding yet.

Avoid putting unbounded dynamic allocation into the live embedded drawing path if it causes fragmentation or latency. If necessary, use preallocated arenas or limit this first implementation to host/performance-test construction.

The eventual intended packed representation is approximately:

* first point / stroke origin in absolute world coordinates
* subsequent points delta encoded
* fixed-point coordinates
* packed radius

A target around 6 bytes per point appears realistic, but Phase 1 does not need to achieve this yet.

### 2. Record post-`InkStream` samples

Hook the vector prototype into the existing live stroke pipeline.

Whenever the normal drawing path receives the processed `InkPoint` from `InkStream`, record:

```cpp
position.x
position.y
radius
```

along with stroke metadata.

Do not alter the visual live drawing behavior.

The existing `StrokeRaster` path must continue producing the actual display while this recording happens in parallel.

### 3. Build a dedicated offline viewport renderer

Do **not** reconstruct old strokes by blindly feeding every saved point through the existing live `StrokeRaster::update()` path.

That path performs work required for interactive drawing:

* provisional geometry replacement
* coverage-plane reads/writes
* dirty-tile bookkeeping
* presentation-region calculation
* display updates
* raster undo support

Most of that is unnecessary during a full viewport rebuild.

Create a separate renderer, conceptually:

```cpp
class ViewportRenderer {
public:
    void render(
        const VectorDocument& document,
        const Camera& camera,
        std::span<std::uint16_t> destination);
};
```

The renderer should:

1. Clear the destination viewport.
2. Determine the visible world-space rectangle.
3. Skip strokes whose bounding boxes do not intersect it.
4. Transform stored world-space samples into local screen coordinates.
5. Reconstruct the stroke geometry.
6. Rasterize into the RGB565 destination.
7. Perform no physical display transfers while rebuilding.

Reuse the existing geometry/rasterization primitives where sensible.

`CurvedRibbonStream` is a good candidate for reconstructing stroke geometry incrementally.

Do not use `build_pf_ribbon()` on the embedded path if it requires large temporary `std::vector` allocations.

A new batch/offline raster API is acceptable if that avoids repeatedly invoking all of `StrokeRaster::update()`'s live-stroke machinery.

### 4. Add a camera abstraction

Introduce a camera model without changing the current UI yet.

Conceptually:

```cpp
struct Camera {
    WorldCoord x;
    WorldCoord y;
    float zoom = 1.0F;
};
```

For world coordinates, prefer integer/fixed-point storage long term.

Avoid keeping globally large world coordinates as floats because precision will degrade far from the origin.

A good pattern is:

```text
integer world coordinate
        ↓
subtract camera origin
        ↓
small local coordinate
        ↓
convert to float for existing rasterizer
```

Phase 1 may use floats internally where needed, but isolate the transform so fixed-point world coordinates can replace them later.

### 5. Handle zoomed-out stroke visibility deliberately

At low zoom, thin strokes become subpixel.

Current small-brush strokes can reach about a 1.125 px radius before zoom.

At 25% zoom that becomes roughly 0.28 px.

The current rasterizer uses 4×4 coverage sampling, so such strokes may become very faint rather than disappear completely.

Do not silently bake in a minimum-radius rule without testing.

Add an experimental renderer option for something like:

```cpp
screen_radius = world_radius * zoom;
```

versus:

```cpp
screen_radius = std::max(world_radius * zoom, minimum_legible_radius);
```

Test minimums around 0.4–0.5 px.

This is a display/LOD policy, not document data.

### 6. Build performance benchmarks immediately

This is the most important Phase 1 deliverable.

Create synthetic vector documents containing approximately:

* 100 strokes
* 1,000 strokes
* 5,000 strokes

Include multiple patterns:

* short sparse strokes
* medium handwriting-like strokes
* long dense strokes
* many offscreen strokes
* many strokes intersecting the viewport

Benchmark rebuilds at:

* 25%
* 50%
* 100%
* 200%

At minimum record:

* total viewport rebuild time
* number of strokes tested
* number of strokes intersecting viewport
* number of stored samples processed
* number of ribbon primitives rasterized
* PSRAM free before/after
* largest free block where available

Run benchmarks on host first if useful, but the meaningful result is the physical ESP32-S3 at 240 MHz.

Do not remove `WorldCanvas` until physical viewport-rebuild timing is known.

The architecture decision depends on this benchmark.

Useful rough interpretation:

* < ~30 ms: excellent
* ~30–100 ms: likely fine, especially for button-triggered zoom and release-time redraw
* ~100–300 ms: probably workable with cached pan previews / smarter indexing
* > ~300 ms on realistic documents: likely requires more aggressive spatial indexing, tile caching, LOD, or stroke simplification

These are guidance, not hard pass/fail thresholds.

### 7. Add tests

Add unit tests for:

* stored stroke replay producing stable geometry
* camera transform correctness
* viewport culling
* negative world coordinates
* very large world coordinates
* zoom transforms
* stroke bounding boxes
* clearing/rebuilding a viewport
* no changes to existing raster behavior

Keep existing tests green.

Run:

```sh
./scripts/dev test
./scripts/dev asan
./scripts/esp32 build
./scripts/esp32 graphics-test
```

Do not regress the existing ESP32 physical drawing path.

## Explicitly out of scope for Phase 1

Do not yet:

* delete `WorldCanvas`
* delete `TileUndoHistory`
* redesign the toolbar
* add zoom buttons to production UI
* replace current autosave
* write the vector document to flash
* redesign USB export
* implement vector boolean erasing
* implement sophisticated LOD
* implement compaction
* change New/Undo semantics
* make pan use the vector renderer in production
* optimize the stroke encoding down to its final packed format

We first need hard rebuild-performance numbers.

## Phase 2 direction, but do not implement yet

If viewport rebuild performance is promising, the intended next architecture is:

```text
VectorDocument
    │
    ├── Stroke spatial index / bounding boxes
    │
    ├── Operation history
    │
    └── eventual flash log
            │
            ▼
        Camera
      x / y / zoom
            │
            ▼
    ViewportRenderer
            │
            ▼
    368×448 RGB565 cache
            │
            ▼
          AMOLED
```

Likely future undo model:

```cpp
enum class OpKind {
    Stroke,
    EraserStroke,
    Clear,
};
```

Pan itself should probably remain non-undoable.

An undo entry may optionally remember camera position so Undo can bring the affected operation back into view.

`New` should become a logical clear/generation boundary rather than destroying old strokes immediately, allowing cheap Undo.

Eraser can initially be represented as an ordered eraser stroke rather than vector boolean subtraction.

## Persistence direction

Do not assume "pen up = synchronous flash write."

Physical flash writes/erases can stall cache access, and PSRAM depends on that cache under the current ESP-IDF configuration.

The current project has already measured a physical 18-sector save taking about 2.27 seconds.

Eventually use:

* RAM write buffering
* append-only records
* checksums / commit markers
* pre-erased sectors
* low-priority flushes while idle
* explicit compaction / garbage collection

Possible future layout:

```text
[document header]
[stroke record]
[stroke record]
[clear marker]
[stroke record]
...
```

A two-arena compaction strategy may be appropriate:

```text
Arena A: active log
Arena B: compaction target
```

When A gets crowded, copy only live document state into B, atomically mark B current, then erase A later.

Do not implement this in Phase 1.

## Engineering constraints

Preserve existing behavior first.

Avoid large rewrites.

Keep commits small and logically isolated.

Prefer additions that let the old and new architectures run side by side.

Add instrumentation before optimization.

Do not claim the infinite-canvas architecture is viable until physical ESP32 rebuild numbers exist.

If a shared-core change affects RP2350 compilation, keep RP2350 green without expanding its feature scope.

## Desired Phase 1 deliverables

At the end of this work, provide:

1. `VectorDocument` prototype.
2. Recording of post-`InkStream` position/radius samples alongside normal drawing.
3. `ViewportRenderer` capable of rebuilding a 368×448 RGB565 buffer from stored strokes.
4. Camera transform abstraction.
5. Bounding-box viewport culling.
6. Synthetic benchmark generator.
7. Physical ESP32 timing results for 100 / 1,000 / 5,000-stroke cases.
8. Memory telemetry.
9. Tests.
10. A short engineering note answering:

* Is full viewport reconstruction fast enough?
* What dominates runtime?
* How much memory does the vector representation consume?
* At what document density does performance become uncomfortable?
* Do we need a spatial index?
* Do we need a pan cache?
* Do we need LOD/simplification?
* Is it now safe to plan removal of `WorldCanvas` and raster undo?

Do not proceed to replacing the production canvas architecture until those results are reviewed.
