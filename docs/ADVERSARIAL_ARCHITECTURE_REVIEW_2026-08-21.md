# Adversarial architecture review — final corrected edition

Date: 2026-08-21. This is the **final version** of the review. It is the first
edition after one full second-pass challenge cycle: seven claims were
challenged by an independent adversarial pass, every challenge was re-verified
against source, **all seven were accepted** (five outright, two with nuance),
and the corrections are incorporated inline below rather than left as
footnotes. The first-edition text and the challenge-by-challenge verdicts are
preserved verbatim in [Appendix A](#appendix-a-second-pass-challenges-and-corrections-2026-08-21)
as the audit trail.

Scope: architecture-first review of `core/` (Raster V1), `vector_v2/`
(Vector V2 engine), `esp32/main/`, `host/`, `puck/`, `rp2350/`, the build/test
system, and docs-vs-code consistency. Every finding carries a `file:line`
receipt from this checkout. Companion reports with per-area detail:
`.codex-archaeology/adversarial-build-test-docs-review-2026-08-21.md` and
`.codex-archaeology/core-v1-adversarial-review-2026-08-21.md`.

Finding IDs are renumbered in this edition; the mapping from first-edition IDs
is given in [Finding ID mapping](#finding-id-mapping).

## Verdict

The engines are genuinely clean: zero platform includes in `core/` or
`vector_v2/`, a real authority journal with CRC marker-last commits, centrally
pinned capacity invariants (`memory_layout.h` static_asserts), and bit-exact
painter-order oracles. One pipeline contract is even documented at the right
place — the `move_history_incrementally` lockstep precondition lives in its
engine header (`incremental_document.h:243-245`). The problems sit elsewhere:

1. The evidence chain anchoring the receipts culture is partially dangling and
   entirely unenforced (C1).
2. Two generations share one storage partition and each silently erases the
   other's files, short of what the ship contract promises (C2).
3. The render pipeline's scratch-sharing and serialization disciplines are
   prose, not types or owner functions (H2) — the history-move precondition
   shows the codebase knows how to do better.
4. V2's portability is real but misplaced: a portable transport contract sits
   inside `esp32/main/` while the host build cannot host V2 at all (M1).

## Finding ID mapping

| First edition | Final | Change |
|---|---|---|
| C1 | C1 | Restated: "unrecoverable" softened; tag/receipt drift added |
| C2 | C2 | Unchanged |
| C3 | H4 | Downgraded to High; leak reframed as secondary |
| H1 | H1 | Unchanged |
| H2 | M1 | Demoted to Medium; "clone"/"missing seam" framing withdrawn |
| H3 | H2 | Renumbered only |
| H4 | H3 | Renumbered only |
| M1 | L1 | Corrected (both pipelines are host-tested); downgraded |
| M2 | M2 | Diagnosis stands; remedy replaced after Fix 4 retraction |
| M3 | M3 (+L3) | Split: fake seam stays Medium; dead abstractions move to Low |
| M4 | M4 | Unchanged |
| M5 | M5 | Flash-cost claim marked unverified |
| M6 | M6 | Unchanged |
| M7 | L2 | Chunk half withdrawn; Operation-half drift downgraded to Low |
| M8 | M7 | Renumbered only |

## Findings

Severity reflects architectural impact, not bug count.

### Critical

#### C1 — The evidence chain is unenforced and partially dangling

The project's epistemology is dated SHA-anchored receipts (`PROJECT_STATE.md`:
"The final 604-slot physical battery passed every gate at `a5db58d`").
Verified state of that chain:

- `git cat-file a5db58d` → `Not a valid object name`. The revision the release
  battery and scorecard cite is absent from the object database after an
  acknowledged identity rewrite (`9e6467e "Repair the Puck bundle after
  identity rewrite"`). The chain is repairable, not lost: checksummed gate and
  boot logs survive (`docs/receipts/vector-v2/VECTOR_V2_RELEASE_2026_08_19.md:52-54`,
  SHA-256 per artifact) and `.codex-archaeology/git-history.md:310-312`
  records the pre-rewrite commit subjects. Tags `2.0.0` (`e8728d5`) and
  `2.1.0` anchor post-rewrite history.
- The receipts themselves have drifted: `VECTOR_V2_RELEASE_2026_08_19.md:312`
  says "annotated tag `v2` dereferences to that commit" (`fd05d7d`), but `v2`
  now dereferences to `9e6467e`. Even the receipt no longer describes the
  repository it certifies.
- There is no CI of any kind (no `.github/`, no `.gitlab-ci.yml`). Every green
  claim in `PROJECT_STATE.md` is an unenforceable manual local run. Scope
  caveat, stated plainly: CI can enforce the host suites; physical-battery and
  glass claims are inherently manual. What CI enforces is the *regression*
  perimeter, which today has zero enforcement.

For a project whose differentiator is receipt culture: the anchors dangle, the
receipts misdescribe their tags, and nothing runs automatically to catch the
next drift.

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
from foreign bytes and is erased without ceremony.

### High

#### H1 — Authority imports the derived-pixel hub: tenet 3 contradicted by the include DAG

The shared value types every module needs — `ZoomLevel`
(`materialized_canvas.h:42`), `DocumentRevision` (:52), `PixelRect` (:62) —
live inside the 604-line derived-pixel hub. Authority-side `operation.h:8`
therefore imports the entire canvas module for one rectangle type. "Operations
are document authority; tiles are derived pixels" (SHIP_CONTRACT tenet 3) is
true in prose and false in the dependency graph. Eight engine headers include
`materialized_canvas.h`.

#### H2 — The pipeline's hardest contracts are comments

Producer↔absorption chord-workspace aliasing, scratch-buffer exclusivity
across three workspaces (`vector_v2_app_start.cpp:180-201,265,294`), and
general serialization discipline exist as prose in ≥22 places with zero
machine enforcement. The platform-side mitigation is itself a comment:
"Dropping it also makes the shared chord workspace exclusively available"
(`vector_v2_background_pipeline.cpp:745-748`) plus scattered
`producer_.cancel_pending_work()` call sites. A future absorb call site that
skips the cancel gets silent pixel corruption, not a crash.

The contrast that proves this is a choice, not a necessity: the engine's
history-move precondition is documented exactly where it belongs — "Pending
appends must be drained so log and canvas revisions are equal before entry"
(`incremental_document.h:243-245`) — and enforced at its single call site
(M2). The scratch-aliasing contracts deserve the same treatment.

#### H3 — The tested binary is not the shipped binary

Gate-harness firmware compiles with `TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS=1`
(`esp32/main/CMakeLists.txt:148-151`; defined `PRIVATE` to the IDF
component), which adds a member to `MaterializedCanvas`
(`materialized_canvas.h:571-573`) — so the battery-tested image has a
different class layout than the product image that ships. Compounding factors,
all verified:

- The ASan suite skips the entire snapshot/e2e host suite
  (`TINYDRAW_BUILD_HOST=OFF` under `scripts/dev asan`).
- `third_party/pngenc` receives none of the project warning options (only
  `tinydraw_core` links them, `core/CMakeLists.txt:61`).
- `rp2350/CMakeLists.txt` is a separate CMake project with no `-Werror` and no
  project options.
- The ESP-IDF build compiles all engine sources under IDF default warnings.

The shipped firmware is the least compiler-checked build of all.

#### H4 — Boot failure wedges to a black screen

`AppStorage::allocate()` performs ~30 allocations and bails with a bare
`return false` on any failure; there is no cleanup anywhere in the file
(`vector_v2_app_storage.cpp:140-160`). The caller prints `TINYDRAW_LIVE_FAIL`
and returns before the display exists (`vector_v2_app_start.cpp:70-76`;
display emplace at `:127`), and the outer caller simply returns into an idle
task (`vector_v2_app.cpp:802-804`). Result: PSRAM exhaustion yields a black
screen with diagnostics on a console nobody is watching, until power cycle.

Severity note: the leaked buffers are inert — no retry path exists, and heap
resets on reboot — so this is a liveness/diagnostics defect, not a resource
leak in steady state. The leak still matters for any future retry path, which
is why Fix 2 remains worthwhile hygiene. The substantive fix is
display-first initialization so failure is visible on glass.

### Medium

#### M1 — V2's portability is real but misplaced

The transport contract header is IDF-free — standard headers plus
`tinydraw/platform/display_backend.h` only
(`co5300_panel_transport.h:3-9`) — and Puck implements it unmodified as an
intentional emulator (`puck/platform/co5300_panel_transport.cpp:1-3`),
including `class Co5300PanelTransport final : public DisplayBackend`
(`co5300_panel_transport.h:94`). So the display seam exists; the earlier
"clone"/"no seam" framing was wrong and is withdrawn. What survives:

- The portable contract physically lives under `esp32/main/`.
- `VectorV2Presenter` binds the concrete type (`Co5300PanelTransport&`,
  `vector_v2_presenter.h:255`) rather than the interface it already
  implements.
- `host/` cannot host V2 because of build wiring alone
  (`host/CMakeLists.txt:6-9` links core only) — not because an abstraction is
  missing.

Reclassifying from High to Medium changes the remedy: not interface
extraction, but relocation of an already-portable header plus a host (or
SDL-backed) implementation of it, giving the repo's most complex code a
desktop development loop. Puck's emulator implementation stays exactly as it
is; that is what emulation is.

#### M2 — Unbounded synchronous drain on the lift→history path

`drain_boundary()` loops `while (absorption_.active() || pending != 0)` with a
per-slice budget (`kInPlaceRetentionBudgetUs = 10 ms`) but no total deadline —
`started` is logging-only (`vector_v2_background_pipeline.cpp:199-245`) — and
runs synchronously inside tap handling before the action applies
(`vector_v2_app.cpp:632`). Pending work is capped at
`kPendingOperationHighWater = 24` (`presenter.h:63`), so worst case is tens to
hundreds of milliseconds between tap and feedback — precisely after heavy
strokes, when pending is deepest.

The governing contract (found and verified): `move_history_incrementally`
requires "Pending appends must be drained so log and canvas revisions are
equal before entry" (`incremental_document.h:243-245`), and
`vector_v2_app.cpp:632-634` upholds it by gating `chrome_controller.apply` on
`drain_boundary()` returning lockstep. Any naive deadline that breaks out of
the drain early returns false and silently drops the Undo/Redo tap — the
first edition's proposed diff did exactly that and was retracted (Appendix A,
challenge 1).

Correct remedy shape (design required before diffing): keep per-slice budgets;
add a total deadline; on expiry, do not drop the action — acknowledge it
immediately through the existing hourglass machinery
(`chrome_.history_busy` + busy-region present, `chrome_controller.cpp:322-327`),
finish the drain asynchronously at elevated urgency, and apply the remembered
action once lockstep resumes. That bounds acknowledgment latency, upholds the
engine contract, and drops nothing.

#### M3 — Fake seams and dead abstractions

- `live_ink_coordinator.process_live_ink_move` has zero product callers; the
  product re-implements visual-before-authority ordering inline
  (`vector_v2_live_stroke_session.cpp:118-127`). Its header also publicly
  includes core ribbon geometry while CMake links core PRIVATE.

The dead abstractions from this finding's first edition moved to L3 in the
final numbering.

#### M4 — Duplication is already drifting — proof attached

Three toolbar switches exist (host, esp32 V1, rp2350), and rp2350 has already
mutated: `kSelectPan` sets `toolbar.tool = kPen`
(`rp2350/src/main.cpp:280-283`) where both other copies set `kPan`
(`host/main.cpp:408-411`, `hardware_app.cpp:473-476`) — a live bug delivered
by copy-paste. The same panel fact is encoded twice under unrelated names:
`kMainToolbarOverlayTop = 372` (`core/include/tinydraw/ui/toolbar.h:13`) vs
`kChromeCanvasBottom = 372` (`vector_v2/include/tinydraw/vector_v2/chrome.h:30`).
Also: two demo_tape modules, two transport implementations (one of them the
legitimate Puck emulator, but still hand-synced against golden traces), two
retain-pipeline implementations (L1). The pattern is systemic.

#### M5 — The shared kernel is real but unowned; dead sources ride in firmware builds

V2 production consumes six core artifacts (`CurvedRibbonStream`/`RibbonUpdate`,
`PixelPainter`, `RibbonRenderer`+coverage_tile at
`vector_v2_presenter.cpp:466`, `png_encoder`, `geometry.h`, `InkStream`), all
buried in the V1 tree with hand-maintained source lists. Three sources sit
unconditionally in firmware source lists with no non-gated callers:
`coordinate_transform.cpp` (`esp32/main/CMakeLists.txt:17`),
`perfect_freehand.cpp` (:22), `vector_document.cpp` (:80 — its only V2-side
caller is the device-gated harness, `vector_v2_gate_harness.cpp:87`). Cost
correction from the second pass: whether these reach the shipped ELF is
unverified — unreferenced static-lib members are typically dropped by the
linker, and no linker map was produced. The verified cost is compile time and
drift risk; the flash question is answerable in minutes from `out/` ELF sizes
with and without the three TUs. No reverse deps exist (deleting vector_v2
cannot break core), but nothing marks these six files as load-bearing for two
products.

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

#### M7 — Always-on diagnostic printf in ship builds' latency-critical paths

`print_stroke`, `print_lift_baseline`, `print_presentation`,
`print_pan_baseline` (`vector_v2_app_diagnostics.cpp:48-172`) are ungated;
every stroke lift emits two multi-hundred-byte lines over UART inside the very
paths being measured. The code even patches around its own pollution ("Keep it
out of the pre-existing stroke poll-gap metric", `vector_v2_app.cpp:782-784`).

### Low

#### L1 — Dual retain-pipeline implementations can diverge

Synchronous `retain_uniform_tile` (`incremental_document.cpp:114ff`) versus
resumable state machine (`incremental_document_absorption.cpp:355ff`).
Corrected severity from the second pass: both paths are host-tested — the
resumable slice API directly (`incremental_document_test.cpp:137-153`, >100
injected pauses asserted) and the sync path via `absorb_pending_operation` —
and the device gate harness exercises both as well. The finding is divergence
risk between parallel implementations with already-different budget
semantics, not a test-coverage hole.

#### L2 — Language drift on "Operation"

CONTEXT.md's `_Avoid_: Operation` note (:7) governs synonyms for the
whole-gesture Stroke concept; the "chunk" example from the first edition was a
misread and is withdrawn (`Stroke chunk` is defined, sanctioned vocabulary,
:9-12). What survives: the authority record is gesture-granular
(`OperationRecord.gesture_id`, one Stroke's samples per record,
`operation.h:36-48`), yet the codebase names it `Operation*` (~28 types) and
`PROJECT_STATE.md` itself says "102 active operations." Real prose-vs-code
vocabulary drift; cosmetic-to-moderate impact.

#### L3 — Dead abstractions

- `TouchTransform::touch_to_logical` (`coordinate_transform.h:11-19`): zero
  production callers; real mapping happens four different implicit ways
  (CST816S logical extents `physical_touch.cpp:40-41`, host bounds-check
  `input_coordinates.h:12-17`, rp2350 raw cast, puck injection).
- Dead wire kinds `kReset`/`kHistoryUpdate`: decodable, never encodable by
  anything in the repo.

#### L4 — README's "shared C++20 modules" are plain headers

Zero `.cppm` files and zero `import` statements in the repo (README.md:11).

#### L5 — Housekeeping

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
- The `move_history_incrementally` lockstep precondition
  (`incremental_document.h:243-245`) is the model for what H2's other
  contracts should look like: documented in the engine header, enforced at a
  single call site.
- Puck is honest, maintained, and golden-trace-verified; it compiles the real
  firmware TUs against a documented fake-IDF surface.
- Toolchain pinning (SHA256-pinned wasi-sdk, ARM gcc) is exemplary.
- Doc claims that survived attack: 604-slot pool (`memory_layout.h:37`),
  4 MiB journal + partition map, zoom enum exactly 25–400%, AA receipt numbers
  matching digit-for-digit.

## Suggested fixes

Hand-written unified diffs against this checkout, review-sized. Four diffs
survived the second pass unchanged in substance; the fifth was retracted and
replaced by a design note grounded in the verified contract.

### Fix 1 — CI running the existing suites (C1)

New file. Enforces what `PROJECT_STATE.md` already claims, using the repo's
own scripts. Runner/cache details may need tuning; the commands are exactly
the documented ones. Scope caveat: this enforces the host regression perimeter
only — physical-battery and glass claims remain manual by nature.

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
rewritten SHAs to their successors, retag `v2` to match what
`VECTOR_V2_RELEASE_2026_08_19.md:312` claims (or correct the receipt), and add
a guard that fails CI if any doc cites a SHA missing from history.

### Fix 2 — AppStorage failure-path cleanup (H4)

Hygiene for any future retry path; the liveness fix itself is display-first
initialization below. Free everything allocated so far when allocation fails
instead of leaking the partial set. Nothing has been `std::construct_at`-ed at
the failure point (the construct loops run after the null check), so freeing
raw buffers is safe. `heap_caps_free(nullptr)` is a no-op, so unconditional
frees are fine.

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
be shown on glass instead of printing to a console nobody is watching. That
companion change is the actual H4 fix; this diff is supporting hygiene.

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

### Fix 4 — RETRACTED; design note for M2

The first edition proposed a total-deadline break inside
`drain_boundary()`. The second pass proved that diff unsafe: breaking early
returns `lockstep == false`, and `vector_v2_app.cpp:632-634` gates
`chrome_controller.apply` on exactly that boolean — so deep pending work plus
an expired deadline would silently swallow the Undo/Redo tap. Retracted in
full; no replacement diff is offered until the design below is agreed.

The verified contract the remedy must respect:

> "Pending appends must be drained so log and canvas revisions are equal
> before entry." — `move_history_incrementally`,
> `vector_v2/include/tinydraw/vector_v2/incremental_document.h:243-245`

Enforced today at its single call site: `drain_boundary(kHistory)` absorbs
pending work synchronously and returns revision lockstep
(`vector_v2_background_pipeline.cpp:225,252`), gating the apply
(`vector_v2_app.cpp:632-634`).

Design sketch consistent with the contract and ship tenets:

1. Keep per-slice absorption budgets; add a total deadline to the synchronous
   drain.
2. On expiry, do not drop the action: raise the hourglass immediately
   (`chrome_.history_busy` + busy-region present already exist,
   `chrome_controller.cpp:322-327`), remember the requested history action,
   and finish the drain through the background pipeline at elevated urgency
   (the idle path already preempts on touch urgency; history-completion needs
   its own priority lane).
3. Apply the remembered action when `pending_operation_count(log_, canvas_)`
   reaches zero, then consume `take_history_damage()` exactly as the
   synchronous path does today.

This bounds acknowledgment latency (hourglass on tap), upholds the engine
precondition, and drops nothing. It needs owner sign-off because it changes
when authority moves relative to the tap — a product-feel decision, not just
a mechanical one.

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

1. **Relocate V2's portable transport contract (M1).** Move
   `co5300_panel_transport.h` (already IDF-free) to a target-neutral home and
   add a host implementation so the V2 app layer gains a desktop dev loop.
   Puck's emulator implementation is untouched by this — it is a legitimate
   port, not debt.
2. **Executable serialization contracts (H2).** Fold cancel-before-absorb
   into one pipeline-owned function, or add debug-build canaries (ownership
   token checked at absorb entry). The `move_history_incrementally`
   precondition shows the house style: document in the engine header, enforce
   at one call site. Extend that pattern to the scratch-aliasing contracts.
3. **Single retain pipeline (L1).** Make the resumable implementation the only
   one, following the repo's own `stage_authority_journal` precedent, so
   budget semantics cannot fork further.
4. **Named kernel target (M5).** Promote the six shared artifacts into an
   explicitly named, PUBLIC-linked target; capture a linker map to settle the
   dead-source flash question, then purge whatever the map confirms is
   unreferenced.
5. **Teardown liveness (M6).** Replace autosave `vTaskDelete` with the touch
   sampler's join-handshake pattern; give product builds an actually-exercised
   shutdown path.
6. **Cross-generation storage policy (C2).** Write down the intended behavior
   for foreign-format bytes on the shared partition, add snapshot CRCs before
   any destructive erase, and make the ship contract match the code (or vice
   versa).
7. **Quality perimeter (H3).** Link `tinydraw_project_options` into pngenc,
   rp2350, and the IDF engine compilation; gate diagnostic printf behind
   build levels so ship images stop paying UART jitter inside measured paths.

## Appendix A — Second-pass challenges and corrections (2026-08-21)

A follow-up adversarial pass challenged seven claims above. Each challenge was
re-verified against source; all seven were accepted (five outright, two with
nuance); none rejected. This appendix preserves the original challenge
verdicts verbatim as the audit trail; the corrections are incorporated inline
above.

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
unsafe and is retracted.

### 2. M1 overstated test divergence — CORRECTED (challenge: High)

Accepted. Host tests exercise the resumable path directly:
`vector_v2/tests/incremental_document_test.cpp:137-153` drives
`absorb_pending_operation_slice` with injected yields (>100 pauses asserted),
completion, and lockstep checks; the device gate harness also calls the sync
path. Both retain implementations are host-tested, so "host gates run the
sync one, device runs the resumable one" was wrong. Severity dropped to Low.

### 3. H2 mislabeled Puck's transport — CORRECTED (challenge: High)

Largely accepted. Verified facts that reframed it: the transport contract
header is IDF-free (`co5300_panel_transport.h:3-9`); Puck implements it
unmodified by design (`puck/platform/co5300_panel_transport.cpp:1-3`);
`Co5300PanelTransport` already implements `DisplayBackend`
(`co5300_panel_transport.h:94`). Interface extraction would not delete
Puck's file. Reclassified from High ("no seam") to Medium
(composition/placement).

### 4. C1 overstatements — PARTIALLY ACCEPTED (challenge: Medium)

Facts that stand: `git cat-file a5db58d` fails; no CI config exists.
Overstatements corrected: "unrecoverable" was too strong — checksummed logs
(`VECTOR_V2_RELEASE_2026_08_19.md:52-54`) and
`.codex-archaeology/git-history.md:310-312` preserve the chain; tags `2.0.0`
and `2.1.0` anchor post-rewrite history. Scope caveat accepted: proposed CI
enforces host suites only. Bonus verified this pass: the release receipt's
own tag claim (`:312`) is now false — receipts drifted too.

### 5. M5 flash-cost claim unevidenced — ACCEPTED (challenge: Medium)

Accepted. `vector_document.cpp` sits unconditionally in the V2 firmware
source list (`esp32/main/CMakeLists.txt:80`); its only non-test caller is the
device-gated harness (`vector_v2_gate_harness.cpp:87`). But nothing
established linked-size cost; static-library members without references are
typically dropped. Restated as compile-time weight and drift risk; flash cost
unverified pending a linker map.

### 6. M7 misread the language document — CORRECTED (challenge: Medium)

Accepted on the "chunk" half. `CONTEXT.md:9-12` defines Stroke chunk as
legitimate internal vocabulary; `maximum_chunk_samples` is compliant, and
that example was withdrawn. The "Operation" half survives as Low-severity
prose-vs-code vocabulary drift.

### 7. C3 severity inflation — ACCEPTED (challenge: severity)

Accepted. On start failure `vector_v2_app.cpp:802-804` simply returns: no
retry loop exists, so the leak is inert and heap resets on reboot. The genuine
defect is the wedged black screen. Downgraded from Critical to High
(final H4) and reframed as liveness/diagnostics.

### Surviving core

Confirmed by both passes: C2, M2's diagnosis (remedy replaced), M4, M6, M7
(formerly M8).
