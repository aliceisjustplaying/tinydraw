# Logical SVG path gate receipt — 2026-08-17

## Result

The device SVG readback gate now validates the export contract it actually
ships: one `<path>` per logical finger gesture, not one path per bounded
operation chunk.

The physical gate reports:

```text
TINYDRAW_GATE1_EXPORT ... operations=52 ... paths=1 path_only=1 pass=1
```

The complete verdict also reports `export_encode=1 export_reserve=1`, along
with `minimap_navigation=1`, `zoom_cycle_return=1`, `zoom_overlay_pan=1`,
`color_dialog=1`, and `ink_trace=1` (`gate-device.log`). The standing unrelated
50% overlap cold gate remains `overlap_cold=0`; it is documented in
`benchmark-results/minimap-navigation-2026-08-17/RECEIPT.md`.

The latest ordinary product at commit `c404456` was restored after the gate.
`product-boot.log` records startup presentation `pass=1`, nine TE edges,
`TINYDRAW_VECTOR_V2_READY`, and 6,312 bytes of main-task stack headroom. No USB
mass-storage command was run.

## Fix

`svg_path_count()` uses the same gesture-continuation predicate as
`export_svg()`: adjacent chunks group only when their nonzero gesture ID, tool,
and color match. It validates the operation-log epoch, revision, and count
before returning its snapshot result.

The gate passes that logical count to its streaming SVG verifier. The existing
SVG grouping test now asserts both the encoded two-path output and the reported
two-path count. `esp32/main/vector_v2/vector_v2_export.h` also describes the
physical-gesture contract instead of the obsolete one-operation/one-path rule.

## Validation

- The test-first debug compile failed at
  `vector_v2/tests/svg_export_test.cpp:320` because `svg_path_count` did not yet
  exist (`host-red.log`).
- Host release: 29/29 CTest targets passed (`host-release.log`).
- Host ASan/UBSan: 11/11 targets passed (`host-asan.log`).
- Physical SVG encode/readback: `path_only=1 pass=1` (`gate-device.log`).
- Product restore: app version `c404456`, startup `pass=1`
  (`product-boot.log`).

## Commands

```sh
cmake --build --preset host-release
ctest --preset host-release --output-on-failure
cmake --build --preset host-asan
ctest --preset host-asan --output-on-failure
./scripts/esp32 vector-v2-gate-harness /dev/cu.usbmodem101 448 verify
./scripts/esp32 vector-v2 /dev/cu.usbmodem101
```
