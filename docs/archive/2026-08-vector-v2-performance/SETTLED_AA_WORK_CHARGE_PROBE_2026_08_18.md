# Settled AA work-charge recalibration probe — 2026-08-18 (late night)

## Verdict

**NO-GO; production charging reverted.** Charging raster rows by actual
coverage evaluations instead of full window width halved slice counts but
moved device settle totals less than 1.5% — inside the ±2–3% dice class —
while growing the worst atomic quantum from ~2.3 ms to ~3.9 ms. The
retained artifact is attribution only: the `raster_pixels` /
`saturated_skip_pixels` stats split in `SettledTileStats`.

This was lever 1 of the final-round handover
([`HANDOVER_2026_08_18_FINAL_ROUND.md`](../../HANDOVER_2026_08_18_FINAL_ROUND.md)
§1), motivated by the observation that after the accepted
saturated-destination skip, `advance_chord_raster` still charged
`chord_x1_ - chord_x0_` per row regardless of skips.

## Treatment

`raster_chord_row` counted computed coverage evaluations and
saturated-destination skips separately; the slice budget charged
`computed + skipped/8` (skips overcharged at 1/8 pixel as a slice-time
bound). The full-width row pre-check was unchanged, preserving the
"exceed `max_work_px` by at most one row" contract. Pixels and stats are
policy-independent; the synchronous path shares the cursor code, so the
sliced-equals-synchronous oracle held by construction.

## Instrument

Same-image per-policy A/B, mirroring the history gate's
`per_publication`/`holdback` pattern: a `charge_full_rows` request flag
reproduced the historical slice boundaries exactly, and
`run_settle_timing_gate` ran every zoom twice in one boot — pass 0
`charge_full` (render only), pass 1 `charge_actual` (render + publish).
Identical corpus, tiles, boot, and icache layout; this eliminates the
±2–3% cross-build layout dice entirely. Gate: 604-slot battery on head
`41c7db4` + treatment, `/dev/cu.usbmodem1101`, all-ones verdict at
23:23:32 with `settle_timing=1` (raw log `/tmp/gate-verify.log`,
transcribed below).

## Measurements (evil-hairline settle corpus, 512-px quantum)

| Zoom | slices full→actual | total_us full→actual | Δ total | max_slice_us full→actual |
|---:|---:|---:|---:|---:|
| 25 | 419→180 | 72,781→71,874 | −1.2% | 330→1,822 |
| 50 | 521→181 | 85,330→84,009 | −1.5% | 333→2,148 |
| 100 | 966→390 | 173,336→171,757 | −0.9% | 1,326→3,880 |
| 200 | 2,445→1,083 | 400,720→398,257 | −0.6% | 2,657→2,865 |
| 400 | 7,270→3,459 | 1,072,845→1,066,576 | −0.6% | 3,279→3,643 |

Zero failures in both policies at every zoom. Same-log paced cold stayed
green (adversarial 400% wall 493.860 ms ≤ 500; overlap 50% 482.528 ms).
Host: 31/31 release, 31/31 debug, 13/13 ASan; the 25 frozen RGB565
benchmark checksums are byte-identical under both policies.

## Attribution

1. **Per-slice overhead is ~1.6 µs, not a lever.** At 400%, 3,811 fewer
   slices saved 6,269 µs. The gate loop's per-slice cost (continuation
   bind/fingerprint revalidation plus two timer reads) is µs-class; the
   product loop's per-quantum cost (touch-urgency check, canvas lookup,
   timer reads) is the same class, and its outer loop is already
   time-bounded (`kSettleSliceBudgetUs = 8 ms`), so quantum count does not
   affect main-loop cadence at all. The ceiling on any product-side win
   was therefore ~1–2%.
2. **The cost is a real preemption regression.** A quantum is atomic; at
   the production 512-px budget the worst quantum grew 1,326→3,880 µs at
   100%. Packing more computed pixels per quantum trades input-poll
   granularity during settle for nothing measurable.
3. **Skip fraction sizes the real lever.** Charge ratios imply ~60–75% of
   raster-window pixels in the dense corpus are saturated skips
   (charge ratio r = c + (1−c)/8 → computed fraction c = (8r−1)/7 =
   0.26–0.48 across zooms). Their coverage math is already elided; their
   *traversal* rides inside slice time and is untouched by charging. The
   structural fix remains edge-span recording (handover lever 2), which
   eliminates both the skip traversal and the interior coverage
   rediscovery.

## Disposition

- Production charging: reverted to full row width (`settled_tile.cpp`,
  comment records the no-go).
- Gate harness: restored to the single-pass `TINYDRAW_GATE1_SETTLE_TIMING`
  format; the per-policy method is preserved by this receipt.
- Retained: `saturated_skip_pixels` stat, benchmark aggregation, and the
  sliced-stats equality check — zero-cost attribution for the edge-span
  round.
