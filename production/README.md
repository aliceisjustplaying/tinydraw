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

Host tests remain the oracle for the no-checkerboard composition policy. Final
scheduler behavior, incremental publication, and the ≤35 ms pan gate require the later display
adapter and scheduler; no claim about those hardware gates is made here.

## Task #55 revision seam

Incremental append must not use `publish_overview`, which deliberately replaces
a whole revision and invalidates all tiles. The next state change will add one
transactional revision operation with this behavior:

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

The interface should express a revision plus bounded affected-tile publications,
not expose mutable pool storage or renderer callbacks. Tile size and slot count
remain provisional and may change after captured workloads. Shared read pins
protect source storage only while a display adapter copies it into DMA staging;
`pins_outstanding()` is the fail-closed diagnostic. Renderer work never holds a
pin while waiting for panel capacity.
