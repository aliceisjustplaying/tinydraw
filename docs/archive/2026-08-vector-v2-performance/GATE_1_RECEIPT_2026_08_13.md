# Gate 1 receipt — production tile producer

Date: 2026-08-13  
Branch: `feat/vector-canvas-production`  
Hardware: ESP32-S3, 8 MiB PSRAM, 240 MHz, CO5300 368×448 panel  
Historical plan: [`PRODUCTION_GATE_PLAN_2026_08_13.md`](../2026-08-vector-v2-foundation/PRODUCTION_GATE_PLAN_2026_08_13.md)

## Verdict: YELLOW

The hard-edged production tile path passes the original 500 ms viewport gate at
aligned 100% and 400% verdict views for both required 1,000-operation workloads.
Cache feasibility and valid-cache pan are now separately closed by
[`GATE_1_CACHE_CLOSURE_2026_08_13.md`](GATE_1_CACHE_CLOSURE_2026_08_13.md): the
complete 100% world fits as 266 raw tiles plus 378 compact uniform identities,
and cached pan reaches first physical completion in 30.6–30.7 ms.
A later worst-case unaligned cache-retention probe measured **0.645–0.721 s**
for a cold viewport; on 2026-08-13 the user accepted that latency as visible
optimization debt so work can proceed, while requiring retained views not to
replay. The measured 4-sample SSAA path still does not pass: one complete
visible 100% viewport took **807.961 ms**. The cheap supersampling route is not
funded as the product renderer. The analytic AA gate moves ahead of Gate 2.

This is not a ship-quality rendering pass. Hard-edged tiles are provisional
`kImmediate`; `kSettled` remains reserved for anti-aliased output.

## Hardware measurements

Evidence logs: [`hardware-receipts/gate1-tile-producer.log`](hardware-receipts/gate1-tile-producer.log),
[`hardware-receipts/gate1-p95-20-runs.log`](hardware-receipts/gate1-p95-20-runs.log),
[`hardware-receipts/gate1-pan-p95-20-runs.log`](hardware-receipts/gate1-pan-p95-20-runs.log),
[`hardware-receipts/gate1-fable-fix.log`](hardware-receipts/gate1-fable-fix.log),
[`hardware-receipts/gate1-clean-head-p95-20-runs.log`](hardware-receipts/gate1-clean-head-p95-20-runs.log),
[`hardware-receipts/gate1-grok-fixes-p95-20-runs.log`](hardware-receipts/gate1-grok-fixes-p95-20-runs.log),
[`hardware-receipts/gate1-cache-retention-final.log`](hardware-receipts/gate1-cache-retention-final.log),
[`hardware-receipts/636b9c7-memory-layout-320.log`](hardware-receipts/636b9c7-memory-layout-320.log),
and [`hardware-receipts/gate1-paper-cache-scroller.log`](hardware-receipts/gate1-paper-cache-scroller.log).

### Deterministic synthetic regression workload

Workload: 1,000 operations × 20 samples, generated on device.

| Zoom | Cold complete visible fill p95 (20 runs) | Maximum replay slice | Result |
|---|---:|---:|---|
| 100% | 380.148 ms | 7.506 ms | PASS |
| 400% | 297.135 ms | 28.392 ms | PASS |

### Seed-7 realistic handwriting verdict workload

Workload identity: `populate_realistic_handwriting`, seed 7, 1,000 operations,
19,844 raw source samples, maximum stroke 198 samples. Conversion/load took
240.535 ms.

| Zoom | Cold complete visible fill p95 (20 runs) | Maximum replay slice | Visible operations rendered | Result |
|---|---:|---:|---:|---|
| 100% | 424.673 ms | 5.346 ms | 475 sliced applications | PASS |
| 400% | 376.894 ms | 28.102 ms | 90 sliced applications | PASS |

The first 400% capture exposed a 35.684 ms replay slice caused by a 198-sample
stroke. The final producer bounds work by operation count, sample segments, and
a conservative projected raster-area budget. Splitting can count one source
operation in multiple slices, hence “sliced applications” rather than distinct
operations. The final post-review 20-run recapture at clean commit `765104b` measured:

| Workload | Zoom | Cold complete visible fill p95 | Maximum producer unit | Result |
|---|---:|---:|---:|---|
| synthetic | 100% | 429.683 ms | 7.582 ms | PASS |
| synthetic | 400% | 347.599 ms | 12.064 ms | PASS |
| seed-7 realistic | 100% | 488.678 ms | 5.158 ms | PASS |
| seed-7 realistic | 400% | 430.955 ms | 10.832 ms | PASS |

An external review found unsigned subtraction could wrap after admitting one
segment larger than the raster-work budget. The producer now isolates that
first unsplittable segment and clips subsequent work estimates to the active
128×128 supertask. A first hardware attempt without clipping kept units small
but took 608.104 ms for the realistic 400% fill; it was rejected rather than
accepted as a responsiveness tradeoff.

### Four-sample SSAA probe

The probe rendered each visible 100% tile through the existing 200% raster path
into a 128×128 workspace, then box-downsampled to 64×64 and progressively
presented the result.

- complete visible viewport p95 (20 runs): **807.990 ms**
- tile steps: 42
- maximum tile compute: 29.658 ms
- display presentation portion: 57.313 ms
- verdict: **FAIL** against 500 ms

This is a measured complete viewport, not an extrapolation from hard-edged cost.

### Pan adapter

Automated fallback composition moved the view origin from `(0,0)` to `(120,120)`
and completed presentation at both 100% and 400%. Twenty cold-start runs measured:

| Zoom | Compose p95 | Event-to-submit p95 | Event-to-first-complete p95 | Result |
|---|---:|---:|---:|---|
| 100% | 26.142 ms | 26.800 ms | 27.606 ms | PASS |
| 400% | 25.943 ms | 26.603 ms | 27.409 ms | PASS |

This closes the ≤35 ms fallback pan path and basic adapter defect. The final
human test still checks physical toolbar mode selection and touch behavior.

### Cache retention and cold-pan characterization

A 320-slot LRU pool retains five worst-case arbitrary-alignment viewport
footprints (`5 × 56 = 280` tiles) with 40 additional slots. The final hardware
probe filled an unaligned viewport at every tiled zoom, filled a disjoint 400%
destination, then returned to the original viewport at every zoom.

| View | Cold fill | Cached return compose + transfer | Remaining tiles | Fallback pixels |
|---|---:|---:|---:|---:|
| 50% origin | 687.995 ms | 48.605 ms | 0 | 0 |
| 100% origin | 687.997 ms | 48.942 ms | 0 | 0 |
| 200% origin | 644.997 ms | 49.691 ms | 0 | 0 |
| 400% origin | 669.997 ms | 50.524 ms | 0 | 0 |
| disjoint 400% | 720.998 ms | — | 0 after fill | 0 after fill |

All four post-disjoint round trips were cache hits. The automated receipt ended
with `cache=1` and `return=1`. The cold 0.64–0.72 s range is not relabeled as a
sub-500-ms pass; it is an explicitly accepted optimization target. Revisited
views must continue to avoid replay.

### Draw while filling and adversarial XL input

The post-review hardware run started a live preview while 400% fill was active,
then committed an eight-sample fast zig-zag at the actual XL world radius for
400%. The commit invalidated active producer work; stale work was rejected and
restarted.

Twenty final clean-HEAD runs measured:

- producer poll-gap p95: **1.135 ms**
- event-to-first-submit p95: **3.217 ms**
- event-to-first-transfer-complete p95: **3.390 ms**
- maximum replay-compute unit: **12.883 ms**
- maximum producer/display unit: **12.883 ms**
- complete restarted fill p95: **466.211 ms**
- stale publication accepted: **no**
- result: **PASS**

Progressive composition now updates the presenter's live frame, preventing live
stroke rectangles from restoring stale fallback pixels. Snapshot reset also
rebases the producer's uniform baseline.

### Live memory receipt

The complete live image reports **5,540,584 bytes** of explicitly allocated
caller-owned storage, then **2,801,332 bytes free PSRAM** with a **2,752,512-byte
largest block** after loading the seed-7 document and completing all automated
probes. Incremental tile scratch covers the 56-tile arbitrary-alignment visible
worst case; its publication metadata and affected-key list are also in this
declared PSRAM storage rather than the 6 KiB main-task stack.

A separate fresh empty-heap probe allocated the complete **4,948,576-byte**
320-slot plan and then a distinct **1,572,864-byte** contiguous reserve. This
replaces the stale 128-slot allocation receipt; the live numbers above remain
the more representative coexistence evidence.

## Correctness and architecture evidence

- Host oracle: produced tiles byte-equal direct painter-ordered viewport replay.
- Bounds-filtered replay preserves source order and skips distant operations.
- Hard-edged producer and incremental append publish `kImmediate`.
- Same-revision quality downgrade is rejected; AA can replace provisional
  output one-directionally.
- Tile fill is resumable in bounded operation batches, without threads.
- Revision/epoch changes abort active work rather than publish stale pixels.
- A worst-case 7×8 refined viewport survives an intersecting XL append without
  any tile falling back to the overview.
- Five 7×8 viewport footprints fit simultaneously; the 321st distinct
  publication evicts the host-proven least-recently-used tile.
- Every tiled zoom survives a disjoint 400% fill and returns with zero fallback
  pixels on hardware.
- Progressive presentation composes bounded regions through DisplayScheduler.
- Source geometry is the single raw operation sample log. **No per-zoom LOD
  copies and no simplifier are used.**

## Automated validation

Passed before flashing:

```text
./scripts/dev test          22/22 CTest entries passed
./scripts/dev format-check  passed
./scripts/dev cppcheck      passed
./scripts/dev tidy          passed
./scripts/dev asan          4/4 sanitizer CTest entries passed
ESP-IDF production live app build and flash passed
```

Known build warning: ESP-IDF's `esp_lcd_touch_get_coordinates` dependency API is
deprecated upstream. It is unrelated to Gate 1.

## Cache closure addendum

The final paper-aware hardware run retains the 320-slot raw pool and adds a
compact complete identity catalog. At 100%, every world tile was simultaneously
materialized with `raw=266`, `uniform=378`, and `fallback=0`. A distinct
1,572,864-byte export reserve also allocated successfully alongside the live
working set. See the cache closure receipt for the full breakdown and the
one-core decision.

## One remaining consolidated glass test

The flashed app ends at 25% with the seed-7 document loaded. One short human pass
must confirm:

1. handwriting is present at 25%, including very thin marks;
2. cycle to 100% and 400%; the Gate 1 button now opens the inked upper-left
   origin, where image detail must become crisp rather than overview pixels;
3. select Pan in the toolbar and drag at 100% and 400%; the canvas follows;
4. draw one XL stroke at 400%, then return to 25%; it remains present;
5. no corruption, missing chunks, or obvious input lag.

This human observation is the remaining physical-input check. It cannot reverse
the YELLOW AA verdict; it can only reveal an interaction/correctness failure
that would turn the gate RED.

## Next funded work at receipt time

This receipt originally recommended running the timeboxed analytic AA gate next.
The later aggressive glass test exposed input starvation, lost camera position,
and cross-zoom cache churn, so `PROJECT_STATE.md` and `V2_ROADMAP.md` now fund
that bounded interaction batch first. The warning still stands: do not optimize
or productionize the failed per-tile supersample probe unless new evidence
changes the cost model.
