# Transient color-popup byte-swap incident — 2026-08-18

During the settled-AA glass capture session (owner photo
`benchmark-results/history-latency-2026-08-18/color-popup-byteswap-incident.png`,
log `/tmp/settle-glass.log`), the color popup opened once with corrupted
colors and recovered on its own. First occurrence ever.

## Evidence read from the photo

- Black chrome glyphs and pure-white regions are pixel-perfect.
- Every colored region in the popup is hue-rotated; the dark-gray swatch
  outlines render magenta — the exact signature of one extra or missing
  RGB565 byte swap (0x39E7 dark gray ↔ 0xE739 magenta; 0x0000/0xFFFF are
  swap-invariant).
- The corruption stops exactly at the canvas/dock seam (single red column
  artifact at the boundary); the dock's own swatches and battery are
  correct. The dock presents through the chrome strip path; the popup
  through a canvas-region present — so one canvas-region present pushed
  wrong-swap-domain bytes.
- Timing: the popup was opened amid rapid zoom/pan/undo-redo torture,
  around the 22:55:41 long 50% settle pass; the session log shows zero
  failures, overflows, resyncs, or watchdogs.

## Hypothesis queue for the diagnosis session

1. The color dialog's frame re-presentation fast path re-pushing content
   that was already in the swapped/staged domain (double swap), exposed by
   today's changed presentation interleaving (settle-hold batching, ring
   locality, history swap presents).
2. A popup present over an active frame ring reading ring content with a
   stale swap/rotation assumption.
3. A settle-staged frame region raced by the popup present.

All of today's presentation changes are in the suspect window; the bug is
transient and self-healing, so the diagnosis needs a deterministic repro:
cached pan at 50–100%, let a long settle pass run, open the color popup
mid-pass, capture serial + photo. Audit every canvas-region path that can
push bytes without passing through stage_pixels_swapped exactly once.
