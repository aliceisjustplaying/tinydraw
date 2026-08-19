# Known bug — one-sample Strokes render inconsistently across tiers ("Schrödinger dots")

Status: **fixed, host-verified, and full-device-battery verified 2026-08-19.** Recorded
2026-08-19 (~00:20) at author request; author has two live mystery dots in
the current document and independently hypothesized the cause: taps that
never became lines are stored as one-sample operations and surface only
in some views/undo levels.

Follow-up: the cross-renderer mismatch fixed here exposed a separate input
admission bug. Physical authority proved that unintended top-edge exits were
being stored as valid one-sample dots. That source is closed in
[`PHANTOM_TOP_EDGE_CONTACT_2026_08_19.md`](PHANTOM_TOP_EDGE_CONTACT_2026_08_19.md).

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
specific history levels. Author adds: current dots may sit near a
boundary edge; the receipted SVG pair sat at y=4, near the world edge.

## Cheap verification recipe (author has two live dots)

Export now: `DRAWING.SVG` should contain both dots as filled paths
(identifying their exact operations/coordinates); `DRAWING.PNG` should
lack them. Then let idle settle finish at the current zoom: dots should
disappear from glass; pan away and back before settle: dots should
reappear. Any deviation from that pattern falsifies this mechanism.

## Fix shipped

Tap operations are already defined as visible dots by the live builder,
hard replay, SVG export, and existing tests. Settled replay now begins a
one-sample operation at endpoint zero. The existing incremental curve
preparer consequently produces its degenerate capsule without a second
representation or special raster path. PNG export inherits the result
through settled world windows.

Two regressions lock the contract: settled synchronous and 17-pixel
sliced rendering are bit-identical with a visible dot, and the existing
world-export fixture's green tap is asserted at its center pixel. The
complete 31-test host suite passes; all frozen AA checksums are unchanged
because none of their fixtures contained a one-sample operation.

The final 604-slot device battery passed with `failure_marker=False`, including
settled rendering, PNG/SVG export, history, and the real captured-drawing corpus.
The unverified live-draw note above describes the pre-fix investigation and is
not needed for the cross-tier consistency verdict. The follow-up receipt
contains the later physical screen/PNG/SVG comparison and input-admission fix.
