# Cold compute campaign — wave 3

Date: 2026-08-16

Baseline authority: `a560d20` (frozen combined corpus, curved committed ink)

Device: ESP32-S3, 240 MHz, 8 MiB PSRAM, effective 40 MHz panel clock

Corpus: frozen `adversarial_tapered_4x+evil_hairlines` (910 operations,
12,157 samples), 400% closure statistic at origin `(0,0)`, all zooms
reported. Every run is a full gate-harness boot with touch service and all
existing product services; exactness is producer-vs-forward equality at all
four zooms plus the collinear (4,000 cases) and mixed-document (150 cases)
fuzz suites and the 29-test host battery (debug, release, asan).

## Result

| Cold wall ms | 50% | 100% | 200% | 400% |
|---|---:|---:|---:|---:|
| HEAD baseline (`gate-baseline-head.log`) | 712.9 | 764.9 | 991.3 | 1,243.0 |
| 1. stateless windowed span search | 669.0 | 717.8 | 914.0 | 923.9 |
| 2. internal-SRAM producer scratch | 643.7 | 693.0 | 889.8 | 905.9 |
| 3. prepared curve units | 533.6 | 611.9 | 799.0 | 786.1 |
| 4. unit-merged masked row sweep | 517.0 | 551.9 | 693.6 | 683.0 |
| 5. caller-split painters + warm append sweep | 514.1 | 546.7 | 678.0 | **668.0** |

Final three reset-separated runs at 400%: 667.978 / 668.980 / 668.977 ms
wall (compute 581.9 / 582.9 / 581.0 ms). Maximum **668.980 ms** against the
1,269.157 ms baseline maximum: **-47.3%**. Presentation is ~67 ms, pacing
~19 ms, touch ~0.9 ms. The ≤500 ms product gate remains open by ~169 ms;
compute must fall from ~582 ms to ~410 ms.

The 20-run closure statistic was not run; this is a development
characterization on the same protocol as the baseline receipt.

## Sibling gates

Every previously passing gate stayed green and most improved; the gate
cascade now reaches gates unreachable since the corpus freeze:
`hard_100` (660.4 → 431.3 ms, passing again), `hard_400`, pan, cache tour,
idle repair, long gesture, and export all pass. Hairline capacity improved
to 367.0 ms at 100% and 271.0 ms at 400%. Interaction ticks fell from
~10 ms to ~8.5 ms maximum.

## Accepted changes

1. **Stateless windowed span search** (masked constant-radius replay). The
   warm-start search reset after every prefinalized row, so ~20K rows paid
   full-width probes: 2.51M `covers_pixel` calls at 400%, previously
   uncounted (the census's own comment admitted the blind spot). Each row
   now intersects a conservative chord seed with the row's unfinalized mask
   window; both bound the true chord one-sidedly, so monotone probes cannot
   miss coverage and `covers_pixel` stays sole geometry authority. Search
   calls fell 39x (3.72M → 96K host census). Two seed tiers cover the
   degenerate shapes: world-margin-clamped fat strokes replay as
   zero-length segments (pure circles, where the parallelogram bound
   degenerates to the bounding box) and use the exact one-sqrt circle
   chord; segments shorter than their radius use the midpoint circle.
2. **Device-sympathetic arithmetic.** The Xtensa toolchain emits library
   calls for float division, floor, ceil, and sqrt (verified in
   disassembly: 4 `callx8` per row in the conservative span). Crossings
   hoist into two per-segment reciprocals, floor/ceil use native
   `trunc.s`-based helpers (bit-identical), the seed sqrt is a padded
   rsqrt bit hack (probe-verified, never authoritative), and committed
   zoom scales are exact binary constants. This step alone flipped the
   change from +9%/-15% (regression at low zoom, win at 400%) to a win at
   every zoom.
3. **Internal-SRAM producer scratch.** The 32 KiB supertask and 8 KiB
   packed tile moved from PSRAM to internal SRAM with fallback; 319 KiB
   internal remains free. Confirms the round-display workshop lesson that
   memory placement beats compute once the algorithm is sane.
4. **Prepared curve units.** Step counting, step bounds, and step apply
   each re-derived the quadratic subdivision (three reciprocal-length
   divisions per call) — paid even for the 30K bbox-rejected steps at
   400%. Units now prepare once per endpoint (~110-120 ms at every zoom).
5. **Unit-merged masked row sweep.** A unit's chords overlap at shared
   joints; painting them in one row sweep pays one window scan per row
   instead of one per chord. Exact by the union argument: chords of a unit
   share one color, so under the finalized mask the written set is
   identical in any order.
6. **Caller-split painters.** The windowed machinery serves only the cold
   producer; interactive appends keep the historical warm-start search
   (fresh append masks never break warm chains) plus the warm variant of
   the unit sweep.

## Rejected experiments (measured, reverted)

- **Summary-bitmap row saturation probe**: -2.2% at 400% but +4.0% at 50%;
  reconfirms the original code comment's partial-saturation tax.
- **Word-based mask window scan**: +7 to +13% on device despite host
  neutrality; GCC-Xtensa refuses to inline `memcpy` word loads without
  alignment proof, so every load was a `callx8` (disassembly-verified).
  Byte loads (`l8ui`) are the right grain on this ISA.
- **6x2-tile band replay unit**: host proxy showed 2.7x regression at
  400%. Wider surface rows almost never saturate, which un-buries exactly
  the expensive tapered content that newest-first saturation was skipping.
  A future band experiment needs block-granular saturation first.
- **Hybrid warm/seeded search shared by all callers**: lost on both fronts
  (cold +5%, appends worse); replaced by the caller split.

## Cycle-bucket attribution (census firmware, `gate-census-attribution.log`)

At 400% before steps 4-5: gate 20.6 ms, painted-step setup 43.5 ms, paint
489.0 ms, publish 39.5 ms, residue ~256 ms (rejected-step setup and batch
loop). Paint remains the dominant bucket; its cost is row visits
(~370K/fill before unit merging), each paying a window scan, a seed
evaluation, and probes.

## Known open issue: mixed_draw appends at 50%

`mixed_draw` fails at 50% with 18.8 ms max append against the 15 ms
budget. Evidence says this predates the campaign: the last green mixed
receipt (12.7 ms max, `gate-invariant-final.log`) was recorded before
`19ebbe3` moved committed ink onto the curved path (2-3 chords per
endpoint on the append path), and no later boot reached the gate because
the cascade stopped at the failing cold gates until this campaign fixed
them. At 25%, where no tile paints in place, the append already averages
11.4 ms (overview replay plus commit bookkeeping), leaving ~3.6 ms of
headroom for tile painting at higher zooms. Needs its own work item; the
warm unit sweep (step 5) helps but cannot absorb the curved-path doubling
alone.

## Remaining road toward ≤500 ms

Compute is ~582 ms. Ranked candidates: row-visit reduction via op-level
active-chord sweeps (the unit merge captured pairwise overlap only),
publish-path direct tile copies, PIE 128-bit fixed-point coverage probing
(only pays now that scratch is internal), and presentation/compute
overlap (reopens pan optical gates per the dependency matrix).
