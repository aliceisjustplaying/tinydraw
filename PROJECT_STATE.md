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
- the final aggressive glass test committed 45 strokes with zero touch errors,
  presentation failures, authority mismatch, or visible corruption;
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

The architecture works, but the product still needs substantial integration and
performance work:

- ordinary pan currently occupies roughly 46–50 ms end-to-end, around 20 FPS;
- the final glass test measured only 18.3% framebuffer reuse across 814 frames
  at 400%, and 2.9% across 244 frames at 200%;
- drawing with a warm multi-zoom cache is not bounded at lower zooms: the
  manual session reached 120.1 ms per chunk at 25% and 131.8 ms at 100%;
- the 384-slot cache improves mutation-free revisits but still needs a mixed
  draw/pan A/B against 320 before it becomes a settled product choice;
- complete PNG/USB export works, but the blocking encoder starves CPU 0 long
  enough to trigger the five-second task watchdog;
- popup outside taps do not dismiss the popup and may pan behind it;
- anti-aliasing, persistence, Undo/Redo, autosave, device lifecycle, and final
  UI are incomplete.

At `264b60e` (2026-08-14), intermediate long-stroke chunk commits moved in
place and the deterministic 400% gate fell from about 70 ms to 11.1 ms. The
manual 400% long gesture later measured 13.3 ms worst-case and looked smooth,
but lower-zoom cached drawing exposed the unbounded cross-zoom mutation work
listed above. The cold campaign remains a large measured win: final adversarial
400% p95 is 646 ms, down from 1.452 s, with every producer tick under 12.7 ms.
See
[`vector_v2/hardware-receipts/PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md`](vector_v2/hardware-receipts/PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md).

These are product and optimization tasks, not evidence for another rewrite.

## Immediate next sequence

1. Run the bounded UI refinement round: popup dismissal and hit behavior,
   color-picker changes, the zoom rail, battery status, export progress, and a
   minimap skeleton.
2. Follow the accepted camera behavior in
   [`VECTOR_V2_ZOOM_NAVIGATION.md`](VECTOR_V2_ZOOM_NAVIGATION.md).
3. Resume performance work with the documented priority: drawing latency,
   cold refinement (p95 below 800 ms), pan responsiveness, then export speed.
4. Fix export watchdog starvation before treating export as release-ready.
5. Continue through anti-aliasing, persistence, Undo/Redo, and lifecycle parity.

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
