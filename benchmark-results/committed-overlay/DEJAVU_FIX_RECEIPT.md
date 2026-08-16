# Déjà-vu fix — receipt (2026-08-16)

Owner directive: "stop when you have a fix for deja vu." Delivered in one
batch with the live ledger instrumentation and the battery-region refresh.

## Mechanism

The overlay/revision split made cross-zoom retention affordable: absorption
runs in idle slices, so the synchronous-cost argument that justified
dropping affected tiles at non-active zooms is dead. Idle absorption now:

1. **Retains resident raw tiles at every zoom** (paints them in place —
   no slot cost; `retain_all_zooms` scope in `run_in_place_phases`,
   absorption-only; the input-path fallback keeps priority-only).
2. **Materializes revisit-bound uniforms at other zooms**: affected fresh
   paper tiles inside each remembered view (`recent_views()` — exactly the
   viewports a zoom return lands on) materialize and paint during idle,
   via the new `append_recent_view_uniform_keys` enumeration seam.
3. **Idle absorb budget 25 ms** (`kIdleAbsorbBudgetUs`): the 10 ms
   input-path budget skipped 150–208 retention tiles per XL stroke
   (`off_skip` receipts in `/tmp/dejavu-3` iteration); 25 ms bounds the
   idle poll gap while letting the revisit-bound population retain.
   Skips still fall back to the old lazy-repair behavior.

## Receipts (device battery, `dejavu-fix-battery-1.log`)

The mixed_draw revisit gate — draw at every zoom over a fully warm
multi-zoom cache, then revisit each zoom:

| zoom | missing before | missing after | refill before | refill after |
|---|---|---|---|---|
| 50 | 4 | **0** | 188.0 ms | **0.38 ms** |
| 100 | 9 | **0** | 319.8 ms | **0.38 ms** |
| 200 | 16 | **0** | 326.3 ms | **0.37 ms** |
| 400 | 0 | 0 | 0.3 ms | 0.32 ms |

Every guard held: verdict vector identical (green except owner-sequenced
overlap_cold), mixed_draw worst append 176 µs, five INKTRACE pass=1,
cache-tour ledger amplification=1.000 stale=0 unexplained=0, cold 400%
wall 503.0 ms inside the 520 ceiling, host 230/230 + ASan.

Host proofs (doctest): "deferred absorption retains resident raw tiles at
every zoom" and "deferred absorption materializes revisit-bound uniforms
at other zooms" — both compare retained tile pixels bit-exactly against
ground-truth full-log replay.

## Also in this batch

- **Battery-region refresh** (owner question): a battery change now
  re-presents only `chrome_battery_region()` (124×44) instead of the full
  frame (was 60–140 ms; the measured "lift spike").
- **Live ledger instrumentation** (`TINYDRAW_LIVE_LEDGER site=zoom|
  pan_end|drain`): per-transition cause deltas (cold/damage/evict/stale/
  unexplained) + cumulative amplification, printed during glass sessions.

## Residual déjà-vu causes (for the next glass session's histograms)

- Slot eviction under capacity pressure (the ledger `evict` column now
  attributes it live).
- XL strokes over huge fresh-paper areas can still skip some off-view
  retention within the 25 ms budget (`off_skip` receipts) — idle repair
  rebuilds those before a revisit in typical use.
- Cross-zoom damage from the synchronous high-water fallback path (rare:
  only when the pending range hits 24 mid-stroke).
