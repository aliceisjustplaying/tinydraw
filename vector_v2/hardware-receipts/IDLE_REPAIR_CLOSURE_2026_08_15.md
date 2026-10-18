# Idle cache repair closure — 2026-08-15

The 2026-08-14 manual glass session's strongest complaint: panning at 100%
kept hitting visible cold refinement every 10–15 seconds
(`b76b992-manual-glass.log`: ~one `TINYDRAW_FILL_BASELINE` cycle per pan
batch). Closed at `24a9fe9`.

## Root cause

Two accepted designs compounding:

1. The drawing-latency bargain (`1848cc6`): strokes drop affected tiles at
   non-active zooms instead of painting them, keeping chunk commits under
   15 ms.
2. Strictly view-driven refinement: dropped tiles were re-produced only when
   the user panned onto them — on screen, as visible cold fills.

## Fix

Idle repair is the other half of the bargain. When input is quiet and the
visible fill is complete, the product loop walks a `plan_idle_repair`
schedule (host-tested, `vector_v2/src/idle_repair.cpp`):

1. The active view's cardinal neighbors at the active zoom (one viewport
   step, clamped, deduplicated) — the next pan lands sharp.
2. The remembered view at every other tiled zoom — zoom returns land sharp.
3. At 100% only: the full 4×4 level viewport grid — the 100% world's raw
   tiles fit the 384-slot pool, so edge panning at 100% never cold-renders
   after a quiet moment. Larger levels stay neighborhood-only (a full sweep
   would churn the pool).

Idle-repair publications never present; each tick is bounded by the same
slice deadline as the visible fill, so input latency is untouched. The plan
resets on any view or revision change. Completion logs
`TINYDRAW_LIVE_REPAIR views=N steps=M` in product telemetry.

## Gate

`TINYDRAW_GATE1_IDLE_REPAIR` (battery, part of the final verdict) encodes
the glass scenario deterministically: warm 100%, sweep an XL 25% pen stroke
across the world, return to 100%, refill the visible view, run the plan.

Result at `24a9fe9` (384 slots, `24a9fe9-full-gate-384.log`):

- damaged=588 of 672 identities cold after the stroke (scenario reproduced)
- remaining=0 after repair (whole-level zero fallback: any pan destination
  composes without cold work)
- repair wall 3.65 s of pure idle; worst single producer slice 7.5 ms,
  under the 15 ms input-poll alarm
- all 25 battery gates green; `TINYDRAW_GATE1_PANSEQ` unchanged
  (27.9 ms avg / p50 25.96 / p95 32.95)

## Residuals

- 200%/400% repair covers the neighborhood and remembered views, not the
  full level (does not fit the pool). Fast continuous panning at 400% can
  still outrun repair; pauses heal it.
- The product-loop repair branch mirrors the gate's primitives but its
  scheduling (idle detection, tick pacing) is validated on glass, not by
  the gate; watch for `TINYDRAW_LIVE_REPAIR` lines in the next manual log.
