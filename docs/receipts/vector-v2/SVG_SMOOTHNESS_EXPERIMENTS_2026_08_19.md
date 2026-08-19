# SVG smoothness experiments — 2026-08-19

Budget: at most five measured hypotheses. Baseline does not consume an attempt.

## Baseline

The owner's mounted `$HOME/Desktop/DRAWING.SVG` is 480,475 bytes with
29 logical paths and 6,502 primitive subpaths. Although chunks sharing a
gesture ID were placed in one SVG `<path>`, each operation chunk constructed a
fresh `CurvedRibbonStream`. Every 32-sample boundary therefore finalized a cap
and restarted the midpoint curve, leaving redundant round bulges and tangent
breaks inside one physical stroke.

## Attempt 1 — preserve curve state across logical chunks (accepted)

SVG pen and eraser geometry now retain one curve stream for the complete
nonzero gesture ID. The one-sample overlap inserted by
`ChainedOperationBuilder` is consumed once. A regression proves the SVG from
two overlapping chunks is byte-identical to the same samples stored as one
operation; the authority suite passes 86 cases / 25,708 assertions.

This removes a known geometric discontinuity and redundant internal caps while
retaining bounded streaming, logical path count, painter order, and all
one-operation output. It also reduces export work for long physical strokes.

## Attempt 2 — adaptive centerline subdivision (rejected and reverted)

An SVG-only stream sampled each midpoint quadratic at one, two, or four
segments and added round joins. The focused raster oracle found 142 pixels
different from the displayed ribbon in its 64x32 authority fixture, including
fully covered pixels absent from one representation. The experiment changed
the stroke silhouette, not just its polygonal approximation, so it was
reverted. SVG remains byte-for-byte tied to the renderer's geometry authority.

Three experiments remain.
