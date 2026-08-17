# Stroke-logical SVG and minimap acquisition receipt — 2026-08-17

## Status

**Host and firmware builds pass; physical flash is waiting for the owner to
finish the currently mounted SVG test and reset the device.**

## Owner SVG artifact

The owner mounted the product export and opened `DRAWING.SVG` in Inkscape. The
read-only mounted pre-fix artifact was inspected without copying its drawing
geometry into Git:

```text
bytes=98456
paths=24
rects=1
sha256=6addd2c0b7a359ad2cd3c63f0e7152c2130b8e1dd9ba58df7b5f99d7c635f796
```

The complete metadata-only observation is in
[`mounted-svg-observation.txt`](mounted-svg-observation.txt). The volume was not
written, ejected, or reset by the agent.

## SVG diagnosis and correction

A physical finger-down/up gesture already receives one nonzero `gesture_id`.
`ChainedOperationBuilder` preserves that ID when bounded sample/time storage
splits the Stroke, and `OperationLog` persists it in every chunk record. The
exporter was nevertheless opening and closing one `<path>` around every record.
It also emitted a white `<rect>` unconditionally in the SVG prolog.

The exporter now:

- opens one path for the first chunk of a nonzero gesture ID;
- streams adjacent same-ID/tool/color chunks as additional exact ribbon
  subpaths inside that path;
- closes the path only at the next logical Stroke;
- leaves legacy/imported zero-ID operations independent;
- emits no synthetic background rectangle;
- retains bounded 1 KiB writer buffering and no document-sized geometry/output
  allocation.

The original deterministic repro went red with `paths=3` instead of `2` and
one unexpected rectangle. The fixed focused SVG suite passes 7 cases and
13,443 assertions, including exact output, primitive/raster coverage, random
documents, maximum capacity, chunk grouping, and background omission.

V2 Undo/Redo are not implemented yet—the toolbar actions currently fall
through as placeholders. The domain glossary now records a **Stroke** as the
logical export/Undo/Redo unit and a **Stroke chunk** as internal only. The
existing roadmap already requires whole-gesture Undo/Redo and whole-gesture
damage unions.

## Minimap diagnosis and correction

The owner's correction was valid: although the whole minimap frame passed the
hit-test, a drag beginning away from the tiny viewport applied only its small
relative delta to the old origin. At 400%, that left the ~5×5 px viewport far
from the finger and made the frame appear inactive.

The corrected mapping has two explicit modes:

- the truthful viewport gets a 28×28 px invisible grab target clamped inside
  the minimap; a start there preserves the existing finger/viewport offset;
- a start elsewhere directly acquires the viewport beneath the finger, then
  follows the drag and remains captured outside the minimap.

The visible box remains truthful. Intent promotion stays 4 px at 25–100%, 3 px
at 200%, and 2 px at 400%. A deterministic 400% host fixture proves a near-box
move resolves to `(146,146)` while a far-center move acquires `(2906,3544)`;
the old relative-only result for that far move would have been `(146,146)`.
The physical gate classifier contains the same far-acquisition check.

## Validation

- [`host-release.log`](host-release.log): 29/29 CTest targets pass.
- [`host-asan.log`](host-asan.log): 11/11 ASan/UBSan CTest targets pass.
- [`product-build.log`](product-build.log): ordinary product firmware compiles
  and links.
- [`gate-build.log`](gate-build.log): non-MSC physical gate firmware compiles
  and links with the new acquisition classifier.
- `./scripts/dev format-check`: pass.

The first gate flash attempt in [`gate-flash.log`](gate-flash.log) stopped
before connecting because `/dev/cu.usbmodem101` was absent while the owner's
`TINYDRAW` export volume was mounted. No flash write occurred in that attempt,
and the agent did not invoke mass-storage mode.
