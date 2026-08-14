# Vector V2 hardware receipts

These logs are immutable evidence captured from the physical ESP32-S3. Historical log names and telemetry markers retain their original `production` wording so they remain traceable to the commits and commands that produced them.

## Current milestone evidence

- [`LONGSTROKE_COLDRENDER_INVESTIGATION_2026_08_14.md`](LONGSTROKE_COLDRENDER_INVESTIGATION_2026_08_14.md) — measured root causes and closure for the 70 ms long-stroke commit stalls and the 1.45 s adversarial 400% cold replay; saturation-gated cold replay plus in-place interactive commits.
- [`264b60e-cold-p95-20-runs.log`](264b60e-cold-p95-20-runs.log) — clean 20-reset distribution after both fixes: adversarial 400% p95 674,901 us (from 1,451,905), overlap −14..30%, every tick under 11 ms.
- [`264b60e-full-gate.log`](264b60e-full-gate.log) — complete clean harness pass including the new deterministic long-gesture A/B gate (reference 33.2 ms vs in-place 11.1 ms worst chunk commit).
- [`264b60e-inplace-phase-profile.log`](264b60e-inplace-phase-profile.log) — temporary phase instrumentation attributing the in-place commit cost (painting ~10 ms; everything else ~1.4 ms).
- [`264b60e-chunk-sweep-experiment.log`](264b60e-chunk-sweep-experiment.log) — temporary chunk-size sweep (64/48/32 → 14.9/11.5/9.1 ms worst commit) behind the 48-sample choice.
- [`264b60e-product-boot.log`](264b60e-product-boot.log) — product (non-harness) boot after dropping the staging workspace: live storage 4,842,144 bytes, largest free PSRAM block 3,473,408.
- [`7302963-export-gate.log`](7302963-export-gate.log) — full harness pass including the new export-encode gate: complete-world PNG in 5.7 s, transient 51 KiB internal + ~390 KiB PSRAM, export reserve re-verified afterward.
- [`7302963-exported-world.png`](7302963-exported-world.png) — the device-encoded PNG pulled from the flash partition over serial; decodes strictly (zlib, CRCs, defilter) and matches the loaded document. First fully validated V2 export artifact.
- [`7302963-product-boot.log`](7302963-product-boot.log) — product boot with the Export action live: live storage and PSRAM margins unchanged by the export feature.
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
