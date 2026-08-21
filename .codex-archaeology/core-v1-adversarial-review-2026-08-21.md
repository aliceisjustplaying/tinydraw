# Adversarial architecture review: Raster V1 core and the V1↔V2 seam

Date: 2026-08-21. Reviewer scope: `core/**`, cross-generation sharing, esp32/host/puck consumption of core. Every claim carries a file:line receipt verified in this checkout.

## (a) The actual shared-kernel map

What `vector_v2/` + the V2 firmware really import from core (verified by include grep over all of vector_v2 plus the esp32 V2 source list):

| Core artifact | Consumed by V2 production | Consumed by V2 test/tool only |
|---|---|---|
| `ink/ribbon_geometry.h` (`CurvedRibbonStream`, `RibbonUpdate`, `RibbonPrimitive`) | `vector_v2/include/tinydraw/vector_v2/live_ink_coordinator.h:8`, `vector_v2/src/svg_export.cpp:11` | `vector_v2/tests/svg_export_test.cpp`, `vector_v2/tests/incremental_rasterizer_test.cpp` |
| `ui/pixel_painter.h` (`PixelPainter`) | `vector_v2/src/chrome.cpp` (`using Painter = PixelPainter;`, chrome.cpp:15) | — |
| `graphics/ribbon_renderer.h` (+ `coverage_tile.cpp`) | V2 firmware presenter live tail: `esp32/main/vector_v2/vector_v2_presenter.cpp:38,466` | `vector_v2/tests/svg_export_test.cpp` |
| `export/png_encoder.h` (+ third_party/pngenc) | V2 firmware export: `esp32/main/vector_v2/vector_v2_export.cpp`, `svg_export_store.cpp`; `esp32/main/image_export_store.cpp:12` | `vector_v2/tests/world_export_test.cpp` |
| `geometry.h` (`Point`/`Rect`/`PanelGeometry`, 368×448) | esp32 V2 app layer (`esp32/main/vector_v2/vector_v2_app.h`, `_presenter.h`, `_chrome_controller.h`) | — |
| `ink_config.h`, `touch_point.h`, `ink/ink_stream.h` | esp32 V2 live ink: `esp32/main/vector_v2/vector_v2_live_stroke_session.h` (`InkStream ink_;`) | — |
| `document/realistic_workload.h` | — | `vector_v2/tools/raster_census.cpp`; gate harness (`vector_v2_gate_harness.cpp:87,93`) |

Consumers of core outside V2: host SDL app (8 tinydraw headers, host/main.cpp:14–22), V1 firmware (hardware_app.cpp:24–33), rp2350 (`ink_stream` + `toolbar` only, rp2350/src/main.cpp:12–13), puck (stubs `fat16_disk.h` only).

Reverse dependency: none. No file in core/ references vector_v2 (grep over core/ is empty), so deleting V2 cannot break core/V1.

**Would deleting V1 break V2?** Deleting the V1 *product* files (world_canvas, stroke_raster, tile_undo_history, toolbar, drawing_snapshot, fat16_disk…) would not break V2. Deleting `core/` wholesale would: the six-row table above is a real, load-bearing shared kernel — but it has no name, no directory, no owner, and no build target. It is an accretion, not a design.

## (b) Duplication matrix V1↔V2

| Pair | Verdict | Evidence |
|---|---|---|
| `CurvedRibbonStream` geometry vs V2 SVG export | **Shared code** (healthy) | svg_export.cpp:265 constructs `CurvedRibbonStream ribbon{RibbonSpanJoin::kSharedBoundary}` from core |
| Core live-tail ribbon vs V2 authority rasterizer | **Parallel math** — same midpoint-quadratic curve model implemented twice with different output representations | core `emit_quadratic` (ribbon_geometry.cpp:118–146: quadratic position+radius eval, tangent directions, convex spans) vs `curved_unit` (incremental_rasterizer.cpp:690+: midpoint de Casteljau into centerline chords + per-pixel analytic coverage). Consistency is enforced by oracle tests, not by construction |
| `perfect_freehand` batch vs `InkStream` incremental | **Evolved fork inside core itself**; the batch variant survives as a pinned behavioral baseline ("Behavioral baseline matching the pinned perfect-freehand implementation", perfect_freehand.h:17–18) with zero production callers (grep: only its own test) | |
| `InkStream` smoothing | **Shared implementation, two tunings** (the healthy pattern) | default `streamline = 0.35F` (core/include/tinydraw/ink_config.h:9); V2 overrides to 0.4 with dated comment "Owner experiment 2026-08-16… Raster V1 remains independent with its established 0.35 setting" (vector_v2_live_stroke_session.cpp:62–65) |
| `camera.{h,cpp}` vs `navigation_state` | **Genuinely different needs**, not duplication: continuous double-precision pan/zoom projector vs discrete `ZoomLevel` + clamped integer origins. camera is already quarantined in test-only `tinydraw_vector_prototype` (core/CMakeLists.txt:74–88) | |
| `demo_tape` ×2 | **Evolved fork**: V2 adds replay scheduling (`replay_due`/`pop_replay`), zoom events, semantic kinds; ~30% record/offset logic duplicated. 61 vs 129 LOC. Tolerable | core/src/demo_tape.cpp vs vector_v2/src/demo_tape.cpp:34–47 |
| `toolbar.cpp` vs V2 `chrome` | **Evolved fork with duplicated hardware truth**: panel constant 372 defined twice (`kMainToolbarOverlayTop = 372` toolbar.h:13; `kChromeCanvasBottom = 372` chrome.h:30); parallel `DrawingTool/PenSize` ↔ `ChromeTool/ChromeSize` enums; palettes deliberately diverged (12 named RGB565 constants, toolbar.cpp `rgb565(InkColor)` vs 2×16 PICO-8 entries, chrome.h:113–126) | |
| V1 coverage-tile compositing vs V2 analytic settled AA | **Different needs by contract** ("Live strokes may be hard-edged. Idle settlement applies the accepted analytic antialiasing", SHIP_CONTRACT.md §Rendering) | settled_tile.cpp:141 `coverage_alpha(float distance_squared, float radius)` |
| `coordinate_transform` vs V2 touch path | **Not duplication — dead on both sides** (see F3) | |

## (c) Findings by severity

### HIGH

**H1. Cross-generation storage lifecycle silently destroys the other generation's files — the contract's "Raster V1 files remain Raster V1 files" is not actually guaranteed end-to-end.**
Both generations claim the same physical partition (`drawing`, subtype 0x40, partitions.csv line for `drawing`).
- V2 treats a V1 snapshot as corrupt-journal and erases it: recovery of non-TDJ1 magic returns kCorrupt (authority_journal.cpp:192–196, 850–857), then `// Empty flash and pre-V2 Raster snapshot bytes both start a fresh V2 journal.` → `impl_->erase_partition_before_next = true;` (vector_v2_autosave_store.cpp:440–448). First autosave wipes the drawing partition.
- Reverse direction: V1's DrawingStore boots, finds magic ≠ `0x57415244` ("DRAW"), and synchronously erases ~2.87 MiB (`kRequiredPartitionBytes = 4096 + 735×4096`) before the UI exists: `else if (esp_partition_erase_range(partition, 0, kRequiredPartitionBytes) != ESP_OK || …)` (drawing_store.cpp:85, header check 90–99).
So flashing either supported generation over the other destroys the user's drawing on first save/boot. It is disclosed only in a code comment; SHIP_CONTRACT.md:76 promises accessibility "through the V1 build" with no mention of erasure-by-the-other-build. Not reinterpretation — but the tenet reads as stronger than what the code does.

**H2. The shared kernel exists but is unowned: one directory mixes kernel, V1 product code, retired prototypes, and characterization fixtures, and every build hand-maintains its own source list (already drifted).**
- Four independent lists compile core sources by path: root `tinydraw_core` lib (core/CMakeLists.txt:23–52), esp32 initial+RASTER_V1 list (esp32/main/CMakeLists.txt:16–31, 55–58), esp32 V2 else-list (:70–82), rp2350 list (rp2350/CMakeLists.txt:20–21). Adding a core .cpp requires touching up to four places.
- Drift already happened: `coordinate_transform.cpp` and `perfect_freehand.cpp` are compiled into V1/QEMU firmware though nothing calls them (list lines 17, 22; zero callers — see F3/F4); `vector_document.cpp` is compiled into shipped V2 firmware though its only consumer is gate-harness diagnostics (line 80; sole referencer vector_v2_gate_harness.cpp:87).
- Usage-requirement leak: public V2 header `live_ink_coordinator.h:8` includes `tinydraw/ink/ribbon_geometry.h`, but vector_v2 links core `PRIVATE` (vector_v2/CMakeLists.txt:13). It compiles today only because esp32 re-adds `${TINYDRAW_CORE}/include` manually (esp32/main/CMakeLists.txt INCLUDE_DIRS). Any other consumer of `tinydraw::vector_v2` gets broken include paths.

### MEDIUM

**M1. `platform/coordinate_transform.{h,cpp}` — the flagship "shared platform" touch abstraction — is dead code in every product.**
`touch_to_logical` has zero callers outside its own test (rg across esp32/, host/, rp2350/, puck/: none). Why it can be dead: the CST816S driver is configured to report directly in logical coordinates (`touch_config.x_max = kCanvasWidth; touch_config.y_max = kCanvasHeight;` physical_touch.cpp:32–33), and the host relies on SDL logical-size scaling (host/input_coordinates.h:9–17). The swap/mirror/scale machinery solves a problem neither product has, yet sits under `platform/` looking like the canonical input seam and is compiled into V1 firmware.

**M2. V1 raster autosave has no data-integrity check on load and a reject-and-wipe versioning strategy.**
The XOR "checksum" covers only header fields (drawing_store.cpp:52–57). Data sectors carry no CRC; `restore()` reads each sector straight into the world with no validation (drawing_store.cpp:297–315) — a torn write from power loss during `erase+write` (save_task, :212–224) comes back as garbage pixels with no detection. Versioning is `kHeaderVersion = 2` (:25) with `valid_header` rejecting any mismatch of magic/version/width/height → blank canvas + full erase (:90–99, :85). There is no migration path and no detection distinct from corruption; a future geometry change silently invalidates every saved drawing.

**M3. Retired prototype and characterization machinery still lives in the shared include tree / product sources.**
Good news first: the heavy prototype (camera, viewport_renderer, settled_renderer, stroke_lod, raster_materializer, stroke_macrogrid, vector_benchmark) is correctly quarantined out of normal builds into test-only `tinydraw_vector_prototype` ("Retired vector-atlas prototype machinery…", core/CMakeLists.txt:71–88). Remaining issues:
- Their headers remain under `core/include/tinydraw/{graphics,document}/`, indistinguishable from live API.
- `realistic_workload.cpp` ships inside the product static lib (core/CMakeLists.txt:48) with consumers only in tests/gate harness/census tool.
- `perfect_freehand.cpp`, `coordinate_transform.cpp`, `vector_document.cpp` sit on firmware SRCS lists with zero product references (H2).
Binary-size impact today ≈ zero (ESP-IDF links with function-sections/GC, so unreferenced objects drop), so this costs compile time and cognitive load — but these lists feed a 1.75 MiB app partition, and every accidental reference pulls real weight.

### LOW

**L1. README's "shared C++20 modules are tested natively" (README.md:11) mislabels plain headers as modules.**
There are no module units anywhere: no `.cppm`/`.ixx`, no `export module`, no `import` (greps empty); everything is `#pragma once` headers + CMake static libs under `CXX_STANDARD 20`. The native-testing half of the sentence is true (29 files in tests/ cover core headers). The phrasing is defensible as colloquial "modules," but given actual C++20 modules exist, it's a docs-trust nit worth fixing.

**L2. `run_hardware_app()` is a ~580-line composition function whose central invariant is maintained by hand at six call sites.**
The "world storage must be re-captured after writing committed pixels" discipline appears at hardware_app.cpp:392, 400, 445, 650, 763, 769. Core itself is genuinely testable (world_canvas takes caller-funded spans, no display dependency — world_canvas.h:28–56); the shell around it is where V1 fragility concentrates.

**L3. World size truth is derived twice.**
V1 world = `kCanvasWidth * 3` (world_canvas.h:19–20, computed); V2 world is hardcoded independently: `inline constexpr int kWorldWidth = 1472;` (materialized_canvas.h:12–13, = 368×4 by arithmetic, not by expression). A panel change requires editing both plus the duplicated 372 constant.

### What is honestly good (credit where due)
- The platform seam is real and respected: `DisplayBackend` is a one-method pure interface (platform/display_backend.h:10–16), implemented only at the edges (physical_display.h, qemu_display.h, co5300_panel_transport.h), consumed optionally by StrokeRaster/TileUndoHistory, and **no core .cpp includes any HW header** (rg for esp/SDL/pico includes in core/: none).
- Ink is genuinely shared end-to-end: one `InkStream` + one `CurvedRibbonStream` produce both generations' strokes and the SVG export — the strongest possible form of "shared ribbon geometry."
- Tile accounting is disciplined: fixed-capacity 10-slot undo (tile_undo_history.h:20, `kMaxEntries = 10U`), dirty-sector autosave with generation-counter race guards (drawing_store.cpp:222–235), `static_assert`s pinning sector math (drawing_snapshot.h:60–61).

## (d) Top 3 recommendations by leverage

1. **Extract the named kernel.** Move the six production-shared artifacts (geometry.h, ink_config.h/touch_point.h/ink_stream, ribbon_geometry, pixel_painter, png_encoder, coverage_tile+ribbon_renderer if the V2 presenter keeps using them) into `core/kernel/` with its own target; make `tinydraw_vector_v2` link it `PUBLIC` (fixes H2's usage leak); move retired prototype headers out of the include tree next to their test-only lib. Then add a CI grep like the one used for this review asserting vector_v2 includes nothing else from core. This converts the accidental split into the stable-kernel contract the docs imply.
2. **Make cross-generation coexistence explicit or impossible.** Cheapest honest fix: detect a foreign-format header at boot (both sides already read offset 0 first) and refuse to erase until first user-authored save, logging which generation was destroyed — or update SHIP_CONTRACT.md:76 to state that booting generation N erases generation M's document. While touching V1 storage, add a CRC to snapshot data sectors (M2): the format bump will wipe old snapshots anyway, so the cost window is now.
3. **Purge zero-caller sources from firmware lists and delete or wire TouchTransform.** Drop `coordinate_transform.cpp`, `perfect_freehand.cpp`, `vector_document.cpp` from esp32 SRCS lists (move to a test-only target like `tinydraw_vector_prototype`); single-source the 372 panel constant and derive `kWorldWidth` from `kCanvasWidth`. Small diffs, immediate honesty improvement, protects the 1.75 MiB budget.
