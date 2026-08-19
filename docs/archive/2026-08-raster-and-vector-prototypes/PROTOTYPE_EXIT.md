# Vector materialized-cache prototype exit

Status: **evidence-complete** on 2026-08-12. The camera-aligned 3×3 atlas is retired as a product architecture. Do not add features or continue hardening it; retain it only as historical benchmark machinery.

## Verdict

Proceed with vector authority plus:

1. a complete 25% overview for the bounded 1472×1792 world;
2. sparse world-aligned raster tiles for 50–400%;
3. overview-derived valid fallback for every cache miss;
4. incremental application of newly appended operations to resident materializations;
5. a purpose-built settled span/microtile renderer;
6. canonical curved-ribbon replay only for idle exact refinement, export, and reference validation.

The prototype proved the experience is feasible. It also proved that a camera-aligned double-atlas coordinator is the wrong production mechanism.

## Questions answered

| Question | Evidence | Answer |
|---|---|---|
| Can raster-cache pan retain existing responsiveness? | Valid-cache hardware pan normally ~26.1 ms; historical direct raster pan ~25.45 ms | **Yes** |
| Can zoom present valid first feedback under 100 ms? | First physical fallback strip 7.0–9.8 ms | **Yes** |
| Can full fallback complete under 150–180 ms? | Last physical transfer 40.2–49.8 ms | **Yes** |
| Can publication reject stale generations/revisions? | Mutation stale refusal → repair → accepted retry; deterministic revision/phase hashes | **Yes** |
| Can live drawing remain fast beside vector recording? | Update averages 1.67–2.80 ms, finish 36–60 ms in the final hardware session | **Yes, for measured update work**; full touch-to-photon remains a production test |
| Can the current settled renderer meet 500 ms? | 50% 490–491 ms; 100% 792–828 ms; 200% 737–740 ms | **Not yet** |
| Can the 3×3 atlas avoid mutation stalls/refusals? | 103 rejected pan requests and up to ~12 s cumulative repair during a six-stroke burst | **No** |
| Is settled coverage slow primarily because its scratch is in PSRAM? | Controlled three-way Step 0 below | **No** |

## Final Step 0: controlled scratch-placement experiment

Firmware source state: based on `8817a88`; physical ESP32-S3 rev 0.2, 240 MHz, 8 MB 80 MHz octal PSRAM. Raw log:

- `second_review_hardware_ab/8817a88-step0-scratch-ab.log`

All variants rendered the same 368×384 region from the same 1,000-stroke document and 100% camera. Hash and ink count were identical:

- `ink=117220`
- `hash=c2c4938d`

| Variant | Strokes tested | Segments | Clear | Raster | Composite | Wall |
|---|---:|---:|---:|---:|---:|---:|
| Grouped region, PSRAM scratch | 1,000 | 6,192 | 12.380 ms | 437.398 ms | 98.073 ms | 553.283 ms |
| 12×32-row bands, PSRAM scratch | 7,989 | 10,949 | 10.693 ms | 470.262 ms | 91.215 ms | 588.240 ms |
| 12×32-row bands, internal scratch | 7,989 | 10,949 | 6.994 ms | 468.570 ms | 87.000 ms | 578.317 ms |

Isolated internal-vs-PSRAM effect for the identical banded workload:

- clear: **34.6% faster**;
- raster: **0.36% faster**;
- composite: **4.62% faster**;
- total wall: **1.69% faster**.

The pre-registered prediction was a ≥40% raster reduction. It failed decisively. The settled capsule loop is primarily compute/algorithm bound for this workload, not coverage-scratch bandwidth bound. Do not build a borrowed-live-coverage ownership mechanism for this optimization.

Banding itself repeated intersecting stroke/segment work: segment count increased 76.8%, and wall time increased 6.3% despite smaller scratch. Production must generate/project geometry once for a larger supertask, then bin it into smaller ordered coverage/publication tiles.

## Mechanisms retained

- Vector document/operation log as authority.
- Fast raster presentation for pan.
- Complete valid low-resolution fallback.
- Fixed zoom levels and zoom-specific LOD.
- Revision/generation-gated publication.
- Completion-based physical display endpoints.
- Center-out progressive fallback strips.
- Separate fallback, settled, and exact quality tiers.
- Publication hashes as determinism/provenance diagnostics.
- Slight settled edge/join differences are acceptable; painter order, stroke presence, eraser semantics, and revision correctness are not.

## Mechanisms rejected

- Camera-aligned 3×3 atlas as production cache.
- Two multi-megabyte camera-aligned source/destination arenas.
- Ordinary pan refusal while exact repair catches up.
- O(document) repair after each appended stroke.
- Interaction-time giant clears/rebases.
- Reconstructing geometry separately for every 32-row publication band.
- Treating PSRAM scratch placement as the settled renderer's dominant bottleneck.
- Optimizing exact canonical replay as the interactive settled path.

## Production risks still open

1. The memory table is a design envelope, not a proven allocation. Compact vector encoding, LOD/index size, renderer scratch, overlay shrinkage, and largest-block reserve need an allocation spike.
2. The 3,000–4,000-stroke estimate is not yet derived from captured sample distributions and metadata costs.
3. 25%, 400%, and optional 800% have not been exercised on the production architecture.
4. The settled renderer needs ordered spans/microtiles, zoom-specific LOD, and geometry supertasks to reach <500 ms at 100–400%.
5. The production coordinator and display scheduler need host state-machine/concurrency tests.
6. Incremental resident-tile updates must preserve live drawing latency and painter/eraser order.
7. Old-operation undo needs checkpointed tile replay; it is deliberately deferred.
8. “Show everything” at 25% geometrically fills 368×448, but the current toolbar obscures the bottom 76 rows. Production UX must address that.

## Production gates

- First physical valid feedback: <100 ms.
- Full visible fallback: <150–180 ms.
- Valid-cache pan p95: ≤35 ms with zero ordinary refusal.
- Visible settled p95: <500 ms on a captured realistic 1,000-stroke document at 25/50/100/200/400%.
- Live pen event-to-submit measured separately from full touch-to-photon; no regression from background work.
- No stale or wrong-revision publication.
- Painter order, stroke presence, and eraser behavior preserved.
- Production allocations retain a measured 1–1.5 MiB largest contiguous PSRAM reserve.
- If only 800% fails, cap at 400%; do not abandon vector authority.

## Next implementation seam

Begin with host-testable production modules rather than porting the prototype coordinator:

- **MaterializedCanvas** — owns the complete overview, world-aligned tile identities/slots, applied operation revisions, fallback resolution, invalidation, and incremental append application behind a small interface.
- **DisplayScheduler** — single author of overlay composition, staging, panel submission, completion, slot/version pinning, and physical timing behind a small interface.

The first milestone is a memory/layout and state-machine spike: allocate the overview and bounded tile pool, prove overview-derived no-refusal pan on the host, and record the actual PSRAM/largest-block receipt on hardware before filling in all rendering tiers.
