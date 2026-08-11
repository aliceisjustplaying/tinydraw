# Vector Infinite Canvas — Phase 1 Findings (ESP32-S3)

> Phase 2 cached-pan/progressive-zoom prototype completed successfully. See
> `V2_PHASE2_PROTOTYPE_FINDINGS.md` for the physical results and updated verdict.

Status as of 2026-08-11, branch `feat/vector-rebuild-prototype`. Companion to
`V2_INITIAL_SPEC.md`. All numbers are from the physical ESP32-S3 at 240 MHz,
octal PSRAM at 80 MHz, rebuilding the full 368×448 RGB565 viewport.

## Verdict: conditional PASS

The vector infinite canvas is feasible on this hardware, **but only with
incremental tile rendering + a tile cache as architectural requirements**.
Full rebuild per interaction frame is permanently off the table on this
silicon; full rebuilds must be rare events (zoom jumps, undo, document load),
with pan served by strips/cache and zoom rendered progressively.

## Final benchmark matrix (dual-core renderer)

Times are full-viewport rebuild, milliseconds. Baseline = first successful
physical run (single core, per-stroke composite, unoptimized rasterizer).

| pattern | strokes | zoom | baseline | final | speedup |
|---|---|---|---|---|---|
| handwriting | 100 | 25% | 184 | **87** | 2.1× |
| handwriting | 100 | 100% | 489 | **191** | 2.6× |
| dense | 100 | 25% | 1,837 | **731** | 2.5× |
| dense | 100 | 100% | 10,226 | **2,675** | 3.8× |
| handwriting | 1,000 | 25% | 1,809 | **776** | 2.3× |
| handwriting | 1,000 | 100% | 4,737 | **1,777** | 2.7× |
| visible (short) | 1,000 | 25% | 532 | **111** | 4.8× |
| visible (short) | 1,000 | 100% | 1,118 | **292** | 3.8× |
| visible (short) | 5,000 | 25% | 2,627 | **516** | 5.1× |
| visible (short) | 5,000 | 100% | 5,543 | **1,428** | 3.9× |
| offscreen | 5,000 | any | 12.3 | **12.3** | culling only |

Zoom 50%/200% fall between the shown zoom levels. Full raw reports with phase
breakdowns in `/tmp/tinydraw-*.bin` capture logs (see "reading reports" below).

Unit costs (single-core measurements):

- Stroke culling: **2.5 µs/stroke** (5,000 offscreen strokes = 12.3 ms).
- Viewport clear: 9.2 ms constant (330 KB PSRAM fill, ~36 MB/s).
- Tile composite (PSRAM 4 KB round trip): was ~285 µs/visit-tile, now paid
  once per touched tile per batch and restricted to dirty rects.
- Rasterization: 84–267 µs per primitive-tile visit (dominant cost).
- Geometry: ~15–25 µs/sample (CurvedRibbonStream + arena writes).

## What dominates runtime (phase instrumentation)

Cycle-accurate phase counters (clear/geometry/rasterize/composite) were added
to `ViewportRenderer` via an injected tick callback. Findings, in order of
discovery and fix:

1. **Composite traffic** (was 23–50%): per-stroke-per-tile PSRAM round trips,
   ~285 µs each, up to 9,663 per rebuild. Fixed by tile-major batching.
2. **Rasterization** (now 60–80%): the 4×4 supersampled coverage rasterizer.
   Partially fixed (interior fast path, hoisted software float division);
   remaining headroom exists (see "remaining levers").
3. **Geometry** (~10–25%): ribbon reconstruction per sample. Halved by
   skipping provisional (display-tail) geometry that offline replay discards.

## Memory (the decisive argument for the architecture)

- 5,000-stroke / 15,000-sample document: **320 KB** PSRAM.
- Benchmark arenas + renderer scratch: ~490 KB total during rebuild.
- PSRAM free held at exactly 285,032 bytes through every run; minimum ==
  final on all 24 cases → zero leaks, zero fragmentation.
- The raster architecture this replaces (3×3 WorldCanvas 2.83 MiB + raster
  undo 3.28 MiB) frees **6.1 MiB** — enough to cache ~9 viewports of rendered
  tiles. The old architecture funds its replacement's pan cache.

## Spec deliverable-10 answers

- **Fast enough?** For rare full rebuilds with progressive display: yes.
  As a per-frame operation: no, and it never will be (~2× more headroom
  exists; the gap to 30 ms is ~25×).
- **What dominates?** Rasterization (supersampled coverage), then geometry.
  Traffic and composite are tamed.
- **Vector memory cost?** ~21 bytes/sample unpacked; a heavy document is
  ~2–5% of the PSRAM the raster architecture uses.
- **Uncomfortable density?** >~1,000 long/dense curved strokes intersecting
  the viewport at ≥100% zoom (2.7 s worst measured). Needs progressive
  rendering + LOD, not a redesign.
- **Spatial index?** Not below ~50k strokes; linear cull is 2.5 µs/stroke.
- **Pan cache?** Yes — required. Fund with freed WorldCanvas memory.
- **LOD/simplification?** Yes at low zoom (also resolves the subpixel
  visibility question in spec §5; `minimum_screen_radius` option exists).
- **Safe to plan WorldCanvas/raster-undo removal?** Yes, contingent on
  strip-pan + tile cache + progressive zoom landing first.

## Optimization rounds (all bit-exact, all committed)

Golden snapshot tests never changed; every round verified `./scripts/dev
test` (22/22) before flashing.

1. **Tile-major compositing** (`perf: composite viewport rebuilds
   tile-major`): strokes stream through the scratch arena in document-order
   batches (1,536-primitive capacity), counting-sorted into per-tile bins;
   each touched tile costs one PSRAM read + write per batch. Ordering per
   tile preserved → bit-identical, eraser-safe.
2. **Software-float hoisting** (`perf: hoist software float division and
   sqrt...`): S3 FPU has no divide/sqrt instruction. One reciprocal per
   convex edge per primitive (was: one divide per edge per sample row),
   precomputed squared deltas for circle sampling, `std::hypot` → sqrt of
   squared length in ribbon `unit()`.
3. **Dirty-rect coverage** (`perf: track coverage dirty rects...`):
   `CoverageTile` tracks nonzero-coverage bounds; blend and clear scan only
   that rect. `StrokeRaster::load_coverage_tile` must call `mark_dirty`
   (writes rows directly) — documented invariant on `row()`.
4. **Provisional-skip + dual core** (this round): `CurvedRibbonStream::
   append(point, provisional_needed=false)` skips the replaceable display
   tail during offline replay (committed geometry identical).
   `ViewportRenderOptions.execute` runs tile compositing on two lanes;
   tiles are partitioned by lane so output is order-preserving and
   bit-identical. On device, lane 1 runs on core 1 at **priority 1 —
   strictly below the touch task (priority 5, core 1)** so input capture is
   never delayed. Geometry stays sequential on core 0.

## Diagnosis war stories (read before touching the benchmark)

- **The benchmark reboot loop was a stack overflow, not a watchdog.** The
  ribbon path carries ~1 KiB `RibbonUpdate` frames through
  `render → finish → append`; curved strokes (`emit_quadratic`) overflowed
  the 6 KiB main-task stack. Handwriting was the first pattern to take that
  path (offscreen is culled; 2-sample strokes take a degenerate path).
  The benchmark now runs in a dedicated 16 KiB-stack task. **Production
  integration must budget stack for any task calling the renderer.**
- **Flash coredump infrastructure is installed and proven.** `espcoredump`
  is in the build, a 64 KB `coredump` partition was appended (existing
  partition offsets unchanged), and `idf.py coredump-info` decodes panics
  offline. This converted an opaque reset into an exact file:line once and
  will again.
- **Serial monitoring on this board is a trap.** Opening the port with
  default DTR/RTS drives GPIO0/EN and can wedge the chip into ROM download
  mode (black screen, `waiting for download`). Recovery: hold the lower
  power button ~4 s, then short-press (battery keeps the state alive
  otherwise). **Do not monitor; use the flash-report channel.**
- `RibbonPrimitiveBatch` capacity is exactly 8 and worst-case `finish()` is
  exactly 8 (fuzzed 6M cases; comment in header undercounts by omitting the
  sharp-turn circle). Knife-edge but not currently reachable at 9.

## Benchmark workflow (no serial monitor needed)

- Build+flash: `./scripts/esp32 vector-benchmark /dev/cu.usbmodem101`
  (or `idf.py -B out/build/esp32-vector-benchmark -DTINYDRAW_VECTOR_BENCHMARK=ON build` + `flash`).
- Red toolbar dot = running; dot gone = complete (~60 s full matrix).
- Results persist to the last 8 KiB of the `export` partition after every
  case (crash-safe): read with
  `python -m esptool --chip esp32s3 -p PORT read-flash 0x90e000 0x2000 out.bin`
  (text, NUL-terminated). Header records `esp_reset_reason()` of the boot.
- Reading flash resets the device and restarts the benchmark — never read
  mid-run.
- Restore normal firmware: `./scripts/esp32 build` then flash from
  `out/build/esp32`. Drawing/export partitions are never disturbed.
- During the benchmark the screen may look black/garbled (it renders into
  the visible buffer); the normal UI returns when the run completes.

## Remaining levers (est. combined ~2×, not yet done)

1. Scanline interior via difference array (spans add ±N at ends, one prefix
   pass) — biggest remaining raster win for fat strokes.
2. LOD at low zoom: skip strokes with subpixel screen radius (kills the
   dense@25% case), cluster-collapse for distant content.
3. Geometry cache: persist projected primitives per stroke across rebuilds
   at fixed zoom (pan case) — eliminates the ~280 ms geometry phase.
4. Reduced supersampling (4×4 → 2×2) for rebuild-only rendering — quality
   tradeoff, decide with screenshots.
5. Second-core geometry overlap (pipeline batch N+1 geometry against batch
   N compositing).

## Phase 2 architecture (per verdict)

```
VectorDocument (PSRAM, arena-backed)
  → linear cull (no index yet)
  → ViewportRenderer, tile-major, dual-lane
      → tile cache (~6 MiB freed by WorldCanvas removal)
      → pan = shift + render exposed strips
      → zoom = scaled preview, then progressive tile refinement
      → live drawing stays on the existing StrokeRaster path
```

Undo becomes operation-log replay (strokes/eraser-strokes/clear markers),
`New` becomes a generation marker, eraser stays an ordered stroke.

## Known loose ends

- `strokes_tested` stat counts a stroke twice when a batch flush forces a
  retry (cosmetic; totals like `n=1015` in reports).
- Benchmark build binary is at 95% of the 1 MB factory app partition.
- `tinydraw_ntp` task shares core 1 / priority 1 with the render worker —
  fine for the benchmark (starts later), revisit when integrating.
- Phase tick sums exceed wall time under dual-core (two counters in
  parallel); `elapsed_us` is wall truth.
- RP2350 target is archived/abandonware; not part of any regression matrix.
