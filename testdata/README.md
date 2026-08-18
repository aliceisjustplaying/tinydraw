# Test data

Everything in this directory is active evidence or a reproducible fixture:

- `strokes/` drives host replay, lifecycle, and syntax tests.
- `snapshots/` supplies exact image oracles for replay and UI tests.
- `reference/` pins Perfect Freehand compatibility outputs and their generation metadata.
- `ink-traces/` supplies Vector V2 latency/replay fixtures; five traces are embedded in the hardware
  gate, while `under-overlay.csv` is retained as the canonical streamed long trace.

Update fixtures only with the corresponding behavior change and validation. They are not historical
scraps and should not be moved into the documentation archive.
