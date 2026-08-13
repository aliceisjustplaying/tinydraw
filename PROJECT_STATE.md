# TinyDraw project state

Last updated: 2026-08-13

## Resume point

Branch: `feat/vector-canvas-production`

The camera-aligned vector/materialized-cache prototype is complete and rejected as a product architecture. Tasks #52, #53, #54a, and the bounded immediate path of #55 now exist under `production/`. A fixed-capacity ordered `OperationLog` is the first production document authority; `MaterializedCanvas` commits conservative world-bounds invalidation across every zoom; and compact pen and eraser operations update the complete overview plus affected resident tiles without replaying prior operations. The exact-commit hardware walk appended both operations through the log and passed them through the panel at revisions 1 and 2. These receipts do not close representative capacity, settled rendering, concurrency, coexistence, or interactive-pan gates. The branch is not ready to ship.

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

1. extract the narrow append/materialization coordinator from the hardware walk into a host-tested production module;
2. make the coordinator render every affected resident tile available to it, while missing resident publications safely invalidate to overview fallback;
3. preserve the proven immediate rasterizer; anti-aliased settled quality belongs to the later settled-render path rather than this commit seam;
4. exercise a representative multi-operation sequence and capacity boundaries before default-loop integration;
5. keep pulling the minimum display scheduler seam forward so live hardware can prove interaction without moving production policy into `hardware_app.cpp`.

The current exact-commit receipt is [`production/hardware-receipts/29c4d1d-transactional-operation-walk.log`](production/hardware-receipts/29c4d1d-transactional-operation-walk.log). It proves prepared operation storage remains non-authoritative until materialization commits, then log publication advances the same revision; it also proves opaque pen/eraser painter order, conservative state publication, deterministic panel hashes, transfer completion, zero panel rejection, and measured PSRAM headroom. It does not prove representative operation-log capacity, live touch integration, settled anti-aliasing, or interactive pan.

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
