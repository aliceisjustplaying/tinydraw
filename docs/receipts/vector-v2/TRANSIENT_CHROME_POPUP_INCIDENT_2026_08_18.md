# Transient chrome/popup incident — 2026-08-18

## Owner observation

At 100% zoom, after minimap navigation/panning to a blank corner, the owner
drew thick circles, opened the pen-size selector, selected the smallest size,
and was about to draw a thin circle. The device log recorded three committed
Strokes before an unintended Undo at 16:52:59. The photographed glass showed
two thick circles, a missing zoom-plus control, selector remnants at the lower
left, and a black dot among the remnants. The power glyph was initially missing; its
later periodic update repaired that glyph. This is a real optical incident,
but it has not been reproduced deterministically.

## Bounded host diagnosis

A temporary host oracle exercised the real `VectorV2Presenter` path at 100%:
full refresh, minimap jump, reusable toroidal pan, `show_start`/`show_update`,
three distinct `OperationLog` Stroke authorities, size-popup open/select/close,
and the same battery-only `refresh_region` used by the app. After every
presentation it compared the zoom rail, power glyph, and lower-left popup area
against an independently composed canvas/chrome reference. The natural trace
was exact and kept all three active Strokes with no Redo tail.

The production `TouchEventBuffer` and the app's popup gesture-dispatch rules
were then enumerated across 12,288 bounded cases: capacities 4/8/16, queued
Down/Move/confirmed-Up events, every consumer-drain interleaving, rapid and
overlapping contacts, popup transition timing, and a delayed 50 ms sample. No
small-size gesture dispatched Undo and every case retained all three Strokes.
This enumeration covered the action boundary; it did not instantiate the full
ESP-only `VectorV2ChromeController` dependency graph.

The oracle's negative control deliberately retained popup pixels during close.
It failed with 520 lower-left pixel mismatches immediately after close and
again after the periodic power refresh, while zoom and power comparisons
remained exact. This proved that the pixel oracle could detect the photographed
selector-remnant class. The temporary oracle and host stubs were removed after
the timeboxed investigation.

## Verdict and reopen trigger

**INCONCLUSIVE / deferred.** No production hypothesis or behavior change is
supported by the available evidence. Reopen when either the device reproduces
the incident with an ordered touch-event/action log spanning popup open through
the unintended Undo, or a deterministic host/device trace makes the natural
pixel/action oracle red. Preserve at minimum event kind, point, sequence,
timestamp/age, popup before/after, resolved action, authority read view, ring
state, and submitted panel bounds.

## Second occurrence — 2026-08-18 owner glass session (history hold-back test)

Reproduced on ordinary product firmware during the Undo hold-back glass
test. While selecting a pen size, the owner also managed to hit the
Undo/Redo control ("this time I tapped redo") in the same gesture; both
actions fired and the UI corrupted ("the whole UI just gets fucked").
This is no longer a single ambiguous event: the size popup and the history
controls can trigger together with unpredictable ordering.

Owner direction: fix after the performance round. This second occurrence
upgrades the incident from "inconclusive, needs natural red reproduction"
to a reproducible-by-hand interaction defect; the fix belongs with the
roadmap §3 touch-target review (overlapping targets, pressed feedback,
missed-tap check). The ordered-event capture requirements above still
define the diagnostic bar.
