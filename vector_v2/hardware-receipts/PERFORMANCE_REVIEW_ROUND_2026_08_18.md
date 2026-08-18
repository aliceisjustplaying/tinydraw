# Performance review round — 2026-08-18

## Status

This receipt records the implementation round against
[`tinydraw-v2-performance-review-20260818.md`](../../tinydraw-v2-performance-review-20260818.md),
reviewed at `b63b07f905b40b3d435679911d2de89e57e1f062`. The accepted work is the
commit series `594c218..af114d6`. It covers startup occupancy, ring-local presentation,
input wake/drain behavior, the operation spatial index, composition cleanup,
settled retry correctness, panel-reset recovery, and the 400% hairline/eraser
glass regression found during acceptance.

The current automated hardware battery is green. The owner’s final glass
retest is green: no tearing, no persistent white blocks, and the previously
slow dense hairline/eraser region now completes within the accepted cold gate.
Settled AA remains the pre-existing yellow receipt; this round does not claim
full closure of the review’s history-generation, banded-settle, or
intra-operation preemption proposals.

## Measurement baseline

The pre-round device reference was captured on 2026-08-17. The accepted device
run was captured on 2026-08-18 with gate image `0x12b4d0`. Times include the
existing pacing and presentation contract.

| Workload | Pre-round | Accepted | Result |
|---|---:|---:|---|
| Seed-7 sparse, 400% | 263 steps, 175.795 ms compute, 259.976 ms wall | 25 steps, 158.270 ms compute, 248.974 ms wall | 90.5% fewer producer calls; 4.2% lower wall |
| Hard corpus, 400% | 12,096 scans, 200 steps, 202.060 ms wall | 456 scans, 19 steps, 193.532 ms wall | 96.2% fewer scans; 6.4% lower wall |
| Mixed tapered + evil hairlines, 400% | 491 steps, 413.178 ms compute, 486.473 ms wall | 302 steps, 416.645 ms compute, 494.993 ms wall | fewer slices; dense paint remains dominant; under 520 ms gate |
| Warm pan p95, 100% / 400% | 33.930 / 33.932 ms | 33.940 / 33.939 ms | unchanged; all frames reused and no tear-edge failures |

The host release scorecard before the spatial index was 1.840 ms seed-7,
3.341 ms adversarial, and 1.287 ms overlap. With the accepted index and dense
bypass it measured 1.719, 3.299, and 1.319 ms respectively, with exact pixels.
The index’s payoff is therefore work elimination and device slice reduction,
not a universal host-wall win.

## Accepted changes mapped to the review

| Review findings | Accepted change and evidence |
|---|---|
| F1; F2 partial | Blank boot uses `reset_blank`; autosave restore rebuilds the exact overview and a conservative tiled-may-ink map. Pen bounds only set bits; erasers never unsafely clear them. Boundary and 400% minimum-radius tests cover the occupancy certificate. Affected-cell clearing/reclassification remains open, so erase-heavy sessions can retain false-positive occupancy until a safe rebuild. |
| F3–F5, F16–F18 | Local refresh, settled staging, chrome, provisional ink, committed ink, and exposed-strip pending-authority overlays operate on the toroidal frame ring. Canvas and chrome bands remain separate, byte order is preserved, and partial failures drain queued display work. `TINYDRAW_GATE1_RING_LOCAL ... pass=1`; the next pan remains reusable. |
| F6–F8 | The sampler now signals empty-to-nonempty through a binary semaphore, keeps stop signaling separate, publishes an allocation-free urgent atomic, and self-deletes after cross-core teardown. The main loop drains queued events before cosmetic work, handles one cosmetic transition per iteration, and blocks on the event wake when idle. |
| F6–F9 | Hard queue overflow now resynchronizes to a valid Down/Up stream and reports `touch_resyncs`. Background fill, repair, settle, absorption, and presentation boundaries consult touch urgency. In the accepted gate, `touch_resyncs=0`; draw-while-fill measured 4.904 ms poll gap, 8.049 ms maximum compute slice, and passed. |
| F11, F22–F23 | An append-maintained 128-world-pixel dense bitset index covers 168 cells plus a large-operation set. Queries OR words, deduplicate without heap allocation, return newest first, and retain exact authority fallback. Producer and settled paths use the index only when it rejects at least 25%, avoiding the dense PSRAM-indirection regression. Product allocation is 93,176 bytes, dead-last in PSRAM, with authority/candidate/dedup counters. |
| F14 | Settled compositing uses the exact remaining-white fold directly. Transient settled failures retain their cursor and retry up to three times; permanent failures are explicit. This closes F33’s silent-promotion bug as well. |
| F15 | Raw materialized composition copies rows, and overview fallback uses zoom shifts and run fills; division/modulo is gone from the per-pixel loops. |
| F22 | Rerender cause, spatial candidate, touch resync, phase, and panel-reset receipts are present in the harness/product paths. |
| F33 | Settled retry state advances only after success or an intentional skip; telemetry distinguishes transient and permanent failures. |

Additional hardening: CO5300 warm reset retries the full expander
configure/power-down/power-up sequence three times, resets the I²C bus between
failed attempts, and reports the first causal stage. Reset success also requires
I²C device and bus cleanup; transport initialization fails closed otherwise.
The accepted gate and normal product boot both completed on attempt 1 with no
bus reset.

## White-block glass regression and fix

The first acceptance glass pass found no tearing, but reproduced a severe
400% regression after panning into dense minimum-radius hairlines covered by a
dense eraser grid. Tiles appeared block by block, two blocks remained overview
white beyond the expected 500 ms interval, and neighboring blocks completed
visibly slowly. The owner photograph is
`/Users/sarah/Downloads/IMG_2766.HEIC`.

The minimized host command is:

```sh
out/build/host-release/vector_v2/tinydraw_vector_v2_raster_census \
  --hairline-pan-repro 10 1 1 16 8
```

Before the fix it deterministically required 30 producer calls; at the
29-call/approximately-500-ms checkpoint it reported two missing blocks and
5,376 fallback pixels. Completion was eventually exact (`mismatches=0`,
`white_blocks=0`), proving starvation-by-work rather than corrupt authority.
The compact before/after per-slice trace is
[`HAIRLINE_PAN_TRACE_2026_08_18.md`](HAIRLINE_PAN_TRACE_2026_08_18.md).

The accepted fix adds a strict certificate for a constant-radius capsule that
covers every pixel center of the complete contiguous producer surface. Only a
fresh mask, full-surface bounds, four guarded corner proofs, and sufficient
slice budget permit the packed pixel/mask bulk fill; `covers_pixel` remains the
geometric authority. The minimized repro now completes in 29 calls with zero
missing blocks, zero fallback, zero mismatches, and `regression=0`.

On device, the evil-hairline 400% capacity gate completed in 186.976 ms with a
9.154 ms worst producer tick. The broader mixed 400% corpus completed in
494.993 ms with a 9.343 ms worst tick. The complete gate passed, and the owner
repeated the photographed pan on product glass with no tearing or white-block
recurrence.

## Rejected and reverted experiments

- **Prepared geometry cache (F12):** exact caller-funded cache keyed by
  epoch/operation/zoom recorded 131 hits, 34 misses, and 25,632 live bytes.
  Setup fell from roughly 0.36 to 0.031 ms in the minimized census, but the
  paint-work ceiling still required 30 calls and two blocks were still missing
  at the checkpoint. Release wall had no reliable improvement, while product
  cost was 100 KiB. The code, tests, and product allocation were fully removed.
- **Completed-group deferral removal:** publishing a group in the same call
  after a slice-filling final sweep did not change the 30-call repro and
  weakened the established interaction boundary. The experiment was reverted;
  the producer still publishes that completed group on the next call.
- **Summary-bitmap row probe:** device 400% improved 2.2%, but 50% regressed
  4.0%; reverted. Evidence is summarized in
  [`COLD_COMPUTE_CAMPAIGN_RECEIPT.md`](../../benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md).
- **Word mask-window loads:** device regressed 7–13%; Xtensa emitted `callx8`
  for unproven-alignment loads. Byte `l8ui` remains the correct grain.
- **6×2 tile band replay:** host 400% regressed 2.7× because broad rows rarely
  saturated. It remains blocked on block-granular saturation.
- **Shared hybrid warm/seeded painter:** cold regressed about 5% and append
  latency worsened; caller-specific warm and cold paths remain separate.
- **Curved segment-chunk metadata:** improved the old tapered-only device wall
  8.5% but consumed 200,002 bytes and missed the campaign trajectory; removed.
  See [`COLD_SEGMENT_CHUNK_RECEIPT.md`](../../benchmark-results/wave2-compositor/COLD_SEGMENT_CHUNK_RECEIPT.md).
- **Exact publication batching:** best candidate moved wall only 1.6%; broader
  batching violated the 15 ms interaction limit. See
  [`COLD_PUBLICATION_BATCH_RECEIPT.md`](../../benchmark-results/wave2-compositor/COLD_PUBLICATION_BATCH_RECEIPT.md).

## Validation

Representative commands used during the round:

```sh
CCACHE_DISABLE=1 cmake --build --preset host-debug -j 8
ctest --preset host-debug --output-on-failure
CCACHE_DISABLE=1 cmake --build --preset host-release -j 8
ctest --preset host-release --output-on-failure
CCACHE_DISABLE=1 cmake --build --preset host-asan -j 8
ctest --preset host-asan --output-on-failure
out/build/host-release/vector_v2/tinydraw_vector_v2_raster_census --cold-scorecard
out/build/host-release/vector_v2/tinydraw_vector_v2_raster_census --hairline-pan-repro
CCACHE_DISABLE=1 ./scripts/esp32 build
./scripts/esp32 vector-v2-gate-harness /dev/cu.usbmodem1101 448 verify
./scripts/esp32 raster-v1
./scripts/dev format-check
```

Before the final hairline certificate, the complete debug and release host
suites passed 31/31; ASan passed 13/13. Focused release rendering after the
white-block fix passed 112/112 tests (61,371 assertions), and the minimized
hairline repro is exact and green. Focused clang-tidy passed for the new
operation index/log. Full format/tidy/cppcheck retain known pre-existing
findings, notably `esp32/main/firmware_canvas.h:28`, authority-journal
complexity, the materialized-canvas constructor parameter count, and older
cppcheck items; no new warning was accepted as closure evidence.

The accepted hardware verdict is the all-ones `TINYDRAW_GATE1_AUTOMATED_DONE`
line captured in the 2026-08-18 run. Product firmware then restored autosave,
completed panel reset on attempt 1, reached `TINYDRAW_VECTOR_V2_READY`, retained
2,187,528 bytes free PSRAM / 2,162,688 largest block, and settled without a
failure. Product firmware was left installed for the final glass retest.

The post-audit rerun with gate image `0x12b5d0` also returned the all-ones
verdict. The photographed hairline region completed in 186.978 ms with a 9.132
ms worst tick; the broader mixed 400% corpus completed in 502.114 ms under its
520 ms guard. Ring locality passed, draw-while-fill held a 4.917 ms poll gap,
and all paced-cold runs reported zero touch overflows and resynchronizations.
The restored product boot again reported complete panel/I²C cleanup and READY.

## Remaining work

1. F9 is incomplete inside the largest transaction units: absorption and full
   composition still need persistent sub-operation cursors rather than urgency
   checks only at phase boundaries.
2. F10 history generations remain architectural work; Undo/Redo can still
   rebuild derived state from authority.
3. F12 prepared geometry reuse is not justified in its tested 100 KiB form.
   Revisit only with a paint-dominant design or a demonstrably smaller budget.
4. F13 banded 25% settle, F21 cache-directory complexity, and F26 spatially
   prioritized repair remain open.
5. F20 autosave encode/allocation remains main-task work.
6. F24 targeted IRAM placement was not attempted; the current evidence does
   not justify spending internal RAM without a kernel-level A/B.
7. F28/F29 cooperative full compose and perceptual AA ordering remain open.
8. Settled AA stays yellow pending its separate optical acceptance receipt.
9. The ring-local hardware gate proves reuse and presentation success but does
   not yet carry a framebuffer pixel oracle or submitted-area assertion. The
   current address correctness evidence is host staging coverage plus the
   no-tearing product glass pass.

## Evidence and artifact paths

- Review source: `tinydraw-v2-performance-review-20260818.md`
- Minimized hairline trace: [`HAIRLINE_PAN_TRACE_2026_08_18.md`](HAIRLINE_PAN_TRACE_2026_08_18.md)
- Earlier cold campaign: `benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md`
- Combined-corpus baseline: `benchmark-results/wave2-compositor/COLD_GENERAL_BASELINE_RECEIPT.md`
- Pan/staging evidence: `benchmark-results/wave2-compositor/STAGING_INVARIANT_RECEIPT.md`, `benchmark-results/wave2-compositor/CHROME_LIFETIME_RECEIPT.md`
- Historical performance receipts: `vector_v2/hardware-receipts/PERF_ROUND_2_BASELINES_2026_08_14.md`, `vector_v2/hardware-receipts/PAN_FLOOR_CLOSURE_2026_08_15.md`, `vector_v2/hardware-receipts/LONGSTROKE_COLDRENDER_INVESTIGATION_2026_08_14.md`

Raw serial logs and the owner photograph were intentionally not checked in;
their acceptance measurements are transcribed above.
