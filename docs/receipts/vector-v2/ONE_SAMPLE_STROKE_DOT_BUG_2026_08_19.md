# Known bug — one-sample Strokes render inconsistently across tiers ("Schrödinger dots")

Status: **open, diagnosed mechanism candidate, not yet fixed.** Recorded
2026-08-19 (~00:20) at owner request; owner has two live mystery dots in
the current document and independently hypothesized the cause: taps that
never became lines are stored as one-sample operations and surface only
in some views/undo levels.

## Confirmed code mechanism (partial)

The settled renderer provably never paints a one-sample operation:
`advance_operation_scan` starts every operation at `endpoint_ = 1`, and
`advance_endpoint_preparation` completes as soon as
`endpoint_ == operation_samples_.size()` — immediately true for
size 1, leaving `operation_touched_` false
(`vector_v2/src/settled_tile.cpp`, scan/endpoint phases). This matches
the receipted 2026-08-18 physical export pair: SVG emitted two blue dots
at (772,4) and (996,4); PNG (which streams settled windows) painted
neither. `prepare_incremental_curve_unit` cannot yield a unit for a
single sample (`incremental_rasterizer.cpp:691–715` requires a prior
endpoint), so any renderer that only walks curve units skips the dot;
the hard-edged paths that paint dots (SVG core, and apparently raw tile
replay, since dots are seen on glass) use different entry points. The
live-draw path's behavior for a tap-only stroke was NOT traced this
session — whether the dot is visible at draw time is unverified.

## Resulting observable behavior (fits all prior sightings)

One authority operation, up to five renderers, disagreeing:
SVG paints the dot; PNG omits it; settled tiles omit it; raw (hard)
tiles apparently paint it; live draw unverified. Therefore a dot
appears when its tile is at raw quality (cold rebuild, pan-in, history
damage repair after Undo/Redo) and **vanishes when idle settle
republishes the tile at settled quality** — exactly the previously
"inconclusive" transient blue dots during 400% pan and 50→100% zoom
(F13/F14 receipts). Undo/Redo moving the active prefix additionally
includes/excludes the operation entirely, so dots pop in and out at
specific history levels. Owner adds: current dots may sit near a
boundary edge; the receipted SVG pair sat at y=4, near the world edge.

## Cheap verification recipe (owner has two live dots)

Export now: `DRAWING.SVG` should contain both dots as filled paths
(identifying their exact operations/coordinates); `DRAWING.PNG` should
lack them. Then let idle settle finish at the current zoom: dots should
disappear from glass; pan away and back before settle: dots should
reappear. Any deviation from that pattern falsifies this mechanism.

## Fix direction (not started)

Decide the semantic first (owner call): a tap paints a dot (SVG already
committed to this). Then settled rendering must paint size-1 operations
as one degenerate capsule — the coverage math already guards zero-length
chords (`chord_inverse_length_squared_ = 0` → distance-to-point), so a
synthetic single chord with `first == second == sample` is the natural
form. PNG inherits the fix through settled windows. Check whether the 25
frozen AA checksums contain any one-sample operation (likely not, or the
tier mismatch would have been caught); if any do, re-freezing needs
owner-approved semantics. Also trace and align the live-draw path so
draw-time and replay visuals agree. The world-export fixture contains a
dot but does not assert its pixels — turn that into a real assertion.
