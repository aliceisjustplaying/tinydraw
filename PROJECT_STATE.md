# TinyDraw project state

Last updated: 2026-08-09

This is the durable engineering handoff for the repository. Read it together with
`INITIAL_RESEARCH.md`, which remains the product and architecture specification. Update this file
whenever a milestone changes the current behavior, constraints, or next step.

## Current resume point

The PSRAM-backed incremental ESP-IDF/QEMU slice is complete. No physical hardware was needed.
Resume with:

```sh
git status --short
./scripts/dev test
./scripts/esp32 qemu          # automated headless Xtensa replay
./scripts/esp32 graphics      # visible QEMU framebuffer; Ctrl-A then X to close
```

The native drawing/UI loop, bounded streaming raster, ten-level dirty-tile Undo, PSRAM-backed
ESP-IDF target, and headless/graphics QEMU checks are complete. Physical panel and touch adapters
remain unimplemented. The next evidence must come from identifying and measuring the actual V1 or
V2 board; do not make speculative speed changes from QEMU timing.

Physical builds use performance optimization, 240 MHz, and octal PSRAM. Separate QEMU builds use
the same CPU/optimization settings and the emulator's required 8 MB quad-PSRAM model.
`scripts/esp32` asserts those settings. Keep native debug/release/ASan, headless QEMU, and graphics
QEMU green. Do not introduce PlatformIO or globally source ESP-IDF.

Durable implementation preferences from the user:

- prioritize something visible/runnable and end-to-end verification;
- keep modules and commits small; commit frequently and atomically;
- aggressively avoid speculative architecture and dependencies;
- keep ASan/UBSan green for all SDL-free code;
- use tldraw as the UI reference, with one default toolbar row and one popup row;
- document state meticulously before compaction.

## Current user-visible state

TinyDraw is runnable as a native macOS host prototype:

```sh
./scripts/dev run
```

Controls:

- the default full-width row is `[undo] [pen] [eraser] [color] [size] [new]`;
- color shows the selected ink and opens a second row with four large color choices;
- size shows the selected width and opens a second row with S/M/L/XL choices;
- drag with the primary mouse button to draw or erase;
- `Cmd-Z` also undoes up to ten completed operations, `C` starts an undoable new drawing, and `Esc`
  quits.

The window models a 368×448 RGB565 canvas. On a 14-inch 2021 MacBook Pro at default
scaling it opens at approximately the physical 1.8-inch panel size: a 145×177-point drawable
window. It remains resizable for inspection. Drawing currently has:

- timestamp-adaptive streamlining;
- simulated pressure and variable width;
- PF-style ribbon spans;
- round joins and caps;
- 4×4 supersampled antialiasing into 8-bit coverage;
- RGB565 compositing;
- overlap union without self-intersection holes;
- a persistent committed canvas plus dirty-tile active-stroke coverage.

The prototype is useful for visual and input-loop testing, but it is not yet the embedded product.
Its tldraw-inspired UI is intentionally direct-drawn and minimal. Undo stores ten bounded dirty-tile
before-images in caller-provided memory and is shared by host and firmware. There is no persistent
point arena or physical panel/touch driver. A real ESP-IDF ESP32-S3 target compiles the shared core,
boots under Espressif QEMU, replays the deterministic zigzag stroke, and shows the stroke plus the
shared six-control TinyDraw toolbar in QEMU's virtual framebuffer. QEMU input remains scripted
rather than interactive.

## Fast development loop

One-time host setup:

```sh
./scripts/bootstrap-macos
```

Daily commands:

```sh
./scripts/dev test          # debug build, core tests, host replay E2E tests
./scripts/dev run           # interactive SDL host
./scripts/dev asan          # ASan + UBSan on SDL-free project code
./scripts/dev release       # optimized build and all non-SDL-sanitizer tests
./scripts/dev format-check
./scripts/dev format
```

Expected current results:

- 56 doctest cases;
- 13 CTest entries, including sustained-XL/Undo characterization and process-level replay/UI/Undo
  checks;
- debug, release, ASan, and UBSan green.

The ASan preset excludes the SDL executable because Homebrew `sdl2-compat` aborts in its external
SDL3 loader under Apple's sanitizer runtime before application code starts. All SDL-free project
code is sanitized. Full host replay executes in debug and release.

## Repository and toolchain

The repository was initialized here from `INITIAL_RESEARCH.md`.

Host baseline used so far:

- ARM64 macOS;
- Apple Clang 21;
- C++20;
- CMake presets, minimum CMake 3.28;
- Ninja;
- ccache when installed;
- `sdl2-compat` through pkg-config;
- pinned doctest 2.5.3 under `third_party/`.

Build products and `compile_commands.json` stay under `out/build/<preset>/`.

ESP-IDF is pinned in `.idf-version` to v6.0.2 and remains isolated from the native build. The
current machine has v6.0.2 plus `qemu-xtensa` installed through `eim`. A new machine uses:

```sh
./scripts/bootstrap-idf
```

Do not add PlatformIO or source ESP-IDF globally. `scripts/esp32` enters the isolated environment.
The official `espressif/esp_lcd_qemu_rgb` component is locked at 1.0.2.

## Source map

### Platform-independent core

- `core/include/tinydraw/touch_point.h`
  - common `TouchPoint { x, y, timestamp_us }` input.
- `core/include/tinydraw/ink_config.h`
  - runtime brush/filter configuration and current defaults.
- `core/include/tinydraw/geometry.h`
  - logical canvas size, `Point`, `Rect`, and `PanelGeometry`.
- `core/include/tinydraw/platform/coordinate_transform.h`
  - touch-controller → logical and logical → panel transforms.
- `core/include/tinydraw/platform/display_backend.h`
  - synchronous no-retention display submission seam used by QEMU and future physical adapters.
- `core/include/tinydraw/ink/ink_stream.h`
  - active stroke lifecycle and dt-adaptive filtering/pressure.
- `core/include/tinydraw/ink/perfect_freehand.h`
  - understandable batch PF behavioral baseline used for oracle comparisons and finalization tests.
- `core/include/tinydraw/ink/ribbon_geometry.h`
  - visible-path PF-style ribbon primitives plus the fixed-capacity `RibbonStream`.
- `core/include/tinydraw/graphics/coverage_tile.h`
  - fixed 64×64 8-bit coverage tile and RGB565 compositing.
- `core/include/tinydraw/graphics/ribbon_renderer.h`
  - shared one-pass tiled primitive renderer and explicitly owned scratch arena.
- `core/include/tinydraw/graphics/stroke_raster.h`
  - bounded dirty-tile active-stroke raster, persistent coverage union, and operation statistics.
- `core/include/tinydraw/graphics/tile_undo_history.h`
  - ten fixed dirty-tile before-image entries with caller-owned storage and exact tile restore.
- `core/include/tinydraw/ui/toolbar.h`
  - platform-independent tldraw-inspired toolbar hit testing, state, sizing, and RGB565 drawing.

### Host adapter

- `host/main.cpp`
  - interactive SDL shell, replay/UI-preview CLI, toolbar behavior, ten-step dirty-tile undo,
    dirty-tile stroke orchestration, and PPM output.
- `host/input_coordinates.h`
  - SDL2-compat logical mouse-coordinate policy.

### ESP32-S3 / QEMU adapter

- `esp32/`
  - minimal ESP-IDF project compiling the same `core/src` files as Xtensa C++20;
  - deterministic seven-point incremental firmware replay with structural/work markers and an
    informational checksum;
  - `FirmwareCanvas`, which capability-allocates committed/coverage/history state in PSRAM and both
    raster/Undo tile scratch buffers in DMA-capable internal RAM;
  - `QemuDisplayBackend`, a thin `esp_lcd_qemu_rgb` implementation of `DisplayBackend`;
  - visible graphics mode receives only dirty tiles, redraws the shared toolbar before every frame,
    and refreshes once per simulated input frame;
  - locked managed-component manifest; downloaded `managed_components/` remains ignored.
- `scripts/esp32`
  - isolated physical-octal and QEMU-quad build directories, effective-config assertions, headless
    replay, and visible graphics commands.
- `tools/qemu-replay.py`
  - boots QEMU, captures completion, rejects firmware/stack failures, and checks structural values
    with bounds tolerance while treating the framebuffer checksum as informational.

### Tests and reference data

- `tests/`
  - public-interface behavior tests for transforms, input, PF reference fidelity, coverage,
    primitives, and rendering;
  - `perf_characterization.cpp`, a deterministic 1,000-sample XL operation and memory-traffic
    budget with no wall-clock assertion.
- `testdata/strokes/`
  - deterministic replay recordings, including invalid input and tight-join stress cases.
- `testdata/reference/`
  - independently generated output from pinned upstream perfect-freehand.
- `testdata/snapshots/`
  - manually reviewed host characterization images. These detect changes but are not independent
    correctness oracles.
- `tools/pf-reference.mjs`
  - regenerates upstream PF oracle fixtures and dependency observations.

## Input pipeline

The interactive path is:

```text
SDL logical mouse event
  → TouchPoint with timestamp
  → InkStream
  → adjusted InkPoint {position, pressure, radius, distance, running length}
  → RibbonStream {newly stable primitives + replacement provisional tail}
  → coverage tiles
  → RGB565 canvas
```

`InkStream` lifecycle:

```cpp
begin(point);      // exact touch-down coordinate
update(point);     // dt-adaptive filtered sample
finish(point);     // exact lift coordinate and ends the stroke
end();             // cancellation/reset without a final point
```

A new touch-down while a stroke is still active first cancels the abandoned lifecycle across
`InkStream`, `RibbonStream`, and `StrokeRaster`; it cannot weld the new stroke to stale geometry.

Important timestamp policies:

- equal timestamps use one nominal interval rather than freezing;
- decreasing timestamps use one nominal interval and do not poison the stored valid timestamp;
- natural `uint32_t` wrap-around remains supported;
- `finish()` bypasses streamline interpolation so the committed stroke reaches the release point.

Streamline behavior uses an interval-adjusted recurrence:

```text
alpha = 1 - (1 - nominal_alpha)^(dt / nominal_dt)
```

Pressure similarly normalizes distance to nominal cadence and interval-adjusts its recurrence.
`pow`/`hypot` remain deliberate readable v1 choices pending hardware profiling.

No heap allocation occurs inside `InkStream::begin`, `update`, or `finish`.

## macOS coordinate lesson

With `SDL_RenderSetLogicalSize(renderer, 368, 448)`, the current `sdl2-compat` stack supplies mouse
events in renderer-logical coordinates already. They must not be divided by the Retina window scale.

Observed failure and fix:

- incorrect policy mapped a bottom-right click to the center of the canvas;
- `host/input_coordinates.h` now accepts logical event coordinates directly and rejects values
  outside 0..367 × 0..447;
- regression tests encode center and bottom-right cases.

Do not reintroduce `SDL_RenderWindowToLogical` or window-size scaling without first proving the
actual event-coordinate contract on the installed SDL stack.

## Perfect-freehand reference and fidelity

The ignored upstream checkout is pinned to:

```text
steveruizok/perfect-freehand
176e00f2399f4969e1b0965c5921d96a3e50ce9f
```

See `reference/PERFECT_FREEHAND.md` for clone/build/regeneration commands.

Two related implementations currently coexist intentionally:

1. **Batch behavioral baseline** in `perfect_freehand.cpp`
   - ports upstream stroke-point and outline behavior;
   - matches every outline coordinate in the committed `zigzag` and `dependency-probe` oracle
     fixtures within host floating-point tolerance;
   - uses vectors and is not the intended active embedded hot path.

2. **Visible unionable ribbon path** in `ribbon_geometry.cpp`
   - consumes timestamp-aware `InkPoint`s;
   - emits simple convex spans plus circles instead of one self-intersecting outline polygon;
   - preserves pressure width and PF-style vector blending;
   - adds a round circle at every accepted interior sample to close joins;
   - falls back to two triangles if a four-point span is non-convex;
   - now has a fixed-capacity `RibbonStream` that emits only newly append-stable pieces and a
     replacement one-span tail.

The visible path is PF-style but is not yet a perfect structural reproduction of all upstream
outline suppression and corner tessellation behavior. The batch builder remains a behavioral
comparison target for the visible path. Tests prove that accumulating stream commits plus the
current provisional tail reproduces `build_pf_ribbon` after every appended point; they do not claim
structural equivalence to upstream's complete outline.

## PF dependency finding

`tools/pf-reference.mjs` reruns upstream PF after each appended input for the dependency probe.
Empirical result:

- during the first ten inputs, earlier outline radii mutate because upstream computes initial
  pressure from up to ten points;
- after warm-up, the beginning of the left outline advances with a two-point provisional tail in
  the current fixture;
- the report is evidence, not a general proof, because upstream's returned array concatenates the
  left track, end cap, reversed right track, and start cap.

`RibbonStream` does not copy upstream's mutable ten-point initial-pressure estimate. It accepts
already causal `InkPoint` radii from `InkStream`; an accepted point's radius is immutable. Under
that explicit policy, section `i` becomes stable when point `i + 1` supplies its only forward
direction dependency, and the span ending at that section can be emitted. The implementation keeps
two points and one pending section rather than a guessed sample window. Prefix-equivalence and
long-stroke tests now encode this structural commit rule.

## Raster pipeline

The one-pass `RibbonRenderer` reference path is:

```text
RibbonPrimitive[]
  → enumerate dirty 64×64 tiles from primitive bounds
  → 4×4 supersample each circle/convex piece
  → union piece coverage in one uint8 coverage tile
  → copy RGB565 canvas tile into working tile
  → composite stroke color once in stored sRGB-like RGB565 channel space
  → copy the result back to the canvas
```

`CoverageTile` is fixed at 4096 bytes. `RibbonRenderer` owns:

- one `CoverageTile` (~4 KiB);
- one 64×64 RGB565 working tile (~8 KiB).

The ~12 KiB scratch arena is a member rather than a render-function stack frame. Both
`RibbonRenderer` and `StrokeRaster` own one such arena. Embedded code must explicitly place it in
appropriate internal SRAM rather than on a modest task stack.

`StrokeRaster` additionally receives a 368×448 active-stroke coverage plane (164,864 bytes). This
belongs in PSRAM-equivalent memory, not task stack or DMA SRAM. Newly append-stable geometry is
max-unioned into that plane. Only tiles intersecting the old tail, new commits, or new tail are
regenerated for display. At lift, touched tiles are composited into RGB565 once and the active
coverage is cleared. This preserves same-stroke AA union across arbitrarily late self-overlaps
without retaining or rerasterizing the whole primitive history.

Coverage union currently uses:

```text
coverage = max(existing, incoming)
```

This is the deliberate bounded-error v1 policy from `INITIAL_RESEARCH.md`. Convex ribbon spans are
rasterized as one quad whenever possible, avoiding a translucent seam between complementary
triangle samples. Non-convex fallback triangles may still use max-union approximation at their
shared edge.

The compositor deliberately blends directly in stored RGB565/sRGB-like channel space. This is not
linear-light blending.

Malformed public primitives are rejected if:

- a circle is non-finite, unreasonably large, or has non-positive radius;
- a convex primitive has fewer than 3 or more than 4 points;
- a convex point is non-finite or unreasonably large.

Coverage entry points independently reject non-finite/extreme coordinates. Raster bounds are
clamped in floating-point space before integer conversion, preventing out-of-range float-to-int
undefined behavior. Repeated-point and collinear zero-area polygons produce no coverage.
`InkStream` ignores non-finite updates without poisoning prior state; an invalid begin leaves the
stream inactive.

## Critical committed/provisional invariant

`StrokeRaster` now enforces and directly tests the required invariant:

> The persistent canvas contains committed geometry only.

During an active stroke:

1. max-union newly stable primitives into the active 8-bit coverage plane;
2. restore only dirty visible tiles from `committed_pixels`;
3. union the current provisional tail in tile scratch and display it;
4. leave `committed_pixels` byte-for-byte unchanged.

At lift:

1. promote the final tail and cap through `RibbonStream::finish()`;
2. union that final geometry into active coverage;
3. copy each touched tile's already-loaded pre-composite pixels into the current Undo entry;
4. composite each touched tile exactly once into `committed_pixels`;
5. clear active coverage.

Cancellation clears coverage only in touched/provisional tiles, restores those tiles from the
committed canvas, and submits them to the display. This works when the optional host-visible span is
absent, as it is in firmware. `C`/New captures all 42 tiles as one undoable operation before
clearing. Core tests prove provisional pixels never enter persistence, firmware-style cancellation
repaints the display, and incremental final output matches a one-pass whole-stroke coverage union.

## Dirty-tile Undo

`TileUndoHistory` deliberately uses a simple fixed layout rather than compression or a variable-size
arena:

- ten entries × 42 tile slots × 64×64 RGB565 = 3,440,640 bytes of reserved storage;
- storage is caller-owned (`std::vector` on host, `MALLOC_CAP_SPIRAM` in firmware);
- each entry has a 42-element touched-tile map in internal memory;
- only canonical 64×64 or clamped edge tiles are accepted and copied;
- the oldest entry is overwritten after ten completed non-empty operations; empty/off-canvas
  operations preserve the existing history; there is no redo;
- stroke capture reuses the internal working tile after the committed canvas read, avoiding another
  committed-canvas read;
- Undo stages each PSRAM tile through DMA-capable internal scratch, then restores and submits it;
- New is undoable and intentionally captures the whole screen.

The fixed 3.28 MiB reservation is larger than a variable arena but guarantees ten operations,
including ten worst-case full-screen operations, and keeps ownership, eviction, and failure behavior
obvious. Together with committed RGB565 and active coverage, modeled PSRAM use is about 3.75 MiB.
QEMU proves that allocation against the 8 MiB model, not that either physical board revision has the
same bandwidth or allocator behavior.

## Durable pitfalls

- Characterization snapshots are generated by TinyDraw and are not goldens. Only upstream PF
  fixtures are independent geometry oracles.
- Current `sdl2-compat` mouse events are already renderer-logical. Scaling them again maps the
  bottom-right toward the center; the regression is directly tested.
- A whole self-intersecting PF outline creates overlap holes. Keep the visible path as unioned simple
  pieces.
- Independently rasterized triangles can expose their shared diagonal under max-union. Normal convex
  spans remain single quads; triangles are only a non-convex fallback.
- Tight turns need explicit round join coverage. `tight-joins.stroke` retains that reproduction.
- `InkStream::finish()` must preserve the exact release coordinate rather than filtering it short.
- Tile scratch and bounded primitive storage must not live on the ESP-IDF task stack. The QEMU
  harness requires a post-replay context switch to expose delayed stack-canary failures.
- Headless QEMU must not initialize `esp_lcd_qemu_rgb`; graphics mode uses a separate build and
  virtual framebuffer. Dirty tiles and toolbar pixels must share that framebuffer before one
  refresh, rather than mixing bitmap MMIO writes with framebuffer refresh.
- `scripts/esp32` locates `qemu-system-xtensa` explicitly because `eim` activation does not always
  add its nested binary directory to `PATH`.

## Test policy and current coverage

Host-only snapshots may be byte-exact under the controlled host toolchain. They currently cover:

- `zigzag.stroke`: representative changes in direction;
- `dependency-probe.stroke`: loop/self-overlap behavior;
- `tight-joins.stroke`: dense sharp joins that previously exposed white gaps.

Independent PF tests compare floating-point geometry to upstream-generated fixtures with tolerance.

Core behavior tests cover:

- coordinate transform order, scaling, clamping, and panel offsets;
- SDL logical input coordinates;
- timestamp equality, regression, wrap, cadence changes, and exact finish;
- PF stroke points, complete outlines, dots, duplicates, and stream finalization;
- streaming ribbon prefix equivalence, exact finalization, duplicate handling, and bounded output
  across 1000 points;
- circle and convex AA coverage;
- max-union idempotence;
- sRGB-space RGB565 compositing;
- ribbon primitive structure, reversals, duplicate points, and round joins;
- overlap idempotence, off-canvas geometry, malformed primitives, tile crossings, and stats;
- committed-canvas isolation, incremental/one-pass pixel equivalence, explicit memory-traffic
  accounting, and bounded XL update work;
- a 1,000-sample sustained XL path with separate update-time, lift-time, history-capture, and Undo
  traffic budgets;
- ten-entry eviction, exact dirty restore, full-screen New capture, and capture-from-raster-scratch;
- process-level draw A → draw B → Undo twice → redraw → erase/Undo → New/Undo exact-pixel flow,
  including toolbar `can_undo` state.

Process-level E2E tests cover:

- replay parsing through output image;
- reviewed stroke snapshots;
- a tldraw-style toolbar preview and characterization snapshot;
- invalid lifecycle rejection;
- invalid syntax rejection.

The QEMU process-level test checks accepted-point, primitive, touched-tile, geometry-bound,
memory-capability, frame-count, dirty-work, display-byte, and modeled-PSRAM-traffic results. The
seven-frame fixture reports 372,736 display bytes, 593,920 PSRAM reads, 438,272 writes, a
237,568-byte maximum read frame, and a 372,736-byte maximum write frame. Graphics mode additionally requires
`TINYDRAW_UI_OK canvas=1 controls=6`, emitted only after checks prove the dedicated framebuffer still
contains a white canvas, blue stroke, and shared toolbar, receives exactly the reported dirty-tile
count, and refreshes exactly once for each of seven input frames. Bounds use a tolerance; the RGB565
checksum is logged but intentionally not asserted as a cross-architecture oracle. Host and Xtensa
pixel identity is not required.

## Current performance and allocation limitations

These are known and intentionally not hidden:

- active geometry generation and raster work are bounded per input update;
- committed and provisional processing now share one active-coverage load for overlapping tiles;
- lift composites every tile touched by the stroke in one frame; this is bounded to 42 screen tiles
  today but must be measured before any larger-than-screen canvas work;
- SDL still uploads the full 368×448 texture every display loop; dirty panel submission remains for
  the display backend;
- the host allocates the active coverage plane and canvases with `std::vector`; firmware uses
  explicit PSRAM/internal capability allocations;
- `build_pf_ribbon` and batch PF reference functions still allocate vectors, but neither is in the
  interactive hot path;
- QEMU will not be accepted as cycle-accurate performance evidence;
- the firmware replay is scripted rather than driven by a real touch IRQ/task;
- the graphics build submits dirty raster tiles through `DisplayBackend`; physical-panel transport
  and measured DMA/bus behavior remain future work.

The reproduced 500-point XL prefix workload previously took 4,479 ms in an optimized host build,
with successive 50-point blocks growing from 41 ms to 1,040 ms. Through `StrokeRaster`, the same
workload took 220 ms total in one local run and no longer grew with stroke history. This is a host
comparison, not ESP32 timing evidence.

`./scripts/dev perf` now supplies the durable characterization: 1,000 XL samples over an
eight-second-equivalent continuous path, 2,052 tile submissions total, max 4 tiles and 16 primitive
visits per update, max 48 KiB PSRAM reads and 16 KiB writes per update. Lift touches 30 tiles, reads
357,376 raster bytes, and writes 592,896 external-memory bytes including 235,520 bytes of history.
Undo restores and submits those same 30 tiles: 235,520 history bytes read, canvas bytes written, and
display bytes. CI locks down these operation/traffic ceilings rather than fragile wall-clock
thresholds. The burst is screen-area-bounded, not stroke-length-bounded, but would need redesign
before a much larger backing canvas.

The active embedded memory model and partial submission path now run in QEMU. `DisplayBackend`
submissions have a synchronous no-retention contract, so a physical adapter must finish or stage DMA
before returning. Physical panel/touch adapters and real-board DMA/performance evidence remain
unimplemented.

## Next engineering step

When hardware arrives:

1. identify V1 (SH8601 + FT3168) versus V2 (CO5300 + CST820) before selecting adapters;
2. confirm the physical octal-PSRAM build boots with the expected capacity;
3. validate touch transforms/cadence and panel offsets, rotation, color order, and partial writes;
4. measure sustained XL drawing, lift, dirty Undo, DMA/bus behavior, power, and visual AA quality.

Keep drawing behavior unchanged until those measurements expose a real bottleneck. Only add queues,
tasks, deeper history, runtime tuning, or larger-canvas machinery when a measured hardware or host
loop needs them. The synchronous replay is intentionally simpler than a speculative firmware task
graph.
