# TinyDraw Vector V2

## Essence

TinyDraw is a white, bounded drawing surface with direct finger ink, a compact bottom toolbar,
zoom controls, and a minimap. Strokes remain editable as vector authority while the visible canvas
is refined in bounded background work.

## Interactions

- Drag on the canvas in Draw mode to draw one uninterrupted Stroke. (intent: ink follows the finger
  and one finger-down/up gesture remains one Undo and Redo unit; the Puck target supports 4,096
  sampled contact points plus the distinct lift, for 4,097 stored samples in one active Stroke.)
- Tap the active tool, then choose Draw, Eraser, or Pan. (intent: the three canvas gestures remain
  explicit modes, so navigation and erasure cannot be mistaken for new ink.)
- In Draw mode, open the color control and choose from the 2 by 16 palette. (intent: the selected
  RGB565 color applies to subsequent Strokes and remains visible in the toolbar.)
- Open the size control and choose one of four brush sizes. (intent: Stroke width is a direct,
  persistent choice shared by drawing and erasing.)
- Drag on the canvas in Eraser mode to remove ink. (intent: erasure is Stroke-based, pressure-aware,
  and participates in Undo and Redo like drawn ink.)
- Tap Undo or Redo in the bottom toolbar to move one Stroke through history. (intent: history is
  immediate and preserves Stroke identity.)
- Tap the document control, choose New, then confirm or cancel. (intent: destructive clearing
  requires explicit confirmation and cancellation leaves the document untouched.)
- Tap zoom minus or zoom plus to change magnification while retaining the Camera focus. (intent:
  zoom keeps the drawing location under consideration.)
- Short-press BOOT to hide the battery, minimap, zoom controls, and bottom toolbar; short-press it
  again to restore them. (intent: the complete 368 by 448 display becomes drawable without losing
  the current tool, color, size, zoom, Camera, or history.)
- Long-press BOOT to record from a blank drawing, long-press again to stop, and long-press once more
  to replay the same touch and chrome-visibility stream from the same blank baseline. (intent:
  demos are deterministic and never depend on persisted drawing state.)
- Drag on the canvas in Pan mode to move the Camera without drawing. (intent: navigation never
  changes drawing authority.)
- Drag on the minimap to move the Camera directly through the bounded world. (intent: distant areas
  remain reachable without repeated canvas drags.)

## Demands

- Required: one 368 by 448 RGB565 panel, single-point touch input, deterministic millisecond ticks,
  and enough WebAssembly linear memory for the fixed-capacity Vector V2 application.
- Preferred: one raw button. The firmware measures the press duration itself; a short release
  toggles chrome and an 800 ms hold controls demo record/replay.
- Persistence, RTC synchronization, USB export, panel timing, and touch-controller defects are
  physical-device facilities and are not claimed by this Puck port.
