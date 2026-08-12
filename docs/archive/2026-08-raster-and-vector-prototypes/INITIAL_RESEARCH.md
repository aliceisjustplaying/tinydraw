# TinyDraw ESP32-S3 — Engineering Handoff

## 1. Goal

Build a tiny, low-latency finger-drawing app for the **Waveshare ESP32-S3 Touch AMOLED 1.8"**.

Target experience:

* draw directly with a finger
* very good-feeling freehand ink
* ~4 colors
* undo
* clear
* optionally 2–3 brush sizes
* no selection
* no shapes
* no infinite canvas
* no editor complexity

Think: **the smallest useful subset of tldraw, focused entirely on drawing feel**.

Expected visible display resolution:

```text
368 × 448
```

Hardware will arrive later. Build as much as possible beforehand using:

1. native ARM64 macOS host tests
2. ESP32-S3 QEMU
3. real ESP32-S3 hardware when available

---

# 2. Hardware assumptions

Target board:

**Waveshare ESP32-S3 Touch AMOLED 1.8"**

Known hardware:

```text
MCU:      ESP32-S3, up to 240 MHz
SRAM:     512 KB internal
PSRAM:    8 MB
Flash:    16 MB
Display:  368 × 448 AMOLED
```

Board revisions:

```text
V1:
  Display: SH8601
  Touch:   FT3168

V2:
  Display: CO5300
  Touch:   CST820
```

Current boards are likely V2, but **verify the physical revision before final hardware integration**.

Display and touch controllers must remain behind platform interfaces.

---

# 3. Freehand algorithm

Use **Steve Ruiz’s `perfect-freehand`** as the behavioral/reference basis.

Repository:

```text
steveruizok/perfect-freehand
```

It is MIT licensed.

Do not embed JavaScript.

Port the relevant behavior into C++.

Important behavior to preserve:

* causal point streamlining
* simulated pressure
* pressure-to-radius behavior
* left/right ribbon generation
* sharp-corner handling
* start/end caps
* outline spacing/smoothing behavior

Modern tldraw has a newer optimized implementation, especially:

```text
tldraw/tldraw PR #9154
perf(draw): optimize the freehand ink algorithm
```

Useful architectural ideas from it:

* struct-of-arrays
* reusable buffers
* no per-point heap allocations
* direct ingest into buffers
* fused radius/pressure/taper passes
* derive vectors instead of retaining redundant objects
* outline simplification
* adaptive cap tessellation

Its reported improvement was roughly:

```text
~1.9× overall
up to ~2.7× for long strokes
```

Do **not** clone/reimplement modern tldraw first.

Start with a clean, understandable PF-compatible implementation, profile on hardware, then apply those optimization ideas only where measurements justify them.

Use `float`, not `double`, for hot-path geometry on ESP32-S3.

---

# 4. Initial ink parameters

Starting values only:

```cpp
size       = 6.0f;
thinning   = 0.55f;
smoothing  = 0.55f;
streamline = 0.35f;
simulatePressure = true;
```

All feel-related parameters must be runtime-adjustable.

Use one configuration object:

```cpp
struct InkConfig {
    float size;
    float thinning;
    float smoothing;
    float streamline;

    float pressureRate;
    float nominalDtMs;

    bool simulatePressure;
    bool antialias;
    bool endTaper;
};
```

Eventually expose these through:

* serial console commands
* and/or hidden debug UI

Final values come from real hardware testing.

---

# 5. Input representation

All input immediately becomes a common type:

```cpp
struct TouchPoint {
    float x;
    float y;
    uint32_t timestamp_us;
};
```

The core must not know whether the point came from:

* Mac mouse
* replay file
* QEMU
* FT3168
* CST820

---

# 6. Coordinate systems and transforms

Do not let physical-controller coordinates leak into the ink engine.

There are at least three coordinate spaces:

```text
touch-controller coordinates
        ↓
logical canvas coordinates
        ↓
panel-controller coordinates
```

The ink engine operates **only in logical canvas coordinates**:

```text
x = 0..367
y = 0..447
```

Platform code owns touch orientation/calibration and panel addressing.

Provide explicit structures such as:

```cpp
struct PanelGeometry {
    int xOffset;
    int yOffset;
};

struct TouchTransform {
    bool swapXY;
    bool mirrorX;
    bool mirrorY;

    // Add scale/calibration terms later if hardware requires them.
};
```

A physical panel may require controller RAM offsets even when the visible canvas begins at logical `(0, 0)`.

Likewise, touch orientation may be:

* swapped X/Y
* mirrored horizontally
* mirrored vertically
* rotated relative to the display

Do not compensate for any of this inside ink geometry.

Correct transformation order should be explicit and covered by tests.

---

# 7. Sample-rate independence

Original perfect-freehand behavior is **sample-rate dependent**.

Its simulated pressure uses distance between consecutive samples.

Its streamline filter is also effectively a per-sample recurrence.

Therefore irregular touch report intervals can change:

* smoothing / apparent lag
* simulated thickness

Timestamps are first-class from day one.

## Default approach

Use **dt-adaptive filtering**.

For EMA-like behavior, derive an interval-adjusted coefficient rather than applying an identical coefficient for every sample.

Conceptually:

```cpp
alpha = 1.0f - powf(base, dt / nominal_dt);
```

Adapt the pressure recurrence similarly.

Goal:

```text
same physical gesture
≈ same resulting stroke
despite modest touch-report timing variation
```

## Alternate input policy

Also support fixed-time resampling:

```text
raw timestamped input
        ↓
fixed cadence resampler
        ↓
PF-compatible stream
```

Useful for:

* deterministic replay
* comparisons
* regression tests

Do not make resampling the primary interactive path unless hardware testing shows it feels better, because it can add latency.

---

# 8. Floating-point determinism

Do **not** expect bit-identical floating-point output across ARM64 host and Xtensa.

Possible differences include:

* fused operations
* compiler contraction choices
* `libm` implementations
* `powf`
* accumulated floating-point rounding

Testing policy:

### Host-only golden tests

Host golden-image tests may be bit-exact when run under the same controlled build/toolchain.

### Cross-target tests

Host ↔ ESP32/QEMU comparisons use tolerances.

Compare things such as:

```text
point coordinates within epsilon
radii within epsilon
outline count
geometry bounds
dirty rectangle
tile count
coverage bounds
```

Do not require pixel-for-pixel equality between ARM64 and Xtensa.

If stronger cross-target determinism later becomes valuable, consider replacing `powf` in the dt-adaptive coefficient with:

* a controlled approximation
* or a small dt-indexed lookup table

That is not required for v1.

---

# 9. Latency architecture

Drawing feel is primarily an end-to-end latency problem:

```text
touch interrupt
    ↓
I²C touch read
    ↓
filter / ink update
    ↓
geometry
    ↓
rasterize
    ↓
QSPI transfer
    ↓
pixels appear
```

Keep this pipeline short.

For real touch:

1. timestamp at IRQ arrival
2. ISR wakes/notifies touch task
3. ISR exits immediately
4. touch task performs I²C read
5. touch task emits `TouchPoint`

Do not perform floating-point ink math in the ISR.

Do not perform I²C transactions in the ISR.

---

# 10. Streaming perfect-freehand

Do **not** call a pure whole-stroke:

```cpp
getStroke(allPoints)
```

after every new sample.

That creates O(n²) total work over a long stroke.

PF is largely causal and should be implemented as streaming state.

Maintain something like:

```cpp
struct InkStreamState {
    // previous raw / adjusted points
    // running length
    // previous pressure
    // previous vectors
    // previous accepted left/right points
    // corner state
    // provisional tail geometry
};
```

## Geometry commit horizon

With end taper disabled:

* adjusted point `i` depends only on earlier samples
* simulated pressure is causal
* outline construction has limited forward dependency for corner handling
* end cap remains provisional until the stroke ends

Do **not** hardcode a guessed window such as “last 12 samples.”

Instead:

1. inspect the current PF implementation
2. determine exact forward/lookahead dependencies
3. encode the commit rule structurally

Expected shape:

```text
committed geometry prefix
+
small provisional geometry tail
+
regenerated end cap
```

Before relying on a two-point horizon, verify the current `getStrokeOutlinePoints` source and add a regression test proving that appended samples cannot mutate geometry older than the chosen boundary.

Keep **end taper disabled in v1** because a total-length-dependent end taper makes earlier geometry mutable.

---

# 11. Separate geometry and raster horizons

Treat these as distinct.

## Geometry commit horizon

Determines which generated ribbon primitives can never change again.

## Raster horizon

A changing centerline/ribbon primitive can affect pixels approximately one maximum stroke radius beyond its center.

Therefore recent geometry may invalidate pixels farther back spatially than the geometry's logical commit index suggests.

Keep these concepts explicit.

---

# 12. Critical canvas invariant

This invariant must hold everywhere:

> **The persistent PSRAM canvas contains committed geometry only.**

It must never contain the currently provisional tail.

Otherwise a provisional tail rendered on one update becomes part of the background on the next update and gets composited again, creating accumulated edge darkening and stale pixels when the tail moves.

There are therefore two categories of raster output:

```text
committed coverage
provisional coverage
```

Committed coverage may be permanently written into the PSRAM canvas.

Provisional coverage may appear:

* in temporary SRAM coverage/render tiles
* on the physical display

but **must not be baked into the PSRAM committed canvas**.

When geometry crosses the commit horizon, it transitions from provisional to committed and may then be folded into the persistent canvas.

At stroke end:

1. final point/lift is processed
2. final cap is generated
3. all remaining provisional geometry becomes committed
4. affected canvas tiles are permanently updated
5. final pixels are pushed to the display

This invariant should have tests.

---

# 13. Rasterization contract

Use one shared raster backend:

```text
geometry primitives
    ↓
8-bit coverage tile
    ↓
composite once
    ↓
RGB565 tile
```

## Coverage tile

Use 8-bit per-pixel coverage:

```cpp
uint8_t coverage[TILE_W * TILE_H];
```

Multiple pieces belonging to the same logical stroke update are unioned into this mask before compositing.

A practical v1 union may use:

```text
coverage = max(existing, incoming)
```

This is not exact area-union math, but its error is bounded and non-accumulating.

Most importantly, do **not** repeatedly alpha-blend overlapping stroke primitives directly into RGB565.

That causes overlapping partially covered pixels to darken repeatedly, producing edge beading/scalloping.

Composite onto the RGB565 tile once after coverage accumulation.

---

# 14. Color-space decision

For v1, antialias coverage blending is performed directly in the stored **sRGB-like RGB565 channel space**, not converted to linear-light RGB first.

That is intentional.

Reasons:

* substantially simpler
* cheaper
* common for embedded UI rendering
* differences are unlikely to justify the cost at this display size and use case

Record this as a deliberate decision rather than an accidental implementation detail.

If stroke edges later appear systematically too dark/thin/light, linear-light compositing may be tested as an experiment.

---

# 15. Primary geometry front-end: PF ribbon

Primary path should preserve Steve/PF geometry.

Generate:

```text
left outline track
right outline track
corner arcs
start cap
end cap
```

Do not construct one giant self-intersecting polygon that then requires sophisticated triangulation.

Instead rasterize simpler pieces:

```text
quad:
left[i]
left[i+1]
right[i+1]
right[i]

+

corner arc pieces

+

cap pieces
```

Union each piece into the coverage tile.

This naturally supports incremental rasterization and avoids relying on polygon winding behavior for self-intersections.

---

# 16. Alternate geometry front-end: capsules

Keep a second deliberately simple implementation:

```text
variable-radius round-capped segments / capsules
```

This does **not** reproduce PF exactly.

Differences include:

* sharp corners
* cap construction
* outline point suppression
* taper behavior

It remains useful as:

* performance floor
* fallback implementation
* visual comparison
* debugging tool

Both feed the same coverage rasterizer.

Conceptually:

```cpp
class StrokeGeometry {
public:
    virtual DirtyGeometry update(const InkStreamState&) = 0;
};
```

Implement:

```text
PerfectFreehandGeometry
CapsuleGeometry
```

---

# 17. Antialiasing

Design for antialiasing from the beginning.

Coverage mask is the natural location for it.

Possible approaches:

* simple supersampling
* analytic/simple convex coverage
* fixed-point subpixel rasterization

Start with correctness and readability.

Do not prematurely build specialized SIMD or aggressive fixed-point code.

RGB565 composite:

```text
coverage 0–255
      ↓
unpack RGB565 destination
      ↓
sRGB-space blend
      ↓
repack RGB565
```

Optimize only after hardware profiling.

---

# 18. Framebuffer / memory architecture

A full RGB565 image is:

```text
368 × 448 × 2
= 329,728 bytes
≈ 322 KiB
```

Do not put this in internal SRAM by default.

Use:

```text
persistent COMMITTED canvas → PSRAM
hot working tiles           → internal SRAM
coverage tiles              → internal SRAM
DMA source buffers          → DMA-capable internal SRAM
```

Initial tile size:

```text
64 × 64 RGB565
= 8192 bytes

two RGB565 tiles
= 16 KB

one coverage tile
= 4096 bytes
```

Double-buffer RGB output from the start.

## Correct tile pipeline

For an update affecting a tile:

```text
1. Copy committed tile from PSRAM canvas → SRAM working tile.

2. Rasterize currently visible provisional geometry
   into SRAM coverage.

3. Composite provisional coverage onto the temporary SRAM copy.

4. Push temporary RGB565 result to panel.

5. Separately determine what geometry has newly crossed
   the commit horizon.

6. Rasterize/composite ONLY newly committed geometry
   into the persistent PSRAM canvas.
```

Do **not** write the entire temporary visible tile back to PSRAM.

The visible tile may contain provisional geometry.

At stroke end, all remaining provisional geometry becomes committed and is permanently folded into PSRAM.

Implementation may optimize this later, but it must preserve the invariant:

```text
PSRAM canvas == committed drawing only
```

---

# 19. Display backend

Keep the interface narrow.

Conceptually:

```cpp
class DisplayBackend {
public:
    virtual void pushRect(
        int x,
        int y,
        int w,
        int h,
        const uint16_t* rgb565
    ) = 0;

    virtual bool busy() const = 0;
};
```

The core must not know about:

* QEMU RGB framebuffer
* QSPI commands
* controller offsets
* CO5300
* SH8601
* SPI DMA setup

Platform code applies logical-canvas → panel-controller coordinate conversion.

---

# 20. DMA safety

QEMU may not faithfully enforce real ESP32 memory/DMA restrictions.

Therefore validate display-buffer assumptions explicitly.

For real flush buffers, use appropriate ESP-IDF capability checks, conceptually:

```cpp
assert(esp_ptr_dma_capable(buf));
```

The application must never rely on:

```text
"QEMU accepted this pointer"
```

as evidence that hardware DMA will.

---

# 21. Dirty regions and tiles

Every ink update returns dirty pixel bounds:

```cpp
struct Rect {
    int x0;
    int y0;
    int x1;
    int y1;
};
```

Dirty regions can exceed one tile.

A fast gesture may span 100+ pixels between useful render updates.

Therefore:

```text
dirty rect
   ↓
enumerate intersecting tiles
   ↓
render each affected tile
```

Track:

* pixels touched
* tiles touched
* display bytes
* display transactions

Where practical, coalesce neighboring tile output into larger contiguous panel writes.

Each partial display update may incur controller command overhead, so six tiny transactions may be worse than one larger rectangle.

Do not optimize this blindly before real QSPI measurements.

---

# 22. Persistent stroke storage

Store original/input stroke data, not only raster output.

Do **not** use an unbounded `std::vector` in the real-time embedded path.

Use a fixed-capacity/chunked storage design.

For example:

```cpp
struct Stroke {
    uint16_t color;
    InkConfig brush;

    PointSpan points;
    Rect bounds;
};
```

Back `PointSpan` with:

* fixed-size chunks from a preallocated PSRAM arena
* or another predictable bounded allocator

Decide explicit limits before implementation.

Example initial policy:

```text
MAX_POINTS_PER_STROKE = measured/conservative fixed cap
MAX_STROKES           = bounded
POINT_CHUNK_SIZE       = fixed
```

If a stroke exceeds its point cap:

* decimate safely
* or stop retaining additional history while continuing raster output
* but never heap-fail unpredictably

No active touch update should depend on general-purpose heap allocation succeeding.

---

# 23. Undo strategy

Undo removes the most recent stroke.

Use PSRAM canvas snapshots.

Before a new stroke begins:

```text
snapshot current committed canvas
```

One snapshot:

```text
~322 KiB
```

Start with:

```text
8 snapshots
```

Approximate storage:

```text
~2.5 MB
```

plus current committed canvas.

Undo:

```text
restore previous committed snapshot
pop stroke
refresh display
```

Snapshot copy cost must be measured on hardware rather than assumed.

Retain stroke history so full replay remains available for:

* deeper undo
* debugging
* future policy changes

Snapshots always contain **committed canvas state only**.

---

# 24. Native host build

Build the platform-independent core natively on ARM64 macOS.

This is the primary development loop.

Core code must have no ESP-IDF dependency.

Native build provides:

* fast iteration
* unit tests
* host-only bit-exact golden images
* deterministic replay
* fuzz testing
* degenerate-input testing
* PNG output
* PF/capsule visual comparisons
* sanitizer/tooling opportunities

Suggested cases:

```text
single point
duplicate points
zero dt
large dt
stationary finger
very fast flick
hard 180° turn
tight spiral
tiny circle
very long stroke
stroke crossing itself
many repeated points
touch-down followed immediately by lift
```

Add explicit tests for:

```text
geometry commit invariant
provisional tail never entering committed canvas
coordinate transforms
tile-boundary crossings
```

---

# 25. ESP32-S3 QEMU

After native core functionality works, integrate ESP-IDF and QEMU.

Espressif QEMU supports ESP32-S3 and includes functional models for:

* CPU
* framebuffer
* PSRAM
* cache
* SPI
* GDMA
* several peripherals

However:

> **QEMU is not cycle accurate.**

Do not use QEMU wall-clock FPS as real ESP32-S3 performance.

Do not trust it to reproduce:

* real cache timing
* PSRAM contention
* DMA restrictions perfectly
* QSPI bandwidth
* CO5300 behavior
* actual touch behavior

Use it for architectural/integration validation.

---

# 26. QEMU graphical backend

Use Espressif's virtual RGB framebuffer / `esp_lcd_qemu_rgb` where practical.

Display:

```text
368 × 448
RGB565
```

Convert mouse events or deterministic replay into `TouchPoint`s.

QEMU validates:

* Xtensa compilation
* ESP-IDF integration
* task/queue design
* PSRAM allocation logic
* MCU-only compile problems
* basic framebuffer behavior
* platform abstractions

The QEMU display backend should implement the same narrow `pushRect()` contract as real hardware.

It should not expose RGB-panel-specific assumptions to application code.

---

# 27. Cross-target test policy

Three categories:

## Host correctness

May use exact images/checksums when toolchain is controlled.

## QEMU/ESP32 structural correctness

Prefer comparisons such as:

```text
number of logical stroke points
number of generated primitives
bounds
dirty rect
tiles touched
committed/provisional counts
memory usage
```

Floating-point coordinates may use tolerances.

## Real hardware

Ground truth for:

* cycles
* latency
* DMA behavior
* display behavior
* ink feel

Do not fail a QEMU test merely because its floating-point image differs by one RGB565 level from ARM64.

---

# 28. Performance instrumentation

Instrument from the beginning.

Per update, collect:

```text
raw samples
stroke points
provisional geometry
newly committed geometry
dirty rectangle
tiles rasterized
pixels rasterized
coverage operations
display bytes
display transactions
```

Provide hooks:

```cpp
INK_BENCH_BEGIN();
...
INK_BENCH_END();
```

Native:

```text
host timing
operation counts
```

QEMU:

```text
structural/proxy measurements
```

Hardware:

use ESP32-S3 cycle counters.

Measure separately:

```text
input/filter cycles
geometry cycles
coverage raster cycles
RGB565 composite cycles
PSRAM copy/write cycles
display-submit cycles
overall touch→submit latency
```

---

# 29. Task/thread structure

Do not over-design multicore placement before profiling.

Architecture should permit:

```text
Touch task
    ↓ ring/queue
Ink/render task
    ↓
Display queue
    ↓
DMA completion
```

Float use on ESP-IDF may affect task/core affinity depending on configuration and FPU context handling.

Therefore:

* no FPU in ISR
* expect the ink task may acquire affinity constraints
* inspect actual behavior
* explicitly pin tasks only when necessary or measured beneficial

Do not assume display work belongs on the other core without evidence.

---

# 30. Touch architecture

Real path:

```text
touch IRQ
   ↓
timestamp
   ↓
task notification
   ↓
I²C read
   ↓
raw controller coordinates
   ↓
TouchTransform
   ↓
logical TouchPoint
```

On first hardware run measure:

```text
interrupt interval histogram
I²C read time
down→first-coordinate latency
lift detection latency
coordinate jitter at rest
```

Do not assume a fixed CST820 report rate beforehand.

---

# 31. Runtime tuning

First real-hardware session should mainly involve instrumentation and tuning, not recompilation.

Expose:

```text
size
thinning
smoothing
streamline
pressure response
nominal dt
AA toggle
PF / capsule geometry
possibly tile size
```

Example serial commands:

```text
ink streamline 0.25
ink thinning 0.60
ink size 7
ink renderer pf
ink renderer capsule
ink aa on
stats
touchhist
```

Exact syntax is unimportant.

Fast tuning is important.

---

# 32. UI v1

Keep UI minimal:

```text
4 color dots
undo
clear
optional size toggle
```

No full color picker.

Do not use LVGL for the drawing canvas.

LVGL may later be useful for peripheral UI, but the ink surface stays direct.

Because 368×448 is small, an auto-hiding or edge toolbar may eventually be preferable.

Do not optimize this before ink quality works.

---

# 33. Clear behavior

Clear:

```text
clear committed PSRAM canvas
clear stroke history
clear undo snapshots
clear provisional state
refresh full panel
```

Clear and undo may be substantially slower than regular drawing updates.

That is acceptable.

---

# 34. Suggested source layout

```text
tinydraw/
│
├── core/
│   ├── ink_config.h
│   ├── touch_point.h
│   │
│   ├── ink/
│   │   ├── ink_stream.h
│   │   ├── ink_stream.cpp
│   │   ├── pressure_model.h
│   │   ├── perfect_freehand_geometry.h
│   │   ├── perfect_freehand_geometry.cpp
│   │   ├── capsule_geometry.h
│   │   └── capsule_geometry.cpp
│   │
│   ├── graphics/
│   │   ├── rect.h
│   │   ├── coverage_tile.h
│   │   ├── coverage_tile.cpp
│   │   ├── rgb565.h
│   │   ├── tile_renderer.h
│   │   └── tile_renderer.cpp
│   │
│   ├── canvas/
│   │   ├── committed_canvas.h
│   │   ├── undo_ring.h
│   │   ├── stroke_store.h
│   │   └── point_arena.h
│   │
│   └── platform/
│       ├── display_backend.h
│       ├── touch_source.h
│       └── coordinate_transform.h
│
├── host/
│   ├── main.cpp
│   ├── host_canvas.cpp
│   ├── mouse_touch.cpp
│   ├── golden_tests/
│   └── replay/
│
├── esp32/
│   ├── main/
│   │   ├── app_main.cpp
│   │   ├── qemu_display.cpp
│   │   ├── qemu_touch.cpp
│   │   ├── waveshare_display.cpp
│   │   ├── waveshare_touch.cpp
│   │   ├── coordinate_transform.cpp
│   │   └── perf.cpp
│   │
│   └── CMakeLists.txt
│
└── testdata/
    └── strokes/
```

Keep the core free of ESP-IDF and host framework dependencies.

---

# 35. First implementation order

## Phase 1 — native core

Implement:

1. `TouchPoint`
2. coordinate-space conventions
3. `InkConfig`
4. timestamp-aware streamlining
5. simulated pressure
6. PF streaming state
7. verify PF lookahead/dependency depth
8. committed-prefix/provisional-tail model
9. PF ribbon geometry
10. coverage tile
11. sRGB-space RGB565 compositing
12. committed-canvas invariant
13. capsule alternate
14. fixed-capacity/chunked point arena
15. host drawing window
16. deterministic replay
17. host golden images
18. fuzz/degenerate tests

Do not micro-optimize yet.

## Phase 2 — memory model

Implement host equivalents of:

```text
PSRAM committed canvas
SRAM working tiles
SRAM coverage tile
undo snapshot ring
fixed point arena
```

Enforce the same semantics even though desktop memory is effectively unlimited.

## Phase 3 — ESP-IDF / QEMU

Implement:

1. ESP32-S3 target
2. QEMU framebuffer
3. fake mouse/replay touch
4. PSRAM committed-canvas allocation
5. internal-SRAM tile buffers
6. DMA-capability checks
7. task/queue structure
8. instrumentation
9. tolerance-based cross-target tests

## Phase 4 — hardware

When board arrives:

1. identify V1 vs V2
2. verify flash/PSRAM
3. bring up panel
4. determine panel address offsets
5. bring up touch
6. determine touch swap/mirror/orientation
7. validate finger→pixel alignment
8. collect touch timing histogram
9. measure cycle counts
10. measure PSRAM copy/write cost
11. measure QSPI dirty-region transfer cost
12. tune ink
13. compare dt-adaptive vs resampled input
14. compare PF vs capsule performance
15. optimize measured bottlenecks only

---

# 36. First hardware checklist

Immediately record:

```text
board revision
PSRAM detected
flash detected
panel controller
touch controller

visible width / height
panel x offset
panel y offset

touch swap_xy
touch mirror_x
touch mirror_y

QSPI clock
touch IRQ timing distribution
touch jitter
CPU frequency
```

Run a simple calibration screen before evaluating ink:

```text
display crosshair at known logical point
touch same point
verify transformed touch coordinate
```

Test corners and center.

Do not debug drawing feel until coordinate alignment is known correct.

---

# 37. Representative performance strokes

Benchmark:

```text
slow diagonal
fast flick
small circle
spiral
zig-zag
tight 180° turn
long scribble
self-crossing scribble
```

Replay the same recorded inputs across:

* host
* QEMU
* hardware

Compare structural statistics across targets and cycles only on hardware.

---

# 38. Performance philosophy

Optimize architecture first, arithmetic second.

Bad:

```text
recompute whole stroke every sample
redraw whole canvas every sample
push whole framebuffer every sample
unbounded hot-path allocation
blend overlapping primitives directly into RGB565
write provisional ink into persistent canvas
```

Good:

```text
streaming state
committed prefix
small provisional tail
dirty rectangles
tile rendering
coverage union
single composite
committed PSRAM canvas
internal SRAM DMA buffers
bounded point arena
explicit coordinate transforms
```

---

# 39. Code-quality requirement

The first implementation should be **obvious and measurable**, not clever.

Hardware is arriving soon.

Prefer:

```text
correct
instrumented
bounded
easy to modify
easy to benchmark
```

over:

```text
clever
highly fused
hard to reason about
prematurely optimized
```

Once real cycle counts exist, optimize actual bottlenecks.

---

# 40. Definition of done before hardware arrives

A successful pre-hardware milestone is:

* native ARM64 build works
* mouse drawing works at 368×448
* logical coordinate system is explicit
* streaming PF implementation works
* simulated pressure works
* dt-adaptive filtering exists
* PF dependency/lookahead depth is verified
* committed/provisional geometry model works
* committed-canvas invariant has tests
* PF ribbon renderer works
* coverage-mask AA works
* sRGB RGB565 blending is explicit
* capsule alternate works
* fixed-capacity/chunked stroke storage exists
* four colors work
* snapshot undo works
* clear works
* deterministic replay works
* host golden tests exist
* cross-target tolerance policy exists
* degenerate/fuzz tests exist
* dirty-tile accounting works
* ESP32-S3 firmware builds
* QEMU graphical output works
* PSRAM committed canvas works under QEMU
* internal working buffers are explicitly allocated
* DMA-source assumptions are checked
* instrumentation is in place

Final objective:

> **When the physical board arrives, hardware integration should reveal transforms, tuning, and performance numbers—not expose a fundamental drawing-engine or memory-model mistake.**
