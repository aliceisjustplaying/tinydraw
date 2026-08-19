# TinyDraw Vector V2 external review brief

Date: 2026-08-18

The product author's message accompanying this packet controls the review. If
anything here conflicts with that message, follow the author's message. This
file supplies measurements, code entry points, and constraints for that broad
review.

## Scope and ambition

TinyDraw runs on the V2 hardware revision of the Waveshare ESP32-S3 Touch
AMOLED 1.8 board:

<https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm>

The review is about the Vector V2 drawing application. Raster V1, the macOS
host, and QEMU remain in the repository as supported targets and useful
references.

The V2 feature set is essentially complete. The remaining job is to push this
hardware as far as it can sensibly go, fix the known bugs, and look for
correctness problems we have missed. The working words are mechanical
sympathy, elegance, and demoscene mindset.

Treat every layer as reviewable: algorithms, representations, memory layout,
cache policy, task scheduling, panel traffic, build layout, and module
boundaries. Large changes are welcome when their expected benefit justifies
their risk. If a rewrite, second-core design, different cache shape, or new
rendering representation is the best answer, explain it and price it. Existing
architecture supplies measured evidence and remains open to challenge.

There is no finding limit. Report every worthwhile opportunity you can support
from the code or the supplied measurements.

## What the author wants improved

Performance is the main event:

- make cold rendering much faster at every zoom;
- reduce how often cold rendering happens at all;
- stop Undo and Redo from exposing a cold rebuild;
- speed up settled anti-aliasing and make its progression less visible;
- improve finger-on-glass ink feel where the measured pipeline permits it;
- remove long uninterruptible presentation and background-work stalls;
- find performance opportunities the current instrumentation has missed.

The author would like cold rendering near half the current time if the hardware
can do it. Please say whether roughly 250 ms is plausible, what would have to
change, and which measurements would settle the question.

Known product work includes transparent SVG erasing, visible save/storage
failure states, touch-target feedback, physical export validation, and release
soak. Check for additional bugs and correctness risks across the whole V2 path.

## Current state, without spin

### Cold rendering

The renderer has moved from a 1,269.157 ms general 400% baseline to roughly
500 ms. The best treated 400% receipt is 496.693 ms. The latest full 448-slot
gate on this cleanup line measured:

| Zoom | General cold wall |
|---:|---:|
| 50% | 421.787 ms |
| 100% | 399.498 ms |
| 200% | 464.071 ms |
| 400% | 515.123 ms |

The 400% firmware guard is temporarily 520 ms; the release requirement remains
500 ms. The final 20-run normal-product distribution with real journal writes
has not been recorded. The project is therefore around 500 ms, with one current
measurement above it, and still has plenty of room for a stronger result.

The separate overlap-heavy 50% corpus fell from 585.821 ms to 476.969 ms after
the rasterizer began refreshing each chord's finalized-pixel window inside the
chord's own bounds. Compute fell from 496.256 to 384.393 ms and producer steps
fell from 235 to 90.

### Cache reuse and the déjà-vu problem

The main deterministic revisit failure is fixed. A mixed-draw tour previously
missed 4, 9, and 16 tiles at 50%, 100%, and 200%, causing 188 to 326 ms visible
refills. The accepted gate now reports zero missing tiles and about 0.38 ms on
each revisit. Pure-revisit amplification is 1.000.

The author still sees occasional stray rerenders. Known possible causes are slot
eviction, off-view XL-stroke work dropped by the idle budget, and the pending
range high-water fallback. The live rerender ledger currently exists only in
the gate build, so ordinary product glass sessions cannot attribute those
events.

### Ink

The measured pipeline is fast, but finger-on-glass feel can still lag. A
controlled physical stroke observed a 12 to 14 ms touch-controller cadence.
Product submission averaged 1.527 ms; DMA completion averaged 2.353 ms and
maxed at 3.810 ms. The five recorded traces lose no Down or Up events.

Committed-operation overlay work reduced the worst interactive chunk append
from 19.324 ms to 0.173 ms. The old 87 to 199 ms lift hitch fell to about 4 to
5 ms in the accepted path. The provisional visual tail reaches the raw clipped
touch point while committed authority stays filtered. Even with those numbers,
the author's intermittent lag report is real review scope. Look at sampling,
filtering, geometry, scheduling, TE wait, panel completion, and long work that
can begin just before a Down event.

### Undo and Redo

Whole-Stroke Undo and Redo are correct in host tests and work on glass. Their
high-zoom presentation behavior is bad. The current path rebuilds the affected
overview from active vector authority, invalidates intersecting detail tiles,
and immediately refreshes the visible damage. That exposes cold detail work.

There is no dedicated device history-latency benchmark yet. We need both the
measurement and a design that avoids needless rerendering.

### Settled anti-aliasing

AA appearance and cache correctness are accepted. Performance is open. Earlier
tiled measurements improved from 5 to 11 ms mean and 17 ms maximum to 1.7 to
5.4 ms mean and 9.3 ms maximum. Those numbers do not cover every current case.

The latest gate settled the 25% view in 42 windows and 152.945 ms total. One
window took 76.416 ms. `run_settle()` claims an 8 ms slice but checks the budget
only between complete windows, so that work is uninterruptible. The harness
still prints `ssaa_receipt=yellow`.

### Presentation stalls

Existing receipts attribute a 166 to 184 ms `poll_max` class to one-shot dense
refreshes during zoom, pan fallback, power/chrome changes, and drain swaps. A
Down arriving during one such stall reached 66 ms event age. Drain-swap refresh
was the largest clearly sliceable contributor at up to 79.5 ms. This is input
responsiveness work even when cold compute itself is unchanged.

## Hardware and current memory shape

- ESP32-S3, 240 MHz, dual LX7 cores.
- 8 MiB octal PSRAM at 80 MHz.
- 368 by 448 RGB565 CO5300 panel.
- Effective panel clock: 40 MHz, 10 Mpixel/s, 20 MB/s.
- TE period: about 16.773 ms.
- A 448-row stream sustains about 29.4 FPS.
- The vector world is 1472 by 1792 units at 25%, 50%, 100%, 200%, and 400%.
- Current detail cache: 448 raw 64 by 64 RGB565 tiles plus compact uniform and
  paper identities.
- Latest gate before export reservation: 2,282,124 bytes free PSRAM, largest
  block 2,228,224 bytes.
- Holding the current 1.5 MiB export reserve leaves 709,256 bytes free and a
  704,512-byte largest block.
- Latest normal-product main-task stack headroom: 8,712 bytes.
- Latest gate main-task stack headroom: 6,248 bytes on its 20 KiB stack.

These figures describe the current allocation. Proposals may reallocate it and
should state the RAM, IRAM, stack, flash, DMA-capable memory, or cache cost and
what current allocation they replace.

## Current architecture

The durable V2 drawing is an ordered operation log over a blank baseline. One
physical Stroke can contain several bounded chunks sharing a nonzero
`gesture_id`. The active prefix is the Undo/Redo cursor; the retained tail holds
Redo. Generation and epoch values reject stale work.

Pixels are derived:

```text
ordered vector operations
        |
        +-- complete hard-edged 368x448 overview at 25%
        |
        +-- sparse world-aligned detail at 50% to 400%
              +-- compact paper/uniform identities
              +-- 448 raw 64x64 tiles
                    |
             canvas-only toroidal frame ring
                    |
             bounded DMA staging + transient ink/chrome
                    |
                 CO5300 panel
```

Interactive chunks publish authority first. The canvas absorbs pending
operations in idle slices, while a committed-operation overlay keeps the
visible result exact. Detail production is resumable and uses fixed caller-owned
workspaces. Settled AA is a derived quality tier under the same revision.

Autosave journals retained operations, active prefix, generation, and epoch.
Navigation and chrome state restart from defaults. The next Stroke identity is
derived from restored active authority.

This model has strong correctness evidence, but you may propose a different
internal design. Preserve the user-visible document, Undo/Redo, recovery, and
export semantics, or describe a safe migration.

## Code map for the known hotspots

### Undo/Redo cold rebuild

- `vector_v2/src/incremental_document.cpp`: `move_history_incrementally()`
- `vector_v2/src/operation_log.cpp`: history preparation and publication
- `vector_v2/src/materialized_canvas.cpp`: revision commit and invalidation
- `esp32/main/vector_v2/vector_v2_chrome_controller.cpp`: history action and
  immediate refresh
- `vector_v2/src/tile_producer.cpp`

Check incremental or reversible detail transitions, retained before/after
derivatives, visible-first reconstruction, spatial indexing, and any better
representation you can justify. Cover erasers, overlap, multi-chunk Strokes,
branch replacement, failure atomicity, and cancellation.

### AA progression and export

- `vector_v2/src/settled_tile.cpp`: per-window operation scan, coverage union,
  boundary `sqrt`, saturation, and RGB565 fold
- `esp32/main/vector_v2/vector_v2_background_pipeline.cpp`: `run_settle()`
- `vector_v2/src/materialized_canvas.cpp`: quality publication
- `esp32/main/vector_v2/vector_v2_app_storage.cpp`: AA workspaces
- `vector_v2/src/world_export.cpp`
- `esp32/main/vector_v2/vector_v2_export.cpp`

Look for work reuse, spatial operation indexes, prepared geometry reuse,
resumable windows, dirty-span folding, boundary-only representations, cheaper
distance math, and better visible-work priority. Preserve self-overlap union,
painter order, settled-cache identity, and PNG/device pixel agreement unless
you can demonstrate a better contract.

### Cold renderer

- `vector_v2/src/tile_producer.cpp`
- `vector_v2/src/incremental_rasterizer.cpp`
- `vector_v2/src/materialized_canvas.cpp`
- `vector_v2/tools/raster_census.cpp`
- `esp32/main/vector_v2/vector_v2_presenter.cpp`
- `esp32/main/vector_v2/vector_v2_ship_contract.h`

The performance chronicle and cold-campaign handover contain both successful
changes and measured failures. Use them to avoid repeating dead experiments,
then look beyond their candidate list.

### Revisit retention

- `vector_v2/src/incremental_document.cpp`: all-zoom retention
- `vector_v2/src/idle_repair.cpp`
- `vector_v2/src/materialized_canvas.cpp`: recent views and eviction
- `vector_v2/src/rerender_ledger.cpp`
- `esp32/main/vector_v2/vector_v2_background_pipeline.cpp`

### Ink and presentation latency

- `esp32/main/vector_v2/vector_v2_live_stroke_session.cpp`
- `core/src/ink_stream.cpp`
- `vector_v2/src/chained_operation_builder.cpp`
- `vector_v2/src/operation_builder.cpp`
- `esp32/main/vector_v2/vector_v2_touch_sampler.cpp`
- `esp32/main/vector_v2/vector_v2_presenter.cpp`
- `esp32/main/co5300_panel_transport.cpp`
- `esp32/main/vector_v2/vector_v2_background_pipeline.cpp`

### Persistence and export correctness

- `vector_v2/src/authority_journal.cpp`
- `esp32/main/vector_v2/vector_v2_autosave_store.cpp`
- `vector_v2/src/svg_export.cpp`
- `esp32/main/vector_v2/svg_export_store.cpp`
- `esp32/main/vector_v2/vector_v2_export.cpp`
- `esp32/main/usb_export.cpp`

SVG eraser Strokes currently render as opaque white paths. They need true
cutout semantics. The journal also lacks compaction and eventually reports full;
the UI does not yet expose every capacity or write failure clearly.

## Correctness that current changes must keep proving

- Pen and eraser painter order agrees across display, Undo/Redo, SVG, PNG,
  autosave recovery, and cold replay.
- A failed or canceled authority/history transition leaves the published state
  unchanged.
- Local damage keeps unrelated cached detail valid.
- Settled pixels never outlive their authority revision or cause sharp-to-blurry
  revisit cycling.
- Touch sampling does not lose Down or Up while renderer work is active.
- Raster V1, Vector V2, macOS, QEMU, and the retained ESP32 variants continue to
  build.

These are behavioral requirements. Their current implementations are open to
improvement.

## Known bugs and unfinished release work

- transparent SVG eraser semantics;
- visible autosave, journal-full, storage, export, and hardware failure UI;
- physical SVG+PNG mount/read/eject/Return-to-Drawing/remount receipt;
- touch target review and pressed feedback;
- reset-storm startup presentation timeout investigation;
- long-document, capacity, cache-pressure, and 25% characterization;
- optical ink latency p95/p99 closure with a positive control;
- pan torn positive-control archive;
- multi-hour draw/pan/Undo/Redo/autosave/export/power soak.

Find and report anything else.

## Evidence worth reading

Start here:

1. `PROJECT_STATE.md` for the current scorecard.
2. `SHIP_CONTRACT.md` for product thresholds and author decisions.
3. `docs/PERFORMANCE_CHRONICLE.md` for the readable speed ledger.
4. `docs/archive/2026-08-code-reviews/review-findings/2026-08-16-cold-campaign/HANDOVER.md` sections 4 and 5 for
   the exhaustive cold-render experiments that did not work.
5. `benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md` for
   the device-side cold campaign.
6. `benchmark-results/overlap-cold-fix-2026-08-17/RECEIPT.md` for the latest
   full battery and overlap fix.
7. `benchmark-results/committed-overlay/RECEIPT.md` and
   `benchmark-results/committed-overlay/DEJAVU_FIX_RECEIPT.md` for interactive
   commit and revisit retention.
8. `VECTOR_V2_AUTHORITY_UNDO_DESIGN.md`,
   `docs/design/VECTOR_V2_COMMITTED_OVERLAY_DESIGN.md`, and
   `docs/design/VECTOR_V2_AUTOSAVE_DESIGN.md` for current authority transitions.

Historical receipts describe the tree that produced them. Current state and
the source snapshot win when prose disagrees.

## Current verification and known analysis debt

The packet snapshot passed:

- host debug 31/31;
- host release 31/31;
- ASan/UBSan 13/13;
- Vector V2 product, Raster V1, 448-slot gate, QEMU, and macOS builds;
- QEMU replay;
- the physical 448-slot gate with every required pass flag green; AA progression
  remains the explicit yellow receipt;
- normal-product flash, boot, and a cursory drawing sanity check.

The physical gate restores the autosave store but does not issue journal writes.
It does not close normal-product contention or the 20-run cold distribution.

Current quality tools also expose review material:

- `authority_journal.cpp` fails clang-tidy complexity limits:
  `validate_payload()` scores 40, and `recover_authority_journal()` scores 50
  against a threshold of 20;
- cppcheck reports possible aggregate initialization issues in
  `materialized_canvas.h` and `incremental_document.cpp`, plus synchronous
  stability checks in `svg_export.cpp` that it considers constant;
- the whole-tree format check finds one existing Raster V1 violation at
  `esp32/main/firmware_canvas.h:28`; changed Vector V2 files are formatted.

Complete tool output is in `provenance/`.

## What a useful response contains

Rank findings by expected user-visible benefit, confidence, cost, and risk.
For each substantial performance idea, give:

- exact file and symbol references;
- the work it removes or makes reusable;
- a rough speedup or latency prediction when the evidence supports one;
- RAM, IRAM, stack, flash, cache, and panel-traffic cost;
- the smallest experiment that can falsify it;
- the correctness and hardware gates that should be rerun.

Separate conclusions proven by code inspection from hypotheses that need the
physical board. Call out attractive ideas that conflict with measured hardware
facts. Suggest new instrumentation when the current counters cannot answer the
question.

Please include architectural simplifications, maintainability problems, and
gold plating when they matter. Performance-motivated complexity is welcome
when the device evidence pays for it. End with the first few changes you would
try and the measurements that decide whether each survives.
