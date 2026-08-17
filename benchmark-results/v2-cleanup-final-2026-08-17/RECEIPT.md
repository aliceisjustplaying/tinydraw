# Feature-complete cleanup receipt

> Historical checkpoint. Later work on the same cleanup branch closed the
> overlap 50% cold gate at 476.969 ms, raised the raw-tile pool to 448 slots,
> and split `vector_v2_app.cpp` below 1,000 lines. See
> [`../overlap-cold-fix-2026-08-17/RECEIPT.md`](../overlap-cold-fix-2026-08-17/RECEIPT.md)
> and the current [`PROJECT_STATE.md`](../../PROJECT_STATE.md).

Baseline: `v2-feature-complete-pre-cleanup` (`3f23c09`).

Cleanup integration: `cleanup/v2-degoldplate` at `23f514e` before this receipt.

## Scope and retained products

The cleanup preserves every supported product surface:

- Raster V1 on macOS, QEMU/graphics replay, and physical ESP32-S3 hardware;
- Vector V2 product firmware and its hardware gate.

Retired benchmark runners, concluded capture/probe firmware, duplicate schema
validation, tool-only hot-path counters, unused raster seams, and gate-only
diagnostics in product builds were removed. The firmware build cross-product is
now a named variant selector. Product allocation order remains stable through
explicit aligned reservations where the ESP32-S3 cache-placement receipts make
layout part of the measured baseline; reclaiming those reservations belongs to
the measured optimization round.

## Repository result

Relative to the baseline tag, the current tree changes 466 files: 1,536 lines
added and 111,170 deleted. The tracked tree fell from 829 files / 76,854,473
bytes to 433 files / 8,320,215 bytes before this receipt. Of that reduction,
377 raw or generated evidence files / 68,217,490 bytes were removed from the
current tree. Every removed evidence file remains available at the baseline
tag; receipts and compact regression fixtures remain in the current tree.

## Validation

- Host release: 31/31 CTests passed.
- ASan/UBSan: 13/13 CTests passed.
- macOS Raster V1 target built.
- Raster V1 QEMU and graphics replay passed with checksum `92d3e6ea`.
- Physical Raster V1 booted through `TINYDRAW_HARDWARE_OK` with the retained
  two-buffer DMA queue required by its internal-SRAM coverage plane.
- The final 384-slot Vector V2 device gate completed without a crash or failure
  marker. Every verdict was green except the known binding `overlap_cold` case;
  the settled-AA receipt remains yellow by design.
- The final gate's general 400% cold wall was 495,024 us against its 520,000 us
  hold line. The 50% overlap wall was 591,996 us against 500,000 us, improved
  from the cleanup baseline's 604,187 us but still red.
- Gate rerender accounting was initialized deterministically: the cache tour
  reported 137 renders / 137 unique / 1.000 amplification / zero unexplained.
- Vector V2 product firmware (`0x102020` bytes) was restored to the attached
  board after the gate. It reached `TINYDRAW_VECTOR_V2_READY` with no failure
  marker, 5,144 bytes of main-stack headroom, and a 2,228,224-byte largest free
  PSRAM block.

The cleanup did not claim a performance closure. The next round owns the 50%
overlap cold failure, high-zoom Undo/Redo rebuild latency, and intentional
re-measurement of the temporary layout reservations. Autosave state writes now
coalesce, while two-arena journal compaction remains explicitly deferred.
