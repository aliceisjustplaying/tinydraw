# Overnight release closure — 2026-08-19

## Verdict

The requested bug fixes and performance campaigns are complete. The full
physical-device battery, host Debug suite, host ASan suite, and format check are
green. The attached device was returned to the normal `product` firmware after
verification.

## Correctness fixes

- One-sample contacts now have a settled endpoint and render consistently as a
  dot on screen, in hard replay, PNG, and SVG.
- Popup contacts require six consecutive lifted polls before a new press can
  begin, preventing a size-selection contact from reaching the history control
  below the popup.
- SVG erasers use painter-ordered masks, so erased pixels are transparent and
  later ink can repaint them.
- SVG gesture chunks share one curve stream. The renderer and SVG exporter now
  use the same three-span midpoint ribbon geometry for smoother joins.
- Modal chrome remains in host RGB565 order until presentation, removing the
  double-swap path behind the inverted-color incident.
- `New` uses the production blank-document reset path. The gate firmware alone
  restores its owned test snapshot so the battery remains deterministic.

## Performance closure

- Anti-aliasing: five experiments completed. Opaque-first compositing was kept;
  the device battery moved from `75.217/87.869/177.282/386.169/959.910 ms` to
  `75.102/87.647/176.885/383.594/946.849 ms` across its five AA workloads.
- Undo/Redo: five experiments completed. Focused chrome presentation fell from
  `5.982 ms` to `3.003 ms`; redo occupancy work roughly halved in the host sparse
  and dense cases (`0.0260→0.0133 ms`, `0.0336→0.0169 ms`). The physical-device
  history gate remained bounded at `27.3 ms` maximum interaction work.
- Cold rendering: five experiments completed. Fewer replay resumptions and the
  removal of a redundant preflight directory scan survived the battery. Final
  400% cold times were `213.970 ms` overlap, `490.747 ms` general, and
  `344.686 ms` owner-document; maximum cooperative tick was `10.719 ms`.
- SVG smoothness: three measured experiments were sufficient. Two experiments
  remain unused because geometry parity and the requested smoothing were
  established by the shared curve change.

Rejected experiments and their measurements are preserved in the linked AA,
history, cold-render, and SVG campaign receipts.

## Verification

- Physical ESP32-S3 gate: every gate passed, including the 102-operation,
  2,706-sample owner torture document; no failure marker.
- Host Debug: 31/31 CTest targets passed.
- Host ASan: 13/13 CTest targets passed.
- `./scripts/dev format-check`: passed.
- Product image: `0x106be0` bytes with 41% of the app partition free; flashed and
  hash-verified on `/dev/cu.usbmodem1101`, followed by a hard reset.

