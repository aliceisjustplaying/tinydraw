# External review round closure — 2026-08-15

Two independent external code reviews (26 distinct findings, from P1
correctness to nits) were landed in four parallel lanes and re-baselined.
Closed at `c86f3ac` with every gate green and one documented perf
regression carried to the next round.

## What the reviews caught (highlights)

- **Beam-race tear holes (both reviews' #1)**: the wrap band started at
  row 0 with no margin; the first band's margin clamped to zero near the
  top; TE waits failed open and unsynchronized frames stayed reusable.
  Now: every band start requires the full 48-row estimated beam lead,
  waits are deadline-based, and any degraded tear state falls back to a
  full refresh — cached pan frames are synchronized by construction
  (`tear_sync_failures=0` is now a hard PANSEQ condition).
- **Overlay x-splits broke row-major order** (the seam-tear family): pan
  now draws overlays into the ring per-overlay and pushes full-width
  row-major strips only, restoring the canvas after staging.
- **Same-color uniforms were dropped at other zooms** on every stroke
  despite the documented contract; **raw publications could downgrade an
  exact uniform**; both fixed with host tests.
- **Strict-aliasing UB in staging**; now shared, aliasing-legal, and
  host-golden-tested including the ring wrap pair and strip seams.
- **The battery could pass while red**: pan gates and tear discipline were
  absent from the verdict, the tearing probe never exercised cached pan,
  the export gate read 24 bytes, watchdog warnings were non-fatal, and
  `scripts/esp32` flashing was indistinguishable from passing. All fixed;
  `scripts/esp32 vector-v2-gate-harness PORT SLOTS verify` now captures,
  parses the verdict, and fails on watchdog/crash output.
- **The export CPU0 watchdog warning** (long-standing) became fatal under
  the new rules and is fixed at the source (encode yields every 32 rows).
- Plus: dock hit-testing off the last visible rows, ghost preview ink on
  rejection, idle-repair failure replan, no-op pan accounting, pan delta
  sign bias, PNG chunk CRC verification, stack parity, rollover-safe
  counters, TE staleness tracking, overflow-safe region subtraction,
  cross-zoom damage metrics, and comment rot.

## The verify gate earned its keep immediately

The first post-fix battery failed its own new rules three ways: the
aliasing-safe staging rewrite was 5x slower on Xtensa (4-byte memcpy
calls do not inline — reverted to scalar element swaps), the new
zero-fallback condition wrongly counted off-view drops (now
`visible_fallback_tiles`), and the export watchdog fired fatally (now
fixed). This is exactly the failure-mode coverage the reviews demanded.

## New baselines at c86f3ac (448 slots, clean commit, verify mode)

- All 27 battery gates green; zero tear-sync failures; zero watchdogs;
  every pan frame reused. `c86f3ac-full-gate-448.log`.
- Host: 76,624 assertions (was 69,036), asan/release/tidy/cppcheck clean.
- **PANSEQ: 41.5/42.0 ms avg, p50 40.2, p95 47.5/48.6** — a documented
  regression from 28.9 ms, entirely from correctness work (per-frame
  overlay prep ~15 ms + scalar staging +2.3 ms). Measured split and
  recovery plan in the board todo; the 30 FPS floor is temporarily
  broken (~24 FPS) in exchange for tear-free-by-construction pans.
- Cold p95 20-run distribution (`c86f3ac-cold-p95-20-runs.log`),
  unchanged from the pre-review baselines (cold paths untouched):
  adversarial 50/100/200/400%: 164 / 226 / 520 / 628 ms p95;
  overlap 471 / 326 / 325 / 314 ms; seed7 400%: 369 ms.
- Hairline gate: unchanged capacity truths (guard stops at saturation,
  no churn by identity signature).

## Residuals

- Pan prep cost recovery (next round, bench-first; board todo).
- Edge white notches: still open; the new staging host model and
  edge-ink gate are the bisect tools.
- Glass re-verdict on the new tear discipline still pending (the fixes
  have not been under Sarah's fingers yet).
