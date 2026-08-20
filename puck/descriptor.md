# TinyDraw Vector V2

## Essence

TinyDraw is a white, bounded drawing surface with direct finger ink, a compact bottom toolbar,
zoom controls, and a minimap. Strokes remain editable as vector authority while the visible canvas
is refined in bounded background work.

## Interactions

- Drag on the canvas to draw one uninterrupted Stroke. (intent: ink follows the finger and one
  finger-down/up gesture remains one Undo and Redo unit; the Puck target supports 4,096 logical
  Down/Move/Up points in one active Stroke.)
- Tap Undo or Redo in the bottom toolbar to move one Stroke through history. (intent: history is
  immediate and preserves Stroke identity.)
- Tap the document control, choose New, and confirm to clear drawing authority. (intent: destructive
  clearing requires an explicit confirmation.)
- Tap the zoom rail or short-press BOOT to advance zoom while retaining the Camera focus. (intent:
  zoom keeps the drawing location under consideration.)
- Long-press BOOT to record from a blank drawing, long-press again to stop, and long-press once more
  to replay the same touch and zoom stream from the same blank baseline. (intent: demos are
  deterministic and never depend on persisted drawing state.)
- Select Pan and drag the canvas to move the camera without drawing. (intent: navigation never
  changes drawing authority.)
- Drag on the minimap to move the camera directly through the bounded world. (intent: distant areas
  remain reachable without repeated canvas drags.)

## Demands

- Required: one 368 by 448 RGB565 panel, single-point touch input, deterministic millisecond ticks,
  and enough WebAssembly linear memory for the fixed-capacity Vector V2 application.
- Preferred: one raw button with an 800 ms long-press verdict for zoom and demo control.
- Persistence, RTC synchronization, USB export, panel timing, and touch-controller defects are
  physical-device facilities and are not claimed by this Puck port.
