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
