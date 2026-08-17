# Vector V2 foundation

This directory contains TinyDraw's platform-independent, vector-authoritative V2 architecture. It is **not** a clean-room rewrite. Raster V1 remains runnable while V2 modules replace it one behavior at a time.

Current state lives in [`PROJECT_STATE.md`](../PROJECT_STATE.md), and the complete forward worklist lives in [`V2_ROADMAP.md`](../V2_ROADMAP.md). Historical prototype plans belong in [`docs/archive/`](../docs/archive/).

## Dependency rule

Vector V2 may reuse stable, platform-independent core mechanisms by dependency. It must not copy them.

The Vector V2 module must not depend on:

- `WorldCanvas`, `ViewOrigin`, or the 3×3 raster geometry;
- `FirmwareCanvas` or ESP-IDF allocation;
- `interactive_pan_benchmark` or other benchmark coordinators;
- hardware display, toolbar, persistence, or task-loop policy;
- camera-aligned atlas identities or arbitrary zoom values.

Adapters outside this directory connect V2 modules to the app and hardware. They must not move platform details into V2 interfaces.

## Module rules

- Prefer deep modules with small interfaces.
- Keep state deterministic and host-testable.
- Use caller-owned fixed-capacity storage; no hidden allocation.
- Represent the bounded 1472×1792 world and committed zoom levels explicitly.
- Keep source identity, quality, provenance, generation, and document revision together in validated values rather than parallel option arrays.
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

## Migration rule

Every V2 milestone must replace or prepare to replace an identified Raster V1 responsibility. Do not create an indefinite third path.

1. Build and review the V2 module through host tests.
2. Prove its memory layout where required.
3. Add one narrow adapter to the V2 application.
4. Validate behavior and hardware gates.
5. Remove the superseded legacy path when the production path owns that responsibility.

The retired prototype remains evidence and benchmark machinery. It is frozen except for evidence-preservation fixes.

## Validated V2 foundation

The initial production milestones and Gate 1 cache/interaction feasibility are complete. The vector-authoritative architecture is accepted for V2. See [`GATE_1_RECEIPT_2026_08_13.md`](GATE_1_RECEIPT_2026_08_13.md) and [`GATE_1_CACHE_CLOSURE_2026_08_13.md`](GATE_1_CACHE_CLOSURE_2026_08_13.md). Remaining work is tracked only in the V2 roadmap; the numbered sections below preserve architectural history and contracts rather than current task order.

The SVG module streams renderer-derived ribbon geometry without document-sized
storage. Adjacent internal chunks with one nonzero gesture ID become one
painter-ordered filled path, so one physical finger-down/up Stroke remains one
SVG path. Paths contain the exact round-cap and variable-width convex subpaths;
erasers emit background-colored paths in operation order, and the root omits a
synthetic background rectangle. The ESP adapter preserves those SVG bytes and
also streams `DRAWING.PNG` from production settled-AA windows. It retains one
64-row world band, one 64×64
window, and fixed PNGenc workspace. A shared metadata page commits the pair
only after the authority epoch, revision, and operation count are rechecked.
The generic FAT/USB adapter exposes both files read-only. SVG host coverage/fuzz
tests and physical evidence are in the
[`original receipt`](../benchmark-results/svg-export-2026-08-17/RECEIPT.md)
and [`stroke-grouping follow-up`](../benchmark-results/stroke-svg-minimap-acquire-2026-08-17/RECEIPT.md).

The ESP application reuses the platform `RtcClock` and one-shot Wi-Fi/NTP
implementation through `TimeSyncController`. The document popup supplies the
only trigger; controller status crosses the task boundary atomically while the
main loop alone owns Chrome state. Connecting and synchronizing are modal, and
a terminal success/error becomes visible only after the task has stopped and
deinitialized Wi-Fi. Credentials remain in the ignored local header. Host and
build evidence is in the
[`NTP receipt`](../benchmark-results/v2-ntp-sync-2026-08-17/RECEIPT.md).

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

The opt-in `TINYDRAW_VECTOR_V2_MEMORY_PROBE` firmware allocates every region
simultaneously, then attempts a separate 1.5 MiB allocation. This is an
allocation receipt, not proof that the eventual encoders and renderers fit the
reserved capacities or meet interaction gates.

The current empty-heap ESP32-S3 allocation receipt is
[`hardware-receipts/636b9c7-memory-layout-320.log`](hardware-receipts/636b9c7-memory-layout-320.log):

- both the live and next-revision overviews are explicitly budgeted;
- the 320-slot plan allocated all **4,948,576 bytes**;
- largest contiguous block after the plan: 3,342,336 bytes;
- a separate 1,572,864-byte reserve allocation succeeded;
- free/largest after holding both plan and reserve: 1,832,000 / 1,802,240 bytes.

The earlier 128-slot and pre-publication-buffer receipts remain archived as
[`hardware-receipts/756e080-memory-layout.log`](hardware-receipts/756e080-memory-layout.log)
and [`hardware-receipts/1f91ed0-memory-layout.log`](hardware-receipts/1f91ed0-memory-layout.log).

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
[`REAL_TOUCH_CHARACTERIZATION.md`](REAL_TOUCH_CHARACTERIZATION.md). This rejects
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
[`hardware-receipts/3b69d59-production-overview-walk.log`](hardware-receipts/3b69d59-production-overview-walk.log).
An exclusive ESP32 image transferred the 25% overview directly, then composed
four complete 100% views in bounded 368×22 strips. Those views completed in
27 ms with fail-closed expected hashes, 105/105 physical transfers complete, and no
CO5300 window rejection. This proves the bounded fallback path on glass, but
not the interactive ≤35 ms gate under concurrent product workloads.

Host tests remain the oracle for the no-checkerboard composition policy. The
same walk passed after the instance-owned CO5300 transport extraction at
[`hardware-receipts/1b0710a-panel-transport-walk.log`](hardware-receipts/1b0710a-panel-transport-walk.log).
The exclusive walk now depends directly on transport, not the legacy toolbar
compositor. Final scheduler behavior, incremental publication, and the ≤35 ms
pan gate require the later display scheduler; no claim about those hardware
gates is made here.

## Task #55 revision seam

Incremental append does not use `publish_overview`, which deliberately replaces
a whole revision and invalidates all tiles. `commit_incremental_revision` now
provides this transactional behavior:

1. The renderer copies only the conservative affected overview rectangle into
   caller-owned compact scratch, applies operation N there, and prepares scratch
   copies of affected resident tiles.
2. The canvas validates the next revision, exact overview bounds and pixel
   count, affected tile keys, replacement pixels, and that no source is pinned
   before changing state.
3. One commit copies the bounded overview rows, carries unaffected resident
   tiles forward to revision N, and publishes or invalidates affected tiles.
   Failure leaves the prior revision and every source identity unchanged.
4. Missing affected tile pixels remain valid overview fallback and may be
   republished from incremental scratch later; operations 1…N−1 are never
   replayed.

Affected identity is now expressed as conservative world bounds, so an
operation cannot accidentally carry stale intersecting residents at other zooms
forward as current. `append_incrementally` owns the prepare/render/commit/publish
ordering behind one host-tested interface: prepared samples are not authoritative
until the overview and bounded affected resident tiles have been rendered and the
canvas revision commits; excess affected residents become current overview
fallback. Its overview, tile scratch, publication, and key spans are caller-owned
and alias-checked. Snapshot restore must go through
`restore_document_snapshot`, which validates both modules before replacing the
canvas overview and resetting operation authority to the same revision.

The interface expresses a revision plus bounded affected-tile publications; it
does not expose mutable pool storage or renderer callbacks. Tile size and slot
count remain provisional and may change after captured workloads. Shared read
pins protect source storage only while a display adapter copies it into DMA
staging; `pins_outstanding()` is the fail-closed diagnostic. Renderer work never
holds a pin while waiting for panel capacity.

The immediate renderer is intentionally opaque and hard-edged; publications are
labeled `kImmediate`, below `kSettled`. `kSettled` is reserved for anti-aliased
output, so provisional pixels cannot masquerade as accepted quality. Long sparse
segments are subdivided into bounded raster steps, and tile enumeration reports
required versus written capacity. Anti-aliased settled convergence remains open.

The latest exclusive hardware proof is
[`hardware-receipts/fa39abe-operation-builder-walk.log`](hardware-receipts/fa39abe-operation-builder-walk.log).
It quantized deterministic input-shaped pen and eraser points through the
fixed-capacity `OperationBuilder`, advanced document authority and materialization
together through 32 compact operations, then composed the expected deterministic
`d4e162c4` result. Every one of the walk's 168 bounded strips passed through
`DisplayScheduler`; all 168 were accepted and completed in order with zero
stale or other scheduler rejects, and transport completed 168/168 with zero
CO5300 window rejects. The 30-operation burst averaged 50.284 ms per coordinated
append. The same deterministic hashes and zero-rejection scheduler/transport
result passed again after the replay-range seam in
[`hardware-receipts/20dbab7-production-regression-walk.log`](hardware-receipts/20dbab7-production-regression-walk.log).
That regression walk does not exercise the host-only range query itself.

A separate PSRAM measurement compared full copy, swappable double buffers, and
bounded dirty-region publication; its method and limits are archived in
[`OVERVIEW_PUBLICATION_MEASUREMENT_2026_08_13.md`](../docs/archive/2026-08-raster-and-vector-prototypes/OVERVIEW_PUBLICATION_MEASUREMENT_2026_08_13.md).
After adopting the bounded path, the exact-commit hardware receipt
[`hardware-receipts/16dc9b2-bounded-overview-publication.log`](hardware-receipts/16dc9b2-bounded-overview-publication.log)
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
baseline revision, destination revision, and operation range together; append-time
zoom-specific LOD storage must also have an explicit owner and capacity receipt.
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

`OperationLodStore` gives append-time zoom-specific centerline samples a separate
fixed-capacity owner. It prepares all four tiled zoom spans together, publishes
them under the exact next operation identity and log epoch, and invalidates them
on snapshot reset. Its source, span, and sample storage are caller-owned and
alias-checked. The first implementation deliberately stores caller-generated
samples without choosing a simplification algorithm; capacity remains
provisional until representative captured input exists. Published LODs remain
readable while the next operation is prepared. Eventual integration must prepare
both log and LOD state before publishing either, so a skipped LOD append cannot
wedge their exact-next identities.
