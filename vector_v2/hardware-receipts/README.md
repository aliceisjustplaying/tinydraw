# Vector V2 hardware receipts

These logs are immutable evidence captured from the physical ESP32-S3. Historical log names and telemetry markers retain their original `production` wording so they remain traceable to the commits and commands that produced them.

## Current milestone evidence

- [`CORRECTNESS_CLOSURE_2026_08_14.md`](CORRECTNESS_CLOSURE_2026_08_14.md) — clean automated and physical closure for touch transitions, long-stroke chaining, pan seams, cache identity, memory reserve, and current measured latency debt.
- [`26a05f5-correctness-closure-gate.log`](26a05f5-correctness-closure-gate.log) — complete clean harness pass, including export reserve and stack margin.
- [`26a05f5-cold-p95-20-runs.log`](26a05f5-cold-p95-20-runs.log) — clean 20-reset adversarial, overlap, and seed-7 distribution.
- [`f722a48-correctness-closure-glass.log`](f722a48-correctness-closure-glass.log) — 323 balanced physical gesture edges, two chained long contacts, and aggressive 400% pan.
- [`26a05f5-te-sync-flake.log`](26a05f5-te-sync-flake.log) — retained startup TE synchronization failure from one repeated harness reset.
- [`3d4bde4-blank-canvas-interaction-baseline.md`](3d4bde4-blank-canvas-interaction-baseline.md) — pre-navigation physical input-latency baseline and blank-paper fallback diagnosis.
- [`gate1-final-glass.log`](gate1-final-glass.log) — final aggressive manual drawing, pan, and zoom session.
- [`gate1-paper-cache-scroller.log`](gate1-paper-cache-scroller.log) — paper catalog, complete 100% cache, framebuffer-reuse pan, and export-reserve closure.
- [`gate1-clean-head-p95-20-runs.log`](gate1-clean-head-p95-20-runs.log) — clean-head cold-render distribution.
- [`gate1-grok-fixes-p95-20-runs.log`](gate1-grok-fixes-p95-20-runs.log) — post-review timing distribution.
- [`tile-class-census-seed7.log`](tile-class-census-seed7.log) — complete tile-class census for the deterministic seed-7 workload.
- [`492f2ef-overlap-cold-p95-20-runs.log`](492f2ef-overlap-cold-p95-20-runs.log) — post-correctness-fix 20-reset distribution: overlapping-XL cold replay p95 stays under one second at every tiled zoom.
- [`0560525-overlap-cold-baseline.log`](0560525-overlap-cold-baseline.log) — pre-optimization baseline for the same corpus, including the 23.66-second 400% failure. The firmware stamp is `9542714-dirty`: the deterministic overlap gate was captured immediately before its test-only harness changes were committed as `0560525`.
- [`636b9c7-memory-layout-320.log`](636b9c7-memory-layout-320.log) — 320-slot memory plan and contiguous-reserve allocation.

Interpret these through:

- [`../GATE_1_RECEIPT_2026_08_13.md`](../GATE_1_RECEIPT_2026_08_13.md)
- [`../GATE_1_CACHE_CLOSURE_2026_08_13.md`](../GATE_1_CACHE_CLOSURE_2026_08_13.md)

## Earlier receipts

The remaining logs are intermediate architectural receipts. They are retained to preserve provenance, not because each is a current acceptance baseline.
