# TinyDraw project state

Last updated: 2026-08-16 (post-Stage-B owner decisions: mixed-draw fix
greenlit, cold 400% hold-the-line accepted, Stage C declined, settled-AA
prototype approved — recorded in
[`SHIP_CONTRACT.md`](SHIP_CONTRACT.md#owner-decisions--2026-08-16-post-cold-stage-b-glass-session))

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
| Ink latency | **Provisional green — visual lane; smoothness yellow** | The five-trace canonical corpus is now recorded owner finger input, and the gate harness replays it through the production `offer()` path: zero lost Down/Up, event→DMA p95 2.3–5.3 ms on every trace ([`BASELINE.md`](benchmark-results/ink-trace-replay-baseline/BASELINE.md)). Smoothness: the angularity tool falsified the four-span smoothness case on real input — 2-chord deviation is ≤0.11 px at 400%; the angular signal is input jitter + chunk boundaries + hard-edge aliasing, pointing at arc-length resampling and settled AA instead ([`BASELINE.md`](benchmark-results/ink-angularity-baseline/BASELINE.md)). Optical latency and resumable lift authority remain open. |
| Cold 400% | **Hold-the-line accepted (owner 2026-08-16)** | Stage B walls: 437.9/428.4/488.0 ms at 50/100/200% under the ≤500 line; 400% is 507.0 ms. Owner accepted the 7 ms residual until autosave exists; the gate now holds the line at 510 ms and the micro-candidates are parked. The ≤500 requirement still governs the final autosave-enabled 20-run closure. See [`RECEIPT.md`](benchmark-results/cold-stage-b-2026-08-16/RECEIPT.md). **Separate standing red, not covered by this ruling:** the `overlap` workload 50% cold gate has been over its 500 ms wall since at least wave-3 (648.9 ms then, 621.9 ms at Stage B HEAD, `stageB-final-1.log`) and appears in no scorecard until now — needs owner triage. |
| Revisit retention | **Green for pure revisits only — glass-confirmed open in general** | The spatial re-render ledger is wired into the product canvas/producer and classifies every group render (cold miss / damage / eviction / stale / unexplained). Tour-scoped device receipt: renders=137 unique=137 amplification=1.000. Owner glass session 2026-08-16: revisit re-rendering is visible at 100% after multi-zoom drawing (cross-zoom damage is by design; eviction pressure adds to it) — the déjà-vu campaign owns this. First step: live ledger cause-histogram receipts during glass sessions. |
| Exactness | **Green for implemented scope** | Host exactness and fuzz tests pass. V2 persistence/Undo authority is not implemented. |
| Settled AA | **Open — prototype approved (owner 2026-08-16)** | Immediate output is intentionally hard-edged. Four-sample SSAA cost ~808 ms and is rejected. The analytic-coverage design (8-bit alpha mask over the existing newest-first replay, boundary-only coverage) is approved for a host prototype with rendered before/afters, paired with an arc-length-resampling prototype (the measured non-AA smoothness lever). |
| Feature parity | **Open** | Undo/Redo, autosave/recovery, device SVG wiring, minimap jump, lifecycle parity, failure UI, and release soak remain. |
| Mixed-draw appends | **Red — fix greenlit (owner 2026-08-16)** | The felt 400% lag is ruled unacceptable and the 15 ms per-append budget stands. Worst chunk commits 17–21.6 ms, dominated by `ph_uniform_max` 18.3 ms (paper tiles materialized+painted mid-stroke) plus `ph_raw` up to 12 ms; at 25% one 42 ms commit. Fix: the committed-overlay / authority-revision split (external review §8.3–8.4), adopted as the Phase 2 execution design. The Phase 1 diagnosis is complete: the mid-stroke pixelation report was falsified as a drop mechanism — all drop counters zero in every harness scenario; benchmark pixelation was pre-existing overview substrate at unwarmed gate views, never reproduced on glass ([`RECEIPT.md`](benchmark-results/ink-fallback-observability/RECEIPT.md)). Latency is the design's only driver. |

Session continuity: Cold Stage B is **closed** and glass-tested (receipt
above); the Stage B session handover is
[`review_findings_2026_08_16_stage_b/HANDOVER.md`](review_findings_2026_08_16_stage_b/HANDOVER.md).
The four post-Stage-B owner decisions are recorded in the ship contract.
Next per the owner-approved queue: the mid-stroke pixelation diagnosis
(fallback observability first), then the committed-overlay /
authority-revision-split design, then AA + resampling host prototypes, the
déjà-vu campaign, and a triage pass over the 2026-08-16 correctness review.
The prior handover context:
(ranked candidates with code receipts, new standing ledger/ink-trace guards,
the post-B queue — AA prototype + resampling, then the déjà-vu campaign — and
pending owner decisions) is
[`review_findings_2026_08_16_oracle_session/HANDOVER.md`](review_findings_2026_08_16_oracle_session/HANDOVER.md).
The wave-3 A/B recipe and device-physics cheat sheet remain authoritative in
[`review_findings_2026_08_16_cold_campaign/HANDOVER.md`](review_findings_2026_08_16_cold_campaign/HANDOVER.md).

Latest permanent receipts:

- [`RECEIPT.md` — Cold Stage B](benchmark-results/cold-stage-b-2026-08-16/RECEIPT.md)
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
3. ~~Mid-stroke pixelation diagnosis~~ — done; drop hypothesis falsified
   with all-zero attribution counters
   ([`RECEIPT.md`](benchmark-results/ink-fallback-observability/RECEIPT.md)).
   The counters stay live on `TINYDRAW_LIVE_STROKE` as a standing oracle.
4. Design and land the committed-overlay / authority-revision split — the
   owner-greenlit mixed_draw latency fix (external review §8.3–8.4); it also
   carries resumable lift drain and part of the déjà-vu retention story.
   **In progress.**
5. Host prototypes with rendered before/afters: settled-AA boundary coverage
   and arc-length resampling (both owner-approved 2026-08-16).
6. Déjà-vu campaign step 1: live ledger cause histograms during glass
   sessions; then gate scenarios; then fix per histogram.
7. Cold is hold-the-line only: 400% guarded at 510 ms, candidates parked,
   20-run closure statistic waits for the autosave-enabled build. Triage the
   undocumented `overlap` 50% cold red with the owner.
8. Establish generation-checked operation snapshots and active-prefix history;
   then implement Undo/Redo, autosave/recovery, and transactional SVG wiring.
9. Finish settled AA, minimap tap-to-jump, power/RTC/NTP/lifecycle parity,
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
