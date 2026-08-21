# Adversarial architecture review — 2026-08-21

Scope: architecture-first review of `core/` (Raster V1), `vector_v2/` (Vector V2
engine), `esp32/main/`, `host/`, `puck/`, `rp2350/`, the build/test system, and
docs-vs-code consistency. Method: four independent adversarial reviewers plus a
verification pass on the 16 most load-bearing claims. **16/16 verified claims
held; one wording correction is noted inline (H4).** Every finding carries a
`file:line` receipt from this checkout.

Companion reports with additional per-area detail:

- `.codex-archaeology/adversarial-build-test-docs-review-2026-08-21.md`
- `.codex-archaeology/core-v1-adversarial-review-2026-08-21.md`

Proposed fixes are in [Suggested fixes](#suggested-fixes) as hand-written
unified diffs grounded in the current tree. They are proposals for review, not
applied changes.

## Verdict

The engines are genuinely clean: zero platform includes in `core/` or
`vector_v2/`, a real authority journal with CRC marker-last commits, centrally
pinned capacity invariants (`memory_layout.h` static_asserts), and bit-exact
painter-order oracles. The problems sit elsewhere:

1. The evidence chain anchoring the project's receipts culture is broken
   (C1).
2. V2's "narrow platform adapter" is actually the whole application welded to
   concrete IDF types; portability is bought by SDK shadowing, not interfaces
   (H2).
3. The dominant coupling mechanism in the render pipeline is prose comments
   where the codebase should have types or owner functions (H3).

## Findings

Severity reflects architectural impact, not bug count.

### Critical

#### C1 — The trust model is broken at its foundation: no CI, and the release SHA does not exist

The project's epistemology is dated SHA-anchored receipts
(`PROJECT_STATE.md`: "The final 604-slot physical battery passed every gate at
`a5db58d`"). But:

- `git cat-file a5db58d` → `Not a valid object name`. The revision the release
  battery and scorecard cite is unrecoverable. Git history contains an
  "identity rewrite" (`9e6467e "Repair the Puck bundle after identity
  rewrite"`) that left no old→new SHA mapping.
- The `v2` tag dereferences to that same Puck-bundle commit, not to any
  release revision.
- There is no CI of any kind (no `.github/`, no `.gitlab-ci.yml`). Every green
  claim in `PROJECT_STATE.md` is an unenforceable manual local run.

For a project whose differentiator is receipt culture, this is the deepest
cut: the receipts point at nothing, and nothing enforces them going forward.

#### C2 — Cross-generation data erasure contradicts the ship contract

Both generations bind the same partition — label `"drawing"`, subtype `0x40`
(`esp32/main/drawing_store.cpp:22-23`,
`esp32/main/vector_v2/vector_v2_autosave_store.cpp:25-26`) — and each destroys
the other's files:

- V1 on V2 bytes: header check fails → synchronous erase of the required
  partition range and a fresh header (`drawing_store.cpp:84-88`).
- V2 on V1 bytes: "Empty flash and pre-V2 Raster snapshot bytes both start a
  fresh V2 journal" → `erase_partition_before_next = true`
  (`vector_v2_autosave_store.cpp:443-451`).

`SHIP_CONTRACT.md:76-77` promises "Raster V1 files remain Raster V1 files and
accessible through the V1 build." Strictly read, neither format misreads the
other — but alternating firmware builds silently wipes user drawings each way,
so "accessible through the V1 build" is not honored end-to-end. There is also
no CRC over snapshot data before the wipe: `valid_header` is a magic-XOR check
(`drawing_store.cpp:43`), so a torn or stale V1 image is indistinguishable
from V2 garbage and is erased without ceremony.

#### C3 — Boot failure path: leak everything, show nothing

`AppStorage::allocate()` performs ~30 allocations and bails with a bare
`return false` on any failure; there is no cleanup anywhere in the file
(`esp32/main/vector_v2/vector_v2_app_storage.cpp:140-160`; no `deallocate`/
`free` calls exist). The caller prints `TINYDRAW_LIVE_FAIL` and returns
**before** `session.display.emplace()` (`vector_v2_app_start.cpp:70-76`;
display emplace at `:127`), so PSRAM exhaustion yields a black screen and an
idle task. For a no-exceptions embedded product this is the worst liveness
path in the codebase. The fix pattern already exists in-repo
(`allocate_external` RAII with `heap_caps_free` deleter,
`vector_v2_gate_harness_internal.h:56-61`).

### High

#### H1 — Authority imports the derived-pixel hub: tenet 3 contradicted by the include DAG

The shared value types every module needs — `ZoomLevel`
(`materialized_canvas.h:42`), `DocumentRevision` (:52), `PixelRect` (:62) —
live inside the 604-line derived-pixel hub. Authority-side `operation.h:8`
therefore imports the entire canvas module for one rectangle type. "Operations
are document authority; tiles are derived pixels" (SHIP_CONTRACT tenet 3) is
true in prose and false in the dependency graph. Eight engine headers include
`materialized_canvas.h`.

#### H2 — V2 has no display seam; portability is bought by SDK shadowing

`VectorV2Presenter` binds the concrete hardware class:
`Co5300PanelTransport& display_` (`vector_v2_presenter.h:255`, ctor `:75-77`).
Verified consequences:

- `puck/platform/co5300_panel_transport.cpp` is a **337-line clone** of the
  ESP32 transport (ring staging, byte swap, TE timing) that must be edited in
  lockstep forever. It is honest and golden-trace-verified, but drift
  protection is manual discipline, not structure.
- `host/` links only `tinydraw::core` (`host/CMakeLists.txt:6-9`), so the most
  complex code in the repo has no desktop development loop.
- V1's clean `DisplayBackend` interface
  (`core/include/tinydraw/platform/display_backend.h:9-17`) is unused by V2
  because the presenter needs the richer strip/ring staging protocol
  (`co5300_panel_transport.h:47-71`).

The engines pass the platform-independence grep; the V2 app layer *is* the
application.

#### H3 — The pipeline's hardest contracts are comments

Producer↔absorption chord-workspace aliasing, scratch-buffer exclusivity
across three workspaces (`vector_v2_app_start.cpp:180-201,265,294`), and
general serialization discipline exist as prose in ≥22 places with zero
machine enforcement. The platform-side mitigation is itself a comment:
"Dropping it also makes the shared chord workspace exclusively available"
(`vector_v2_background_pipeline.cpp:745-748`) plus scattered
`producer_.cancel_pending_work()` call sites. A future absorb call site that
skips the cancel gets silent pixel corruption, not a crash.

#### H4 — The tested binary is not the shipped binary

Gate-harness firmware compiles with `TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS=1`
(`esp32/main/CMakeLists.txt:148-151`), which adds a member to
`MaterializedCanvas` (`materialized_canvas.h:571-573`) — so the battery-tested
image has a different class layout than the product image that ships.
Compounding factors, all verified:

- The ASan suite skips the entire snapshot/e2e host suite
  (`TINYDRAW_BUILD_HOST=OFF` under `scripts/dev asan`).
- `third_party/pngenc` receives none of the project warning options (only
  `tinydraw_core` links them, `core/CMakeLists.txt:61`).
- `rp2350/CMakeLists.txt` is a separate CMake project with no
  `-Werror` and no project options.
- The ESP-IDF build compiles all engine sources under IDF default warnings.

The shipped firmware is the least compiler-checked build of all. *(Correction
vs. the original subagent report: the diagnostics define is `PRIVATE` to the
IDF component, not `PUBLIC`; the substance above is unchanged.)*

### Medium

#### M1 — The retain pipeline is implemented twice, and only device runs the resumable one

Synchronous `retain_uniform_tile` (`vector_v2/src/incremental_document.cpp:112ff`)
versus resumable state machine
(`vector_v2/src/incremental_document_absorption.cpp:355ff`). Host gates
exercise the sync path; production runs the other. Budget semantics have
already diverged between the two.

#### M2 — Unbounded drain on the input path

`drain_boundary()` loops `while (absorption_.active() || pending != 0)` with a
per-slice budget (`kInPlaceRetentionBudgetUs = 10 ms`) but **no total
deadline** — `started` is used only for logging
(`vector_v2_background_pipeline.cpp:199-245`) — and it runs synchronously
inside lift handling before the history action applies
(`vector_v2_app.cpp:632`). Pending work is capped at
`kPendingOperationHighWater = 24` (`presenter.h:63`), so worst case is tens to
hundreds of milliseconds between tap and undo feedback — precisely after heavy
strokes, exactly when pending is highest. The idle path does this correctly
(bounded slices + urgency preemption); the boundary path does not.

#### M3 — Fake seams and dead abstractions

- `live_ink_coordinator.process_live_ink_move` has zero product callers; the
  product re-implements visual-before-authority ordering inline
  (`vector_v2_live_stroke_session.cpp:118-127`). Its header also publicly
  includes core ribbon geometry while CMake links core PRIVATE.
- `TouchTransform::touch_to_logical` (`coordinate_transform.h:11-19`): zero
  production callers; real mapping happens four different implicit ways
  (CST816S logical extents `physical_touch.cpp:40-41`, host bounds-check
  `input_coordinates.h:12-17`, rp2350 raw cast, puck injection).
- Dead wire kinds `kReset`/`kHistoryUpdate`: decodable, never encodable by
  anything in the repo.

#### M4 — Duplication is already drifting — proof attached

Three toolbar switches exist (host, esp32 V1, rp2350), and rp2350 has already
mutated: `kSelectPan` sets `toolbar.tool = kPen`
(`rp2350/src/main.cpp:280-283`) where both other copies set `kPan`
(`host/main.cpp:408-411`, `hardware_app.cpp:473-476`) — a live bug delivered
by copy-paste. The same panel fact is encoded twice under unrelated names:
`kMainToolbarOverlayTop = 372` (`core/include/tinydraw/ui/toolbar.h:13`) vs
`kChromeCanvasBottom = 372` (`vector_v2/include/tinydraw/vector_v2/chrome.h:30`).
Also: two demo_tape modules, two transport implementations (H2), two retain
pipelines (M1). The pattern is systemic.

#### M5 — The shared kernel is real but unowned

V2 production consumes six core artifacts (`CurvedRibbonStream`/`RibbonUpdate`,
`PixelPainter`, `RibbonRenderer`+coverage_tile at
`vector_v2_presenter.cpp:466`, `png_encoder`, `geometry.h`, `InkStream`), all
buried in the V1 tree with hand-maintained source lists that have already
drifted: uncalled `perfect_freehand`, `coordinate_transform`, and
`vector_document` sources compile into firmware, paying flash on a 1.75 MiB
app partition. No reverse deps exist (deleting vector_v2 cannot break core),
but nothing marks these six files as load-bearing for two products.

#### M6 — Teardown is dead code in product, and the one live path races

Every `session.running = false` sits inside `#ifdef TINYDRAW_VECTOR_V2_DEMO`
(`vector_v2_app.cpp:222,403,412,436`); product builds spin forever, so the
carefully ordered destructor chain (declaration order in `vector_v2_app.h:41ff`
is a correct reverse-dependency order) is never exercised. Where teardown does
run (demo failure paths), autosave `~Impl` calls `vTaskDelete(worker_task)`
cross-thread (`vector_v2_autosave_store.cpp:99`) — if the worker holds the SPI
flash lock mid-erase/write, the lock leaks and later flash users deadlock. The
correct join handshake exists next door
(`vector_v2_touch_sampler.cpp:66-88`).

#### M7 — Language drift is total

`CONTEXT.md` says the user-facing unit is a "Stroke" and to *avoid* the word
"Operation". The engine's central vocabulary is ~28 `Operation*` types across
five modules; `maximum_chunk_samples` puts banned "chunk" in a public API; and
`PROJECT_STATE.md` itself says "102 active operations." The ubiquitous-language
document describes a dialect nobody speaks. (`OperationRecord.gesture_id`
shows the concepts are right; only the words disagree.)

#### M8 — Always-on diagnostic printf in ship builds' latency-critical paths

`print_stroke`, `print_lift_baseline`, `print_presentation`,
`print_pan_baseline` (`vector_v2_app_diagnostics.cpp:48-172`) are ungated;
every stroke lift emits two multi-hundred-byte lines over UART inside the very
paths being measured. The code even patches around its own pollution ("Keep it
out of the pre-existing stroke poll-gap metric", `vector_v2_app.cpp:782-784`).

### Low

- README's "shared C++20 modules" are plain headers: zero `.cppm` files and
  zero `import` statements in the repo (README.md:11).
- Stale field names acknowledged in comments: `x_quarter` now holds 1/16 units
  (`operation.h:22-27`).
- Tribal golden-image approval process (regenerate-and-compare is encoded;
  approval is not).
- `storage_overlap.h` is included by eight engine files with zero direct
  tests.
- `puck/` is excluded from format-check.
- IDF source lists duplicate engine source lists
  (`esp32/main/vector_v2/sources.cmake` vs `vector_v2/CMakeLists.txt`) —
  drift-prone by construction.

## What is genuinely excellent

- Engine platform-independence is real: `grep -r "esp_|freertos|driver/gpio"`
  over `core/` and `vector_v2/` returns zero hits.
- The journal module is the best-designed module in the repo: CRC
  marker-last commits, monotonic sequences, discardable-tail recovery that
  preserves the last valid recovery point — all enforced internally, not by
  caller discipline.
- Error handling is coherent engine-wide: fail-closed to overview fallback
  with counted drops (`InPlaceRetainDrops`).
- Painter-order exactness has bit-for-bit test oracles; capacity invariants
  are centrally pinned (`memory_layout.h` static_asserts).
- Puck is honest, maintained, and golden-trace-verified; it compiles the real
  firmware TUs against a documented fake-IDF surface.
- Toolchain pinning (SHA256-pinned wasi-sdk, ARM gcc) is exemplary.
- Doc claims that survived attack: 604-slot pool (`memory_layout.h:37`),
  4 MiB journal + partition map, zoom enum exactly 25–400%, AA receipt numbers
  matching digit-for-digit.

## Suggested fixes

Hand-written unified diffs against this checkout. Each is minimal and review-
sized; larger structural work (port interface, kernel target, single retain
pipeline) is described after the diffs rather than faked at diff precision.

### Fix 1 — CI running the existing suites (C1)

New file. Enforces what `PROJECT_STATE.md` already claims, using the repo's
own scripts. Runner/cache details may need tuning; the commands are exactly
the documented ones.

```diff
--- /dev/null
+++ b/.github/workflows/host.yml
@@ -0,0 +1,33 @@
+name: host
+
+on:
+  push:
+    branches: [main]
+  pull_request:
+
+jobs:
+  host-debug:
+    runs-on: macos-14
+    steps:
+      - uses: actions/checkout@v4
+      - name: Bootstrap toolchain
+        run: ./scripts/bootstrap-macos
+      - name: Debug build and tests
+        run: ./scripts/dev test
+      - name: Format check
+        run: ./scripts/dev format-check
+
+  host-sanitizers:
+    runs-on: macos-14
+    steps:
+      - uses: actions/checkout@v4
+      - name: Bootstrap toolchain
+        run: ./scripts/bootstrap-macos
+      - name: ASan and UBSan
+        run: ./scripts/dev asan
```

Follow-up (not diffed): re-anchor the release receipts. Publish a table mapping
rewritten SHAs to their successors, retag `v2` at the true release revision,
and add a guard that fails CI if any doc cites a SHA missing from history.

### Fix 2 — AppStorage failure-path cleanup (C3)

Free everything allocated so far when allocation fails, instead of leaking the
partial set. Nothing has been `std::construct_at`-ed at the failure point (the
construct loops run after the null check), so freeing raw buffers is safe.
`heap_caps_free(nullptr)` is a no-op, so unconditional frees are fine.

```diff
--- a/esp32/main/vector_v2/vector_v2_app_storage.h
+++ b/esp32/main/vector_v2/vector_v2_app_storage.h
@@ -84,6 +84,9 @@ struct AppStorage {
   vector_v2::TileKey* affected_keys = nullptr;
 
   [[nodiscard]] bool allocate();
+  // Releases every buffer allocated by allocate(). Safe to call on a fully
+  // or partially allocated storage, and safe to call more than once.
+  void deallocate();
 };
 
 }  // namespace tinydraw::esp32
```

```diff
--- a/esp32/main/vector_v2/vector_v2_app_storage.cpp
+++ b/esp32/main/vector_v2/vector_v2_app_storage.cpp
@@ -137,6 +137,45 @@ bool AppStorage::allocate() {
 }  // namespace
 
+void AppStorage::deallocate() {
+  heap_caps_free(overview);
+  overview = nullptr;
+#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
+  heap_caps_free(snapshot);
+  snapshot = nullptr;
+#endif
+  heap_caps_free(frame);
+  frame = nullptr;
+  heap_caps_free(tile_pixels);
+  tile_pixels = nullptr;
+  heap_caps_free(overview_scratch);
+  overview_scratch = nullptr;
+  heap_caps_free(region_scratch);
+  region_scratch = nullptr;
+  heap_caps_free(chrome_cache);
+  chrome_cache = nullptr;
+  heap_caps_free(producer_supertask);
+  producer_supertask = nullptr;
+#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
+  heap_caps_free(harness_tile_scratch);
+  harness_tile_scratch = nullptr;
+#endif
+  heap_caps_free(producer_mask);
+  producer_mask = nullptr;
+  heap_caps_free(producer_summary_rows);
+  producer_summary_rows = nullptr;
+  heap_caps_free(producer_summary_words);
+  producer_summary_words = nullptr;
+  heap_caps_free(producer_chord_plans);
+  producer_chord_plans = nullptr;
+  heap_caps_free(chunk_mask);
+  chunk_mask = nullptr;
+  heap_caps_free(uniforms);
+  uniforms = nullptr;
+  heap_caps_free(occupancy);
+  occupancy = nullptr;
+  heap_caps_free(slots);
+  slots = nullptr;
+  heap_caps_free(raw_slot_directory);
+  raw_slot_directory = nullptr;
+  heap_caps_free(records);
+  records = nullptr;
+  heap_caps_free(samples);
+  samples = nullptr;
+  heap_caps_free(input_samples);
+  input_samples = nullptr;
+#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
+  heap_caps_free(rerender_entries);
+  rerender_entries = nullptr;
+#endif
+  heap_caps_free(touch_events);
+  touch_events = nullptr;
+  heap_caps_free(affected_keys);
+  affected_keys = nullptr;
+  heap_caps_free(settle_op_alpha);
+  settle_op_alpha = nullptr;
+  heap_caps_free(settle_accumulated);
+  settle_accumulated = nullptr;
+  heap_caps_free(settle_red);
+  settle_red = nullptr;
+  heap_caps_free(settle_green);
+  settle_green = nullptr;
+  heap_caps_free(settle_blue);
+  settle_blue = nullptr;
+  heap_caps_free(settle_pixels);
+  settle_pixels = nullptr;
+  heap_caps_free(operation_spatial_cells);
+  operation_spatial_cells = nullptr;
+  heap_caps_free(operation_spatial_large);
+  operation_spatial_large = nullptr;
+  heap_caps_free(operation_candidates);
+  operation_candidates = nullptr;
+}
+
 bool AppStorage::allocate() {
   overview = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
@@ -148,6 +148,12 @@ bool AppStorage::allocate() {
       operation_spatial_large == nullptr || operation_candidates == nullptr) {
+    // Partial allocation: release everything taken so far so a retry or
+    // shutdown starts clean, then surface the failure with live heap stats.
+    deallocate();
     return false;
   }
```

Companion change (recommended, not diffed): bring the display up before
`AppStorage::allocate()` in `vector_v2_app_start.cpp` so allocation failure can
be shown on glass instead of printing to a console nobody is watching.

### Fix 3 — Extract the leaf value-type header (H1)

Move `ZoomLevel`, `DocumentRevision`, `WorldPoint`, and `PixelRect` into a new
leaf header. Because the types keep their namespace and `materialized_canvas.h`
includes the leaf, all existing consumers keep compiling unchanged; authority
code stops importing the canvas hub.

```diff
--- /dev/null
+++ b/vector_v2/include/tinydraw/vector_v2/document_values.h
@@ -0,0 +1,34 @@
+#ifndef TINYDRAW_VECTOR_V2_DOCUMENT_VALUES_H
+#define TINYDRAW_VECTOR_V2_DOCUMENT_VALUES_H
+
+#include <cstdint>
+
+namespace tinydraw::vector_v2 {
+
+enum class ZoomLevel : std::uint8_t {
+  k25Percent,
+  k50Percent,
+  k100Percent,
+  k200Percent,
+  k400Percent,
+};
+
+// Revisions are monotonic within a document. UINT32_MAX is terminal; callers
+// must start a new document identity rather than wrap it to zero.
+struct DocumentRevision {
+  std::uint32_t value = 0;
+  bool operator==(const DocumentRevision&) const = default;
+};
+
+struct WorldPoint {
+  int x = 0;
+  int y = 0;
+};
+
+struct PixelRect {
+  int x0 = 0;
+  int y0 = 0;
+  int x1 = 0;
+  int y1 = 0;
+  bool operator==(const PixelRect&) const = default;
+};
+
+}  // namespace tinydraw::vector_v2
+
+#endif  // TINYDRAW_VECTOR_V2_DOCUMENT_VALUES_H
```

```diff
--- a/vector_v2/include/tinydraw/vector_v2/materialized_canvas.h
+++ b/vector_v2/include/tinydraw/vector_v2/materialized_canvas.h
@@ -6,10 +6,11 @@
 #include <array>
 #include <cstddef>
 #include <cstdint>
 #include <optional>
 #include <span>
 
+#include "tinydraw/vector_v2/document_values.h"
 
 namespace tinydraw::vector_v2 {
 
 inline constexpr int kWorldWidth = 1472;
@@ -39,27 +40,6 @@ inline constexpr std::uint16_t kNoRawSlot = 0xFFFFU;
 
-enum class ZoomLevel : std::uint8_t {
-  k25Percent,
-  k50Percent,
-  k100Percent,
-  k200Percent,
-  k400Percent,
-};
-
-// Revisions are monotonic within a document. UINT32_MAX is terminal; callers
-// must start a new document identity rather than wrap it to zero.
-struct DocumentRevision {
-  std::uint32_t value = 0;
-  bool operator==(const DocumentRevision&) const = default;
-};
-
-struct WorldPoint {
-  int x = 0;
-  int y = 0;
-};
-
-struct PixelRect {
-  int x0 = 0;
-  int y0 = 0;
-  int x1 = 0;
-  int y1 = 0;
-  bool operator==(const PixelRect&) const = default;
-};
-
 struct TileGrid {
   int columns = 0;
   int rows = 0;
```

```diff
--- a/vector_v2/include/tinydraw/vector_v2/operation.h
+++ b/vector_v2/include/tinydraw/vector_v2/operation.h
@@ -5,7 +5,7 @@
 #include <cstdint>
 #include <optional>
 #include <span>
 
-#include "tinydraw/vector_v2/materialized_canvas.h"
+#include "tinydraw/vector_v2/document_values.h"
 
 namespace tinydraw::vector_v2 {
```

Note: audit `operation.h` consumers that transitively relied on it pulling in
canvas constants; the full-repo build will surface any within minutes.

### Fix 4 — Total deadline for the boundary drain (M2)

Bound the synchronous lift-path drain so undo feedback stays interactive even
at maximum pending depth. The idle path keeps its own policy; only the
synchronous boundary gains a ceiling. Value chosen to cover the measured
worst per-slice costs with headroom; tune against the gate corpus.

```diff
--- a/esp32/main/vector_v2/vector_v2_background_pipeline.cpp
+++ b/esp32/main/vector_v2/vector_v2_background_pipeline.cpp
@@ -26,6 +26,13 @@ constexpr std::size_t kFillProducerStepsPerCall = 8U;
 constexpr std::size_t kRepairProducerStepsPerCall = 8U;
 
+// Ceiling for one synchronous boundary drain (lift/pan/history). Per-slice
+// budgets bound each absorption step; this bounds their sum so the input
+// path cannot stack dozens of slices while pending work is deep. Measured
+// ordinary drains complete well under one slice; the ceiling only engages
+// after heavy strokes.
+constexpr std::int64_t kBoundaryDrainDeadlineUs = 50'000;
+
 struct AbsorbSliceLimit {
   TouchUrgencyProbe touch_urgency{};
```

```diff
--- a/esp32/main/vector_v2/vector_v2_background_pipeline.cpp
+++ b/esp32/main/vector_v2/vector_v2_background_pipeline.cpp
@@ -206,6 +207,12 @@ bool VectorV2BackgroundPipeline::drain_boundary(BackgroundDrainBoundary boundary
   absorption_.cancel();
   producer_.cancel_pending_work();
   while (absorption_.active() || vector_v2::pending_operation_count(log_, canvas_) != 0U) {
+    if (esp_timer_get_time() - started > kBoundaryDrainDeadlineUs) {
+      // Deadline exceeded: drop back to the async pipeline rather than
+      // extending input latency. Remaining pending work is idempotent and
+      // resumes on the next quiet tick.
+      absorption_.cancel();
+      break;
+    }
     const auto absorbed = vector_v2::absorb_pending_operation_slice(
         {log_,
          canvas_,
```

### Fix 5 — rp2350 pan-tool drift (M4 receipt)

One-line correctness fix for the copy-paste divergence.

```diff
--- a/rp2350/src/main.cpp
+++ b/rp2350/src/main.cpp
@@ -280,7 +280,7 @@ void handle_toolbar(tinydraw::Point point) {
     case tinydraw::ToolbarAction::kSelectPan:
       restore_overlay();
       close_popups();
-      toolbar.tool = tinydraw::DrawingTool::kPen;
+      toolbar.tool = tinydraw::DrawingTool::kPan;
       break;
     case tinydraw::ToolbarAction::kSelectEraser:
       toolbar.tool = tinydraw::DrawingTool::kEraser;
```

### Structural fixes (described, not diffed)

These need design buy-in before they deserve diff precision:

1. **V2 display port interface (H2).** Extract the presenter-facing transport
   contract — `stream_rect_ring`, `PanelStagePatch`, `tear_signal_timing()` —
   into a `vector_v2` port header. ESP32 transport implements it; puck deletes
   its 337-line clone; host implements it over SDL and becomes a V2 dev loop.
   Highest leverage per line changed in the whole review.
2. **Executable serialization contracts (H3).** Fold cancel-before-absorb into
   one pipeline-owned function, or add debug-build canaries (ownership token
   checked at absorb entry). Comments stop being load-bearing.
3. **Single retain pipeline (M1).** Make the resumable implementation the only
   one, following the repo's own `stage_authority_journal` precedent; host
   gates then exercise production semantics.
4. **Named kernel target (M5).** Promote the six shared artifacts into an
   explicitly named, PUBLIC-linked target; purge zero-caller sources
   (`perfect_freehand`, `coordinate_transform`, `vector_document`) from
   firmware source lists.
5. **Teardown liveness (M6).** Replace autosave `vTaskDelete` with the touch
   sampler's join-handshake pattern; give product builds an actually-exercised
   shutdown path.
6. **Cross-generation storage policy (C2).** Write down the intended behavior
   for foreign-format bytes on the shared partition, add snapshot CRCs before
   any destructive erase, and make the ship contract match the code (or vice
   versa).
7. **Quality perimeter (H4).** Link `tinydraw_project_options` into pngenc,
   rp2350, and the IDF engine compilation; gate diagnostic printf behind
   build levels so ship images stop paying UART jitter inside measured paths.

## Second-pass review — challenges and corrections (2026-08-21)

A follow-up adversarial pass challenged seven claims above. Each challenge was
re-verified against source; the original findings text is left unmodified for
the audit trail, and the verdicts below are authoritative where they conflict
with it. Score: five challenges accepted outright, two accepted with nuance,
none rejected.

### 1. Fix 4 would silently drop Undo/Redo taps — RETRACTED (challenge: Critical)

Accepted in full. `vector_v2_app.cpp:632-634`:

```cpp
const bool boundary_ready =
    save_ready &&
    (!history_action || background.drain_boundary(BackgroundDrainBoundary::kHistory));
const bool applied = boundary_ready && chrome_controller.apply(action, tap);
```

`drain_boundary()` returns revision lockstep (`log_.current_revision() ==
canvas_.current_revision()`, end of function). The proposed deadline break
leaves pending work unabsorbed, so revisions differ, the drain reports false,
and `chrome_controller.apply` never runs — a swallowed Undo/Redo tap under
deep pending, exactly when the deadline would engage. The proposed diff was
unsafe and is retracted. A correct fix needs design first: either
authority-first apply with async pixel repair (which must reconcile with
whatever lockstep precondition `chrome_controller.apply` actually requires),
or deadline escalation that changes absorption priority instead of aborting
the drain. No replacement diff is offered until that contract is pinned down.

### 2. M1 overstated test divergence — CORRECTED (challenge: High)

Accepted. Host tests exercise the resumable path directly:
`vector_v2/tests/incremental_document_test.cpp:137-153` drives
`absorb_pending_operation_slice` with injected yields (>100 pauses asserted),
completion, and lockstep checks; the device gate harness also calls the sync
path. Both retain implementations are host-tested, so "host gates run the
sync one, device runs the resumable one" was wrong. What survives is only
divergence risk: two parallel implementations of §8.4 retention with already
different budget semantics can drift apart over time even though both are
tested today. Severity drops to Low-Medium.

### 3. H2 mislabeled Puck's transport — CORRECTED (challenge: High)

Largely accepted. Verified facts that reframe it:

- The transport contract header is IDF-free — it includes only standard
  headers plus `tinydraw/platform/display_backend.h`
  (`esp32/main/co5300_panel_transport.h:3-9`).
- `puck/platform/co5300_panel_transport.cpp:1-3` states it implements "the
  same public contract (esp32/main/co5300_panel_transport.h, unmodified)" —
  an intentional emulator implementation, not a clone.
- `class Co5300PanelTransport final : public DisplayBackend`
  (`co5300_panel_transport.h:94`) — V2's transport already implements V1's
  display interface.

So the seam exists, and interface extraction would not delete Puck's file —
a port always needs its own implementation; that is what emulation is. What
survives of H2 is narrower: the portable contract physically lives under
`esp32/main/`, and `host/` cannot host V2 because of build wiring
(`host/CMakeLists.txt:6-9` links core only), not because an abstraction is
missing. Reclassified from High ("no seam") to Medium (composition/placement).
Recommendation 2's "deletes puck's 337-line clone" claim is withdrawn; the
honest benefit of relocation is discoverability and a host V2 dev loop.

### 4. C1 overstatements — PARTIALLY ACCEPTED (challenge: Medium)

Facts that stand, verified this pass: `git cat-file a5db58d` fails; `.github/`
and any other CI config do not exist. Overstatements corrected:

- "Unrecoverable" was too strong. Evidence content is preserved outside git
  refs: checksummed gate/boot logs
  (`docs/receipts/vector-v2/VECTOR_V2_RELEASE_2026_08_19.md:52-54`, SHA-256
  per artifact) and the pre-rewrite chain recorded in
  `.codex-archaeology/git-history.md:310-312`. Tags `2.0.0` (`e8728d5` "docs:
  mark Vector V2 released") and `2.1.0` anchor post-rewrite history.
- New wrinkle found while verifying: the release receipt itself now contains
  a false statement — `VECTOR_V2_RELEASE_2026_08_19.md:312` says "annotated
  tag `v2` dereferences to that commit," but `v2` dereferences to `9e6467e`
  after the rewrite. The receipts drifted along with the history, which
  reinforces C1's doc-vs-reality theme even as its wording softens.
- Scope caveat accepted: the proposed CI workflow enforces host suites only;
  physical-battery and glass claims are inherently manual and stay outside
  CI's reach.

C1 remains serious — dangling cited SHAs, no enforcement, and a receipt that
misdescribes a tag — but it is a repairable records problem, not total loss.

### 5. M5 flash-cost claim unevidenced — ACCEPTED (challenge: Medium)

Accepted. `vector_document.cpp` sits unconditionally in the V2 firmware
source list (`esp32/main/CMakeLists.txt:80`); its only non-test caller is the
device-gated harness (`vector_v2_gate_harness.cpp:87`).
`perfect_freehand.cpp` (:22) and `coordinate_transform.cpp` (:17) are likewise
unconditional in the V1 list. But nothing here establishes linked-size cost:
static-library members that nothing references are typically dropped by the
linker, and no linker map or binary-size measurement was produced. Restated:
unreferenced sources add compile-time weight and drift risk to firmware
builds; their flash cost is unverified until someone reads a linker map.
Action item added: capture `out/` ELF size with and without the three TUs.

### 6. M7 misread the language document — CORRECTED (challenge: Medium)

Accepted on the "chunk" half. `CONTEXT.md:9-12` defines **Stroke chunk** as a
legitimate bounded internal fragment; the `_Avoid_: Operation, chunk` note at
`:7` governs synonyms when referring to the whole-gesture Stroke concept. So
`maximum_chunk_samples` is compliant vocabulary, and that example is
withdrawn. The "Operation" half survives: the authority record is
gesture-granular (`OperationRecord.gesture_id`, one stroke's samples per
record, `operation.h:36-48`), CONTEXT.md says to avoid calling that unit an
Operation, and `PROJECT_STATE.md` itself says "102 active operations." That
remains real prose-vs-code vocabulary drift, but M7 downgrades accordingly.

### 7. C3 severity inflation — ACCEPTED (challenge: severity)

Accepted. On start failure `vector_v2_app.cpp:802-804` simply returns: there
is no retry loop, so the leaked buffers are never touched again, and heap
state resets on reboot, so the leak has no cross-boot effect. The genuine
defect is the wedged black screen — boot failure with no visible diagnostics
— not resource leakage. C3 is downgraded from Critical to High-equivalent,
reframed as a liveness/diagnostics defect. Fix 2 remains worthwhile hygiene
for any future retry path but is not itself the liveness fix; display-first
initialization (the companion note) is the substantive half.

### Surviving core

Confirmed by both passes, unchanged: C2 (partition erasure vs. ship contract),
M2's latency diagnosis (per-slice-only bound on the synchronous drain — though
Fix 4's remedy is retracted pending design), M4 (duplication already drifting,
rp2350 pan bug), M6 (dead teardown + `vTaskDelete` race), M8 (ungated printf
in measured paths).
