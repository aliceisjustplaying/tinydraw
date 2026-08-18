# Performance review round — 2026-08-18

## Status

This receipt records the implementation round against
[`tinydraw-v2-performance-review-20260818.md`](../../tinydraw-v2-performance-review-20260818.md),
reviewed at `b63b07f905b40b3d435679911d2de89e57e1f062`. The first accepted work is
the commit series `594c218..af114d6`. It covers startup occupancy, ring-local presentation,
input wake/drain behavior, the operation spatial index, composition cleanup,
settled retry correctness, panel-reset recovery, and the 400% hairline/eraser
glass regression found during acceptance.

The accepted `594c218..af114d6` automated hardware battery is green. The
owner’s glass retest for that series is green: no tearing, no persistent white
blocks, and the previously slow dense hairline/eraser region completes within
the accepted cold gate. The follow-up series `2191d6b..57f9910` adds the
cooperative work required by F9, F19, and F28; their automated hardware battery
is now also green. The owner’s final glass test for this follow-up remains
pending. Settled AA remains the pre-existing yellow receipt; this round does
not claim closure of the review’s history-generation or banded-settle
proposals.

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

## Cooperative follow-up — hardware accepted, glass pending

The follow-up is host-verified and accepted by the automated physical gate.
Its remaining acceptance step is the owner’s glass test.

| Review findings | Accepted follow-up change and evidence |
|---|---|
| F3–F5, F16–F18 | Ring-local receipts now count exact logical panel pixels. A host oracle covers wrapped row/column staging and exposed-strip reconstruction; the device gate proves local canvas, chrome, provisional ink, and committed ink submissions are smaller than the full canvas while the next pan remains reusable. Optical no-tear remains a glass oracle. |
| F9, F19 | Pending-operation absorption is a persistent phase machine covering overview copy/raster, affected identity enumeration, uniform/raw retention, offscreen retention, staged overview publication, bounded metadata, and a scalar final commit. Metadata resumes across uniforms, raw slots, rerender damage, and occupancy. The caller supplies a 256-pixel raster quantum and one slice between input samples. Cancellation abandons unpublished continuation state; restart converges to exact pixels while the pending overlay remains authority. |
| F9 | Settled rendering retains its authority fingerprint, operation/chord position, compositing position, and final-fold position across 512-work-unit slices. Complete output and replay statistics are bit-identical to the synchronous path; transient retry state remains durable. |
| F28 | Full refresh composition advances eight rows per slice, or 56 slices for the 448-row frame, and submits no panel pixels until the complete frame is ready. Pressed input and authority/canvas disagreement block progress. A live-ink interruption preserves refresh intent and restarts from row zero, preventing a partially composed frame from being published. Startup and other hard transition paths remain synchronous. |
| F9, F28 | Tile production can explicitly abandon unpublished work before absorption borrows its chord-plan scratch. This keeps the scratch alias serialization explicit and makes cancellation safe. |

The follow-up adds no external allocation. The final continuation layout is
184 bytes for the overview/metadata stage, 632 bytes for
`PendingOperationAbsorption`, and 240 bytes for `MaterializedCanvas`: 104 bytes
of additional caller state and eight bytes of canvas state over the row-stage
version, with no PSRAM growth. Composition uses the existing 329,728-byte
frame. Absorption reuses the existing 329,728-byte overview scratch, 3,024-byte
affected-key array, 512-byte tile mask, and 12,384-byte producer chord-plan
storage. Settling reuses its existing 40,960-byte workspace.

Several corrections were required before the follow-up matched the product
contract:

- retention carry-over is charged by completed work, so a paused absorption
  restart does not spend its deadline while idle;
- the gate offers an absorption slice after every input sample, matching the
  product cadence and keeping observed backlog at one operation;
- dense unmasked overview rows retain an intra-row cursor rather than treating
  a complete wide row as one work unit;
- refresh interruption retains deferred dirty intent and restarts a fresh
  composition after live input or pending authority drains;
- final overview publication copies one 368-pixel RGB565 row per slice, with a
  validated canvas/revision/source/epoch proof and exact cancel/restart tests;
- metadata invalidation advances through bounded uniform, raw-slot, rerender
  damage, and occupancy phases before a scalar revision commit; the commit
  revalidates the diagnostic-ledger identity captured by the continuation.

### Superseded red diagnostic receipts

The first cooperative device run exposed two independent test/model problems
and one real publication boundary. Its mixed-draw model offered absorption only
once per 32-sample chunk, allowing a 45–48-operation backlog. The dense 25%
recorded trace then measured a 12.472 ms slice. After matching the product’s
per-sample cadence, backlog fell to one; 25% mixed pen/eraser slices measured
4.930/4.943 ms and all 50–400% mixed cases stayed at or below 3.040 ms. The
dense 25% trace still measured 11.277 ms. Attribution identified the atomic
329,728-byte overview publication, not geometry, while every other automated
gate remained green.

Row-staging removed that copy boundary. Two intermediate device confirmations
reduced the dense 25% maximum to 4.386 and 4.751 ms and attributed both maxima
to the metadata commit. The latest mixed-draw maximum is 3.012 ms, all other
recorded traces are at or below 3.417 ms, cooperative full composition was 56
slices with a 0.504 ms maximum, and the ring-local gate was green. The overall
verdict remained red only because 4.751 ms exceeded the unchanged 4 ms
absorption guard. These receipts isolated the next boundary; they are retained
as diagnosis evidence and superseded by the final physical acceptance below.

### Final physical acceptance

The metadata continuation closes the final 4 ms failure without weakening the
guard. Gate image `0x130e50` completed cooperative full composition in 56
slices with a 0.499 ms maximum and no premature submission. The ring-local
gate submitted at most 71,240 logical pixels for a local update versus 136,896
for the full canvas, preserved the next reusable pan, and passed.

At 25%, mixed pen absorption measured a 2.118 ms maximum in enumeration and
mixed eraser measured 2.048 ms in enumeration; both held the pending backlog
to one. The dense 25% recorded trace measured 1.896 ms while staging uniform
metadata, every other trace stayed at or below 1.993 ms, and all traces reported
zero overflow and resynchronization. The complete automated verdict is
all-ones; SSAA remains the established yellow receipt. This is automated
hardware acceptance. Optical no-tear and interruption behavior remain pending
for the owner’s glass test.

## White-block glass regression and fix

The first acceptance glass pass found no tearing, but reproduced a severe
400% regression after panning into dense minimum-radius hairlines covered by a
dense eraser grid. Tiles appeared block by block, two blocks remained overview
white beyond the expected 500 ms interval, and neighboring blocks completed
visibly slowly. The owner photograph is
`$HOME/Downloads/IMG_2766.HEIC`.

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

For the final follow-up, the complete debug and release host suites passed
31/31; ASan passed 13/13. Focused
authority/absorption coverage passed 9 cases / 667 assertions under debug,
release, and ASan; focused staged-publication and rerender-damage coverage
passed 2 cases / 793 assertions under all three configurations. Product
firmware built at `0x103b70`, gate firmware at `0x130e50`, and Raster V1 at
`0xe7a70`. The release cold scorecard and 29-step hairline-pan reproduction
were exact. The final physical run returned the all-ones automated verdict
described above; SSAA remains yellow and the owner’s glass test remains pending.

## Remaining work

1. F9/F19 absorption and F28 full composition now have persistent cursors and
   automated hardware acceptance. Their final optical interruption/no-tear
   acceptance remains pending on glass.
2. F10 history generations remain architectural work; Undo/Redo can still
   rebuild derived state from authority.
3. F12 prepared geometry reuse is not justified in its tested 100 KiB form.
   Revisit only with a paint-dominant design or a demonstrably smaller budget.
4. F13 banded 25% settle remains open. The renderer can now yield within a
   window, but the 25% pass still pays independent window-level authority
   discovery. F21 cache-directory/commit complexity and F26 spatially
   prioritized repair also remain open.
5. F20 autosave encode/allocation remains main-task work.
6. F24 targeted IRAM placement was not attempted; the current evidence does
   not justify spending internal RAM without a kernel-level A/B.
7. F28 is host- and device-complete but awaits the glass interruption test. F29
   perceptual AA ordering remains open.
8. Settled AA stays yellow pending its separate optical acceptance receipt.
9. Ring locality now has a host pixel oracle and device submitted-area
   assertion. A panel framebuffer readback still does not exist, so optical
   no-tear remains a glass acceptance step.
10. Boundedness still has explicit atomic tails. A masked resident-tile row can
    charge roughly 12,676 raster work pixels in the static worst case, although
    the physical 50–400% corpora stayed below the guard. A settled spatial query
    may merge 1,071 64-bit words and emit up to 4,000 candidates in one unit;
    settled presentation follows the measured render slice as an atomic panel
    call. Cold fill also retains its measured approximately 11.2 ms producer
    boundary. These limits remain visible in telemetry and are not described as
    strict 0.5–2 ms guarantees.

## Evidence and artifact paths

- Review source: `tinydraw-v2-performance-review-20260818.md`
- Minimized hairline trace: [`HAIRLINE_PAN_TRACE_2026_08_18.md`](HAIRLINE_PAN_TRACE_2026_08_18.md)
- Earlier cold campaign: `benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md`
- Combined-corpus baseline: `benchmark-results/wave2-compositor/COLD_GENERAL_BASELINE_RECEIPT.md`
- Pan/staging evidence: `benchmark-results/wave2-compositor/STAGING_INVARIANT_RECEIPT.md`, `benchmark-results/wave2-compositor/CHROME_LIFETIME_RECEIPT.md`
- Historical performance receipts: `vector_v2/hardware-receipts/PERF_ROUND_2_BASELINES_2026_08_14.md`, `vector_v2/hardware-receipts/PAN_FLOOR_CLOSURE_2026_08_15.md`, `vector_v2/hardware-receipts/LONGSTROKE_COLDRENDER_INVESTIGATION_2026_08_14.md`

Raw serial logs and the owner photograph were intentionally not checked in;
their acceptance measurements are transcribed above.
