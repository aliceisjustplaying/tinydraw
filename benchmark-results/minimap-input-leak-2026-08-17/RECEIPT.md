# Minimap missed-touch / short-stroke receipt — 2026-08-17

## Owner artifact

The owner supplied `/Users/sarah/Desktop/DRAWING.SVG` after observing one short blue mark and overlapping short yellow marks on glass. The drawing itself is not copied into Git.

```text
sha256=3e868c11689c251c79b4061ad6f028d50e2f7362adc0f7afaec4cb58c823086f
bytes=205281
paths=16
rects=0
fat_birth=1970-01-01T01:00:00+0000
fat_modified=1970-01-01T01:00:00+0000
```

A command-aware M/L/A endpoint scan found one short blue authority path of roughly 82×69 world units. Four later yellow authority paths were roughly 11×17, 10×17, 10×16, and 8×37 world units. The first three yellow paths overlap closely. This matches the owner's glass count/appearance and proves the marks are committed vector operations: they are not settled-AA-only pixels and SVG export did not invent them.

## Input diagnosis

The owner independently reported that the 400% minimap felt harder to hit and suspected the marks came from minimap gestures. Before this correction, `chrome_minimap_contains` owned only the exact 92×114 rendered frame. A finger/calibration sample immediately outside that boundary fell through to `builder.begin(...)` when the draw tool was selected, turning an intended minimap interaction into a real short Stroke. Once committed, that Stroke correctly appeared both on glass and as its own SVG path.

The little 400% viewport had only a 28×28 invisible grab target. Starts elsewhere inside the frame acquired the viewport, but that did not protect near-frame misses from becoming ink.

## Correction

Commit `95796fa` makes the minimap own a larger no-draw guard rectangle from panel `(250,236)` through `(368,372)`. It:

- adds 16 px above/left, 10 px right, and 6 px below the rendered frame;
- stops exactly at the toolbar and leaves a 2 px gap below the zoom-rail hit target;
- clamps out-of-frame starts through the existing minimap projection;
- expands viewport grab targets to 36×36 at 200% and 44×44 at 400%, while leaving lower zooms at 28×28;
- changes no visible minimap geometry.

Host regressions prove guard corners are minimap-owned, adjacent outside points remain canvas, popups disable the guard, high-zoom intent thresholds remain 4/3/2 px, and a farther 400% grab preserves relative viewport movement. The complete Vector V2 suite passes 243 cases / 92,426 assertions (`minimap-guard-host.log`); product firmware compiles (`minimap-guard-product-build.log`). Physical no-leak validation remains pending in the combined product flash.
