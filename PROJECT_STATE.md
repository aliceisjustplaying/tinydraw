# TinyDraw project state

Last updated: 2026-08-09

This is the durable engineering handoff for the repository. Read it together with
`INITIAL_RESEARCH.md`, which remains the product and architecture specification. The complete
high-effort whole-codebase review is preserved in `OPUS_REVIEW_2026-08-09.md`. Update this file
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

The firmware now allocates its 329,728-byte committed RGB565 canvas and 164,864-byte active
coverage plane with explicit `MALLOC_CAP_SPIRAM`, allocates `StrokeRaster` and tile scratch with
`MALLOC_CAP_INTERNAL`, and verifies all three pointer classes at runtime. The seven fixture touches
flow through `StrokeRaster` one frame at a time: 46 dirty-tile submissions total, at most 17 in a
frame, and exactly seven graphics refreshes. The process harness requires memory, structure,
bounded-work, and UI markers.

The first performance pass is complete. `StrokeRaster::update` now processes overlapping committed
and dirty flags in one tile load while still storing committed coverage before adding the
provisional tail. On the sustained XL workload, active-coverage reads fell from 15,739,904 to
8,213,504 bytes (47.8%), and max per-update PSRAM reads fell from 64 to 48 KiB with identical pixels.

Stop making speculative speed changes until hardware supplies cycle/latency evidence.
The sustained-XL harness has exposed a separate screen-bounded lift-time burst (30 tiles and 357,376
bytes each direction); measure it on hardware before deciding whether to spread finalization across
frames. Shared one-step Undo/New history follows performance work unless hardware arrives first.

Our QEMU commands override Espressif's 32 MB default with an 8 MB quad-PSRAM model and assert that
size at boot. That is valid integration evidence for IDF's capability allocator, but not evidence
for the Waveshare board's physical PSRAM mode, capacity,
bandwidth, DMA behavior, or timing. Verify those on the exact board revision. Do not use QEMU
wall-clock speed as performance evidence. Keep native debug/release/ASan, headless QEMU, and
graphics QEMU green. Do not introduce PlatformIO or globally source ESP-IDF.

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
- `Cmd-Z` also undoes, `C` starts a new drawing, and `Esc` quits.

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
Its tldraw-inspired UI is intentionally direct-drawn and minimal. Undo is currently one host
snapshot rather than a bounded embedded snapshot ring. There is no persistent point arena or
hardware driver. A real ESP-IDF ESP32-S3 target now compiles the shared core, boots under Espressif
QEMU, replays the deterministic zigzag stroke, and shows the stroke plus the shared six-control
TinyDraw toolbar in QEMU's virtual framebuffer. QEMU input remains scripted rather than interactive.

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

- 49 doctest cases;
- 12 CTest entries, including sustained-XL characterization and process-level replay/UI checks;
- debug, release, ASan, and UBSan green;
- incremental debug suite typically well below one second.

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
  - narrow display submission seam for later host/QEMU/hardware adapters.
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
- `core/include/tinydraw/ui/toolbar.h`
  - platform-independent tldraw-inspired toolbar hit testing, state, sizing, and RGB565 drawing.

### Host adapter

- `host/main.cpp`
  - interactive SDL shell, replay/UI-preview CLI, toolbar behavior, one-step host undo, dirty-tile
    stroke orchestration, and PPM output.
- `host/input_coordinates.h`
  - SDL2-compat logical mouse-coordinate policy.

### ESP32-S3 / QEMU adapter

- `esp32/`
  - minimal ESP-IDF project compiling the same `core/src` files as Xtensa C++20;
  - deterministic seven-point incremental firmware replay with structural/work markers and an
    informational checksum;
  - `FirmwareCanvas`, which capability-allocates committed/coverage state in modeled PSRAM and the
    raster/tile scratch in internal RAM;
  - `QemuDisplayBackend`, a thin `esp_lcd_qemu_rgb` implementation of `DisplayBackend`;
  - visible graphics mode receives only dirty tiles, refreshes once per simulated input frame, and
    renders the shared toolbar into QEMU's own RGB565 framebuffer;
  - locked managed-component manifest; downloaded `managed_components/` remains ignored.
- `scripts/esp32`
  - isolated build, headless QEMU assertion, and visible graphics commands.
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
current provisional tail reproduces the batch builder after every appended point.

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

Current logical stroke rendering:

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
3. composite each touched tile exactly once into `committed_pixels`;
4. clear active coverage.

`C`/New and cancellation discard active coverage and restore the committed canvas. A core test
proves provisional pixels never enter persistence; another compares incremental final output to a
one-pass whole-stroke coverage union.

## Bugs found and lessons retained

### Snapshot terminology

The first output image was called a golden despite being generated by our own code. It was renamed
to a characterization snapshot. Only upstream PF fixtures are independent geometry oracles.

### Cursor mapped to northwest / bottom-right mapped to center

An initial Retina hypothesis led to manual window scaling, which did not change the bug. A direct
user probe—single click at bottom-right—proved SDL events were already logical. The second scaling
was removed and the exact case was regression-tested.

### Self-overlap holes

Filling upstream PF's whole returned outline as one even-odd polygon produced large holes where a
stroke crossed itself. The visible path now emits and coverage-unions simple pieces. Never restore a
whole self-intersecting polygon raster path.

### Internal diagonal seams

Splitting every normal ribbon span into two independently sampled triangles could leave partial
coverage along the shared diagonal under max-union. Convex spans now rasterize as one quad;
triangles are only a non-convex fallback.

### White slits and speckles at tight joins

Cross-sections alone left uncovered wedges at frequent tight turns. Every accepted interior stream
point now emits explicit round join coverage. `tight-joins.stroke` and its reviewed snapshot retain
the reproduction.

### Exact release endpoint

Filtering the final mouse-up coordinate shortened completed strokes. `InkStream::finish()` now
forces the exact release coordinate while preserving the normal timestamp/pressure update.

### Embedded stack pressure

The first tile renderer held roughly 12.6 KiB of working buffers in one function frame. Scratch is
now owned by `RibbonRenderer`, allowing static/internal-SRAM placement. The first QEMU firmware
replay also kept its bounded primitive array on ESP-IDF's main-task stack; QEMU completed the replay
and then reported a stack-canary failure. Moving that array to static storage fixed it. The QEMU
harness waits for a post-replay FreeRTOS context switch before accepting completion so this class of
failure is not hidden.

### QEMU framebuffer requires graphics mode

`esp_lcd_qemu_rgb` is a QEMU-only MMIO device. Constructing it in a `-nographic` machine without the
virtual framebuffer blocked before replay. The headless build now leaves the display pointer null;
a separate graphics build enables `TINYDRAW_QEMU_GRAPHICS`, instantiates `QemuDisplayBackend`, and
runs QEMU with `--graphics`. Keep headless integration and visible display checks separate.

The first graphics implementation mixed `esp_lcd_panel_draw_bitmap()` tile submissions with a final
`esp_lcd_rgb_qemu_refresh()`. The component's source shows that each bitmap call is a synchronous
emulated MMIO update with a busy-wait, while refresh redraws from a separate dedicated framebuffer.
This made the stroke paint progressively over roughly two seconds, then replaced it with the
mostly-black direct framebuffer containing only the toolbar. `QemuDisplayBackend::push_rect()` now
copies tiles into the dedicated framebuffer; the toolbar draws into the same memory and one refresh
submits the complete frame. This delay was therefore an avoidable adapter bug amplified by QEMU,
not useful hardware performance evidence.

The isolated `eim` installation places on-request QEMU below a nested tools directory that its
activation PATH does not include automatically. The Python test harness already located that binary,
but the first interactive `graphics` command bypassed the harness and failed with
`qemu-system-xtensa is not installed`. `scripts/esp32` now locates the executable and prepends its
parent directory for every QEMU action. The exact visible command was verified through replay
completion under a timeout.

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
- a 1,000-sample sustained XL path with separate update-time and lift-time budgets.

Process-level E2E tests cover:

- replay parsing through output image;
- reviewed stroke snapshots;
- a tldraw-style toolbar preview and characterization snapshot;
- invalid lifecycle rejection;
- invalid syntax rejection.

The QEMU process-level test checks accepted-point, primitive, touched-tile, geometry-bound,
memory-capability, frame-count, dirty-work, display-byte, and modeled-PSRAM-traffic results. The
seven-frame fixture reports 372,736 display bytes, 593,920 PSRAM reads, 303,104 writes, and a
237,568-byte per-frame maximum in either direction. Graphics mode additionally requires
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
visits per update, max 48 KiB PSRAM reads and 16 KiB writes per update. Lift touches 30 tiles and
reads/writes 357,376 bytes. CI locks down these operation/traffic ceilings rather than fragile
wall-clock thresholds. The lift burst is screen-area-bounded, not stroke-length-bounded, but would
need redesign before a much larger backing canvas.

The active embedded memory model and partial submission path now run in QEMU. Physical panel/touch
adapters and real-board capability, DMA, and performance evidence remain unimplemented.

## Exact next engineering steps

### 1. Completed: ESP-IDF / QEMU vertical slice

The ESP32-S3 target compiles every shared core source as Xtensa C++20, boots with modeled quad
PSRAM, and incrementally replays seven input frames. Its accepted result is:

```text
accepted=7 primitives=13 tiles=14 bounds=27.83,37.83,341.44,411.44
frames=7 dirty=46 max_tiles=17 visits=133
 display=372736 psram_read=593920 psram_write=303104
 max_psram_read=237568 max_psram_write=237568
```

The RGB565 FNV checksum remains informational. `./scripts/esp32 qemu` verifies modeled PSRAM boot,
external/internal capability placement, structure, bounded work, and a post-replay FreeRTOS context
switch. `./scripts/esp32 graphics-test` additionally verifies 46 dirty submissions, seven refreshes,
and final toolbar/canvas pixels. `./scripts/esp32 graphics` leaves the window open for inspection.

The framebuffer adapter remains behind `TINYDRAW_QEMU_GRAPHICS`: initializing
`esp_lcd_qemu_rgb` without QEMU's `--graphics` device blocks. The visible build does not allocate a
second firmware-visible framebuffer. Host debug/release/ASan, headless QEMU, and graphics QEMU were
green at this milestone.

QEMU proves the application uses IDF's capability allocator correctly against our configured 8 MB
quad-PSRAM model. It does not prove the physical board's memory wiring, usable capacity,
DMA behavior, timing, or panel correctness.

### 2. Completed: remove duplicate coverage reads

`StrokeRaster::update` now loads each relevant tile once, stores stable coverage, then adds the
provisional tail only to scratch before display composition. Sustained XL active-coverage reads fell
47.8%; max update PSRAM reads fell 25%. Display traffic, writes, geometry visits, and pixels stayed
unchanged.

### 3. Next: shared one-step history

Move current Undo/New snapshot semantics out of the SDL shell and add the deterministic full-flow
E2E described above. Start with one snapshot; no generic command framework, arbitrary-depth history,
or point arena.

### 4. Later software tuning

Only add runtime tuning controls, deeper history, queues/tasks, or more instrumentation when a real
host/hardware loop consumes them. The current synchronous replay is intentionally simpler than a
speculative firmware task graph.

### 5. Physical hardware integration

When the board arrives, validate what QEMU cannot:

- touch-controller initialization, transforms, cadence, and finger feel;
- AMOLED controller offsets, rotation, color order, and partial-window writes;
- actual PSRAM/DMA capability restrictions and bandwidth;
- latency, sustained XL strokes, thermal/power behavior, and visual AA quality.

## Commit milestone history

The repository history was rewritten once to replace placeholder author and committer metadata with
`alice <aliceisjustplaying@gmail.com>`, so older handoff documents and chat transcripts may contain
obsolete commit IDs. The commit subjects and trees were preserved. Use Git as the source of truth:

```sh
git log --reverse --oneline
git log --format='%an <%ae> | %cn <%ce>' | sort -u
```

A high-effort Opus review verified the foundation and identified small correctness preconditions
before streaming work. Float-to-int raster bounds, zero-area convex coverage, non-finite stream
input, and PPM attributes were fixed immediately afterward. Geometry streaming and the dirty-tile
raster horizon are now implemented and directly tested. See `OPUS_REVIEW_2026-08-09.md` for the
review evidence and priorities.

The misleading intermediate cursor-scaling and polygon-rendering approaches remain in history as
useful diagnosis context but are not present in the current tree.
