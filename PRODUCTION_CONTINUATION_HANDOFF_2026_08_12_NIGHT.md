# TinyDraw vector canvas — production continuation handoff

Date: 2026-08-12 night

Branch: `feat/vector-canvas-production`

Starting commit: `e311a46cfe9c24a4d973551004224f757929060e`

## Read this first

The vector/materialized-cache prototype is complete. **Do not continue developing the camera-aligned 3×3 atlas.** Its final purpose was measurement, and the final measurement is done.

The project is ready to **begin production architecture implementation**, not ready to ship. The next job is to build host-testable production state and prove the memory layout—not to optimize or harden the prototype coordinator.

Load-bearing documents, in order:

1. `PROTOTYPE_EXIT.md` — concise final verdict, receipts, rejected mechanisms, production gates.
2. `CONTINUATION_HANDOFF_2026_08_12_EVENING_FACT_CHECK.md` — claim-by-claim audit and corrections.
3. `SECOND_REVIEW_ARCHITECTURE_ASSESSMENT.md` — production architecture rationale.
4. `VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md` — complete performance history, including failed experiments.
5. `second_review_hardware_ab/8817a88-step0-scratch-ab.log` — final hardware receipt.

Historical context exists in many other Markdown files, but the five above are sufficient to begin correctly.

## Repository state

A clean production branch was created from the completed prototype:

```text
feat/vector-canvas-production
└── e311a46 bench: close materialized-cache prototype with scratch A/B
```

The parent prototype branch and commit are pushed:

```text
origin/prototype/vector-materialized-cache = e311a46
```

At handoff time there were no tracked modifications on the production branch.
Six pre-existing, untracked hardware captures were subsequently reviewed and
preserved under
[`second_review_hardware_ab/archive/superseded-diagnostics/`](second_review_hardware_ab/archive/superseded-diagnostics/README.md).
They are historical prototype diagnostics, not current load-bearing receipts.
The local `.pi/` directory remains untracked and must not be committed.

## User decisions and constraints

- Work autonomously through implementation, tests, diagnosis, and fixes. Ask for manual hardware interaction only when it is genuinely required.
- Canvas: **4×4 screens = 1472×1792 world units**.
- Committed zoom range: **25%–400%**.
- **800% is optional**. If only 800% fails its gate, cap at 400%; do not abandon vector authority.
- Slight settled-rendering edge/join differences are acceptable for speed.
- Nonnegotiable correctness: stroke presence, painter order, eraser behavior, and document revision.
- Undo/history redesign comes after rendering and interaction behavior settle.
- Do not invest further in the disposable 3×3 coordinator beyond preserving historical evidence.
- Assembly/PIE comes only after the production renderer is profiled and compute-bound.

## Final prototype verdict

### Firmly demonstrated

- Existing raster/cache pan remains fast: normally approximately **26.1 ms** on the final benchmark; historical direct raster pan was approximately **25.45 ms**.
- Zoom can present a valid first physical strip in **7.0–9.8 ms**.
- Full physical fallback completes in **40.2–49.8 ms**.
- Settled physical completion on the 1,000-stroke synthetic document:
  - 50%: **490–491 ms**
  - 100%: **792–828 ms**
  - 200%: **737–740 ms**
- Live raster update averages: **1.67–2.80 ms**; finish: **36–60 ms**. These are update/finish measurements, not complete finger-to-photon latency.
- Publication generation/revision checks and deterministic hashes worked.
- Stale mutation fallback was refused, repaired, and then accepted.
- The LOD plateau fix independently reproduced approximately **321 ms → 3.7 ms** at n=1200.

### Architectural failure demonstrated

The camera-aligned atlas cannot support good mutation behavior:

- one gesture recorded **103 rejected pan requests**;
- a six-stroke burst caused approximately **12 seconds cumulative repair time**;
- individual observed repairs took approximately **3–4 seconds**;
- zoom attempts failed while repair was pending.

Production must never refuse ordinary pan for this reason. Missing high-resolution tiles must use the complete overview as valid fallback, and newly appended operations must update resident materializations incrementally instead of replaying the document.

## Final Step 0 result: PSRAM scratch hypothesis rejected

A review suggested settled rendering was slow primarily because coverage scratch lived in PSRAM. A controlled three-way hardware experiment rendered the same 368×384 region, document, camera, and output:

| Variant | Strokes tested | Segments | Clear | Raster | Composite | Wall |
|---|---:|---:|---:|---:|---:|---:|
| Grouped region, PSRAM scratch | 1,000 | 6,192 | 12.380 ms | 437.398 ms | 98.073 ms | 553.283 ms |
| 12 bands, PSRAM scratch | 7,989 | 10,949 | 10.693 ms | 470.262 ms | 91.215 ms | 588.240 ms |
| 12 bands, internal scratch | 7,989 | 10,949 | 6.994 ms | 468.570 ms | 87.000 ms | 578.317 ms |

Every output was identical:

```text
ink=117220 hash=c2c4938d
```

Comparing identical banded workloads, internal scratch improved:

- clear: 34.6%
- raster: **0.36%**
- composite: 4.62%
- wall: **1.69%**

The pre-registered prediction was at least a 40% raster improvement. It failed decisively.

**Do not implement a borrowed `active_coverage_` buffer optimization.** The next settled-renderer improvement must be algorithmic: generate geometry once for a larger supertask, bin ordered spans/microtiles, and avoid scanning capsule AABB interiors with full distance math.

Banding itself increased segment work by 76.8%, confirming that publication size and geometry-generation size must be separate.

## Production architecture

```text
Vector operation log (authoritative)
        │
        ├── append-time compact samples / zoom-specific LOD
        │
        ├── complete 368×448 RGB565 overview at 25%
        │      └── always-valid source for cold start, zoom, and tile misses
        │
        └── sparse world-aligned RGB565 tiles at 50–400%
               ├── visible + nearby + recent slots
               ├── incremental operation application
               ├── settled span/microtile rendering
               └── idle canonical exact refinement

MaterializedCanvas state module
        ↓ immutable publication request / pinned slot revision
DisplayScheduler single owner
        ↓ staging + overlays + panel queue + completion
AMOLED
```

### Two intended deep modules

#### `MaterializedCanvas`

This module should hide:

- bounded-world and zoom-level geometry;
- complete overview ownership;
- world-aligned tile keys and fixed-capacity slots;
- tile state, quality, applied operation revision, and replacement policy;
- lookup of the best valid source for a requested screen region;
- overview-derived fallback when active-level tiles are absent;
- invalidation and incremental append targeting.

Its interface should be small and host-testable. Renderer and ESP display concerns should not leak into it.

#### `DisplayScheduler`

This module should be the single owner of:

- panel staging and queue submission;
- overlay composition;
- strip ordering;
- transfer completion;
- cache-slot/version pinning while a transfer is in flight;
- physical timing telemetry.

The cache/state lock must not be held while waiting for panel queue capacity.

## Production roadmap / task order

### #52 — Start production `MaterializedCanvas` (next)

Implement the first pure core state seam:

- `WorldBounds` fixed at 1472×1792;
- zoom-level identity for 25/50/100/200/400;
- complete 368×448 overview identity/storage requirements;
- world-aligned tile key mapping;
- fixed-capacity slot state;
- quality/provenance/revision state;
- overview-derived fallback lookup;
- host tests for boundaries, replacement, revision transitions, and no-invalid-source selection.

Do not begin by wiring it into `hardware_app.cpp`. First make the state model correct and testable.

### #53 — Prove production memory layout

Replace the current forecast with a real allocation plan and hardware receipt:

- 329,728-byte overview;
- fixed tile pool;
- operation storage;
- LOD/index storage;
- renderer workspace;
- overlay/staging/queues;
- measured free PSRAM and largest contiguous block.

Target a measured **1–1.5 MiB largest contiguous PSRAM reserve**, not merely total free bytes.

The current budget's uncertain entries are compact 6–8-byte samples, approximately 650 KB vector log, approximately 700 KB LOD/index, approximately 600 KB scratch/staging, and approximately 100 KB overlay. The claimed 3,000–4,000-stroke capacity has not been derived. Measure captured sample distributions and include per-stroke metadata.

### #54 — Implement no-refusal pan fallback

When high-resolution tiles are absent, compose from the complete overview rather than refusing the requested camera.

Gate:

- zero ordinary pan refusal;
- valid-cache pan p95 ≤35 ms;
- no stale/wrong-revision publication.

### #55 — Implement incremental tile updates

When operation N is appended, apply only operation N in painter order to:

- the complete overview;
- resident visible/nearby active-level tiles;
- queued neighboring levels while idle.

Do not replay operations 1…N−1. Current opaque-white eraser semantics permit ordered incremental application. Old-operation undo requires replay/checkpoints and remains deferred.

### #56 — Build the ordered settled renderer

Leading direction:

- zoom-specific simplified centerline/LOD generated at append time;
- geometry generated/projected once per approximately 128×96 or 128×128 supertask;
- ordered sparse 32×32 coverage microtiles and/or scanline spans;
- solid interior span writes with analytic edge coverage;
- painter-order-safe tile partitioning;
- publication tiles approximately 64×32 or 64×64, independently sized;
- later two-core partition only after sparse work exists.

Do not repeat the failed per-row-square-root span implementation in chronicle §15. Use incremental/fixed-point extent stepping or another design that removes—not adds—per-row expensive math.

### #57 — Add zoom levels sequentially

Bring up and gate:

1. 25%
2. 50%
3. 100%
4. 200%
5. 400%
6. optional 800%

Do not extrapolate 200% results to 400/800. Each level needs zoom-specific LOD quality and hardware receipts.

### #58 — Centralize display scheduling

May proceed after the core state interface stabilizes. Keep it separate from cache replacement/state logic.

### #59 — Validate production interaction gates

Use a captured realistic 1,000-stroke document, not only the synthetic workload. Test pan, zoom, live pen, eraser, mutation bursts, overview→settled→exact transitions, and revision correctness.

### #60 — Persistence and undo replay

Only after in-memory behavior passes. Persist expensive materializations with checksums/revisions. Add per-tile checkpoints or snapshot+journal replay for old-operation undo.

## Product gates

- First physical valid feedback: **<100 ms**.
- Full visible fallback: **<150–180 ms**.
- Valid-cache pan p95: **≤35 ms**, with zero ordinary refusal.
- Visible settled p95: **<500 ms** on a captured realistic 1,000-stroke document at every committed zoom.
- Live pen event-to-submit measured separately from touch-to-photon and not regressed by background work.
- Stroke presence, painter order, eraser behavior, and revision correctness preserved.
- No checkerboards, stale pixels labeled current, blank-valid publications, or panel-window violations.
- If only 800% fails, cap at 400%.

## Important fact-check corrections

Do not accidentally repeat these earlier overstatements:

1. The production memory table is a target, not a proven allocation.
2. `expected_strokes` only counts bounds intersections; it is not an oracle that visible nonwhite pixels must exist.
3. Same-hash publications prove deterministic software raster data, not physical panel correctness or agreement with an independent reference render.
4. No panel-window rejects proves that tested calls passed the boundary guard; it is not exhaustive proof of the CO5300 hypothesis.
5. The approximately 12-second repair was cumulative across a six-stroke burst, not one isolated repair execution.
6. Off-cell views were slower and touched more bands, but the logs do not isolate cell count from content/candidate differences.
7. The historical generation-36 viewport-churn diagnosis is strongly supported, not directly instrumented proof.
8. A 25% 368×448 framebuffer geometrically shows the full world, but the current toolbar obscures rows 372–447. Production UX must hide/overlay the toolbar or accept this.
9. `float32` precision is sufficient, but replacing the `double` camera is cleanup, not the main performance lever.
10. Current full `build_settled_lod` can still abort a generation on total capacity failure; the fixed per-stroke renderer fallback does not solve the production storage design.

## Exact/full-render optimization fact check

A previous agent proposed a stacked 3–6× exact-render speedup. The mechanisms are useful, but multiplying their ranges is unjustified because they overlap.

- Two-core canonical tile compositing exists and was physically used in Phase 1, but it was bundled with provisional-geometry omission. A **1.2–1.7× canonical end-to-end** estimate is better supported than 1.7–2×.
- Scanline/solid interiors are plausible raster-stage improvements, especially for thick ink, but unmeasured end-to-end and less valuable for thin handwriting.
- Geometry reuse across publication bands is definitely needed. Historical direct full-viewport dual-core canonical rendering already measured **1.777 s for 1,000 handwriting strokes at 100%**, versus roughly 4–5 seconds in the current repeated-band coordinator. Workload/code differences prevent treating that as a clean A/B, but approximately 2 seconds exact is demonstrated.
- A 1–1.5-second exact viewport is a reasonable research target, not a promise.
- Persisting overview/tiles and incrementally maintaining them is more valuable than forcing canonical replay under 500 ms.
- PIE/assembly RGB565 blend comes after spans/microtiles and profiling. Fully opaque pixels already bypass the blend.

Exact rendering is idle convergence/export/reference work. The settled renderer—not canonical replay—owns the <500 ms interaction gate.

## Validation commands

For core changes:

```bash
./scripts/dev test
./scripts/dev release
./scripts/dev asan
./scripts/dev format-check
git diff --check
```

For ESP32 changes, also build both configurations:

```bash
cd esp32
eim run "idf.py -B '../out/build/esp32-interactive-pan-benchmark' -DTINYDRAW_INTERACTIVE_PAN_BENCHMARK=ON build"
eim run "idf.py -B '../out/build/esp32-default' -DTINYDRAW_INTERACTIVE_PAN_BENCHMARK=OFF build"
```

Flash benchmark firmware:

```bash
./scripts/esp32 interactive-pan-benchmark /dev/cu.usbmodem1101
```

Capture serial evidence:

```bash
cd esp32
eim run "python ../tools/esp32-capture.py /dev/cu.usbmodem1101 /tmp/run.log 300"
```

`tools/esp32-capture.py` does not include `TINYDRAW_AUTO_ZOOM_DONE` in its default end markers; edit/add the marker or use a bounded timeout for automated runs.

## Suggested first commit

A good first production commit should contain only:

1. production world/zoom/tile constants and strongly typed identities;
2. a pure `MaterializedCanvas` or `MaterializedCanvasState` module;
3. host tests for mapping, validity, revision, slot replacement, and overview fallback;
4. a short design note with exact memory arithmetic;
5. no hardware integration yet.

Then run all host profiles and review the module interface before adding allocation or ESP32 adapters.

## Current task state

- #51 controlled settled-scratch benchmark: completed.
- #52 production `MaterializedCanvas`: pending and is the next task.
- #53 production memory receipt: pending, blocked by #52.
- #54 no-refusal pan: pending, blocked by #52/#53.
- #55 incremental tile updates: pending, blocked by #52/#53.
- #56 ordered settled renderer: pending, blocked by #52/#53.
- #57 production zoom levels: pending, blocked by #54/#56.
- #58 display scheduler: pending, blocked by #52.
- #59 full interaction validation: pending, blocked by #55/#57/#58.
- #60 persistence/undo replay: pending, blocked by #59.

## Bottom line

This is not a dead-end rescue attempt. Hardware has already demonstrated the critical interaction mechanisms. The prototype's remaining failures are specifically the mechanisms the production design removes: camera-aligned rebasing, stale-source repair, refusal, repeated band replay, and giant raster ownership.

Start production now, but make the first two gates rigorous:

1. a small, host-tested materialization-state interface;
2. a real hardware memory-allocation receipt.

After those pass, implement overview-derived no-refusal pan and incremental append updates before spending heavily on renderer micro-optimization.
