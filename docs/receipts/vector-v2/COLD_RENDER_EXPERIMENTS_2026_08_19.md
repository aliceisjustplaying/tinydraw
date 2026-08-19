# Cold-render performance experiments — 2026-08-19

Budget: at most five distinct measured hypotheses. Three used. Baseline does not
consume an attempt.

## Baseline

The current full device battery is green. General cold fill at
50/100/200/400% is approximately 397/389/464/499 ms in the latest run; the
binding 400% case has a 520 ms development guard and a 500 ms release target.
The owner torture document completes in 119/135/199/349 ms.

## Attempts

1. **2x4 vertical supertasks — rejected and reverted.** Doubling the 2x2
   producer group would share geometry setup across more adjacent tiles and
   halve group discovery. It also doubled the internal raster surface from
   32 to 64 KiB. On the complete gate image that exhausted the required
   contiguous internal-memory layout: `presenter=0`, free internal 144,892
   bytes at bootstrap. The harness failed closed before any benchmark, so no
   timing claim is made. The 2x2 product shape is restored.
2. **1x2 narrow supertasks — rejected and reverted.** Halving group width was
   intended to saturate dense masks earlier and reduce each spatial candidate
   set. It duplicated enough operation setup across the viewport that the
   first 100% stress fill starved CPU0's idle task for five seconds and tripped
   the task watchdog before a timing result. The captured backtrace and
   `failure_marker=True` make this an unconditional rejection.
3. **22k honest-work replay slices — accepted.** Raising the exact masked
   sweep allowance from 16k to 22k reduces resumptions without changing a
   supertask, publication, or pixel. Device step counts fell 90→66 on overlap
   50% and 302→237 on general 400%; maximum ticks remained 10.62 ms. Overlap
   walls improved 5.1 ms at 50% and 6.0 ms at 400%; general 100/200% improved
   2.2/4.3 ms. General 400% and owner-document timings were flat. The full
   battery remained green with `failure_marker=False`.

Two experiments remain.
