# TinyDraw project state

Last updated: 2026-08-12

## Resume point

Branch: `feat/vector-canvas-production`

The camera-aligned vector/materialized-cache prototype is complete and rejected as a product architecture. Tasks #52 and host-only #54a now exist under `production/`. The complete-overview fallback has passed an exclusive fail-closed hardware walk, and the empty-heap #53 plan now includes both live and next-revision overview buffers. These receipts do not close the coexistence, concurrency, or interactive-pan gates. The branch is not ready to ship. The next implementation work is the #55 transactional incremental-revision seam, with the minimum #58 panel transport seam pulled forward for early hardware proof.

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

Implement #55 without growing `hardware_app.cpp`:

1. add a transactional revision interface that carries unaffected resident tiles forward and changes only affected tiles;
2. keep rendering outside `MaterializedCanvas`, using caller-owned next-overview and bounded tile scratch already present in the memory plan;
3. preserve the old revision and identities on every rejected commit;
4. host-test painter-order targeting, carry-forward, fallback, pin refusal, and partial-failure behavior;
5. pull forward only the #58 transport/compositor split needed to run an exclusive incremental-operation walk on the connected panel.

The old full-overview `publish_overview` path remains useful for bootstrap and complete replacement, not ordinary append. The first hardware proof must exercise a real incremental operation and physical transfer before broader renderer work.

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
