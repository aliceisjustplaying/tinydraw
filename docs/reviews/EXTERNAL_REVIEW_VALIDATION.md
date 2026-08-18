# External review snapshot validation

Date: 2026-08-18
Branch: `cleanup/v2-degoldplate`

The exact source commit and clean working-tree status are recorded in the
packet's `provenance/` directory. Its per-file manifest covers the extracted
snapshot.

## Passing checks

- host debug: 31/31 CTest targets;
- host release: 31/31 CTest targets;
- ASan/UBSan: 13/13 CTest targets;
- Vector V2 product, Raster V1, 448-slot Vector V2 gate, QEMU, and macOS host
  builds;
- QEMU asserted replay;
- physical 448-slot gate: every firmware verdict passed, including overlap 50%
  at 476.969 ms and general 400% at the 520 ms development guard;
- normal Vector V2 product flashed and reached `TINYDRAW_VECTOR_V2_READY` with
  8,712 bytes of main-task stack headroom;
- owner cursory glass sanity check: drawing looked normal after cleanup;
- `git diff --check`;
- targeted formatting of changed Vector V2 files.

The physical gate restores the autosave store but does not submit journal
writes. Its 515.123 ms general 400% result is above the contract's 500 ms
release line. The final 20-run normal-product distribution with real journal
activity remains open.

## Open quality checks

`./scripts/dev tidy` stops in `vector_v2/src/authority_journal.cpp`:

- `validate_payload()`: cognitive complexity 40, threshold 20;
- `copy_payload()`: seven parameters, threshold six;
- `recover_authority_journal()`: 127 lines, 85 statements, 16 branches, and
  cognitive complexity 50.

`./scripts/dev cppcheck` reports possible uninitialized aggregate members in
`materialized_canvas.h` and `incremental_document.cpp`, synchronous stability
checks in `svg_export.cpp` that it considers constant, plus lower-priority style
and return-by-value findings. The full output is included in packet provenance.

`./scripts/dev format-check` finds one existing Raster V1 violation at
`esp32/main/firmware_canvas.h:28`. The current Vector V2 changes are formatted.

## Fresh performance observations

The latest gate recorded 25% settled-AA work at 152.945 ms for 42 windows, with
one 76.416 ms window. This exceeds the nominal 8 ms cooperative settle slice;
the harness still reports `ssaa_receipt=yellow`.

The latest gate memory lines report 2,282,124 bytes of free PSRAM and a
2,228,224-byte largest block before the export reservation. Holding the
1,572,864-byte reserve left 709,256 bytes free and a 704,512-byte largest block.
