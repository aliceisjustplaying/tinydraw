# TinyDraw project state

Last updated: 2026-08-14

## Resume point

Branch: `main`

**The Vector V2 foundation is validated.** Vector V2 is now the accepted
application architecture under construction, not another prototype and not yet
the shipping/default firmware. The existing shipping code is **Raster V1**.
The full remaining worklist and feature-complete definition live in
[`V2_ROADMAP.md`](V2_ROADMAP.md).

The raster application remains the default firmware and must remain runnable
until V2 reaches feature parity and passes migration acceptance. V2 now has an
explicit `vector-v2` hardware target; the historical Gate 1 workload runs only
under `vector-v2-gate-harness`. V2 is not yet the default or feature complete.

## What is proven

On the physical ESP32-S3 with the deterministic seed-7 1,000-stroke document:

- vector operations are authoritative;
- pen and eraser operations, painter order, and revisions remain correct;
- live drawing is responsive at 400%;
- 25%, 50%, 100%, 200%, and 400% identities exist;
- the complete 25% overview and sparse world-aligned detail path work;
- cache misses use current overview fallback rather than checkerboards;
- paper-aware materialization lets the complete 100% world fit as 266 raw tiles
  plus 378 compact uniform identities, with zero fallback;
- the raw pool uses 384 slots; a 16-stop 400% tour A/B measured zero return-trip
  refills at 384 versus 63 tiles and 409 ms of refill at 320;
- ordinary cached pan reuses framebuffer overlap and reaches first physical
  completion in about 30.6–30.7 ms;
- drawing while refinement is active works without stale publication;
- a separate contiguous 1.5 MiB export reserve allocates alongside the live
  document, cache, and workspaces;
- the final aggressive performance glass test committed 45 strokes with zero
  touch errors, presentation failures, authority mismatch, or corruption;
- the production toolbar now reflects the active V1-derived tool icon and uses
  compact tool, size, document, and round-swatch color popups;
- New requires confirmation, and Export shows band-by-band progress plus a
  visible saved state before USB takes over;
- battery status, the 25–400% zoom rail, and a live noninteractive minimap with
  a viewport rectangle render as fixed canvas overlays;
- physical UI checks confirmed reliable minimap updates and no remaining
  cached-pan ghosts around the zoom rail or minimap;
- the deterministic live-overlay circle gate measures 2.940 ms clear versus
  2.928 ms over the overlays, with zero chrome redraw work or failures;
- independent Fable high and Grok 4.6 high reviews found no remaining blocker
  after their findings were fixed.

Evidence:

1. [`vector_v2/GATE_1_RECEIPT_2026_08_13.md`](vector_v2/GATE_1_RECEIPT_2026_08_13.md)
2. [`vector_v2/GATE_1_CACHE_CLOSURE_2026_08_13.md`](vector_v2/GATE_1_CACHE_CLOSURE_2026_08_13.md)
3. [`vector_v2/hardware-receipts/gate1-paper-cache-scroller.log`](vector_v2/hardware-receipts/gate1-paper-cache-scroller.log)
4. [`vector_v2/hardware-receipts/gate1-final-glass.log`](vector_v2/hardware-receipts/gate1-final-glass.log)
5. [`vector_v2/hardware-receipts/PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md`](vector_v2/hardware-receipts/PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md)

The overall rendering-quality verdict remains **YELLOW** because settled
anti-aliasing is still open. The four-sample SSAA probe took about 808 ms and is
not the funded product path.

## Current measured debt

Phase 0 of the second performance round (`205fefe`,
[`vector_v2/hardware-receipts/PERF_ROUND_2_BASELINES_2026_08_14.md`](vector_v2/hardware-receipts/PERF_ROUND_2_BASELINES_2026_08_14.md))
replaced the manual regression reports with deterministic receipts:

- drawing with a warm multi-zoom cache breaches the 15 ms chunk alarm at
  every zoom: `TINYDRAW_GATE1_MIXED_DRAW` measures 130 ms worst at 25%,
  58 ms at 100%, and 20.5 ms even at 400%, with 700–960 resident tiles
  painted per stroke and zero fallback — the in-place policy paints every
  intersecting resident raw tile at every zoom;
- the 320-versus-384 mixed draw/pan A/B is settled: identical drawing
  latency at both counts, and 384 keeps its 63-tile/411 ms return-trip
  retention win, so the drawing fix is a mutation-policy change and the
  384-slot pool stays;
- warm pan is now about 67 ms per frame (≈15 FPS) with attribution
  15.1 ms scroll memmove + 7.3 ms exposed compose + 8.5 ms tear wait +
  19.7 ms present, plus the UI round's per-frame minimap chrome; the
  single-frame pan gates remain red at about 40.4 ms first-submit with
  7.8 ms chrome;
- the fresh cold 20-run distribution matches the accepted receipt
  (adversarial 400% p95 638 ms); the round target is −50% at every gated
  corpus and zoom;
- the export PNG task-watchdog warning reproduced in both full-gate
  captures and remains open reliability debt;
- anti-aliasing, persistence, Undo/Redo, autosave, low-power lifecycle
  parity, interactive minimap behavior, and final tap-target polish remain
  incomplete;
- SVG export still needs an eraser mask/compositing design; the encoder must
  budget inside the existing contiguous 1.5 MiB export reserve.

These are product and optimization tasks, not evidence for another rewrite.

## Immediate next sequence

1. Fix drawing latency by changing the in-place mutation policy so chunk
   commits never pay cross-zoom resident-tile fanout; acceptance is
   `TINYDRAW_GATE1_MIXED_DRAW` green (10–12 ms chunks, alarm 15 ms) at every
   zoom and both slot counts, with the gate's revisit lines pricing the
   retention cost. The gate then joins the battery's final verdict.
2. Raise warm pan to a 30 FPS floor: ring/offset frame addressing (−15 ms
   memmove), tear-wait/compose overlap, and chrome off the per-frame path;
   acceptance is `TINYDRAW_GATE1_PANSEQ` at or below 33.3 ms per frame.
3. Run the cold −50% campaign from the fresh distribution.
4. Capture a clean hardware export receipt proving watchdog-safe encoding;
   reliability matters more than export speed.
5. Investigate SVG eraser semantics, then continue through anti-aliasing,
   persistence, Undo/Redo, lifecycle parity, and remaining UI polish.

## Repository and coexistence decision

Use a Vector V2 island in this repository—a strangler migration, not a blank
rewrite:

- platform-independent V2 modules live in `vector_v2/`;
- V2 ESP adapters and coordination stay separate from legacy `hardware_app.cpp`;
- stable platform-neutral mechanisms are reused from `core/` by dependency and
  are never copied;
- V1 receives only regression/hardware-preservation work while new product
  behavior lands in V2;
- both firmware variants must build and remain testable until promotion;
- the default switches only after V2 feature parity and final comparison;
- retain an explicit legacy raster build after promotion.

Do not mix V2 tile/document state into `WorldCanvas`, `FirmwareCanvas`, or the
3×3 raster coordinator. Do not add V2 `#ifdef` paths to the V1 interaction loop.

## Target architecture

```text
Vector operation log (authoritative)
        │
        ├── complete 368×448 RGB565 overview at 25%
        └── sparse world-aligned materialization at 50–400%
              ├── compact uniform/paper catalog
              └── 384 raw 64×64 RGB565 slots
                    │
             MaterializedCanvas
                    │
             DisplayScheduler + framebuffer reuse
                    │
                  AMOLED
```

Committed geometry:

- world: 1472×1792 units;
- zoom levels: 25%, 50%, 100%, 200%, and 400%;
- 800% is a stretch goal only;
- overview: 368×448 RGB565, 329,728 bytes.

## Nonnegotiable guardrails

- No clean-room rewrite.
- No camera-aligned 3×3 atlas development.
- No V2 state in `hardware_app.cpp`, `WorldCanvas`, or `FirmwareCanvas`.
- No four separately stored simplified stroke versions.
- No hidden allocation in Vector V2 state modules.
- No speculative second-core concurrency without a measured independent need.
- No broad cosmetic V1 refactor before a real replacement seam exists.
- No deleting V1 before V2 feature parity and promotion.
- No new overlapping handoff/source-of-truth documents: update this file and
  `V2_ROADMAP.md`.

## Validation baseline

For production changes:

```sh
./scripts/dev test
./scripts/dev release
./scripts/dev asan
./scripts/dev format-check
./scripts/dev tidy
./scripts/dev cppcheck
git diff --check
```

ESP integration must also build both raster V1 and vector V2 targets. Physical
hardware remains authoritative for touch latency, PSRAM/fragmentation, DMA,
panel behavior, power, and USB export.

## Historical evidence

Prototype decisions and rejected mechanisms remain in:

- [`docs/archive/2026-08-raster-and-vector-prototypes/PROTOTYPE_EXIT.md`](docs/archive/2026-08-raster-and-vector-prototypes/PROTOTYPE_EXIT.md)
- [`docs/archive/2026-08-raster-and-vector-prototypes/SECOND_REVIEW_ARCHITECTURE_ASSESSMENT.md`](docs/archive/2026-08-raster-and-vector-prototypes/SECOND_REVIEW_ARCHITECTURE_ASSESSMENT.md)
- [`docs/archive/2026-08-raster-and-vector-prototypes/VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md`](docs/archive/2026-08-raster-and-vector-prototypes/VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md)
- [`docs/archive/2026-08-vector-v2-foundation/`](docs/archive/2026-08-vector-v2-foundation/)

Prototype-era handoffs and Gate 1 plans are historical inputs. They no longer
override the V2 roadmap.
