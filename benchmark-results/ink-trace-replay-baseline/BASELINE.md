# Ink-trace replay + re-render ledger — first device baselines

Date: 2026-08-16
Firmware: gate harness at this commit, device ESP32-S3 (`/dev/cu.usbmodem101`)
Log: two full gate-cascade runs; excerpts below are verbatim.

## Ink-trace replay gate (production `offer()` path, recorded recorded touch corpus)

Five embedded recorded traces replayed from a core-1 priority-5 task at
original relative timestamps through the production TouchEventBuffer, then
consumed by a product-mirroring interaction loop (InkStream → curved ribbon →
visual-first coordinator → in-place authority commits, retention budget
identical to the app). `e2c/e2g/e2s/e2d` = event→consumed / →geometry-ready /
→first-submit / →DMA-complete, µs.

```
TINYDRAW_INKTRACE trace=fast-curve-dense-25 zoom=25 events=411 consumed=371 coalesced=40 down=1/1 up=1/1 max_time_gap_us=24111 max_space_gap_px=55.33 e2c_p50=504 e2c_p95=711 e2c_max=2615 e2g_p95=786 e2s_p95=2644 e2d_p50=2355 e2d_p95=3704 e2d_max=4202 latency_samples=33 presentation_failures=0 commit_failures=0 overflows=0 latency_pass=1 pass=1
TINYDRAW_INKTRACE trace=fast-curve-400 zoom=400 events=414 consumed=379 coalesced=35 down=1/1 up=1/1 max_time_gap_us=9259 max_space_gap_px=65.51 e2c_p50=504 e2c_p95=590 e2c_max=1586 e2g_p95=782 e2s_p95=2660 e2d_p50=2489 e2d_p95=3973 e2d_max=3995 latency_samples=33 presentation_failures=0 commit_failures=0 overflows=0 latency_pass=1 pass=1
TINYDRAW_INKTRACE trace=fast-curve-400-xl zoom=400 events=2296 consumed=1920 coalesced=376 down=1/1 up=1/1 max_time_gap_us=12762 max_space_gap_px=47.85 e2c_p50=504 e2c_p95=583 e2c_max=1788 e2g_p95=784 e2s_p95=2990 e2d_p50=3325 e2d_p95=4620 e2d_max=8198 latency_samples=172 presentation_failures=0 commit_failures=0 overflows=0 latency_pass=1 pass=1
TINYDRAW_INKTRACE trace=slow-precise-100 zoom=100 events=741 consumed=723 coalesced=18 down=1/1 up=1/1 max_time_gap_us=8667 max_space_gap_px=13.45 e2c_p50=504 e2c_p95=693 e2c_max=1580 e2g_p95=780 e2s_p95=1771 e2d_p50=2028 e2d_p95=2328 e2d_max=2365 latency_samples=53 presentation_failures=0 commit_failures=0 overflows=0 latency_pass=1 pass=1
TINYDRAW_INKTRACE trace=scribble-multistroke zoom=100 events=848 consumed=729 coalesced=119 down=7/7 up=7/7 max_time_gap_us=6082 max_space_gap_px=109.46 e2c_p50=504 e2c_p95=584 e2c_max=1587 e2g_p95=782 e2s_p95=3445 e2d_p50=3190 e2d_p95=5290 e2d_max=6320 latency_samples=55 presentation_failures=0 commit_failures=0 overflows=0 latency_pass=1 pass=1
```

Findings:

- **Zero lost Down/Up across the corpus** (scribble: 7/7 both directions),
  zero buffer overflows, zero presentation or commit failures. The ship §2
  fidelity gate has its first recorded-corpus receipt.
- **event→DMA p95 is 2.3–5.3 ms** on every trace — an order of magnitude
  under the ≤28 ms software proxy for the 45 ms optical requirement. The
  latency lane confirmation matches the wave-2 author receipts, now on the
  real recorded corpus.
- Coalescing is visible and modest (5–16% of moves), with the XL trace
  coalescing most (16%) because its commits are heaviest.
- Caveat: the replay consumption loop polls tighter than the product loop
  (e2c_p50 ≈ 0.5 ms vs the app's ~8.5 ms tick), so consumed counts are
  higher than production would see; the pipeline downstream of consumption
  is production code. Run-to-run spread across the two runs was ≤3%.
- under-overlay (9,284 events, ~190 KiB) is not embedded; it needs streamed
  delivery and remains capture-side only for now.

## Re-render ledger (déjà-vu oracle), tour-scoped

```
TINYDRAW_RERENDER_LEDGER_RESET site=cache_tour_start
TINYDRAW_RERENDER_LEDGER site=cache_tour renders=137 unique=137 amplification=1.000 cold=137 damage=0 evict=0 stale=0 unexplained=0
```

The 448-slot cache tour re-renders **nothing**: every render is a first
visit, amplification exactly 1.000, zero stale/unexplained/evicted causes.
This is the first product-path receipt for the revisit-retention
architecture claim, and the standing guard for every future cold change:
speed must not buy amplification.

First-run note (before the document-restore hook): a full-cascade cumulative
ledger read 1,418 renders with 260 "unexplained" — all attributable to
un-hooked document restores between gates. `restore_snapshot` now resets the
ledger; the tour-scoped receipt above is the meaningful number.

## Gate verdict context

Full cascade after both features: every previously green gate stayed green
(`cache_tour=1 idle_repair=1 ink_trace=1 hairline_capacity=1 long_gesture=1
export=1 pan*=1`), and the two known reds are unchanged and pre-existing
(`mixed_draw=0` — the open 18.8 ms author decision; cold gates red while the
≤500 ms campaign continues).
