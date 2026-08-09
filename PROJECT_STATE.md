# TinyDraw project state

Last updated: 2026-08-09

This is the durable engineering handoff for the repository. Read it together with
`INITIAL_RESEARCH.md`, which remains the product and architecture specification. The complete
high-effort whole-codebase review is preserved in `OPUS_REVIEW_2026-08-09.md`. Update this file
whenever a milestone changes the current behavior, constraints, or next step.

## Current user-visible state

TinyDraw is runnable as a native macOS host prototype:

```sh
./scripts/dev run
```

Controls:

- use the floating bottom dock to select pen, eraser, one of four colors, or cycle S/M/L sizes;
- use the small dock above it for one-step undo or a new blank drawing;
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
- separate committed and provisional host framebuffers.

The prototype is useful for visual and input-loop testing, but it is not yet the embedded product.
Its tldraw-inspired UI is intentionally direct-drawn and minimal. Undo is currently one host
snapshot rather than a bounded embedded snapshot ring. There is no persistent point arena,
ESP-IDF target, QEMU target, or hardware driver.

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

- 43 doctest cases;
- 11 CTest entries, including process-level replay and UI snapshot checks;
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

ESP-IDF is pinned in `.idf-version` to v6.0.2 but is intentionally isolated from the native build.
Install it later with:

```sh
./scripts/bootstrap-idf
```

Do not add PlatformIO or source ESP-IDF globally. QEMU has not been installed or integrated yet.

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
  - shared tiled primitive renderer and explicitly owned scratch arena.
- `core/include/tinydraw/ui/toolbar.h`
  - platform-independent tldraw-inspired toolbar hit testing, state, sizing, and RGB565 drawing.

### Host adapter

- `host/main.cpp`
  - interactive SDL shell, replay/UI-preview CLI, toolbar behavior, one-step host undo, temporary
    full host canvas, provisional/committed orchestration, and PPM output.
- `host/input_coordinates.h`
  - SDL2-compat logical mouse-coordinate policy.

### Tests and reference data

- `tests/`
  - public-interface behavior tests for transforms, input, PF reference fidelity, coverage,
    primitives, and rendering.
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

The ~12 KiB scratch arena is a member of `RibbonRenderer`, not a render-function stack frame. The
host places its renderer in static storage. Embedded code must explicitly place the renderer in
appropriate internal SRAM rather than on a modest task stack.

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

The host currently models the required invariant:

> The persistent canvas contains committed geometry only.

During an active stroke:

1. copy `committed_pixels` to temporary `pixels`;
2. accumulate append-stable geometry from `RibbonStream` and replace only its provisional tail;
3. render the active geometry into temporary `pixels` and display it;
4. do not write that provisional result to `committed_pixels`.

At lift:

1. call `InkStream::finish()` and `RibbonStream::finish()`;
2. promote the final tail and cap in the geometry stream;
3. render the completed stroke once into `committed_pixels`;
4. clear active host geometry.

`C` resets both streams, clears active geometry, and clears committed pixels.

This invariant is structurally present in the host, but the definitive regression test should live
in the upcoming reusable committed-canvas/tile module rather than remain coupled to SDL code.

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
now owned by `RibbonRenderer`, allowing static/internal-SRAM placement.

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
- overlap idempotence, off-canvas geometry, malformed primitives, tile crossings, and stats.

Process-level E2E tests cover:

- replay parsing through output image;
- reviewed stroke snapshots;
- a tldraw-style toolbar preview and characterization snapshot;
- invalid lifecycle rejection;
- invalid syntax rejection.

Cross-target tests do not exist yet. When added, compare structure/bounds/counts with tolerances—not
host and Xtensa pixel identity.

## Current performance and allocation limitations

These are known and intentionally not hidden:

- the host copies the full 368×448 framebuffer for every displayed provisional frame;
- ribbon geometry generation is now incremental, but the host retains the emitted primitives in a
  `std::vector` and rerasterizes the full active stroke each frame;
- dirty tile enumeration uses one global primitive bounding rectangle, so a long diagonal visits
  many untouched tiles;
- every visited tile scans every primitive;
- `build_pf_ribbon` uses temporary `std::vector`s for unique points, vectors, sections, and output;
- batch PF reference functions also allocate vectors;
- a 1000-point headless debug replay was approximately 0.08 seconds on the current Mac, but that
  measures one final render—not interactive per-frame rerasterization—and is not evidence of ESP32
  performance;
- QEMU will not be accepted as cycle-accurate performance evidence.

The active embedded path is therefore not ready. Do not carry host vectors, full-canvas copies, or
whole-stroke rerasterization into ESP32 integration.

## Exact next engineering steps

### 1. Dirty-tile provisional renderer and raster horizon

Stop rebuilding the full stroke and copying the full framebuffer each frame.

- determine the raster commit horizon separately from the proven geometry horizon;
- avoid compositing overlapping antialiased pieces into RGB565 on separate updates, which would
  darken edges even when each geometry piece is stable;
- return dirty bounds from geometry updates;
- enumerate only intersecting tiles;
- maintain distinct newly committed and provisional coverage;
- regenerate only provisional tiles from committed background;
- test directly that provisional pixels never enter persistent canvas;
- track tiles, pixels, coverage operations, and display bytes.

### 2. Bounded stroke storage

Add fixed-size chunks in a bounded arena, with explicit limits and overflow policy. The active path
must not depend on `std::vector` allocation succeeding.

### 3. Finish host-visible product behavior

The host now has four colors, pen/eraser tools, three sizes, one-step undo, and a new-drawing
button. After streaming rendering is stable:

- move new/clear behavior through the shared canvas module;
- replace the host-only one-step snapshot with a bounded undo ring;
- add runtime tuning controls and stats only where they aid hardware tuning.

### 4. ESP-IDF and QEMU

Only after the native memory and streaming semantics are enforceable:

- scaffold ESP32-S3 IDF target;
- install Espressif QEMU;
- use `esp_lcd_qemu_rgb` behind `DisplayBackend`;
- allocate committed canvas as PSRAM-equivalent;
- place tiles and DMA source buffers explicitly in internal memory;
- add capability assertions;
- replay the same stroke fixtures and compare structural stats with tolerance.

## Commit milestone history

Chronological commits through this handoff:

```text
76a6177 chore: bootstrap native development loop
13cc4d0 chore: pin perfect-freehand reference
fd5c98e fix: keep sanitizer preset SDL-free
41ce512 feat: add timestamp-aware ink stream
f9ccded test: add end-to-end stroke replay golden
b78315e fix: handle regressing touch timestamps
de16b72 fix: enforce input lifecycle end to end
99f9554 test: label replay image as characterization snapshot
bfe726e test: add perfect-freehand reference oracle
71b5789 feat: port perfect-freehand stroke points
41f2bd7 feat: port perfect-freehand outline geometry
05ba5a6 feat: render PF geometry end to end
992af1d fix: preserve exact stroke endpoint on lift
cfbd5e1 fix: map Retina mouse coordinates in window space
376b922 fix: union overlapping stroke segments
be48c6b fix: trust SDL logical mouse coordinates
75f0e9b feat: add antialiased coverage tiles
188fb77 feat: emit unionable PF ribbon primitives
ad16176 fix: keep release coverage build warning-free
a6325f1 feat: render PF ribbons through coverage tiles
07ff533 fix: rasterize convex ribbon spans without seams
17b5704 fix: move ribbon scratch buffers off task stack
6c15de4 fix: close coverage gaps at ribbon joins
6cd7bd9 docs: record complete engineering handoff
1120a05 docs: preserve Opus whole-codebase review
3fe4ad1 chore: mark PPM snapshots as binary
d9a1960 fix: reject unsafe raster and input degenerates
4d2d70d feat: stream append-stable ribbon geometry
81fe7d7 test: prove ribbon stream finalization bounds
dcebd49 feat: drive host geometry through ribbon stream
94f0b73 docs: record streaming geometry milestone
9ea62f5 feat: add tldraw-inspired drawing toolbar
bbc6c54 feat: wire toolbar drawing controls
831da0d chore: open host near hardware scale
a7523da fix: match host window to physical display size
61b5f41 test: snapshot toolbar end to end
```

A high-effort Opus review verified the foundation and identified small correctness preconditions
before streaming work. Float-to-int raster bounds, zero-area convex coverage, non-finite stream
input, and PPM attributes were fixed immediately afterward. Geometry streaming is now implemented;
the direct persistent-canvas invariant test remains part of the upcoming raster-horizon milestone.
See `OPUS_REVIEW_2026-08-09.md` for the review evidence and priorities.

The misleading intermediate cursor-scaling and polygon-rendering approaches remain in history as
useful diagnosis context but are not present in the current tree.
