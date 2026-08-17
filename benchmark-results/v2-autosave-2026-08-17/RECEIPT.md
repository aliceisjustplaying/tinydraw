# Vector V2 autosave/recovery receipt — 2026-08-17

> Historical receipt. It records the first autosave implementation and its
> physical recovery run. The 2026-08-17 maintainability cleanup later narrowed
> the durable format to drawing authority only. Navigation, tool, palette, and
> color now restart from product defaults; the next Stroke identity is derived
> from restored active authority. Current policy lives in
> [`VECTOR_V2_AUTOSAVE_DESIGN.md`](../../VECTOR_V2_AUTOSAVE_DESIGN.md) and
> [`SHIP_CONTRACT.md`](../../SHIP_CONTRACT.md).

## Verdict

**Software integration and real drawn-document reset/recovery are green;
autosave-enabled performance joins the final optimization round.**

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

## Drawn-document hardware recovery

With serial capture active, the owner drew pen and eraser Strokes, Undid two
whole Strokes to retain a Redo tail, changed selections, zoomed, and panned. The
worker published sequences 2 through 13; per-lift authority encoding took
609–1,130 µs in the captured four-Stroke sample. The normal reset restored:

```text
TINYDRAW_AUTOSAVE_RESTORE status=2 generation=12 active=6 retained=10 sequence=13
TINYDRAW_LIVE_PRESENT kind=startup zoom=200 x=1288 y=1606 ...
```

The owner confirmed the drawing, zoom/pan position, selected state, and both
Redo actions on glass. A later normal reset then restored the resulting fully
active document and reached product ready:

```text
TINYDRAW_AUTOSAVE_RESTORE status=2 generation=52 active=14 retained=14 sequence=56
TINYDRAW_LIVE_PRESENT kind=startup zoom=25 x=0 y=0 ... pass=1
TINYDRAW_VECTOR_V2_READY ... largest_psram=2228224 ...
```

Sources: `/tmp/autosave-drawn-session.log`,
`/tmp/autosave-drawn-reset.log`, and `/tmp/autosave-after-redo-reset.log`.

The first scripted reset produced two consecutive USB-UART reset banners and
then three startup presentation TE timeouts. Two immediate repeat startups and
the post-Redo startup passed. This reset-storm presentation robustness case
remains tracked with power lifecycle work; authority recovery itself succeeded
on that attempt before presentation timed out.

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

## Remaining performance gate

Capture flash commit latency, long-document checkpoint cost, PSRAM headroom,
watchdog status, ink, pan, cold, and settled-AA gates with autosave enabled in
the final optimization round. Two-arena compaction/metadata remains deferred.
