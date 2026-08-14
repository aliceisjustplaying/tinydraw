# Vector V2 hardware receipts

These logs are immutable evidence captured from the physical ESP32-S3. Historical log names and telemetry markers retain their original `production` wording so they remain traceable to the commits and commands that produced them.

## Current milestone evidence

- [`PAN_FLOOR_CLOSURE_2026_08_15.md`](PAN_FLOOR_CLOSURE_2026_08_15.md) / [`1cd7f1b-full-gate-384.log`](1cd7f1b-full-gate-384.log) / [`1cd7f1b-full-gate-320.log`](1cd7f1b-full-gate-320.log) / [`4022917-full-gate-384.log`](4022917-full-gate-384.log) — Phase 2 closure: warm pan 67.3 ms → 28.1 ms avg (p50 26.95 ms, p95 32.95 ms) via the toroidal frame ring, beam-raced push sweep, fused exposed compose, and wild-reuse fixes; TE boot flake fixed at the root with a runtime heal. Slot count remains irrelevant to pan.
- [`b76b992-manual-glass.log`](b76b992-manual-glass.log) — 2026-08-14 manual glass session at `b76b992`: no tearing/ghosting under violent 100%/400% scrubbing (validates the pan tear discipline), minimap tracking good, but reused=0 on all 386 real pan frames and ~one cold-fill cycle per pan batch — the telemetry that drove the wild-reuse fixes and the idle-repair work item.
- [`DRAWING_LATENCY_CLOSURE_2026_08_14.md`](DRAWING_LATENCY_CLOSURE_2026_08_14.md) / [`1848cc6-full-gate-384.log`](1848cc6-full-gate-384.log) / [`1848cc6-full-gate-320.log`](1848cc6-full-gate-320.log) — Phase 1 closure: warm-cache chunk commits fell from 130 ms worst to 13.8 ms via the active-zoom mutation policy plus a 10 ms commit budget; the mixed-zoom gate is green at both slot counts and joined the battery's final verdict. Also records the fixed empty-world-bounds result bug and the settled-fallback gate contract.
- [`PERF_ROUND_2_BASELINES_2026_08_14.md`](PERF_ROUND_2_BASELINES_2026_08_14.md) / [`205fefe-full-gate-384.log`](205fefe-full-gate-384.log) / [`205fefe-full-gate-320.log`](205fefe-full-gate-320.log) / [`205fefe-cold-p95-20-runs.log`](205fefe-cold-p95-20-runs.log) — complete Phase 0 baseline for the second performance round: the deterministic mixed-zoom drawing gate reproduces the 130 ms warm-cache chunk regression at every zoom, the reconciled warm-pan attribution gate measures 67.3 ms frames (≈15 FPS) with full per-term attribution, the fresh cold 20-run distribution confirms the accepted p95s, and the 320-versus-384 mixed A/B is settled (drawing latency identical; 384 keeps its retention win).
- [`live-ink-overlay-clipping-2026-08-14.md`](live-ink-overlay-clipping-2026-08-14.md) — fingerless hardware circle gate proving fixed overlays no longer starve live ink: clear/overlay worst updates 2.940/2.928 ms, overlay submit 2.335 ms, and zero overlay redraw work. Also records the related cold-fill recovery and explicitly retains the separate red pan-overlay gate.
- [`PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md`](PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md) / [`00d054a-manual-glass.log`](00d054a-manual-glass.log) — final product glass verdict: smooth 3,751-sample 400% gesture at 13.3 ms worst append and successful physical PNG/USB export, alongside newly measured 120–132 ms lower-zoom cached commits, roughly 20 FPS pan with low framebuffer reuse, popup dismissal debt, and an export task-watchdog warning.
- [`cache-tour-384.log`](cache-tour-384.log) / [`cache-tour-320.log`](cache-tour-320.log) — the 384-versus-320 slot A/B on the new 16-stop 400% tour gate: return-trip refill 0 tiles / 40 ms at 384 versus 63 tiles / 409 ms at 320; the protected home footprint returns sharp at both.
- [`6abfa0f-cold-p95-20-runs.log`](6abfa0f-cold-p95-20-runs.log) — final slice distribution: adversarial 400% p95 646,305 us (−55.5% vs 26a05f5) with deadline slicing and the 384-slot pool; all ticks under 12.7 ms.
- [`6abfa0f-full-gate.log`](6abfa0f-full-gate.log) — full harness pass at 384 slots including cache retention, export encode, and the live export-reserve allocation. The encode completes, but the receipt contains a CPU-0 task-watchdog warning; treat export reliability as open.
- [`6abfa0f-product-boot.log`](6abfa0f-product-boot.log) — product boot at 384 slots: largest free PSRAM block 2,949,120 bytes.
- [`LONGSTROKE_COLDRENDER_INVESTIGATION_2026_08_14.md`](LONGSTROKE_COLDRENDER_INVESTIGATION_2026_08_14.md) — measured root causes and closure for the 70 ms long-stroke commit stalls and the 1.45 s adversarial 400% cold replay; saturation-gated cold replay plus in-place interactive commits.
- [`264b60e-cold-p95-20-runs.log`](264b60e-cold-p95-20-runs.log) — clean 20-reset distribution after both fixes: adversarial 400% p95 674,901 us (from 1,451,905), overlap −14..30%, every tick under 11 ms.
- [`264b60e-full-gate.log`](264b60e-full-gate.log) — complete clean harness pass including the new deterministic long-gesture A/B gate (reference 33.2 ms vs in-place 11.1 ms worst chunk commit).
- [`264b60e-inplace-phase-profile.log`](264b60e-inplace-phase-profile.log) — temporary phase instrumentation attributing the in-place commit cost (painting ~10 ms; everything else ~1.4 ms).
- [`264b60e-chunk-sweep-experiment.log`](264b60e-chunk-sweep-experiment.log) — temporary chunk-size sweep (64/48/32 → 14.9/11.5/9.1 ms worst commit) behind the 48-sample choice.
- [`264b60e-product-boot.log`](264b60e-product-boot.log) — product (non-harness) boot after dropping the staging workspace: live storage 4,842,144 bytes, largest free PSRAM block 3,473,408.
- [`7302963-export-gate.log`](7302963-export-gate.log) — full harness pass including the new export-encode gate: complete-world PNG in 5.7 s, transient 51 KiB internal + ~390 KiB PSRAM, export reserve re-verified afterward. This receipt also contains the CPU-0 task-watchdog warning that the original closure summary omitted.
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
