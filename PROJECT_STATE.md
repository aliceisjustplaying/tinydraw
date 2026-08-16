# TinyDraw project state

Last updated: 2026-08-16

Branch: `feat/v2-performance-followup`

State audited through: `gate-chrome-lifetime-split.log`

Raster V1 remains the default firmware and operational fallback. Vector V2 is
the accepted product architecture, but it is not feature complete or ready for
promotion. [`SHIP_CONTRACT.md`](SHIP_CONTRACT.md) owns acceptance thresholds;
[`V2_ROADMAP.md`](V2_ROADMAP.md) is the only forward queue.

## Finish-line scorecard

| Area | State | Current evidence / gap |
|---|---|---|
| Pan correctness | **Reopened for glass** | The cache split changed cadence. All 432 strips stayed faster than wire and TE failures were zero; same-session product glass plus torn positive control remains. |
| Pan pacing | **Provisional green** | PANSEQ p95 is 33.939 ms at 100% and 33.934 ms at 400% (~29.5 FPS), below the 38 ms guard. Camera motion caused zero persistent chrome redraws. |
| Ink latency | **Red** | Authority/materialization precedes visible update, provisional ribbon geometry is omitted, and most calls use loop time instead of the original touch timestamp. |
| Cold 400% | **Red / kill gate** | Frozen adversarial run is 663.829 ms: 577.667 compute, 69.371 present, 15.618 pacing, 1.173 touch. Requirement is ≤500 ms maximum across the defined 20-run closure. |
| Revisit retention | **Architecture promising; oracle incomplete** | 448-slot tour returns with zero missing tiles, but revision-keyed accounting can hide spatially unnecessary rerenders and is not connected to the product producer. |
| Exactness | **Green for implemented scope** | Host exactness and fuzz tests pass. V2 persistence/Undo authority is not implemented. |
| Settled AA | **Open** | Immediate output is intentionally hard-edged. Four-sample SSAA cost ~808 ms and is rejected. |
| Feature parity | **Open** | Undo/Redo, autosave/recovery, device SVG wiring, minimap jump, lifecycle parity, failure UI, and release soak remain. |

Latest permanent receipts:

- [`HARDWARE_LIMITS.md`](HARDWARE_LIMITS.md)
- [`STAGING_INVARIANT_RECEIPT.md`](benchmark-results/wave2-compositor/STAGING_INVARIANT_RECEIPT.md)
- [`CHROME_LIFETIME_RECEIPT.md`](benchmark-results/wave2-compositor/CHROME_LIFETIME_RECEIPT.md)
- [`GLASS_OBSERVATIONS.md`](benchmark-results/wave2-compositor/GLASS_OBSERVATIONS.md)
- [`gate-invariant-final.log`](benchmark-results/wave2-compositor/gate-invariant-final.log)

## Measured machine

- ESP32-S3 at 240 MHz with 8 MiB PSRAM; CO5300 368×448 RGB565 panel.
- Requested 40/50/60 MHz panel clocks all produce **40 MHz effective** because
  of the GPSPI divider: 10 Mpixel/s, 20 MB/s, 27.2 full-width rows/ms.
- TE period is 16.773 ms; ISR-to-task resume is ~9 µs; register reads provide no
  usable scanline oracle.
- A 448-row edge-synchronized stream sustains 29.4 FPS. A ≤368-row stream
  sustains 58.8 FPS; the one-period boundary is roughly 390–400 rows.
- The product pan sweep covers rows 0–371. Its 13.69 ms payload fits the panel
  envelope; the cache split now catches the two-TE cadence in device PANSEQ.
- The final product allocation uses 448 raw 64×64 tile slots. With the 1.5 MiB
  export reserve held, the final receipt leaves ~306 KiB free and ~303 KiB as
  the largest block. Broad viewport checkpoint caches are therefore unfunded.

## Current architecture

```text
blank baseline + ordered vector operations (durable authority)
        │
        ├── complete 368×448 overview at 25%
        └── sparse world-aligned materialization at 50–400%
              ├── compact paper/uniform identities
              └── 448 raw 64×64 tiles
                    │
             canvas-only toroidal frame ring
                    │
             internal DMA staging + transient chrome/ink
                    │
                  CO5300 panel
```

Committed geometry is a 1472×1792 world at 25%, 50%, 100%, 200%, and
400%. Vector operations are the complete V2 drawing authority. Overviews,
tiles, chrome, previews, settled output, and export buffers are derived or
transient.

Raster V1 documents remain Raster V1. They are never silently restored as a
raster baseline with an empty V2 operation log. The policy and implications for
Undo, persistence, and SVG are frozen in
[`SHIP_CONTRACT.md`](SHIP_CONTRACT.md#document-authority-policy).

## Immediate work order

1. Close pan on glass with product motion, under-overlay drawing, and a torn
   positive control.
2. Make ink visual-first: carry original touch timestamps, stage the old/new
   provisional tail without mutating the reusable ring, submit visibility before
   authority work, and make materialization/lift draining resumable.
3. Run the bounded cold viability campaign: segment-chunk bounds, tapered-raster
   inner-loop optimization, then exact-publication batching. Stop and reassess if
   the frozen corpus remains above 500 ms.
4. Establish generation-checked operation snapshots and active-prefix history;
   then implement Undo/Redo, autosave/recovery, and transactional SVG wiring.
5. Finish settled AA, minimap tap-to-jump, power/RTC/NTP/lifecycle parity,
   capacity/failure UI, export receipt, and all-on release closure.

## Proven foundation worth preserving

- Exact pen/eraser painter order and transactional incremental publication.
- Complete overview fallback with no checkerboards.
- World-aligned cache identities, paper-aware materialization, and bounded tile
  production with stale-work cancellation.
- Canvas-only toroidal reuse and one ordered row-zero presentation sweep.
- Independent touch sampling with transition-preserving Down/Up behavior.
- Exact variable-width SVG core with renderer-raster fidelity tests.
- Full-world PNG/USB export and a separately proven 1.5 MiB reserve.
- Production toolbar, two PICO-8 palettes, zoom rail, battery, confirmation UI,
  and live minimap with viewport rectangle.
- Separate Raster V1 and Vector V2 firmware targets.

Foundation receipts and architectural history live in
[`vector_v2/README.md`](vector_v2/README.md),
[`vector_v2/hardware-receipts/`](vector_v2/hardware-receipts/), and
[`docs/archive/`](docs/archive/). Superseded results remain valuable evidence,
but do not override this scorecard or the frozen contract.

## Guardrails and validation

- No rewrite, camera-aligned atlas, hidden V2 allocation, or speculative
  second-core concurrency.
- Keep V2 state out of `WorldCanvas`, `FirmwareCanvas`, and the V1 interaction
  loop. Share stable platform-neutral mechanisms through narrow dependencies.
- Every cache needs a byte budget, identity, invalidation owner, reuse receipt,
  exactness oracle, and removal condition.
- One measured hot-path hypothesis per change. A shared-path change reopens its
  dependent gates as defined in the roadmap.
- Glass is authoritative for visible correctness and feel; software receipts
  provide attribution.

Host validation:

```sh
./scripts/dev test
./scripts/dev release
./scripts/dev asan
./scripts/dev format-check
./scripts/dev tidy
./scripts/dev cppcheck
git diff --check
```

ESP integration must build both firmware variants. Final promotion additionally
requires the gate harness, physical optical/ink checks, interrupted-write tests,
long-session soak, and explicit V1/V2 parity review.
