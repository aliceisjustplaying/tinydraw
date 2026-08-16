# Prepared correctness-fix patch series — 2026-08-16

## What this is

This directory contains a **read-only integration artifact**: ten ordered Git-format patches prepared from the consolidated findings in [`CORRECTNESS_REVIEW_SYNTHESIS_2026-08-16.md`](../CORRECTNESS_REVIEW_SYNTHESIS_2026-08-16.md). The implementation was made in an isolated worktree; none of these code changes were applied to the active performance branch.

| Boundary | Commit |
|---|---|
| Immutable patch base | `f2f6da7e97265626ace1b96e363d92dbbe09c7df` (`perf: publish tiles straight from the supertask surface`) |
| Prepared series tip | `46bf8399f94ade3c0767d99b4ec0f821a76f66c8` (`fix: preflight tile group publications`) |
| Active repository tip when exported | `75c9145ac225ba5373d5a16ee723e7b944d9386d` on `feat/v2-performance-followup`, five commits ahead of its remote |
| Aggregate diff | 57 files, 1,645 insertions, 402 deletions (`git diff --shortstat f2f6da7..46bf839`) |

Because the active branch moved from the immutable base, three-way application may conflict in the hot performance files. The individual patches are intentionally separated by subsystem so the other agent can take, revise, or omit one concern without untangling a monolithic diff.

## Recommended application

Start from a clean integration branch containing the other agent's completed work:

```bash
git status --short
git switch -c integrate/correctness-fixes
git am -3 prepared_correctness_fixes_2026_08_16/0*.patch
```

If a patch conflicts:

```bash
git status
git add <resolved-files>
git am --continue
```

To abandon the entire attempt:

```bash
git am --abort
```

After resolution, run the validation commands in the **Validation receipts** section. Patch 9 changes live-ink raster semantics, and patches 7–8 change input/panel failure behavior; those patches especially need the hardware gates and optical checks after integration.

## Patch order and intent

| Patch | Commit | Main findings | Intent |
|---|---|---|---|
| `0001-fix-make-firmware-replay-gates-fail-closed.patch` | `515fe80` | CR-010–CR-017, CR-033, CR-038 | Propagate replay/latency failures, count first-contact latency, preserve capture timestamps, align capacities and parser grammar, and prevent gate state from entering product mode. |
| `0002-fix-validate-raster-and-staging-descriptors.patch` | `8c92b65` | CR-027–CR-030, CR-042, CR-043, CR-048, CR-050 | Add checked surface extents and runtime descriptor validation; reject overlap, odd-width staging, malformed tile payloads, and invalid painter/coverage surfaces. |
| `0003-fix-preserve-pressure-changes-at-stationary-samples.patch` | `a951a6e` | CR-002 | Render coincident unequal-radius samples with the larger radius. |
| `0004-fix-make-raster-metrics-match-production-behavior.patch` | `2492849` | CR-021–CR-024, CR-026; CR-025 support | Repair angularity reconstruction/chunk-joint accounting, include skipped setup work, isolate cold census counters, report metadata lines correctly, and match Python/C++ integer grammar. |
| `0005-fix-keep-cache-repair-and-rerender-accounting-truthf.patch` | `d200e2c` | CR-018, CR-019, CR-025, CR-037 | Grade only complete render groups, reset ledger sessions across explicit cache replacement/discard, enforce ledger verdicts, and apply saturation stopping to neighbor repair. |
| `0006-fix-make-drawing-snapshots-crash-consistent.patch` | `6c04dbb` | CR-005 | Replace in-place V1 persistence with dual-bank generations, per-sector checksums, commit-last headers, mirror repair, and legacy migration. |
| `0007-fix-conserve-input-transitions-under-backpressure.patch` | `a369b8d` | CR-001, CR-006, CR-007, CR-020, CR-031, CR-034, CR-039, CR-049 | Preserve controls and gesture edges, drop/coalesce only moves, capture consumed events, stop sampling during dump, defer physical zoom, clean partial allocations, and invalidate export state correctly. |
| `0008-fix-make-panel-presentation-failures-converge.patch` | `78c7a84` | CR-008, CR-009, CR-032, CR-035, CR-036, CR-040, CR-041, CR-044–CR-046 | Converge via full refresh after failures/pan, synchronize substantial partial updates, pre-render modal chrome, drain failed transfers, correct ring byte order, select the configured TE edge, and bound completion/acquire waits. |
| `0009-fix-unify-live-export-and-authority-stroke-geometry.patch` | `0292d82` | CR-003, CR-004 | Introduce authority midpoint-chord geometry, quantize live points through durable representation, use exact binary live coverage, and emit the same tapered segments to SVG. |
| `0010-fix-preflight-tile-group-publications.patch` | `46bf839` | CR-047 | Analyze and capacity-check a whole 2×2 group before publication; refuse pinned paper groups before any tile changes. |

## Finding-by-finding disposition

“Addressed” means the patch series contains a concrete fix and regression coverage where a host seam exists. Hardware-dependent behavior remains compile-validated rather than device-proven.

| Finding | Disposition | Patch / receipt |
|---|---|---|
| CR-001 | Addressed | Patch 7 ignores physical zoom while a gesture is active. |
| CR-002 | Addressed | Patch 3 uses `max(first.radius, second.radius)` for zero-length authority coverage and adds a regression test. |
| CR-003 | Addressed | Patch 9 adds `AuthorityRibbonStream`; `incremental_rasterizer_test.cpp` compares complete live/authority surfaces at all five zooms. |
| CR-004 | Addressed | Patch 9 exports authority tapered chords; `svg_export_test.cpp` compares exported center-sample coverage to `apply_incremental_operation`. |
| CR-005 | Addressed | Patch 6 writes payload/checksums to the inactive bank and commits its generation header last. Hardware power-cut injection is still required. |
| CR-006 | Addressed | Patch 7 separates queue policy by event type and never evicts controls or touch edges to admit a move. |
| CR-007 | Addressed | Patch 7 includes `exporting` in V1 toast invalidation. |
| CR-008 | Addressed | Patch 8 cancels transient authority continuation and requests a durable full refresh after failed live presentation. |
| CR-009 | Addressed | Patch 8 makes pan lift a full-refresh transaction boundary. |
| CR-010 | Addressed | Patch 1 includes ink-trace replay in the harness's final conjunction. |
| CR-011 | Addressed | Patch 1 includes the 28 ms latency verdict and requires a nonempty latency population. |
| CR-012 | Addressed | Patch 1 records successful `show_start` first-contact samples. |
| CR-013 | Safety addressed; refactor deferred | Patch 1 fails the gate closed on chrome-routing mismatch. It does not extract one shared product/replay router. |
| CR-014 | Addressed | Patch 1 terminates exhausted replay explicitly instead of waiting forever while pressed. |
| CR-015 | Addressed | Patch 1 rebases recorded trace timestamps while preserving inter-event timing. |
| CR-016 | Addressed | Patch 1 aligns replay event capacity with capture capacity. |
| CR-017 | Addressed | Patch 1 reports invalid metadata on line 2. |
| CR-018 | Addressed | Patch 5 records ledger render only after every in-grid group tile satisfies immediate quality. |
| CR-019 | Misclassification addressed; historical accounting tradeoff | Patch 5 resets the ledger at explicit all-cache discard/replacement. It prevents false `unexplained` counts but starts a new session instead of recording per-group evictions. |
| CR-020 | Addressed | Patch 7 pauses/stops the sampler before dump and records events at the serialized consumer boundary. |
| CR-021 | Addressed | Patch 4 computes each angularity input point once and acknowledges each chunk once. |
| CR-022 | Addressed | Patch 4 carries the prior terminal chord across same-gesture chunk boundaries. |
| CR-023 | Addressed | Patch 4 records setup ticks before bbox/saturation early returns. |
| CR-024 | Addressed | Patch 4 snapshots cold census before the untimed reuse-accounting revisit. |
| CR-025 | Addressed | Patches 4–5 make the ledger verdict enforce acceptance rather than only printing totals. |
| CR-026 | Addressed | Patch 4 implements the same strict ASCII integer grammar and range checks as production parsing. |
| CR-027 | Addressed | Patch 2 validates `PixelPainter` dimensions/stride with checked extents. |
| CR-028 | Addressed | Patch 2 rejects unsafe/non-finite raster geometry before integer conversion. |
| CR-029 | Addressed | Patch 2 introduces `checked_surface_extent` and uses it for strided surface sizes. |
| CR-030 | Addressed | Patch 2 validates direct strided tile descriptors and exact readable extents. |
| CR-031 | Addressed | Patch 7 preserves RP2350 Down/Up edges and coalesces/drops only moves. RP2350 compilation was unavailable locally. |
| CR-032 | Addressed | Patch 8 treats stage/stream failure as unknown panel state and converges through a full refresh. |
| CR-033 | Addressed | Patch 1 makes replay rejection/cancellation fail the gate instead of silently continuing with divergent policy. |
| CR-034 | Addressed | Patch 7 captures the exact consumed `TouchEvent` sequence instead of independently deriving raw-contact events. |
| CR-035 | Addressed | Patch 8 waits for TE on substantial partial windows (32 rows or more). Electrical validation remains required. |
| CR-036 | Addressed | Patch 8 pre-renders modal chrome into a complete transparent staging cache before panel streaming. |
| CR-037 | Addressed | Patch 5 applies the raw-slot saturation guard to all idle-repair levels, including neighbors. |
| CR-038 | Addressed | Patch 1 returns after a gate pass/fail instead of entering the interactive loop with harness-created authority. |
| CR-039 | Stated trigger addressed; finite-buffer limit remains | Patch 7 grows the V2 queue and reserves two edge slots while displacing/coalescing moves. An arbitrarily long edge-only burst can still exhaust any finite queue. |
| CR-040 | Addressed | Patch 8 drains submitted DMA before aborting a failed stream. |
| CR-041 | Addressed | Patch 8 waits for the first submitted beam-race band before reusing its staging storage. |
| CR-042 | Addressed | Patch 2 rejects overlapping append/publication source storage instead of invoking overlapping `std::copy`. |
| CR-043 | Addressed | Patch 2 rejects odd-width RGB565 stream descriptors before staged reads. |
| CR-044 | Addressed | Patch 8 removes the extra byte swap when copying exposed ring pixels into byte-swapped staging. |
| CR-045 | Addressed in software; hardware proof pending | Patch 8 configures only the selected GPIO edge and counts the ISR invocation directly, avoiding pin-level reclassification. |
| CR-046 | Hang addressed; hard recovery deferred | Patch 8 marks transport not-ready on completion loss and uses bounded semaphore acquisition. It fails closed rather than performing a peripheral hard reset. |
| CR-047 | Addressed under the documented serialized-canvas contract | Patch 10 pre-analyzes all target tiles, checks pins/capacity, then publishes; a pin regression proves no prefix is changed. |
| CR-048 | Addressed | Patch 2 adds explicit stride/runtime validity to `PixelPainter`; invalid painters perform no writes. |
| CR-049 | Addressed | Patch 7 gives `AppStorage` noncopyable RAII cleanup for partial startup allocation. |
| CR-050 | Addressed | Patch 2 makes `CoverageTile::reset` return false and preserve safe empty state for invalid dimensions. |

## Validation receipts

All validation below ran at prepared tip `46bf8399f94ade3c0767d99b4ec0f821a76f66c8` in `/tmp/espdraw-correctness-fixes-f2f6da7`.

| Command / build | Result |
|---|---|
| `./scripts/dev test` | **29/29 passed**, 43.56 s |
| `./scripts/dev release` | **29/29 passed**, 2.68 s |
| `./scripts/dev asan` | **11/11 passed**, 72.85 s |
| `./scripts/dev format-check` | **Passed** |
| `python3 -m compileall -q tools second_review_hardware_ab` | **Passed** |
| `git diff --check f2f6da7..HEAD` | **Passed** |
| Clean `git am` round trip onto detached `f2f6da7` | **Passed**; applied tree `94232d09da27cce8c64ad410e3c48a1c3a01edb1` exactly matched prepared tip's tree |
| ESP-IDF 6.0.2, V2 gate harness, rising TE (`out/build/esp32-correctness-v2`) | **Built**; app size `0xb2970`, 30% app-partition free |
| ESP-IDF 6.0.2, default Raster V1 (`out/build/esp32-correctness-v1`) | **Built**; app size `0x576e0`, 66% free |
| ESP-IDF 6.0.2, V2 ink capture (`out/build/esp32-correctness-capture`) | **Built**; app size `0x65f80`, 60% free |
| ESP-IDF 6.0.2, V2 falling-edge build (`out/build/esp32-correctness-falling`) | **Built**; app size `0x65930`, 60% free |
| RP2350 | **Not run:** `PICO_SDK_PATH` was unset; `scripts/rp2350` requires bootstrap |

The ESP images were compile/link validated only. No panel, touch, TE, power-cut, optical tearing, or RP2350 hardware run was available. Patch 9's exact binary live renderer also needs the device latency gate because host correctness tests do not establish the 28 ms finger-to-glass budget.

## Integration hot spots

The active performance branch was already five commits past the patch base at export. Expect the highest conflict probability in:

- `vector_v2/src/tile_producer.cpp` (patches 4, 5, and 10),
- `vector_v2/src/materialized_canvas.cpp` (patches 2 and 5),
- `esp32/main/vector_v2/vector_v2_app.cpp` (patches 1, 5, 7–9),
- `esp32/main/vector_v2/vector_v2_presenter.cpp` (patches 8–9), and
- associated raster/canvas tests.

Resolve toward the performance branch's newer algorithm while preserving each patch's invariant and regression test. `git range-diff f2f6da7..46bf839 <integration-base>..<integration-tip>` is useful after conflict resolution.
