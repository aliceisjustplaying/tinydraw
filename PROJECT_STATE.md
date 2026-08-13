# TinyDraw project state

Last updated: 2026-08-13

## Resume point

Branch: `feat/vector-canvas-production`

The camera-aligned vector/materialized-cache prototype is complete and rejected as a product architecture. Tasks #52, #53, #54a, the bounded immediate path of #55, and the minimal #58 display-ordering seam now exist under `production/`. A fixed-capacity ordered `OperationLog` is the first production document authority; `MaterializedCanvas` commits conservative world-bounds invalidation across every zoom; and the host-tested `append_incrementally` coordinator prepares storage, renders the complete overview and all affected resident tiles, commits materialization, then publishes document authority. Compact pen and eraser operations do not replay prior operations. The exclusive hardware walk now routes every bounded display strip through `DisplayScheduler` before transport. These receipts do not close representative capacity, settled rendering, concurrency, coexistence, live-input, or interactive-pan gates. The branch is not ready to ship.

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

Continue #55 without growing `hardware_app.cpp`:

1. complete the pending fresh Fable follow-up review after the CLI session limit resets; the first review found and prompted the now-fixed cross-zoom stale-tile defect;
2. preserve the proven immediate rasterizer and define Task #56 around an explicit baseline revision plus ordered replay range—anti-aliasing over the hard-edged result cannot remove its binary edge;
3. connect the host-tested `OperationBuilder` to real samples from extracted `PhysicalTouch` in an exclusive mode when a person can exercise the panel; do not move production policy into `hardware_app.cpp`;
4. measure first feedback from real touch samples and concurrent mutation/display behavior before integrating the default product loop;
5. validate provisional operation and LOD capacities against a captured representative document rather than extrapolating the bounded probe.

The latest exact-commit receipt is [`production/hardware-receipts/fa39abe-operation-builder-walk.log`](production/hardware-receipts/fa39abe-operation-builder-walk.log). Deterministic input-shaped points were quantized through the fixed-capacity `OperationBuilder`; thirty additional ordered pen/eraser appends then advanced log and canvas together to revision 32 in 1.509 seconds total (50.284 ms average) and produced the expected `d4e162c4` panel hash. `DisplayScheduler` accepted and completed all 168 bounded strips in order with no stale or other rejects; transport also completed 168/168 with no CO5300 window rejects. This is an exclusive-image append cost, not a first-feedback measurement; the live event-to-submit and touch-to-photon gates remain open. The approximately 50 ms append cost is dominated by copying the 329,728-byte overview into publication storage and committing it back, not tile composition. The CST820 transport is separated from the legacy app and the exclusive production image initializes it successfully. Its bounded idle probe reported zero I2C/read errors, but no finger was present, so this remains readiness evidence—not live-input evidence. These results do not prove representative operation-log capacity, settled anti-aliasing, concurrent mutation and display, or interactive pan.

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
