# Committed overlay — landing receipts (2026-08-16)

Design: [`VECTOR_V2_COMMITTED_OVERLAY_DESIGN.md`](../../VECTOR_V2_COMMITTED_OVERLAY_DESIGN.md).
Machine: ESP32-S3 `/dev/cu.usbmodem101`, gate harness, frozen corpus.

## Step 1 — §8.4 phase split (visible/offscreen raw retention)

Logs: `step1-phase-split-build1.log`, `step1-phase-split-build2.log`.
Verdict vector identical to the pre-change battery. `ph_raw` (visible
painting, budget-exempt) measured 12.3–12.4 ms; `ph_offscreen` 5–17 µs;
`off_skip=0` on all strokes.

## Step 2 — revision pair + pending range (host)

Host doctest "deferred absorption equals synchronous in-place appends":
564 assertions. Authority-only appends plus any drain cadence (1–3 op
trailing) end bit-identical to synchronous appends; every mid-drain state
equals ground-truth replay of the absorbed prefix; composed trailing canvas
plus `overlay_pending_operations` equals full-log ground truth at every
trailing depth; windows untouched by pending ink stay byte-identical; the
synchronous entry point refuses while the canvas trails.

## Steps 3–5 — overlay staging, deferred commits, drains (device)

Logs: `step345-overlay-run1.log`, `step345-overlay-run2.log` (two
reset-separated captures, identical verdict vectors).

**`mixed_draw` is GREEN for the first time in project history.**

| zoom | append_max before (sync commit) | append_max after (authority-only) | drain_max (background) |
|---|---|---|---|
| 25  | 13.2–13.4 ms | **124–133 µs** | 13.1 ms |
| 50  | 18.3–19.4 ms | **155 µs** | 17.6–19.0 ms |
| 100 | 15.4–16.1 ms | **172–173 µs** | 14.8–15.4 ms |
| 200 | 14.5–15.4 ms | **154–155 µs** | 14.0–14.8 ms |
| 400 | 13.5–14.5 ms | **138–162 µs** | 13.0–13.9 ms |

Worst input-path append across both runs: **173 µs vs the 15 ms budget**
(was 19,324 µs) — a 111× reduction. The retention work did not disappear;
it moved off the input path into receipted drain absorptions
(`drain_ops=50` per stroke, ≤19 ms each, between polls). Correctness
guards on every run:

- `visible_fallback=0` and all drop counters zero on all 10 strokes.
- Five `TINYDRAW_INKTRACE pass=1`, `commit_failures=0`, e2d p95
  2.3–5.2 ms (baseline range), fb probes flat (30/30/24/24) — the
  trailing canvas never dropped a sharp tile.
- Cache-tour ledger `amplification=1.000 stale=0 unexplained=0`.
- Cold walls inside their lines (400%: 505.0 / 512.5 ms vs the 520
  hold-the-line ceiling); producer path untouched.
- Verdict vector: every gate green except the owner-sequenced
  `overlap_cold` (ship contract decision #5).

## Owner glass session (2026-08-16 21:23–21:29, `glass-session-overlay-2026-08-16.log`)

Content: evil hairlines at 25%, heavy overdraw at 50% ("20–30 overlapping
strokes"), dense drawing at 100% and 400%, hairline figure-circles — one
122-chunk mega-stroke. Owner verdict: 400% overlapping drawing "much
better", "it does look fast".

- **Input path confirmed on glass:** `append_max` 93–152 µs on 13 of 14
  strokes. The 122-chunk stroke hit the 24-op high-water exactly as
  designed: one synchronous absorption, 13.4 ms — still under the 15 ms
  alarm (§3.5 degradation working).
- **Lift hitch: 87–199 ms → 10–34 ms** (`detected_to_poll_start_us`,
  old glass session vs this one). The remaining >20 ms tails are fully
  attributed: the first drain absorption ran inside the lift iteration
  (`unattributed_tail_us=29301` worst; dense-content absorptions reach
  ~30 ms). Fixed post-session: the drain now runs only on iterations
  whose input poll came back empty (`step345-overlay-run3-drain-gate.log`
  battery reproduces the verdict). Expected lift tail after the fix:
  ~5 ms; verify at the next glass session.
- **Drain health:** 14 drains, zero failures, every swap refresh passed;
  worst absorption 30.4 ms, worst swap 79.5 ms (idle-time work).
- **No correctness events:** all drop counters zero on every stroke,
  `complete_over_33ms=0` everywhere, zero commit failures.
- **The 166–184 ms `poll_max` gaps are pre-existing** — the pre-overlay
  glass session shows identical 173–183 ms values (standing
  zoom/full-refresh cost, not an overlay regression).
- **Known consequences, owner-deprioritized:** minimap updates trail
  drawing by the drain latency (minimap base is keyed on the canvas
  revision, which now trails during bursts; owner suspects it was always
  laggy — below déjà-vu and inking in priority). New UX bug logged: a pan
  starting over the zoom button is swallowed entirely (todo #11).
