# Transient color-popup byte-swap incident — 2026-08-18

During the settled-AA glass capture session (author photo
`benchmark-results/history-latency-2026-08-18/color-popup-byteswap-incident.jpg`,
log `/tmp/settle-glass.log`), the color popup opened once with corrupted colors
and recovered on its own. First occurrence ever.

The retained 1025×850 crop is 216 KiB and has SHA-256
`e9c74da7c97bcf56b1e4e1a40e73ba5bed696f6c80471a55e36b8f91ec4afbdc`.
The original 4032×3024 lossless camera PNG was 11.3 MiB because sensor noise,
fabric texture, and display moiré defeated PNG compression. Its SHA-256 was
`c69b29a5b006fc9d8e7359ec8c78ac540e4fa024ac68f50518e96ea9dc44e27f`;
Git history retains it before the release cleanup.

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

## 2026-08-19 root cause and fix

The ring transport can fuse de-rotation and byte swapping before its chrome
patch callback. Normal cached chrome explicitly converts each sprite color to
that surface's byte domain. Modal chrome (`kColors`, the compact popups,
dialogs/toasts) instead uses `PixelPainter` directly with host-order RGB565.
`present_ring` advertised every no-live-ink patch as accepting pre-swapped
surfaces, so a stale/reusable ring plus a modal-region present wrote popup
colors in the wrong domain and the transport correctly skipped its final swap.
That is the photographed hue rotation; invariant black/white pixels hid it.

The fused path is now allowed only for normal cached chrome with no history
busy toast. Modal and toast transfers remain host-order through painting and
receive exactly one final transport swap. `ChromeStagingCache` rejects an
unsafe pre-swapped state as a second contract guard. The regression covers the
color popup, new dialog, export toast, and history busy state, including
failure-before-mutation; all 64 interaction cases / 14,367 assertions pass.

Status: **root cause fixed, host-verified, and hardware-battery verified.** The
complete 604-slot device gate passed repeatedly on 2026-08-19 with the modal
color-dialog exercise green and `failure_marker=False`.

## Original hypothesis queue

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
