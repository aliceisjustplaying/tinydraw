# TinyDraw project state

Last updated: 2026-08-13

## Resume point

Branch: `feat/vector-canvas-production`

The camera-aligned vector/materialized-cache prototype is complete and rejected as a product architecture. Tasks #52, #53, #54a, the bounded immediate path of #55, and the minimal #58 display-ordering seam now exist under `production/`. A fixed-capacity ordered `OperationLog` is the first production document authority; `MaterializedCanvas` commits conservative world-bounds invalidation across every zoom; and the host-tested `append_incrementally` coordinator prepares storage, renders the complete overview and all affected resident tiles, commits materialization, then publishes document authority. Compact pen and eraser operations do not replay prior operations. `OperationLog` now exposes epoch-bound contiguous replay ranges, and `OperationLodStore` owns caller-generated append-time centerline LODs for the four tiled zooms. A host rehearsal proves split replay, restore invalidation, and rebasing without choosing a settled renderer. The exclusive hardware walk routes every bounded display strip through `DisplayScheduler` before transport. These receipts do not close representative capacity, settled rendering, concurrency, coexistence, live-input, or interactive-pan gates. The branch is not ready to ship.

The default firmware still runs the existing raster-authoritative product:

- a hard-coded 3×3 `WorldCanvas` (1104×1344);
- live `StrokeRaster` rendering and tile-based raster Undo;
- raster-tile persistence and PNG export;
- no vector-authoritative production document.

Prototype-only builds may still record a `VectorDocument`; default firmware no longer dual-writes an unused vector log.

Do not mistake the current default firmware or retired benchmark coordinator for the target architecture.

## Current sources of truth

Read these in order:

1. [`PRODUCTION_CONTINUATION_HANDOFF_2026_08_12_NIGHT.md`](PRODUCTION_CONTINUATION_HANDOFF_2026_08_12_NIGHT.md) — decisions, roadmap, gates, and task order.
2. [`PROTOTYPE_EXIT.md`](PROTOTYPE_EXIT.md) — final prototype verdict and rejected mechanisms.
3. [`CONTINUATION_HANDOFF_2026_08_12_EVENING_FACT_CHECK.md`](CONTINUATION_HANDOFF_2026_08_12_EVENING_FACT_CHECK.md) — correction of measured claims versus forecasts.
4. [`SECOND_REVIEW_ARCHITECTURE_ASSESSMENT.md`](SECOND_REVIEW_ARCHITECTURE_ASSESSMENT.md) — production architecture rationale.
5. [`VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md`](VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md) — experiment and performance history.

Superseded briefs, plans, findings, demo notes, and interim handoffs are indexed under [`docs/archive/2026-08-raster-and-vector-prototypes/`](docs/archive/2026-08-raster-and-vector-prototypes/README.md). They are evidence, not current direction.

## Production decision

Use a **production island inside this repository**, documented in [`production/README.md`](production/README.md).

This is a strangler migration, not a blank rewrite:

- new platform-independent production modules live behind independent interfaces;
- stable core mechanisms are reused by dependency, never copied;
- the raster app stays runnable while one responsibility at a time is replaced;
- hardware adapters arrive only after the relevant host state and memory contracts pass;
- each superseded legacy path is removed once production owns that responsibility.

This keeps the existing build, tests, hardware integration, UI, persistence, and benchmark receipts while preventing new production state from growing inside `hardware_app.cpp` or the rejected 3×3 types.

## Nonnegotiable guardrails

- Do not develop the camera-aligned 3×3 atlas further.
- Do not start task #52 in `hardware_app.cpp`.
- Do not extend `WorldCanvas`, `ViewOrigin`, or `FirmwareCanvas` to model production tiles.
- Do not use prototype coordinator state as the production interface.
- Do not copy renderer, input-loop, toolbar, or display logic into the production island.
- Do not perform broad cosmetic splits of the legacy app before a real replacement seam exists.
- Do not optimize the settled or canonical renderers as cleanup work.
- Do not introduce hidden allocation into production state modules.
- Prove production state on the host before adding it to an exclusive, opt-in hardware mode.

## Target architecture

```text
Vector operation log (authoritative)
        │
        ├── complete 368×448 RGB565 overview at 25%
        └── sparse world-aligned RGB565 tiles at 50–400%
                    │
             MaterializedCanvas
                    │ immutable publication descriptions / pinned revisions
             DisplayScheduler
                    │ staging, overlays, queueing, completion
                  AMOLED
```

Committed product geometry:

- world: 1472×1792 units (4×4 screens);
- zoom levels: 25%, 50%, 100%, 200%, and 400%;
- 800% is optional and may be dropped independently;
- overview: 368×448 RGB565, exactly 329,728 bytes.

Correctness requirements are stroke presence, painter order, eraser behavior, and document revision. Interaction and memory gates are defined in the production handoff.

## Next task

Continue #56 and hardware validation without growing `hardware_app.cpp`:

1. replace the four-independent-LOD-copy assumption before approving a simplifier: the private 70-stroke capture projects to about 67,942 source points but 225,600–254,057 LOD points at 4,000 strokes, so the 90,000-point model is not credible;
2. retain bounded dirty-region overview publication unless representative captures contradict the hardware result; exact hashes held while the deterministic burst fell from 50.295 ms to 6.404 ms average, with warm coordinated appends around 0.97–0.99 ms;
3. connect the host-tested `OperationBuilder` to real samples from extracted `PhysicalTouch` in an exclusive mode when a person can exercise the panel, and measure event-to-submit plus touch-to-photon behavior;
4. compare shared/nested, on-demand, and fewer-stored-level LOD layouts, then judge synthetic or explicitly approved rendered fixtures at 50–400%; the current `OperationLodStore` is an ownership experiment, not an approved final layout;
5. then implement the smallest ordered settled renderer slice from an explicit epoch/revision checkpoint and revalidated replay range. Do not blend over hard-edged immediate pixels and do not copy the rejected prototype renderer.

The latest behavior receipt is [`production/hardware-receipts/16dc9b2-bounded-overview-publication.log`](production/hardware-receipts/16dc9b2-bounded-overview-publication.log). It repeated every prior deterministic hash and the 168/168 zero-rejection scheduler and transport result after replacing two full-overview copies with validated bounded publication. The 30-operation burst fell from the earlier 50.295 ms average to 6.404 ms average under the same outer probe; warm individual coordinated appends were about 0.97–0.99 ms. The measurement tradeoff is archived in [`OVERVIEW_PUBLICATION_MEASUREMENT_2026_08_13.md`](docs/archive/2026-08-raster-and-vector-prototypes/OVERVIEW_PUBLICATION_MEASUREMENT_2026_08_13.md). This is an exclusive-image append cost, not a first-feedback measurement; the live event-to-submit and touch-to-photon gates remain open. The CST820 transport is separated from the legacy app and the exclusive production image initializes it successfully. Its bounded idle probe reported zero I2C/read errors, but no finger was present, so this remains readiness evidence—not live-input evidence. These results do not prove representative operation-log capacity, settled anti-aliasing, concurrent mutation and display, or interactive pan.

## Validation baseline

For core or production-state changes:

```sh
./scripts/dev test
./scripts/dev release
./scripts/dev asan
./scripts/dev format-check
./scripts/dev tidy
./scripts/dev cppcheck
git diff --check
```

For later ESP32 integration, also build both default and interactive benchmark configurations using the commands in the production handoff. Physical hardware remains the authority for latency, PSRAM behavior, DMA behavior, and panel correctness.
