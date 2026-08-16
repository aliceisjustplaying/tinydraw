# Session handover — 2026-08-16 marathon (overlay → déjà-vu → AA day)

Owner is asleep; no overnight work requested. Read
[`docs/PERFORMANCE_CHRONICLE.md`](../docs/PERFORMANCE_CHRONICLE.md) for the
full speed story with receipts. Everything below is committed and pushed
through `9549e32` on `feat/v2-performance-followup`.

## What landed today (chronological, all glass-tested)

| Commits | What |
|---|---|
| `913a3b3`..`7900310` | Owner decisions recorded; 400% hold-the-line guard (now 520 ms after between-build recalibration) |
| `fb2a05b` | Pixelation diagnosis: drop hypothesis falsified; `InPlaceRetainDrops` cause counters live on LIVE_STROKE/INKTRACE/MIXED_DRAW |
| `39352d6`..`79ce37b` | **Committed overlay**: authority-only commits (19,324→173 µs), pending-range absorption, host-proven overlay exactness, deferred lift swap |
| `902d016` | Drain only on empty polls (lift tail 10–34→4–5 ms) |
| `f813a8f`..`9646969` | AA + resampling host prototypes; float-reference render answered the V1-jaggedness question (quarter-unit quantization) |
| `bc5e0e3` | **Sixteenth-world sample units** — no regressions, joint_p95 −30–40% |
| `693bcdf` | Streamline 0.4 (V2-scoped) + battery-region-only power redraw |
| `d5097b1` | **Déjà-vu fix**: all-zoom retention in idle absorption + remembered-view uniform materialization + 25 ms idle budget; revisit gate 4/9/16 missing → 0/0/0 |
| `4ea05db`, `9549e32` | **Settled AA on device** (v1 then v2: annulus sqrt, batched slices, 25% presentation settle) |

## Current machine/tree state

- Device runs the **plain app build** (`./scripts/esp32 vector-v2` — owner
  wants app-only flashes for glass; the harness build is for batteries).
- Only harness red: `overlap_cold` (owner-sequenced fix, todo #10).
- Cold 400% wall 503–518 vs the 520 ceiling — margin is thin; the icache
  dice (unpinned producer) is the cause; todo #9 is the fix.
- Host: 231 test cases green, ASan green, format clean.

## Next-session queue (owner-approved order)

1. **Overlap-50 cold** (todo #10) — the occlusion-aware replay question.
2. **SVG export** — owner explicitly raised priority tonight.
3. **IRAM-pin producer loops** (todo #9) — restores cold ceiling margin.
4. **AA speed round 2** — owner wants the settle progression
   imperceptible. Ranked ideas:
   a. Exact span-interior fill per row (the review's real design): visit
      only boundary pixels analytically, memset-fill interiors — kills the
      bbox-area scaling that makes thick strokes slowest.
   b. Per-op dirty-rect folding: fold only the op's touched bbox into the
      accumulators (currently full-tile per op — dominant for hairline
      tiles with many small ops).
   c. Reuse prepared chord batches across the ops of a tile (currently
      re-prepared per endpoint per tile).
   d. approximate_sqrt (bit-trick) in the annulus; fixed-point folding.
   e. Settle-order priority: most-recent-stroke tiles first (perceived).
   f. If still short: linker-fragment IRAM for the settle inner loop.
5. Residual déjà-vu strays (todo #15) via LIVE_LEDGER deltas; UX debt
   items #11 (pan over zoom button), #16 (color picker paint-in), #14
   (pan micro-glitch), #12 (band-sliced refreshes), #13 (raw-tip tail).

## Hard-won gotchas (today's tuition)

- **Heap order is a perf surface**: a 40 KiB PSRAM workspace placed
  mid-heap cost +9 ms on the 400% cold wall; placed dead last, nothing.
  Way-size math is not enough — order matters.
- **Idle budgets must be split from input budgets**: the 10 ms input
  retention budget silently skipped 150–208 déjà-vu retention tiles per
  XL stroke (off_skip receipts caught it).
- **The export button wedges USB**: MSC mode re-exposes the TINYDRAW
  volume after eject; only a power cycle restores the serial port. Needs
  an on-device exit affordance before any soak test that touches export.
- **clang-format version drift**: the current formatter reflows code the
  previous session's accepted; run `./scripts/dev format` before diffing
  and expect stale-anchor edit failures.
- The gate battery takes **~70 s**, not minutes; `TINYDRAW_LIVE_*`
  receipts only appear in interactive/app sessions.
- 25% AA is presentation-only BY DESIGN (overview = replay authority);
  don't "fix" it into the overview.

## Owner glass quotes (for the record)

- Déjà-vu fix: "100% very impressive. I'm extremely impressed."
- Sixteenth units: "Yes, it's much better… definitely a big improvement."
- AA v2: "I think this is better… I would like it to be less perceptible
  but it's not too bad."
