# Settled-AA edge spans — design note (2026-08-18/19 final round, lever 2)

Owner-fixed goal: bring 50–200% whole-view settle (real-document glass
429–922 ms; deterministic evil-corpus gate 84–1,067 ms in-slice) toward
the ≤500 ms class. Attribution says raster coverage math is 87–91% of
renderer time and ~26× overdraw exists per dense window
(`benchmark-results/history-latency-2026-08-18/RECEIPT.md`). The
2026-08-18 work-charge probe adds: ~60–75% of raster-walked pixels in the
dense corpus are saturated-destination skips; per-slice overhead is
~1.6 µs and not a lever.

## Where the overdraw actually is

`SettledRenderCursor::raster_chord_row` (`vector_v2/src/settled_tile.cpp`)
walks the **full chord bounding box** every row: `chord_x0_..chord_x1_`
inflated by `radius_max + 1.5` on all four sides. The immediate
rasterizer already solves this problem per row:
`conservative_tapered_row_span` (`incremental_rasterizer.cpp`) intersects
two linear half-planes (y ± radius edges are linear in the segment
parameter), applies a rounding margin plus a whole-pixel guard, and keeps
`covers_pixel` as the sole geometry authority inside the interval. The
settle pass never got this; it rediscovers coverage over whole bboxes.

## Candidate treatments considered

1. **Persisted per-tile boundary spans / 1-px annulus mask at producer
   publication** (the handover's sketch). Strongest end state (settle
   would not re-walk operations for interior pixels at all) but the most
   integration surface: ~512 B × 604 slots ≈ 302 KiB PSRAM cache with
   identity/invalidation/removal governance, COW preserved-slot
   interplay (`commit_history_revision` pre-images need their masks
   preserved or invalidated), no coverage for 25% overview windows (not
   tile slots), and settle would need raw-tile bytes as compositing base,
   coupling settle to canvas state. Deferred, not rejected: staged as the
   follow-up if analytic narrowing leaves the gate short of class.
2. **Analytic per-row span narrowing inside the settled raster**
   (chosen first stage). Zero storage, zero invalidation, zero COW
   interplay, uniform across all zooms including 25%, and it reuses the
   proven conservative-span + guard pattern from the immediate path.

## Chosen treatment (stage 1): exterior-capsule row spans

`coverage_alpha(d², r)` is exactly 0 iff `d ≥ r + 0.5`. Therefore a
conservative row span computed for the **exterior capsule** — the chord
with both radii inflated by 0.5 — contains every pixel with nonzero
alpha. Per chord: build the tapered span table once (12 floats, stored in
the caller-owned cursor); per row: compute the conservative interval,
skip empty rows outright, and run the existing per-pixel loop (including
the saturated-destination skip) only inside the interval.

Exactness argument, mirroring the accepted white-block certificate and
`conservative_tapered_row_span` precedents: excluded pixels satisfy
`d ≥ r + 0.5` by conservative construction (margins widen the interval;
the whole-pixel guard absorbs float edge arithmetic), and for such pixels
the current loop computes `alpha = 0 ≤ row[x]` and contributes nothing.
Exclusion is therefore byte-identical by cases, and the per-pixel
evaluator remains the sole authority inside the interval. The frozen
25-checksum oracle plus the sliced-equals-synchronous test gate the
implementation.

Work charge: rows charge the narrowed span width (true traversal), with a
minimum one-pixel charge for the row-span computation. This is honest
work reduction — traversal itself shrinks — distinct from the rejected
work-charge recalibration, which repriced unchanged traversal. Expected
side effect: fewer slices *and* lower in-slice totals; `max_slice_us`
must hold the ~2.3 ms class since charge tracks true traversal.

Not in stage 1 (explicitly): interior-255 bulk fill (needs a strict
tapered interior certificate; the t-clamped lerped-radius row set is not
provably an interval, which is why `paint_tapered_segment` re-tests every
pixel), and the persistence design above.

## Rejected-precedent check

Not SSAA (sampling unchanged), not adaptive bands (window shape
unchanged), not cross-tile reuse (nothing shared across tiles), not the
prepared-geometry cache (no storage), not the constant-radius micro-probe
(that was a per-pixel branch in the hot loop; this removes whole
traversal regions per row). It eliminates far-exterior rediscovery — the
same class of work the accepted no_ink white path eliminated at window
granularity — at row granularity.

## Outcome (same night)

Stage 1 is **device-accepted**: same-image per-policy A/B on the new
`TINYDRAW_GATE1_SETTLE_LONG` long-chord case measured −23.8/−44.0/−60.7/
−64.0/−62.1% at 25–400% with on-device pixel exactness, while the
short-chord evil-corpus document stayed unchanged (narrowing correctly
never activates there). Receipt:
[`settled-edge-spans-2026-08-18/RECEIPT.md`](../../benchmark-results/settled-edge-spans-2026-08-18/RECEIPT.md).
The saturated-source skip variant was a host no-go (+0.4…+1.9% totals).
The short-chord overdraw (consecutive-chord bbox overlap in dense
documents) remains the open 50–200% target; candidate successors are an
H7-style per-operation row sweep with per-pixel saturation early-out, or
the persisted-span design in §Candidate treatments. The owner's
real-document glass walls are the remaining arbiter.

## Measurement plan

1. Host: `tinydraw_vector_v2_settled_aa_benchmark` — 25 frozen checksums
   must be byte-identical; five-corpus wall A/B (bbox-dominated corpora
   long-crossing/hairline-eraser expected to move most).
2. Host suites: release/debug/ASan 31/31 + 13/13.
3. Device: `./scripts/esp32 vector-v2-gate-harness /dev/cu.usbmodem1101
   604 verify` — `TINYDRAW_GATE1_SETTLE_TIMING` totals and
   `max_slice_us`; full battery all-ones; paced-cold walls ≤500 ms
   (±2–3% icache law; adversarial 400% margin ~3–8 ms).
4. Stage 2 decision (interior fill or persistence) only after stage 1
   device numbers, judged against the ≤500 ms class at 50–200%.
