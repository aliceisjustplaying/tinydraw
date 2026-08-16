# Correctness-fix integration regression and rollback

- Date: 2026-08-16
- Affected branch: `feat/v2-performance-followup`
- Pre-fix boundary: `55d48c7bc36724927c2dee287b71b3d63b7bc29a`
- Fix-series tip: `e14e6e9569a2cf55f464c56ccef786aadd169629`
- Archive branch: `archive/v2-correctness-fixes-e14e6e9`

## Outcome

Ten correctness commits were integrated after the Cold Stage B performance work. A formatting commit followed them. The resulting tree changed 58 files with 1,647 insertions and 421 deletions:

```text
$ git diff --shortstat 55d48c7..e14e6e9
58 files changed, 1647 insertions(+), 421 deletions(-)
```

The series passed host tests and compiled for ESP32, but the prepared patch receipt stated that no panel, touch, optical, or power-cut test had run on hardware. It also called out the input, panel, and live-geometry patches as requiring device validation. See `prepared_correctness_fixes_2026_08_16/MANIFEST.md`.

The first post-integration device benchmark found repeatable performance regressions. Three current-HEAD runs changed the previously green paced-cold gate to red. A one-line device A/B isolated the largest regression to the partial-presentation tear-edge policy added by `ce2cd83`.

The fix series has therefore been removed from the active performance branch and retained on the archive branch named above. No fix from this series should return to the active branch without isolated review and device measurement.

## How the review became an oversized fix series

The work started as a read-only correctness review of the V2 codebase, with extra attention to changes from the prior 24 hours. Two reviews ran against a pinned snapshot while another agent changed performance code concurrently.

The synthesis assigned 50 `CR-*` identifiers:

- Review A contained 32 findings with host tests and targeted reproducers.
- Review B contained 49 source-inspection findings.
- Fourteen Review B findings duplicated Review A.
- Seventeen other Review B labels described feature gaps, performance or maintenance work, unreachable defensive states, or items that still needed a contract or hardware check.

The source and disposition are in `CORRECTNESS_REVIEW_SYNTHESIS_2026-08-16.md`.

The number 50 was subsequently treated too much like a count of confirmed product bugs. It was not. The list mixed:

- product defects and edge cases;
- false-green gates and diagnostic errors;
- malformed-input hardening;
- V1 and unsupported RP2350 work;
- theoretical hardware risks without device reproduction;
- changes to visible geometry and presentation policy.

Those concerns were compressed into ten broad patches. Several patches addressed many findings across multiple products and subsystems. This made it difficult to assess product relevance, review behavior changes, and attribute performance after integration.

## Scope failure

The review request concerned the supported Vector V2 implementation. The integrated series nevertheless included:

- V1 crash-consistent persistence in `cf5d2f3`;
- RP2350 changes in `ce2cd83` and `0679e00`;
- host-only metrics and parser work in `15ec58b`;
- gate-harness behavior in `6d7e4ca`.

The V1 persistence commit alone added 293 lines and removed 36 lines from `esp32/main/drawing_store.cpp` and `esp32/partitions.csv`. `DrawingStore` is used by `esp32/main/hardware_app.cpp`, not the V2 app.

The unsupported and diagnostic changes should not have been folded into the V2 performance branch. Tooling changes should also have been presented separately from product-runtime fixes.

## Integrated commits

| Commit | Files | Diff | Actual effect | Disposition after rollback |
|---|---:|---:|---|---|
| `2710d51` | 17 | `+198/-40` | Checked surface extents and validated raster, tile, painter, and panel staging descriptors | Reassess as narrower supported-V2 hardening |
| `31efca9` | 2 | `+24/-0` | Preserved brush-radius changes when consecutive samples share a position | Small candidate, but measure independently |
| `cf5d2f3` | 2 | `+293/-36` | Replaced V1 in-place persistence with dual-bank crash-consistent storage | Drop from V2 branch |
| `ce2cd83` | 7 | `+174/-37` | Combined presentation recovery, TE policy, chrome preparation, byte order, timeout handling, and an RP2350 delay | Split; do not restore as a unit |
| `6d7e4ca` | 2 | `+72/-27` | Made ink replay and latency failures affect the firmware gate verdict | Gate-only candidate, separate from product code |
| `15ec58b` | 5 | `+82/-37` | Corrected angularity, census, trace parsing, and metrics accounting | Tooling-only candidate |
| `69662f8` | 4 | `+49/-6` | Changed cache ledger sessions and idle-repair saturation behavior | Reassess and benchmark independently |
| `0679e00` | 12 | `+203/-69` | Changed V1, V2, RP2350, capture, and touch-buffer overload behavior | Split to supported V2 only |
| `7361030` | 16 | `+450/-109` | Replaced live/export geometry with authority geometry and exact binary live coverage | High-risk runtime candidate; requires latency and visual A/B |
| `a6a160a` | 3 | `+77/-31` | Preflighted complete 2x2 tile groups before publication | Reassess and benchmark independently |
| `e14e6e9` | 18 | formatting only | Formatted the integrated tree | Removed with the series |

The original patch intent and finding mapping remain in `prepared_correctness_fixes_2026_08_16/MANIFEST.md`.

## Validation gap

The prepared patch series had strong host receipts:

- debug tests: 29/29;
- release tests: 29/29;
- sanitizer tests: 11/11;
- format and Python checks passed;
- ESP32 V1, V2, capture, and gate images compiled.

Those checks did not measure physical presentation timing. The manifest said:

> No panel, touch, TE, power-cut, optical tearing, or RP2350 hardware run was available.

It also stated that the exact binary live renderer needed the device latency gate. Despite those warnings, all ten patches were integrated before running the full device benchmark.

The correct integration sequence was one runtime commit at a time, followed immediately by the relevant device measurement. Host coverage could establish logical invariants, but it could not establish display latency, frame pacing, touch behavior under DMA load, or optical correctness.

## Device benchmark setup

- Device: ESP32-S3 on `/dev/cu.usbmodem101`
- Gate harness slots: 448
- Baseline corpus: `adversarial_tapered_4x+evil_hairlines`
- Baseline receipt: `benchmark-results/cold-stage-b-2026-08-16/RECEIPT.md`
- Baseline commit: `75c9145`
- Treatment commit: `e14e6e9`

The Cold Stage B receipt used three runs plus a confirmation run. It recorded these accepted general-cold maxima:

| Zoom | Compute | Wall | Gate |
|---|---:|---:|---|
| 50% | 356.150 ms | 437.948 ms | green |
| 100% | 348.620 ms | 428.362 ms | green |
| 200% | 410.133 ms | 487.981 ms | green |
| 400% | 431.908 ms | 506.950 ms | red before this series |

The 400% wall gate was already about 7 ms over the 500 ms target. The regression assessment therefore focused on new reds and deltas from that accepted baseline, rather than the harness's overall boolean.

## Current-HEAD device result

All three `e14e6e9` runs reached the automated completion marker without a watchdog or crash. All three produced `paced_cold=0` and `TINYDRAW_VECTOR_V2_GATE_HARNESS_DONE pass=0`.

General-cold three-run maxima:

| Zoom | Baseline wall | Current wall | Delta | Baseline gate | Current gate |
|---|---:|---:|---:|---|---|
| 50% | 437.948 ms | 518.989 ms | +18.50% | green | red in 3/3 |
| 100% | 428.362 ms | 500.409 ms | +16.82% | green | red in 3/3 |
| 200% | 487.981 ms | 549.632 ms | +12.63% | green | red in 3/3 |
| 400% | 506.950 ms | 553.856 ms | +9.25% | red | red in 3/3 |

The regression was concentrated in presentation and tick latency:

| Zoom | Baseline presentation max | Current presentation max | Baseline tick max | Current tick max |
|---|---:|---:|---:|---:|
| 50% | 71.414 ms | 141.848 ms | 8.660 ms | 23.908 ms |
| 100% | 67.191 ms | 135.536 ms | 8.489 ms | 17.997 ms |
| 200% | 64.795 ms | 125.637 ms | 7.918 ms | 21.287 ms |
| 400% | 63.363 ms | 114.607 ms | 10.111 ms | 23.692 ms |

The paced-cold gate requires wall time at or below 500 ms and each tick below 15 ms in `esp32/main/vector_v2/vector_v2_gate_harness.cpp`. The treatment exceeded the tick limit at every tiled zoom.

The seed-7 400% case also changed materially:

| Metric | Baseline max | Current max | Delta |
|---|---:|---:|---:|
| compute | 192.585 ms | 211.354 ms | +9.75% |
| presentation | 80.755 ms | 217.809 ms | +169.72% |
| wall | 279.949 ms | 435.956 ms | +55.73% |
| max tick | 9.652 ms | 21.016 ms | +117.74% |

The live-stress total rose from a baseline maximum of 751.027 ms to 802.941 ms, a 6.91% increase. The partial-TE diagnosis below does not explain that increase. The geometry and input changes remain unmeasured independently.

Raw logs:

- `logs/current-head-run1.log`
- `logs/current-head-run2.log`
- `logs/current-head-run3.log`
- `COMPARISON.txt`
- `SHA256SUMS`

## Root cause of the overt presentation regression

Commit `ce2cd83` changed `VectorV2Presenter::present_pixels` from synchronizing only full-frame presentations:

```cpp
if (full_frame) {
  display_.wait_for_tear_edge(...);
}
```

to synchronizing every presentation at least 32 rows high:

```cpp
constexpr int kPartialTearSyncRows = 32;
const bool tear_synchronized = full_frame || height >= kPartialTearSyncRows;
if (tear_synchronized) {
  display_.wait_for_tear_edge(...);
}
```

V2 tiles are 64 rows high. During progressive cold fill, each publication creates pending bounds and the next loop iteration calls `refresh_region`. The new condition therefore inserted repeated tear-edge waits into the timed fill loop.

CR-035 motivated this policy. The synthesis classified CR-035 as a hardware risk and stated that no panel run was available. The review proposed waiting before each substantial partial presentation to avoid a possible mid-scanout stale band or tear.

The pre-fix owner glass session reported no observed tearing. That observation did not prove every partial timing safe, but it should have prevented an unmeasured scheduling policy from landing as part of a broad correctness batch.

## One-variable device A/B

A temporary probe changed only:

```cpp
const bool tear_synchronized = full_frame || height >= kPartialTearSyncRows;
```

to:

```cpp
const bool tear_synchronized = full_frame;
```

The source file was restored before the probe binary was flashed. No probe change was committed. The probe binary completed the full harness and produced:

| Case | Current presentation max | Probe presentation | Current tick max | Probe tick | Probe gate |
|---|---:|---:|---:|---:|---|
| general 50% | 141.848 ms | 69.332 ms | 23.908 ms | 8.323 ms | green |
| general 100% | 135.536 ms | 67.308 ms | 17.997 ms | 8.331 ms | green |
| general 200% | 125.637 ms | 62.368 ms | 21.287 ms | 7.671 ms | green |
| general 400% | 114.607 ms | 63.780 ms | 23.692 ms | 10.208 ms | existing wall red only |
| seed-7 400% | 217.809 ms | 81.246 ms | 21.016 ms | 9.584 ms | green |

Probe receipt: `logs/full-frame-only-probe-run1.log`.

The one-line change reduced presentation by 44% to 63% and restored every new paced-cold red. It did not revert the rest of `ce2cd83`, so the GPIO edge selection, bounded transport waits, failed-stream DMA draining, byte-order correction, modal chrome preparation, and failure-convergence behavior remained present in the probe.

The device was reflashed with the unmodified `e14e6e9` binary after the probe. The restored binary SHA-256 was `e83f386f84383bd2da5538daa0f9a96da2b24a0f4aee081a6b206bef29aba527`.

## What went wrong in the process

### Source risks were treated as observed failures

Static analysis can establish malformed-input bugs, missing verdict propagation, and unsafe state transitions. It cannot establish that a theoretical panel race occurs on glass, or that a proposed synchronization policy meets the product's latency contract. The review documents contained those distinctions, but the integration decision flattened them.

### The patches were too broad

`ce2cd83` addressed panel failure recovery, optical synchronization, chrome staging, byte order, GPIO classification, timeout handling, and RP2350 behavior in one commit. A regression in one policy required evaluating the whole bundle. `0679e00` similarly mixed supported V2 input behavior with V1, capture, physical display, and RP2350 changes.

### Unsupported and out-of-scope code was included

The task centered on V2. V1 persistence and RP2350 fixes increased review and integration risk without advancing that objective.

### Device validation happened after the batch

The patch manifest correctly warned that device testing remained. The integration still proceeded through all runtime patches before the device gate ran. This forfeited immediate attribution and allowed a large regression to sit under ten commits.

### Host success was overinterpreted

The host suites proved many local invariants and memory-safety cases. They could not exercise ESP32 panel timing. Passing host and compile checks was necessary, but it was not a release verdict for these changes.

## Recovery decision

The active branch returns to `55d48c7`, the remote tip immediately before the ten fixes. The complete fix tree, this postmortem, the review artifacts, prepared patches, and device logs remain on:

```text
archive/v2-correctness-fixes-e14e6e9
```

This is a retreat to a measured baseline, not a claim that every finding was false. The archive exists so individual changes can be recovered without reconstructing the work.

## Rules for any future reintegration

1. Restrict work to supported Vector V2 unless the owner explicitly expands scope.
2. Separate product runtime, firmware gate, host tooling, and unsupported-target changes.
3. Give each runtime behavior change its own commit.
4. Run the smallest relevant device gate immediately after each runtime commit.
5. Run three full device repetitions before accepting a final stack.
6. Do not convert an unobserved hardware risk into a blocking policy without an optical or electrical receipt.
7. Treat performance and correctness as simultaneous constraints. A correctness fix that violates the established interaction budget is not ready.

Suggested order if selected fixes are revisited:

1. Gate and host-tool corrections that do not enter the product binary.
2. The stationary-radius fix as an isolated visual change.
3. Narrow supported-V2 descriptor validation.
4. Supported-V2 touch-overflow behavior, without V1 or RP2350 changes.
5. Cache and tile-publication changes, each with cold and pan measurements.
6. Geometry parity only after live-stress, trace-latency, and glass A/B.
7. Panel failure recovery split into independent changes. Leave partial-TE scheduling open until a nonblocking design has device evidence.

## Preserved artifacts

The archive branch includes:

- `CORRECTNESS_REVIEW_2026-08-16.md`
- `LATEST_tinydraw-review-report.md`
- `review_findings_2026_08_16_correctness/REVIEW.md`
- `CORRECTNESS_REVIEW_SYNTHESIS_2026-08-16.md`
- `prepared_correctness_fixes_2026_08_16/`
- this directory and its device logs

Large unrelated artifacts were deliberately excluded from the archive commit:

- `benchmark-results/wave1a-panel/` (240 MB)
- `reference/CO5300_Datasheet_V0.00.pdf`
- `tinydraw-review-a560d20.zip` and checksum
