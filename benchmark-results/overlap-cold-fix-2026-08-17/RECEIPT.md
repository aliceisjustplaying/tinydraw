# Overlap cold-gate closure — 2026-08-17

The binding ESP32-S3 `overlap` workload at 50% passed its 500 ms product gate.
The same battery passed the general-cold firmware guards, but its 400% result
used the 520 ms development hold line. The final normal-product closure remains
≤500 ms across 20 reset-separated runs.

## Cause and change

Curved committed authority correctly produces 2,376 chords for the eight
stacked strokes. `apply_masked_operation_chord_rows` reused one row-wide unset
window while earlier chords in that row finalized pixels, so later overlapping
chords repeatedly probed already-final spans. The sweep now refreshes the unset
window inside each overlapping chord's narrower bounds.

A host regression fixture records the bounded-work effect: the old loop used
25,486 work units; the fixed loop uses 10,758.

## Source and build provenance

The before/after serial artifacts are `/tmp/gate-verify-current.log` and
`/tmp/gate-verify.log`. They were captured on 2026-08-17 at 23:30 and 23:44
Europe/London, respectively. These are local run artifacts, not tracked files.

Both firmware images were built from an uncommitted working tree based on HEAD
`f8873c3ee796d1a124a6d2a98d721b8e44b5133b` (`docs: close
feature-complete cleanup`). The stale-window change and its host regression
were working-tree modifications in `vector_v2/src/incremental_rasterizer.cpp`
and `vector_v2/tests/incremental_rasterizer_test.cpp`; the tree also contained
unrelated cleanup edits. Therefore this receipt identifies a source diff plus
raw artifacts, not a committed revision. The firmware-reported app version
`v2-feature-complete-pre-cleanup` is the nearest tag and is not exact source
provenance.

The treatment image was built at 23:42:06 with ESP-IDF v6.0.2. The build cache
records `TINYDRAW_FIRMWARE_VARIANT=gate`,
`TINYDRAW_VECTOR_V2_TILE_SLOTS=448`, and target `esp32s3`; the component was
compiled with `TINYDRAW_VECTOR_V2_GATE_HARNESS=1`,
`TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS=1`, and `-O2`. The captured ELF
SHA-256 is `4da4bf68b7e5fc492740fb97d025ca7b79de215d48dce866f13375b523097782`.
The device ran at 240 MHz with 8 MiB octal PSRAM at 80 MHz. The physical panel
transport reported 40 MHz.

## Physical result

Full 448-slot gate battery on `/dev/cu.usbmodem1101`:

| Metric | Before | After | Limit |
|---|---:|---:|---:|
| overlap 50% wall | 585.821 ms | 476.969 ms | 500 ms |
| overlap 50% compute | 496.256 ms | 384.393 ms | informational |
| producer steps | 235 | 90 | informational |

The frozen `adversarial_tapered_4x+evil_hairlines` general-cold workload in
the treatment battery produced:

| Zoom | Wall | Firmware limit | Verdict |
|---:|---:|---:|---:|
| 50% | 421.787 ms | 500 ms | pass |
| 100% | 399.498 ms | 500 ms | pass |
| 200% | 464.071 ms | 500 ms | pass |
| 400% | 515.123 ms | 520 ms development guard | pass |

These are one battery's development results, not the ship-contract closure
statistic. In particular, 515.123 ms does not satisfy the final ≤500 ms
requirement. The reset-separated 20-run maximum with the normal product and
normal services remains open.

## Services and memory

The gate app constructed and restored the production autosave store before the
battery (`generation=4335 active=52 retained=52`). The harness then replaced
and mutated authority directly for its deterministic workloads. It issued no
journal submissions, and the treatment log contains no
`TINYDRAW_AUTOSAVE_COMMIT` or `TINYDRAW_AUTOSAVE_WRITE_FAIL` event. This proves
that autosave initialization and recovery coexisted with the gate; it is not an
autosave-write performance measurement.

After application storage allocation, the internal producer scratch was
active with 235,732 bytes of internal RAM and 2,283,764 bytes of PSRAM free.
At the final ready line the gate reported 5,920,408 live-storage bytes,
2,282,124 PSRAM bytes free, a 2,228,224-byte largest PSRAM block, and 6,248
bytes of main-task stack headroom. The export-reserve gate successfully held
1,572,864 contiguous bytes, leaving 709,256 bytes free and a 704,512-byte
largest block.

The final `TINYDRAW_GATE1_AUTOMATED_DONE` verdict contained no zero-valued
gate, and `TINYDRAW_VECTOR_V2_GATE_HARNESS_DONE pass=1`. Host tests passed
31/31; product V2, Raster V1, and QEMU builds passed; QEMU replay passed.
