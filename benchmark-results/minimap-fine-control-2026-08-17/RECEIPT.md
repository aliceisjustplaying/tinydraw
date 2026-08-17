# Fine 400% minimap control receipt — 2026-08-17

## Owner verdict and scope

The owner accepted current ink as broadly usable and asked that ink remain
unchanged. At 400%, circles, hairlines, long strokes, and thick strokes felt
reasonably fast; mild diagonal XL-brush lag remains a documented lower-priority
optimization. This change touches only minimap mapping, minimap tests/gates,
and formatting violations already present in the responsive-ink call sites.

The owner rejected the current 400% minimap on glass: it was too coarse, the
viewport was hard or apparently impossible to acquire, and returning to a small
central drawing was unreliable.

## Deterministic red repro

`host-focused-red.log` records a focused host test failing all four assertions
against the shipped mapping. The fixture uses the physical 400% geometry:
5,888×7,168 level pixels represented by an 80×98 minimap.

Two independent problems were reproduced:

1. The visible viewport is about 5×5 panel pixels, but the shipped code expanded
   it to an invisible 44×44 preserve-offset zone. Starts at `(287,273)` and
   `(300,290)` looked outside the visible box yet remained relative drags, so
   they produced only `(146,146)` movement instead of acquiring the viewport.
2. A 2 px finger delta projected to roughly 146 level pixels at 400%, about 40%
   of the 368 px viewport width. That made precise return navigation coarse.

The prior test encoded the hidden-zone behavior as expected, so the old green
classifier could not catch the owner's symptom.

## Fix

`chrome_minimap_drag_origin` now has two explicit, visible modes:

- only a start inside the truthful visible viewport preserves its grab offset;
- every other start in the minimap directly acquires the viewport beneath the
  finger.

After acquisition, zooms above 100% scale drag deltas back to the 100% map
scale. At 400%, a 2 px finger delta is now 36 level pixels rather than 146.
Direct acquisition still provides full-world travel, while subsequent movement
provides fine adjustment. The rendered viewport remains geometrically truthful;
no larger visual box or canvas occlusion was added.

The gate now checks both a point inside the removed hidden zone and a far-center
acquisition. Header comments describe the new contract.

## Validation

- Focused test: four failures before the fix (`host-focused-red.log`), four
  passes after it (`host-focused-green.log`).
- Host release: 29/29 CTest targets pass (`host-release.log`).
- ASan/UBSan: 11/11 CTest targets pass (`host-asan.log`).
- Formatting: project format check passes (`format-check.log`). The formatter
  changed only layout in three pre-existing responsive-ink call sites; no ink
  expression or behavior changed.
- Physical classifier: `hit=1`, `intent=1`, old-zone acquisition
  `(1914,2192)`, center acquisition `(2796,3434)`, `fine_400_px=36`, and
  `pass=1` (`device-gate.log:92`).
- Complete physical verdict retains every requested feature gate at 1. The
  standing unrelated `overlap_cold=0` remains visible (`device-gate.log:245`).
- No panic, watchdog, stack overflow, or mass-storage command occurred.

Owner glass acceptance of the revised mapping remains pending after the
combined product flash.
