# Vector V2 foundation

This directory contains TinyDraw V2's platform-independent,
vector-authoritative architecture. TinyDraw V1 remains a separately supported
raster product; V2 modules are integrated by the V2 firmware and host tests.

Current state lives in [`PROJECT_STATE.md`](../PROJECT_STATE.md), and the complete forward worklist lives in [`V2_ROADMAP.md`](../V2_ROADMAP.md). Historical prototype plans belong in [`docs/archive/`](../docs/archive/).

## Dependency rule

Vector V2 may reuse stable, platform-independent core mechanisms by dependency. It must not copy them.

The Vector V2 module must not depend on:

- `WorldCanvas`, `ViewOrigin`, or the 3×3 raster geometry;
- `FirmwareCanvas` or ESP-IDF allocation;
- benchmark coordinators or archived prototype policy;
- hardware display, toolbar, persistence, or task-loop policy;
- camera-aligned atlas identities or arbitrary zoom values.

Adapters outside this directory connect V2 modules to the app and hardware. They must not move platform details into V2 interfaces.

## Module rules

- Prefer deep modules with small interfaces.
- Keep state deterministic and host-testable.
- Use caller-owned fixed-capacity storage; no hidden allocation.
- Represent the bounded 1472×1792 world and committed zoom levels explicitly.
- Keep source identity, quality, provenance, and document revision together in
  validated values rather than parallel option arrays.
- Never expose mutable cache storage through lookup results.
- Do not hold state locks while waiting for display capacity or transfer completion.
- Do not add speculative seams. Add an adapter only when a real second implementation or test substitute exists.

## Automated guards

Vector V2 is compiled through the independent `tinydraw::vector_v2` CMake target with the project's warnings-as-errors policy. It is also covered by the repository formatter and two V2-scoped analyzers:

```sh
./scripts/dev format-check
./scripts/dev tidy
./scripts/dev cppcheck
```

`clang-tidy` treats its curated analyzer, bug-prone, performance, portability, function-size, and cognitive-complexity findings as errors. Cppcheck supplies an independent C++20 analysis pass. Neither analyzer scans the frozen legacy or prototype tree.

## Integration rule

Product behavior enters through one narrow adapter after its module contract is
proved independently.

1. Build and review the V2 module through host tests.
2. Prove its memory layout where required.
3. Add one narrow adapter to the V2 application.
4. Validate behavior and hardware gates.
5. Remove superseded diagnostic or prototype paths once production owns the responsibility.

The retired prototype remains evidence and benchmark machinery. It is frozen except for evidence-preservation fixes.

## Validated V2 foundation

The initial production milestones and Gate 1 cache/interaction feasibility are complete. The vector-authoritative architecture is accepted for V2. See [`GATE_1_RECEIPT_2026_08_13.md`](../docs/archive/2026-08-vector-v2-performance/GATE_1_RECEIPT_2026_08_13.md) and [`GATE_1_CACHE_CLOSURE_2026_08_13.md`](../docs/archive/2026-08-vector-v2-performance/GATE_1_CACHE_CLOSURE_2026_08_13.md). Remaining work is tracked only in the V2 roadmap; the numbered sections below preserve architectural history and contracts rather than current task order.

The SVG module streams renderer-derived ribbon geometry without document-sized
storage. Adjacent internal chunks with one nonzero gesture ID become one
painter-ordered filled path, so one physical finger-down/up Stroke remains one
SVG path. Paths contain round-cap and variable-width convex subpaths from the
shared curve authority. SVG spans meet at exact section boundaries; device and
PNG raster spans retain a 0.75 px overlap that prevents fixed-grid cracks.
Erasers use painter-ordered masks, and the root omits a synthetic background
rectangle. The ESP adapter preserves those SVG bytes and
also streams `DRAWING.PNG` from production settled-AA windows. It retains one
64-row world band, one 64×64 window, and fixed PNGenc workspace. The existing
Saving progress bar advances across both PNG windows and SVG operations. A
shared metadata page commits the pair only after the authority epoch, revision,
and operation count are rechecked. The generic FAT/USB adapter exposes both
files read-only. Export presentation is an explicit app mode: host eject
latches the medium absent, and the on-screen **Return to Drawing** action
deinitializes TinyUSB and releases the USB PHY without a board reset. A failed
shutdown remains modal and offers another return attempt rather than re-enabling
drawing over a live USB stack. Host
coverage/fuzz tests and physical evidence are in the
[`original receipt`](../benchmark-results/svg-export-2026-08-17/RECEIPT.md)
and [`stroke-grouping follow-up`](../benchmark-results/stroke-svg-minimap-acquire-2026-08-17/RECEIPT.md).

The ESP application reuses the platform `RtcClock` and one-shot Wi-Fi/NTP
implementation through `TimeSyncController`. The document popup supplies the
only trigger; controller status crosses the task boundary atomically while the
main loop alone owns Chrome state. Connecting and synchronizing are modal, and
a terminal success/error becomes visible only after the task has stopped and
deinitialized Wi-Fi. Credentials remain in the ignored local header. The owner
accepted unavailable-network handling, successful RTC sync, centering, and text
size on glass. Evidence is in the
[`NTP receipt`](../benchmark-results/v2-ntp-sync-2026-08-17/RECEIPT.md) and
[`text-size follow-up`](../benchmark-results/ntp-text-size-2026-08-17/RECEIPT.md).

Vector persistence uses the portable `authority_journal` module and one ESP
flash adapter. Journal commits persist retained operations/samples, the active
Redo boundary, generation, and epoch; navigation, chrome selections, the next
Stroke counter, and raster caches are not persisted. Navigation and chrome
restart from defaults, and the next Stroke identity is derived from restored
active authority. Each
transaction occupies aligned 4 KiB sectors and publishes its CRC-checked final
marker last. The ESP worker performs erase/write/readback below the touch task,
while startup replays only active authority into a fresh overview. The current
3 MiB journal preserves existing Recovery points and reports full rather than
compacting; owner direction defers two-arena recycling and metadata.

Navigation stores the current zoom/origin and one world-space focus. Every zoom
transition derives and clamps its target origin from that focus; no dormant
per-zoom origin is retained. This focus-centered model supersedes the earlier
exact-origin restoration receipt.

## First milestone: `MaterializedCanvas`

Task #52 may initially add only:

- production world, zoom, tile, revision, quality, and provenance identities;
- a pure `MaterializedCanvas` state module;
- caller-owned overview and fixed-capacity slot state;
- host tests for mapping, replacement, revision transitions, and overview fallback;
- build wiring required by those host tests.

It must not edit `hardware_app.cpp`, add an ESP32 adapter, allocate PSRAM, change `WorldCanvas`, or extend the retired coordinator.

### Initial geometry and memory arithmetic

The state seam uses 64×64 RGB565 world tiles. This is an initial publication
identity, not a permanent renderer-work size:

- complete 25% overview: `368 × 448 × 2 = 329,728` bytes;
- one tile: `64 × 64 × 2 = 8,192` bytes;
- a 320-tile pixel pool: `2,621,440` bytes plus slot metadata; enough for
  five worst-case arbitrary-alignment viewport footprints (`5 × 56 = 280`
  tiles), leaving 40 additional LRU slots;
- 100% world grid: `ceil(1472 / 64) × ceil(1792 / 64) = 23 × 28` keys.

`MaterializedCanvas` owns no hidden allocation. The overview pixels and fixed
slot metadata are caller-owned; pixel-pool allocation and its hardware receipt
belong to Task #53.

## Task #53 memory plan

[`memory_layout.h`](include/tinydraw/vector_v2/memory_layout.h) records the
first complete allocation plan. Its capacity assumptions are explicit:

- 4,000 operations and 80,000 compact 8-byte samples;
- four materialized zoom LOD span tables and 90,000 compact 6-byte LOD samples;
- 320 tile slots;
- two 128×128 renderer workspaces plus 64 KiB shared geometry storage;
- a 368×76 overlay and two 368×32 staging strips;
- a second 368×448 overview buffer for transactional revision publication.

The retired memory-probe firmware allocated every planned region simultaneously,
then attempted a separate 1.5 MiB allocation. Its archived result is an allocation
receipt, not proof that current encoders and renderers meet interaction gates.

The current empty-heap ESP32-S3 allocation receipt is
[`hardware-receipts/636b9c7-memory-layout-320.log`](https://github.com/aliceisjustplaying/tinydraw/blob/v2-feature-complete-pre-cleanup/vector_v2/hardware-receipts/636b9c7-memory-layout-320.log):

- both the live and next-revision overviews are explicitly budgeted;
- the 320-slot plan allocated all **4,948,576 bytes**;
- largest contiguous block after the plan: 3,342,336 bytes;
- a separate 1,572,864-byte reserve allocation succeeded;
- free/largest after holding both plan and reserve: 1,832,000 / 1,802,240 bytes.

The earlier 128-slot and pre-publication-buffer receipts remain archived as
[`hardware-receipts/756e080-memory-layout.log`](https://github.com/aliceisjustplaying/tinydraw/blob/v2-feature-complete-pre-cleanup/vector_v2/hardware-receipts/756e080-memory-layout.log)
and [`hardware-receipts/1f91ed0-memory-layout.log`](https://github.com/aliceisjustplaying/tinydraw/blob/v2-feature-complete-pre-cleanup/vector_v2/hardware-receipts/1f91ed0-memory-layout.log).

This proves only that the provisional external-memory slabs and reserve are
simultaneously allocatable on an otherwise empty 8 MiB PSRAM heap. It does not
close Task #53's product or strangler-coexistence gate: the legacy raster arenas,
internal DMA heap, export workspace, Wi-Fi, USB, and eventual renderer behavior
were not live. Captured workload distributions must also validate that the
capacities are sufficient rather than merely allocatable.

No representative captured operation document is checked into this repository.
The only 1,000-stroke corpus is the deterministic synthetic handwriting
generator; seed 7 produces 20,153 samples (20.153 samples/stroke, 200 maximum),
which fits the provisional 4,000-operation/80,000-sample ratio by a narrow
extrapolation only. Small `.stroke` files under `testdata/` are UI and raster
correctness fixtures, not capacity evidence.

A two-minute private real-touch capture now supplies aggregate sizing evidence
without committing the user's coordinates. Its 70 strokes and 1,189 points
project to 67,942 source points at 4,000 strokes, but four independent LOD copies
project to 225,600–254,057 points across the tested policies. See
[`REAL_TOUCH_CHARACTERIZATION.md`](../docs/archive/2026-08-vector-v2-performance/REAL_TOUCH_CHARACTERIZATION.md). This rejects
the current 90,000-point capacity/model combination and requires a shared,
nested, or on-demand LOD experiment before approving a simplifier. It is not a
representative full-document capacity receipt.

## Task #54a host fallback oracle

`MaterializedCanvas::compose_view` is a host oracle that produces a complete
requested rectangle from current-revision world tiles and fills every cache miss
from the complete current-revision overview. Overview publication commits the
new document revision transactionally, so ordinary mutation does not expose a
new revision before its fallback source exists. It never labels stale tile or
overview pixels as current. The only rejected requests are malformed/out-of-world
rectangles or a bootstrap state where no current source covers every pixel.

The minimal hardware seam is recorded in
[`hardware-receipts/3b69d59-production-overview-walk.log`](https://github.com/aliceisjustplaying/tinydraw/blob/v2-feature-complete-pre-cleanup/vector_v2/hardware-receipts/3b69d59-production-overview-walk.log).
An exclusive ESP32 image transferred the 25% overview directly, then composed
four complete 100% views in bounded 368×22 strips. Those views completed in
27 ms with fail-closed expected hashes, 105/105 physical transfers complete, and no
CO5300 window rejection. This proves the bounded fallback path on glass, but
not the interactive ≤35 ms gate under concurrent product workloads.

Host tests remain the oracle for the no-checkerboard composition policy. The
same walk passed after the instance-owned CO5300 transport extraction at
[`hardware-receipts/1b0710a-panel-transport-walk.log`](https://github.com/aliceisjustplaying/tinydraw/blob/v2-feature-complete-pre-cleanup/vector_v2/hardware-receipts/1b0710a-panel-transport-walk.log).
The exclusive walk now depends directly on transport, not the legacy toolbar
compositor. Final scheduler behavior, incremental publication, and the ≤35 ms
pan gate require the later display scheduler; no claim about those hardware
gates is made here.

## Task #55 revision seam

Deferred absorption does not use `publish_overview`, which deliberately replaces
a whole revision and invalidates all tiles. The committed-overlay path provides
this behavior:

1. The renderer copies only the conservative affected overview rectangle into
   caller-owned compact scratch and applies operation N there.
2. The canvas validates the next revision, exact overview bounds and affected
   tile keys before changing revision metadata.
3. One commit copies the bounded overview rows, carries retained resident
   tiles forward to revision N, and invalidates the rest.
   Failure leaves the prior revision and every source identity unchanged.
4. Missing affected tile pixels remain valid overview fallback and may be
   republished from incremental scratch later; operations 1…N−1 are never
   replayed.

Affected identity is now expressed as conservative world bounds, so an
operation cannot accidentally carry stale intersecting residents at other zooms
forward as current. `append_authority_only` publishes the operation immediately;
`absorb_pending_operation` advances materialization through caller-owned,
alias-checked overview, tile-key, and mask scratch. Pending operations patch
presentation from vector truth until absorption catches up. Snapshot restore must go through
`restore_document_snapshot`, which validates both modules before replacing the
canvas overview and resetting operation authority to the same revision.

`OperationLog` also owns the active Stroke-chunk prefix and retained Redo tail.
Undo/Redo advances one monotonic generation across every adjacent chunk with the
same nonzero Stroke identity; new ink truncates Redo only when publication
succeeds. `move_history_incrementally` rebuilds the affected overview rectangle
from paper and active painter order, invalidates only intersecting tile
identities, then publishes the prepared history transition. Host fixtures cover
at least ten levels, canceled/rejected branches, a pen line cut by an eraser,
and producer reconstruction after history. Product buttons drain deferred ink
before this transaction and refresh bounded canvas damage plus the dock;
correctness and ordinary-glass checks are green. High-zoom affected-region
rebuilding remains the final history performance problem.

The interface expresses a revision plus bounded affected-tile publications; it
does not expose mutable pool storage or renderer callbacks. The current tile
size and slot count are part of the measured memory/cache baseline; changing
them reopens retention, cold, and export-reserve gates. Callers
serialize composition and publication, so source lookup does not expose a
second lifetime protocol.

The immediate renderer is intentionally opaque and hard-edged; publications are
labeled `kImmediate`, below `kSettled`. `kSettled` is reserved for anti-aliased
output, so provisional pixels cannot masquerade as accepted quality. Long sparse
segments are subdivided into bounded raster steps, and tile enumeration reports
required versus written capacity. Analytic settled convergence is implemented
and appearance is accepted. Progression remains an open performance gate: the
current 25% verification reached 76.416 ms for one tile, above the nominal 8 ms
cooperative slice.

The current presenter validates each strip and sends it directly to the panel
transport. The former synchronous `DisplayScheduler` queue was removed because
no production path ever queued behind an in-flight strip. Existing receipts
collectively cover transport, cold replay, interaction pacing, cache retention,
long gestures, and export. The cleanup/application split passed the current
448-slot hardware battery; the final release candidate still needs its own
same-head battery.

A separate PSRAM measurement compared full copy, swappable double buffers, and
bounded dirty-region publication; its method and limits are archived in
[`OVERVIEW_PUBLICATION_MEASUREMENT_2026_08_13.md`](../docs/archive/2026-08-raster-and-vector-prototypes/OVERVIEW_PUBLICATION_MEASUREMENT_2026_08_13.md).
After adopting the bounded path, the exact-commit hardware receipt
[`hardware-receipts/16dc9b2-bounded-overview-publication.log`](https://github.com/aliceisjustplaying/tinydraw/blob/v2-feature-complete-pre-cleanup/vector_v2/hardware-receipts/16dc9b2-bounded-overview-publication.log)
preserved every expected hash and all 168 ordered transfers with zero rejects.
The deterministic 30-operation burst fell from 50.295 ms to 6.404 ms average
under the existing outer probe; warm individual coordinated appends were about
0.97–0.99 ms. These receipts prove bounded input collection and ordered staging
on glass, not real touch input, concurrent mutation and transport,
representative capacity, settled anti-aliasing, or the final interaction gates.

## Task #56 settlement prerequisite

A settled renderer cannot safely anti-alias in place over the hard-edged immediate
materialization: partial edge coverage blended over already-painted edge pixels
cannot remove the immediate renderer's wider binary silhouette. Settlement must
start from an explicit pre-operation source or checkpoint and replay a contiguous
painter-ordered operation range. Its interface must therefore identify the
baseline revision, destination revision, and operation range together.
Do not hide these requirements behind a `settle(current_pixels)` helper or copy
the rejected prototype renderer into Vector V2.

`OperationLog::replay_range()` is the first bounded ownership seam: it accepts an
explicit log epoch, baseline revision, and destination revision and returns only
a contiguous range that is still represented after the current snapshot base.
Every reset advances the epoch, so revision values reused after restore cannot
validate a checkpoint from the old document history. It exposes no range while
an append is prepared. The settled renderer must consume this range in painter
order; it must not infer history from the current hard-edged pixels.

A host-only settlement rehearsal snapshots the overview at an epoch/revision,
revalidates and consumes an eight-operation range in two slices through the
existing immediate rasterizer, and proves byte equality with live materialization.
It then restores an older snapshot, rejects the held epoch, rebases, and repeats.
This validates ordering and reset ownership only; it is not settled-renderer
quality or performance evidence.
