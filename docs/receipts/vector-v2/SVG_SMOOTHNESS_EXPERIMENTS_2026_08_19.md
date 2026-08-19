# SVG smoothness experiments — 2026-08-19

Budget: at most five measured hypotheses. Baseline does not consume an attempt.

## Baseline

The author's mounted `/Users/sarah/Desktop/DRAWING.SVG` is 480,475 bytes with
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

## Attempt 3 — three-span shared quadratics (accepted)

The shared ribbon authority now resolves each stable midpoint quadratic into
three tangent-following spans instead of two. This raises curve resolution by
50% in SVG while keeping exported and on-device geometry identical; internal
joins retain the overlap that prevents raster cracks. Worst-case fixed batch
storage grows from 10 to 12 primitives and remains bounded by an adversarial
finish test.

The complete device battery is green with `failure_marker=False`. General cold
render was 393/386/461/498 ms at 50/100/200/400%, versus approximately
397/389/464/499 ms before the experiment. Settled AA was
74.7/87.2/176.1/382.2/944.4 ms, versus 75.1/87.6/176.9/383.6/946.8 ms. Both
are flat within run noise, with unchanged slicing and no responsiveness cost.
The host ribbon, SVG authority, and rendering suites pass 366 cases and
759,573 assertions.

## Attempt 4 — exact SVG span boundaries (accepted)

The tiny outward teeth visible only at extreme SVG magnification came from the
renderer authority's deliberate `0.75 px` tangent overlap between adjacent
quadratic subspans. That overlap prevents fixed-grid raster cracks, but in a
vector renderer its two displaced corners protrude beyond the curve outline.

SVG pen and eraser streams now request shared-boundary spans: adjacent convexes
meet at the same two section vertices with no overlap. Screen and PNG rendering
retain the overlap, so their crack prevention and performance are unchanged.
A red-first regression reproduced the bug by finding zero shared vertices at
both seams; it now finds exactly two at each seam. In the 64x32 raster-parity
stress fixture, 79 of 2,048 pixels differ only in antialiased edge coverage and
there are no fully covered black/white disagreements.

Host Debug passes 31/31 CTest targets, host ASan passes 13/13, host Release
passes 31/31, and the SVG authority suite passes 87 cases / 25,718 assertions.

One experiment remains.
