# TinyDraw project state

Last updated: 2026-08-16

Branch: `feat/v2-performance-followup`

State audited through: [`COLD_COMPUTE_CAMPAIGN_RECEIPT.md`](benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md)

Raster V1 remains the default firmware and operational fallback. Vector V2 is
the accepted product architecture, but it is not feature complete or ready for
promotion. [`SHIP_CONTRACT.md`](SHIP_CONTRACT.md) owns acceptance thresholds;
[`V2_ROADMAP.md`](V2_ROADMAP.md) is the only forward queue.

## Finish-line scorecard

| Area | State | Current evidence / gap |
|---|---|---|
| Pan correctness | **Green — owner accepted** | Product pan is tear-free on glass at 50%, 100%, 200%, and 400%, including dense hairline content. Formal positive-control evidence still needs archiving for the release packet. |
| Pan pacing | **Provisional green** | PANSEQ p95 is 33.939 ms at 100% and 33.934 ms at 400% (~29.5 FPS), below the 38 ms guard. Camera motion caused zero persistent chrome redraws. |
| Ink latency | **Provisional green — visual lane; smoothness yellow** | Curved authority no longer replaces the preview with overt straight-segment corners. The latest owner pass measured 1.47–1.82 ms event→submit and 2.12–2.76 ms event→DMA averages, with zero presentation failures, overflows, or authority mismatches. Remaining live-curve angularity is visible under scrutiny; the four-span experiment was rejected on a cost model that the wave-3 unit sweep has since invalidated — a rematch is owner-gated (see roadmap Phase 2). Formal traces, optical latency, and resumable lift authority remain open. |
| Cold 400% | **Red, closing** | The wave-3 compute campaign cut the combined corpus from 1,269.157 ms to 668.980 ms maximum across three device runs (-47.3%): 582.9 compute, 66.8 present, 18.4 pacing, 0.9 touch. Requirement is ≤500 ms; compute must fall another ~170 ms. See the wave-3 receipt for accepted/rejected steps. |
| Revisit retention | **Architecture promising; oracle incomplete** | 448-slot tour returns with zero missing tiles, but revision-keyed accounting can hide spatially unnecessary rerenders and is not connected to the product producer. |
| Exactness | **Green for implemented scope** | Host exactness and fuzz tests pass. V2 persistence/Undo authority is not implemented. |
| Settled AA | **Open — design candidate exists** | Immediate output is intentionally hard-edged. Four-sample SSAA cost ~808 ms and is rejected. An unmeasured analytic-coverage design (8-bit alpha mask over the existing newest-first replay, boundary-only coverage) is sketched in the 2026-08-16 review; next step is a host prototype. |
| Feature parity | **Open** | Undo/Redo, autosave/recovery, device SVG wiring, minimap jump, lifecycle parity, failure UI, and release soak remain. |
| Mixed-draw appends | **Red — pre-existing, owner decision pending** | 50% in-place appends peak at 18.8 ms against the 15 ms budget. Dating evidence places the regression at the curved committed-ink change (19ebbe3), masked until wave-3 reopened the gate cascade. Owner intends a glass check; if feel is acceptable, adjust the budget with a dated contract note. |

Session continuity: the 2026-08-16 handover for the next working session
(A/B recipes, ranked cold candidates, device physics notes, open owner
decisions) is
[`review_findings_2026_08_16_cold_campaign/HANDOVER.md`](review_findings_2026_08_16_cold_campaign/HANDOVER.md).

Latest permanent receipts:

- [`COLD_COMPUTE_CAMPAIGN_RECEIPT.md`](benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md)
- [`HARDWARE_LIMITS.md`](HARDWARE_LIMITS.md)
- [`STAGING_INVARIANT_RECEIPT.md`](benchmark-results/wave2-compositor/STAGING_INVARIANT_RECEIPT.md)
- [`CHROME_LIFETIME_RECEIPT.md`](benchmark-results/wave2-compositor/CHROME_LIFETIME_RECEIPT.md)
- [`VISUAL_FIRST_INK_RECEIPT.md`](benchmark-results/wave2-compositor/VISUAL_FIRST_INK_RECEIPT.md)
- [`CHROME_PRESTAGE_RECEIPT.md`](benchmark-results/wave2-compositor/CHROME_PRESTAGE_RECEIPT.md)
- [`CURVED_AUTHORITY_GLASS_RECEIPT.md`](benchmark-results/wave2-compositor/CURVED_AUTHORITY_GLASS_RECEIPT.md)
- [`COLD_SEGMENT_CHUNK_RECEIPT.md`](benchmark-results/wave2-compositor/COLD_SEGMENT_CHUNK_RECEIPT.md)
- [`COLD_RASTER_RECURRENCE_RECEIPT.md`](benchmark-results/wave2-compositor/COLD_RASTER_RECURRENCE_RECEIPT.md)
- [`COLD_PUBLICATION_BATCH_RECEIPT.md`](benchmark-results/wave2-compositor/COLD_PUBLICATION_BATCH_RECEIPT.md)
- [`COLD_GENERAL_BASELINE_RECEIPT.md`](benchmark-results/wave2-compositor/COLD_GENERAL_BASELINE_RECEIPT.md)
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

1. Archive the formal torn-positive-control receipt, then tag provisional pan
   closure; product glass acceptance is complete.
2. Finish ink closure: make materialization/lift draining resumable, run the
   canonical production-buffer traces, and archive the optical latency receipt.
3. Continue the cold compute campaign from 668.980 ms toward ≤500 ms; the
   wave-3 receipt ranks the remaining candidates (op-level chord sweeps,
   publish-path copies, PIE fixed-point probing). The stop/go checkpoint
   question is deferred while the current trajectory holds.
3b. Resolve the `mixed_draw` 50% append budget (18.8 ms vs 15 ms): evidence
   dates it to the curved committed-ink change (19ebbe3), masked until now by
   the cold-gate cascade. Owner should confirm whether the 15 ms per-append
   budget or the curved append cost is the item to move.
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
- Deferred structural debt: `vector_v2_app.cpp` is over 1,300 lines. Split its
  interaction, authority, and lifecycle coordinators after hot-path closure;
  do not mix that refactor into the performance campaign.

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
