# Vector V2 autosave/recovery receipt — 2026-08-17

## Verdict

**Software integration green; real drawn-document reset/recovery and the
autosave-enabled performance battery remain.**

The product firmware now persists Vector V2 authority and drawing-session state
in the 3 MiB `drawing` partition. Owner direction explicitly defers two-arena
compaction/metadata. The current journal never erases a committed Recovery
point to make capacity: it reports full instead.

## Persisted authority

One Journal commit records the resulting active/retained operation counts,
generation, epoch, complete zoom-return navigation, selected tool/size/palette
/color, and next nonzero Stroke identity. Checkpoint and whole-Stroke append
payloads contain exact painter-ordered operation metadata and compact samples.
Overview, tiles, settled pixels, replay indexes, and chrome pixels are rebuilt.

Relevant seams:

- portable format/recovery: `vector_v2/include/tinydraw/vector_v2/authority_journal.h`
- atomic authority restore: `vector_v2/include/tinydraw/vector_v2/operation_log.h`
- active-only overview replay: `vector_v2/include/tinydraw/vector_v2/incremental_document.h`
- ESP worker: `esp32/main/vector_v2/vector_v2_autosave_store.{h,cpp}`
- product wiring: `esp32/main/vector_v2/vector_v2_app.cpp`

## Interruption model

Each transaction occupies a whole number of 4 KiB sectors. Header and payload
are written first; the final 16-byte commit marker is a separate last flash
write. Startup validates explicit little-endian fields, header CRC, payload
CRC, whole-transaction CRC, sequence, counts, operation/sample semantics, and
whole-Stroke active boundaries before applying a transaction.

An interrupted tail starts at a sector boundary. Recovery retains the prior
complete transaction, erases only the tail on the background worker, and
replaces it with a complete checkpoint. Initial migration from non-V2 drawing
bytes erases the partition on that same low-priority worker before publishing
the first checkpoint.

## Host evidence

Focused portable fixtures:

```sh
./out/build/host-debug/vector_v2/tinydraw_vector_v2_tests \
  --test-case='authority journal*'
```

Result before product wiring: **7/7 cases, 3,003/3,003 assertions passed**.
Coverage includes:

- checkpoint with retained Redo;
- branch-after-Undo Stroke append;
- history, state, and New/reset replay;
- blank first checkpoint;
- every possible byte truncation in a later transaction;
- every single-byte corruption in a later header, payload, CRC, or marker;
- sector-aligned transaction publication.

Additional focused fixtures cover malformed atomic restore and exact
active-prefix overview replay.

Full host suite:

```sh
cmake --build --preset host-debug -j 6
ctest --preset host-debug --output-on-failure
```

Result: **29/29 targets passed** in 33.92 seconds (`/tmp/autosave-host-test.log`).

Sanitizer suite:

```sh
cmake --preset host-asan
cmake --build --preset host-asan -j 6
ctest --preset host-asan --output-on-failure
```

Result: **11/11 ASan/UBSan targets passed** in 76.76 seconds
(`/tmp/autosave-host-asan-test.log`).

## Firmware evidence

Production build:

```sh
cd esp32
eim run "idf.py -B '../out/build/esp32-vector-v2' build"
```

Result: success; `tinydraw_esp32.bin` is `0x1037c0` bytes with 32% of the
1.5 MiB app slot free (`/tmp/autosave-app-esp-build2.log`).

Normal firmware flash, without entering USB mass storage:

```sh
eim run "idf.py -B '../out/build/esp32-vector-v2' \
  -p /dev/cu.usbmodem1101 flash"
```

Result: bootloader, partition table, and 1,062,848-byte app image verified;
hard reset completed (`/tmp/autosave-flash.log`).

The first boot migrated the previous drawing-partition contents and published
blank checkpoint sequence 1. A serial-driven normal reset then produced:

```text
TINYDRAW_AUTOSAVE_RESTORE status=2 generation=0 active=0 retained=0 sequence=1
TINYDRAW_VECTOR_V2_READY ... free_psram=2278540 largest_psram=2228224 ...
```

Source: `/tmp/autosave-reset-serial.log`.

## Performance boundary

Flash erase, body writes, final-marker publication, and full readback execute on
a low-priority Core 0 worker. The main app copies/encodes the just-completed
Stroke after lift and records that cost as `stroke_logging_us`; no flash call
runs in the touch or visual ink path. Initial partition migration and rare
resynchronization checkpoint encoding remain to be measured on a nonempty real
document.

## Deliberate limitation

The minimum Journal commit consumes one 4 KiB sector. The 3 MiB partition
therefore holds at most 768 minimum-size commits; multi-sector long Strokes
reduce that count. When full, the adapter preserves all existing Recovery
points and blocks unsafe transitions rather than erasing them. Two-arena
compaction/metadata is deferred by owner direction.

## Remaining physical gate

1. Draw several pen and eraser Strokes at multiple zooms.
2. Undo at least one Stroke so a Redo tail exists.
3. Change zoom/origin, tool, size, and color; wait for autosave commits.
4. Reset normally, without entering mass storage.
5. Confirm exact active pixels, Redo restoration, camera/tool state, and a new
   Stroke remaining separate from the final restored Stroke.
6. Repeat once after Redo and once after New.
7. Capture `stroke_logging_us`, flash commit latency, PSRAM headroom, watchdog,
   ink, pan, cold, and settled-AA gates with autosave enabled.
