# Gate 1 receipt — production tile producer

Date: 2026-08-13  
Branch: `feat/vector-canvas-production`  
Hardware: ESP32-S3, 8 MiB PSRAM, 240 MHz, CO5300 368×448 panel  
Plan: [`../PRODUCTION_GATE_PLAN_2026_08_13.md`](../PRODUCTION_GATE_PLAN_2026_08_13.md)

## Verdict: YELLOW

The hard-edged production tile path passes the 500 ms viewport gate at 100% and
400% for both required 1,000-operation workloads. The measured 4-sample SSAA
path does not: one complete visible 100% viewport took **807.961 ms**. Per the
precommitted verdict table, the cheap supersampling route is not funded as the
product renderer. The analytic AA gate moves ahead of Gate 2.

This is not a ship-quality rendering pass. Hard-edged tiles are provisional
`kImmediate`; `kSettled` remains reserved for anti-aliased output.

## Hardware measurements

Evidence logs: [`hardware-receipts/gate1-tile-producer.log`](hardware-receipts/gate1-tile-producer.log),
[`hardware-receipts/gate1-p95-20-runs.log`](hardware-receipts/gate1-p95-20-runs.log),
[`hardware-receipts/gate1-pan-p95-20-runs.log`](hardware-receipts/gate1-pan-p95-20-runs.log), and
[`hardware-receipts/gate1-fable-fix.log`](hardware-receipts/gate1-fable-fix.log).

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
operations. After review fixes, viewport-only publication (42 rather than 48
tiles) still measured 444.738 ms at 100% and 438.758 ms at 400% in the single
hardware recapture; the earlier table remains the pre-fix 20-run p95 receipt.

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

### Draw while filling and adversarial XL input

The post-review hardware run started a live preview while 400% fill was active,
then committed an eight-sample fast zig-zag at the actual XL world radius for
400%. The commit invalidated active producer work; stale work was rejected and
restarted.

- pre-preview producer poll gap: **2.010 ms**
- event-to-first-submit: **4.092 ms**
- event-to-first-transfer-complete: **4.268 ms**
- maximum replay-compute slice: **29.311 ms**
- maximum producer/display unit: **29.311 ms**
- complete restarted fill: **473.983 ms**
- stale publication accepted: **no**
- result: **PASS**

Progressive composition now updates the presenter's live frame, preventing live
stroke rectangles from restoring stale fallback pixels. Snapshot reset also
rebases the producer's uniform baseline.

### Live memory receipt

The complete live image reports **3,628,512 bytes** of explicitly allocated
caller-owned storage, then **4,714,428 bytes free PSRAM** with a **4,587,520-byte
largest block** after loading the seed-7 document and completing all automated
probes. This is the live Gate 1 image, not the earlier empty-heap plan receipt.

## Correctness and architecture evidence

- Host oracle: produced tiles byte-equal direct painter-ordered viewport replay.
- Bounds-filtered replay preserves source order and skips distant operations.
- Hard-edged producer and incremental append publish `kImmediate`.
- Same-revision quality downgrade is rejected; AA can replace provisional
  output one-directionally.
- Tile fill is resumable in bounded operation batches, without threads.
- Revision/epoch changes abort active work rather than publish stale pixels.
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
ESP-IDF production live app build and flash passed
```

Known build warning: ESP-IDF's `esp_lcd_touch_get_coordinates` dependency API is
deprecated upstream. It is unrelated to Gate 1.

## One remaining consolidated glass test

The flashed app ends at 25% with the seed-7 document loaded. One short human pass
must confirm:

1. handwriting is present at 25%, including very thin marks;
2. cycle to 100% and 400%; image detail is crisp rather than overview pixels;
3. select Pan in the toolbar and drag at 100% and 400%; the canvas follows;
4. draw one XL stroke at 400%, then return to 25%; it remains present;
5. no corruption, missing chunks, or obvious input lag.

This human observation is the remaining physical-input check. It cannot reverse
the YELLOW AA verdict; it can only reveal an interaction/correctness failure
that would turn the gate RED.

## Next funded work

Run the timeboxed analytic AA gate immediately, before Gate 2. Do not optimize or
productionize the failed per-tile supersample probe unless new evidence changes
the cost model.
