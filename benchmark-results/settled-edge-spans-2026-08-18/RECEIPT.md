# Settled-AA exterior-capsule row narrowing — device accepted, 2026-08-18 (late night)

## Scope

Final-round AA lever 2, stage 1 (design:
[`VECTOR_V2_SETTLED_EDGE_SPANS.md`](../../docs/design/VECTOR_V2_SETTLED_EDGE_SPANS.md)).
The settled raster walked every chord's **full bounding box** per row
(bbox inflated by `radius_max + 1.5` on all sides) while the immediate
rasterizer has narrowed rows conservatively for months. This treatment
ports the conservative row-span pattern to settled AA: per chord, an
exterior-capsule span table (radii + 0.5, since `coverage_alpha` is
exactly 0 at `d ≥ r + 0.5`); per row, a conservative interval outside
which pixels provably contribute nothing. The per-pixel evaluator remains
the sole coverage authority inside the interval; margins plus a
whole-pixel guard mirror `conservative_tapered_row_span`.

A cost-model discriminator gates the solve to chords with ≥16 px of
expected exterior per row (`bbox width − 2·radius_max − 3`): a slanted
chord's exterior is ≈ its |dx|, so fat short chords (almost no exterior,
often saturated-skip cheap) keep the raw bbox walk. Both paths are exact.
Rows charge the traversed span width (real work reduction — distinct from
the rejected same-day work-charge recalibration, which repriced unchanged
traversal); empty rows charge one pixel.

## Devices and instruments

ESP32-S3, 604-slot gate build, `/dev/cu.usbmodem1101`. Owner's rule
applied: host numbers do not accept anything; the receipt instrument is a
**same-image per-policy device A/B** (`disable_row_narrowing` request
flag, one boot, one icache layout), because the standing evil-hairline
settle document contains only densely-sampled short chords — the
narrowing never activates on it (measured: slices 7,268 vs baseline
7,270; totals within +0.1–0.4%). The new `TINYDRAW_GATE1_SETTLE_LONG`
case supplies the long-chord class: a local 28-operation two-sample
straight-crossing document (hairline 0.6 / 2.5 / 4.25 world-px radii,
24 pens + 4 erasers, crossing world center), 3×3 windows per zoom,
rendered under both policies with an on-device FNV cross-check. The
shared gate document is untouched. Raw lines: `settle-long-ab.log`.

## Device results (same image, 23:57 battery, all-ones verdict)

| Zoom | slices full→narrowed | total_us full→narrowed | Δ total | max_slice_us | exact |
|---:|---:|---:|---:|---:|---:|
| 25 | 257→205 | 20,793→15,842 | −23.8% | 264→288 | 1 |
| 50 | 510→271 | 52,694→29,503 | −44.0% | 290→434 | 1 |
| 100 | 1,222→385 | 132,662→52,126 | −60.7% | 282→623 | 1 |
| 200 | 1,816→507 | 181,814→65,518 | −64.0% | 285→635 | 1 |
| 400 | 1,882→616 | 180,661→68,394 | −62.1% | 247→623 | 1 |

Zero failures; checksums identical between policies at every zoom. Worst
slice 635 µs, inside the ~2.3 ms class. Same-image evil-corpus settle
document: unchanged (1,076,563 µs at 400% vs 1,072,845 baseline, +0.35%
dice). Adversarial 400% paced cold: 496.787 ms ≤ 500. Full battery
all-ones with `settle_timing=1`.

The Xtensa-specific risk was checked explicitly: the span table costs two
`__divsf3` calls per chord (the documented device trap that killed other
host-only wins). On long chords they amortize over many rows; the
discriminator keeps them off short chords. Host A/B (5 alternating
process pairs) had shown −50% five-corpus totals driven by long-chord
corpora (−37…−75%) with heavy corpora inside the ±2% guard; the device
receipt above, not the host numbers, is the acceptance basis. Host
retains the exactness oracle: 25 frozen RGB565 checksums byte-identical,
31/31 release, 31/31 debug, 13/13 ASan, sliced-equals-synchronous stats.

## What this does and does not claim

- Device-proven: −24…−64% settled-AA compute on the long-chord class
  (fast/straight strokes — at a 12–14 ms touch cadence, fast travel at
  200–400% produces exactly these chords).
- Not claimed: movement of the dense short-chord evil-corpus document
  (measured unchanged) or of the owner's real-document 50–200% full-pass
  walls (690–922 ms). The owner's product glass session on the real
  document remains the arbiter for those; the short-chord overdraw
  (consecutive-chord bbox overlap) is the remaining structural target —
  candidates are an H7-style per-operation row sweep with per-pixel
  saturation early-out, or the persisted-span design staged in the design
  note.

## Same-night no-gos (host-rejected before device time)

- **Saturated-source skip** (`row[x] == 255` pre-check mirroring the
  accepted destination skip): five-corpus totals +0.4…+1.9%, no corpus
  improved beyond noise; the destination skip already absorbs interior
  repeats once the newest operation saturates. Reverted; joins the
  constant-radius hot-loop-branch precedent.
- **Plain bbox-width discriminator** (≥16 px row width): left dense
  200/400% regressing 7–10% host; replaced by the exterior-width
  discriminator above (dense within ±4%, noise).

## Memory and product surface

Cursor grows by one 10-float span table plus two bools (caller-owned,
no heap, no PSRAM, no continuation-receipt budget change). The
`disable_row_narrowing` request flag is the retained standing A/B
instrument, product default false. `SettledTileStats` gained
`saturated_skip_pixels` earlier the same night (attribution only).

## Operational note (paid for again tonight)

`CompactOperationSample` coordinates are **sixteenths** of a world pixel
(`kSampleUnitsPerWorldUnit = 16`); the `_quarter` field name is
historical. The first long-chord run rendered blank windows because of a
×4 conversion — caught by the tiles/slices/checksum telemetry before any
conclusion was drawn.

## Addendum (00:17, same night): saturation aggregation — dense-document treatment

Owner-directed follow-up targeting dense documents. The work-charge probe
attributed 60–75% of raster-walked pixels to saturated-destination skips
that still cost traversal, plus curve prep and row setup for wholly
buried operations. Treatment: per-row saturated-pixel counts maintained
on the existing saturation transition in `composite_pixels`, gated by a
`fully_saturated_rows_` counter; a fully saturated destination row is
skipped without traversal, and an intersecting operation whose whole
conservative window row-range is saturated is skipped before curve
preparation. Exact by the accepted per-pixel skip's own certificate
(saturation is monotone; a saturated destination receives nothing).

Device, standing dense evil-corpus settle document, same corpus vs the
three same-night pre-treatment builds (72,781–73,880 / 85,330–86,287 /
173,336–175,112 / 400,720–403,649 / 1,072,845–1,076,781 µs at 25–400%):

| Zoom | slices → | total_us → | Δ |
|---:|---:|---:|---:|
| 25 | 419→419 | 75,204 | +1.8…+3.3% |
| 50 | 521→521 | 87,864 | +1.8…+3.0% |
| 100 | 966→966 | 177,424 | +1.3…+2.4% |
| 200 | 2,445→2,171 | 386,285 | **−3.6…−4.3%** |
| 400 | 7,268→5,289 | 959,253 | **−10.6…−10.9%** |

Battery all-ones; 25 frozen host checksums byte-identical; 31/31+31/31+
13/13; `SETTLE_LONG` cross-check exact (FNV also identical across
builds). Host pairs: dense 400 −22.3%, dense 200 −9.2%, hairline-eraser
400 −12.6%.

Honest assessment: the 25–100% deltas (+1.3–3.3%) sit at the edge of the
±2–3% cross-build icache dice band and are not clearly attributable;
absolute cost is ≤2.9 ms against −114 ms at 400%. The gate's 50–100%
lines are one window with never-saturating ink-free margins — the
unfavorable shape for full-row aggregation; real dense documents (zero
ink-free windows) saturate rows the way the gate's 400% case does. An
intermediate build without the `fully_saturated_rows_` gate measured
+2.6…+4.8% at 25–100%; the gate reduced that overhead by roughly half.
Owner glass on the real dense document remains the decisive test.
