# TinyDraw project state

Last updated: 2026-08-13

## Resume point

Branch: `feat/vector-canvas-production`

**The production vector foundation is validated.** TinyDraw V2 is now the
accepted application architecture under construction, not another prototype.
The full remaining worklist and feature-complete definition live in
[`V2_ROADMAP.md`](V2_ROADMAP.md).

The raster application remains the default firmware and must remain runnable
until V2 reaches feature parity and passes migration acceptance. V2 currently
runs as the exclusive `production-live-app` hardware target; it is not yet the
default or feature complete.

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
- the raw pool remains 320 slots;
- ordinary cached pan reuses framebuffer overlap and reaches first physical
  completion in about 30.6–30.7 ms;
- drawing while refinement is active works without stale publication;
- a separate contiguous 1.5 MiB export reserve allocates alongside the live
  document, cache, and workspaces;
- the final aggressive glass test committed 45 strokes with zero touch errors,
  presentation failures, authority mismatch, or visible corruption;
- independent Fable high and Grok 4.6 high reviews found no remaining blocker
  after their findings were fixed.

Evidence:

1. [`production/GATE_1_RECEIPT_2026_08_13.md`](production/GATE_1_RECEIPT_2026_08_13.md)
2. [`production/GATE_1_CACHE_CLOSURE_2026_08_13.md`](production/GATE_1_CACHE_CLOSURE_2026_08_13.md)
3. [`production/hardware-receipts/gate1-paper-cache-scroller.log`](production/hardware-receipts/gate1-paper-cache-scroller.log)
4. [`production/hardware-receipts/gate1-final-glass.log`](production/hardware-receipts/gate1-final-glass.log)

The overall rendering-quality verdict remains **YELLOW** because settled
anti-aliasing is still open. The four-sample SSAA probe took about 808 ms and is
not the funded product path.

## Current measured debt

The architecture works, but the product still needs substantial integration and
performance work:

- ordinary pan currently occupies roughly 46–50 ms end-to-end, around 20 FPS;
- stroke completion blocked touch polling for about 127 ms median, 175 ms p95,
  and 195 ms maximum during the aggressive glass test;
- long 400% tours evict useful raw views from other zooms under global LRU;
- the test UI opens zooms at `(0,0)` rather than preserving the user's focus;
- 400% cold refinement remains visible;
- anti-aliasing, persistence, Undo/Redo, real export, autosave, device lifecycle,
  and final UI are incomplete.

These are product and optimization tasks, not evidence for another rewrite.

## Immediate next sequence

1. Write the zoom/navigation behavior document.
2. Implement and hardware-test input-first scheduling, camera preservation, and
   protected recent per-zoom cache footprints.
3. Run the bounded V2 cleanup/repository-shaping round in `V2_ROADMAP.md`.
4. Run the timeboxed analytic anti-aliasing gate.
5. Continue through production UI and vector feature parity.

## Repository and coexistence decision

Use a production island in this repository—a strangler migration, not a blank
rewrite:

- platform-independent V2 modules live in `production/`;
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
              └── 320 raw 64×64 RGB565 slots
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
- No hidden allocation in production state modules.
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

- [`PROTOTYPE_EXIT.md`](PROTOTYPE_EXIT.md)
- [`SECOND_REVIEW_ARCHITECTURE_ASSESSMENT.md`](SECOND_REVIEW_ARCHITECTURE_ASSESSMENT.md)
- [`VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md`](VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md)
- [`docs/archive/2026-08-raster-and-vector-prototypes/`](docs/archive/2026-08-raster-and-vector-prototypes/)

The prototype-era production handoff and Gate 1 plans are historical inputs.
They no longer override the V2 roadmap.
