# Production island

This directory is the migration island for TinyDraw's vector-authoritative production architecture. It is **not** a second application or a clean-room rewrite. The existing raster app remains runnable while production modules replace it one behavior at a time.

Current architecture and task order live in [`PROJECT_STATE.md`](../PROJECT_STATE.md). Historical prototype plans belong in [`docs/archive/`](../docs/archive/).

## Dependency rule

Production code may reuse stable, platform-independent core mechanisms by dependency. It must not copy them.

The production island must not depend on:

- `WorldCanvas`, `ViewOrigin`, or the 3×3 raster geometry;
- `FirmwareCanvas` or ESP-IDF allocation;
- `interactive_pan_benchmark` or other benchmark coordinators;
- hardware display, toolbar, persistence, or task-loop policy;
- camera-aligned atlas identities or arbitrary zoom values.

Adapters outside this directory may eventually connect production modules to the existing app and hardware. Those adapters must not move platform details into the production interfaces.

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

Production code is compiled through the independent `tinydraw::production` CMake target with the project's warnings-as-errors policy. It is also covered by the repository formatter and two production-scoped analyzers:

```sh
./scripts/dev format-check
./scripts/dev tidy
./scripts/dev cppcheck
```

`clang-tidy` treats its curated analyzer, bug-prone, performance, portability, function-size, and cognitive-complexity findings as errors. Cppcheck supplies an independent C++20 analysis pass. Neither analyzer scans the frozen legacy or prototype tree.

## Migration rule

Every production milestone must replace or prepare to replace an identified legacy responsibility. Do not create an indefinite third path.

1. Build and review the production module through host tests.
2. Prove its memory layout where required.
3. Add one narrow adapter to the running application.
4. Validate behavior and hardware gates.
5. Remove the superseded legacy path when the production path owns that responsibility.

The retired prototype remains evidence and benchmark machinery. It is frozen except for evidence-preservation fixes.

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
- a 128-tile pixel pool: `1,048,576` bytes plus slot metadata;
- 100% world grid: `ceil(1472 / 64) × ceil(1792 / 64) = 23 × 28` keys.

`MaterializedCanvas` owns no hidden allocation. The overview pixels and fixed
slot metadata are caller-owned; pixel-pool allocation and its hardware receipt
belong to Task #53.

## Task #53 memory plan

[`memory_layout.h`](include/tinydraw/production/memory_layout.h) records the
first complete allocation plan. Its capacity assumptions are explicit:

- 4,000 operations and 80,000 compact 8-byte samples;
- four materialized zoom LOD span tables and 90,000 compact 6-byte LOD samples;
- 128 tile slots;
- two 128×128 renderer workspaces plus 64 KiB shared geometry storage;
- a 368×76 overlay and two 368×32 staging strips;
- a second 368×448 overview buffer for transactional revision publication.

The opt-in `TINYDRAW_PRODUCTION_MEMORY_PROBE` firmware allocates every region
simultaneously, then attempts a separate 1.5 MiB allocation. This is an
allocation receipt, not proof that the eventual encoders and renderers fit the
reserved capacities or meet interaction gates.

The current empty-heap ESP32-S3 allocation receipt is
[`hardware-receipts/756e080-memory-layout.log`](hardware-receipts/756e080-memory-layout.log):

- both the live and next-revision overviews are explicitly budgeted;
- all 3,368,032 planned bytes allocated simultaneously;
- largest contiguous block after the plan: 4,980,736 bytes;
- a separate 1,572,864-byte reserve allocation succeeded;
- free/largest after holding both plan and reserve: 3,412,544 / 3,407,872 bytes.

The earlier pre-publication-buffer receipt remains archived as
[`hardware-receipts/1f91ed0-memory-layout.log`](hardware-receipts/1f91ed0-memory-layout.log).

This proves only that the provisional external-memory slabs and reserve are
simultaneously allocatable on an otherwise empty 8 MiB PSRAM heap. It does not
close Task #53's product or strangler-coexistence gate: the legacy raster arenas,
internal DMA heap, export workspace, Wi-Fi, USB, and eventual renderer behavior
were not live. Captured workload distributions must also validate that the
capacities are sufficient rather than merely allocatable.

No representative captured operation document is currently checked into this
repository. The only 1,000-stroke corpus is the deterministic synthetic
handwriting generator; seed 7 produces 20,153 samples (20.153 samples/stroke,
200 maximum), which fits the provisional 4,000-operation/80,000-sample ratio by
a narrow extrapolation only. Small `.stroke` files under `testdata/` are UI and
raster correctness fixtures, not capacity evidence. Do not promote either set
to a representative capacity receipt. A real captured document remains a
required input to the capacity and Task #59 gates.

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

1. The renderer applies operation N to the caller-owned next-overview buffer and
   to scratch copies of only the affected resident tiles.
2. The canvas validates the next revision, complete overview, affected tile
   keys, replacement pixels, and that no source is pinned before changing state.
3. One commit copies the complete overview, carries unaffected resident tiles
   forward to revision N, and publishes or invalidates affected tiles. Failure
   leaves the prior revision and every source identity unchanged.
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
and alias-checked. Snapshot restore must publish the canvas overview and call
`OperationLog::reset` with the same revision before coordinated appends resume.

The interface expresses a revision plus bounded affected-tile publications; it
does not expose mutable pool storage or renderer callbacks. Tile size and slot
count remain provisional and may change after captured workloads. Shared read
pins protect source storage only while a display adapter copies it into DMA
staging; `pins_outstanding()` is the fail-closed diagnostic. Renderer work never
holds a pin while waiting for panel capacity.

The immediate renderer is intentionally opaque and hard-edged; publications are
labeled `kSettled`, not `kExact`. Long sparse segments are subdivided into bounded
raster steps, and tile enumeration reports required versus written capacity.
Anti-aliased settled convergence remains Task #56.

The latest exclusive hardware proof is
[`hardware-receipts/fa39abe-operation-builder-walk.log`](hardware-receipts/fa39abe-operation-builder-walk.log).
It quantized deterministic input-shaped pen and eraser points through the
fixed-capacity `OperationBuilder`, advanced document authority and materialization
together through 32 compact operations, then composed the expected deterministic
`d4e162c4` result. Every one of the walk's 168 bounded strips passed through
`DisplayScheduler`; all 168 were accepted and completed in order with zero
stale or other scheduler rejects, and transport completed 168/168 with zero
CO5300 window rejects. The 30-operation burst averaged 50.284 ms per coordinated
append. This proves bounded input collection and ordered staging on glass, not
real touch input, concurrent mutation and transport, representative capacity,
settled anti-aliasing, or the final interaction gates.

## Task #56 settlement prerequisite

A settled renderer cannot safely anti-alias in place over the hard-edged immediate
materialization: partial edge coverage blended over already-painted edge pixels
cannot remove the immediate renderer's wider binary silhouette. Settlement must
start from an explicit pre-operation source or checkpoint and replay a contiguous
painter-ordered operation range. Its interface must therefore identify the
baseline revision, destination revision, and operation range together; append-time
zoom-specific LOD storage must also have an explicit owner and capacity receipt.
Do not hide these requirements behind a `settle(current_pixels)` helper or copy
the rejected prototype renderer into the production island.
