# Vector V2 code-quality cleanup — 2026-08-18

This receipt closes the pre-final overengineering and repository-structure round.

## Scope

- Moved product-loop diagnostics out of `vector_v2_app.cpp` and merged its duplicate pending-stroke
  report state. The app entry point fell from 1,025 to 812 lines.
- Removed unused raster, live-stroke, and export APIs.
- Reused the canonical zoom and history-control helpers.
- Replaced product calls to full tile payload measurement with early-exit uniform classification;
  the detailed row-run/RLE census remains available to diagnostic tooling.
- Consolidated current design/review documents, historical campaigns, and hardware receipts under
  `docs/`. Active test fixtures remain in `testdata/` and are indexed there.

## Acceptance

- Host debug: 31/31 passed.
- Host release: 31/31 passed.
- Host ASan/UBSan: 13/13 passed.
- ESP32 product, 448-slot gate-harness, tile-census, and QEMU builds passed; QEMU replay passed.
- Nine-run release cache A/B against `98174f5`: absorption 3.306 → 3.273 µs, raw-history commit
  59.171 → 58.648 µs, uniform-history commit 132.445 → 132.306 µs. Exactness and work counters
  were unchanged; remaining sub-microsecond variation was within timer noise.
- The physical 448-slot automated battery passed all 31 flags with no crash or watchdog marker.
  Settled AA retains its existing yellow performance classification.
- The normal product image was restored after the gate. Boot reached `TINYDRAW_VECTOR_V2_READY`
  with 2,187,528 bytes free PSRAM, a 2,162,688-byte largest block, and 6,088 bytes of main-stack
  headroom.

The full static-analysis commands still stop on established complexity warnings in the authority
journal and raster hot paths. Changed code added no analyzer finding.
