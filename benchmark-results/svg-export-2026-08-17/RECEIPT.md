# Detailed SVG export receipt — 2026-08-17

## Verdict

**PASS on the attached ESP32-S3.** Vector V2 now exports the operation authority
directly as SVG: each pen or eraser operation becomes one painter-ordered,
filled `<path>`. The path contains the same round caps and variable-width
convex ribbon geometry used by the renderer. It is not a centerline stroke and
contains no raster image, `<circle>`, or `stroke-width` fallback.

The production encoder streams transactionally into the export partition with
a 4 KiB workspace, commits metadata last, then exposes `DRAWING.SVG` through
the existing read-only FAT/USB adapter. Coordinates use four SVG decimal
places, bounding geometry-formatting error to 0.00005 world pixels.

## Physical-device result

Clean final gate line from [`svg-final-device.log`](svg-final-device.log):

```text
TINYDRAW_GATE1_EXPORT format=svg encoded=1 bytes=157660 elapsed_us=1023846 workspace_bytes=4096 operations=52 sink_calls=155 flash_pages=39 crc32=140c70b9 free_psram=1818936 free_internal=180704 prolog=1 dimensions=1 terminator=1 crc_ok=1 paths=52 path_only=1 pass=1
```

The gate reads every stored byte back from flash and verifies:

- XML prolog and 1472×1792 dimensions;
- `</svg>` terminator;
- full-file CRC against the streaming encoder CRC;
- exactly one `<path>` per operation (52/52);
- no `<circle>` or `stroke-width` representation.

The capture ended on `TINYDRAW_GATE1_EXPORT` with no `task_wdt`, Guru
Meditation, or `TG1WDT_SYS_RST` marker. The verifier uses a 512-byte streaming
window so it fits the product main-task stack.

## Speed comparison

The prior full-world PNG encoder line in
[`png-baseline-device.log`](png-baseline-device.log) reports 5,829,264 µs and
a 50,652-byte workspace plus a 376,832-byte band allocation. The final SVG
line reports 1,023,846 µs and a 4,096-byte workspace.

- elapsed-time reduction: **82.4%**;
- speedup: **5.69×**;
- transient encoder workspace reduction: 50,652 → 4,096 bytes, with no raster
  band allocation in the V2 firmware target.

## Commands

```sh
cmake --build out/build/host-release -j8
./out/build/host-release/vector_v2/tinydraw_vector_v2_tests \
  --test-case='*SVG export*,exported primitive coverage*,hundreds of random*,maximum-capacity authority*'
./out/build/host-release/tests/tinydraw_tests --test-case='FAT16 export disk*'

cd esp32
eim run "idf.py -B '$PWD/../out/build/esp32-vector-v2-gate-harness' build"
eim run "idf.py -B '$PWD/../out/build/esp32-vector-v2-gate-harness' -p /dev/cu.usbmodem101 flash"
cd ..
uv run --script tools/esp32-capture.py /dev/cu.usbmodem101 \
  /tmp/svg-final-device.log 120 \
  --end-marker TINYDRAW_GATE1_EXPORT \
  --failure-regex 'task_wdt|Guru Meditation|TG1WDT_SYS_RST'
```

The serial gate intentionally calls encode/readback only. It does **not** call
`present_usb()`, so this receipt did not switch the unattended device into USB
mass-storage mode. Physical host mounting/ejection of `DRAWING.SVG` remains a
manual follow-up; FAT filename behavior is covered by host tests.
