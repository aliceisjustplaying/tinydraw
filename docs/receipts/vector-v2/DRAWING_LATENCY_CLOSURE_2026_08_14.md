# Drawing latency closure — 2026-08-14

> **Historical component-benchmark closure; product interaction remains open.**
> The later product glass run showed visible lag, 15.5–35.8 ms chunks, and
> 92–143 ms loop gaps. The 10 ms setting is not a structural preemption bound,
> the product commits before preview, and the provisional ribbon tail is omitted.
> See [`c86f3ac-manual-glass.log`](c86f3ac-manual-glass.log) and
> [`espdraw-offline-review-2026-08-15.md`](../../archive/2026-08-code-reviews/external/espdraw-offline-review-2026-08-15.md).
> The body remains unchanged as a dated component receipt.

Phase 1 of the second performance round is closed at `1848cc6`. Warm-cache
interactive drawing no longer breaches the 15 ms chunk alarm at any zoom, at
either cache size, and `TINYDRAW_GATE1_MIXED_DRAW` is now part of the gate
battery's final verdict.

Raw captures (physical ESP32-S3, `vector-v2-gate-harness` builds):

- [`a652666-full-gate-384.log`](a652666-full-gate-384.log) — active-zoom
  policy alone;
- [`e43b99d-full-gate-384.log`](e43b99d-full-gate-384.log) — honest re-warmed
  gate exposing the residual 17.6 ms breach;
- [`eccfc72-full-gate-384.log`](eccfc72-full-gate-384.log) — 12 ms budget
  (still 15.2 ms worst);
- [`1848cc6-full-gate-384.log`](1848cc6-full-gate-384.log) /
  [`1848cc6-full-gate-320.log`](1848cc6-full-gate-320.log) — accepted final
  state at both slot counts.

## Result

Worst chunk commit with a fully re-warmed multi-zoom cache before every
stroke pair (pen and eraser, 33 chunks each):

| Zoom | Phase 0 baseline | Accepted (384) | Accepted (320) |
|---:|---:|---:|---:|
| 25% | 130.0 ms | **13.8 ms** | 13.6 ms |
| 50% | 88.2 ms | **13.5 ms** | 13.4 ms |
| 100% | 58.1 ms | **12.6 ms** | 12.8 ms |
| 200% | 34.5 ms | **12.2 ms** | 12.2 ms |
| 400% | 21.0 ms | **11.8 ms** | 12.0 ms |

The deterministic 400% long-gesture gate still measures 11.6 ms worst with
zero settled fallback. The full battery is green at both slot counts,
including cache tour, export, and the live-overlay gate; only the known-red
single-frame pan gates remain, which are Phase 2.

## What changed

1. **Active-zoom mutation policy** (`a652666`). `retain_affected_tiles`
   painted every intersecting resident raw tile at every zoom, so a warmer
   cache made drawing slower (700–960 tiles per stroke at Phase 0). Raw tiles
   are now painted only at the priority view's zoom; affected tiles at other
   zooms are dropped by `commit_in_place_revision`'s existing invalidation
   and re-produced lazily. Same-color uniforms are retained everywhere
   (retention is free). This converges the in-place path with the reference
   path's priority-view publication scope. Host equivalence tests keep the
   safety net: a resident tile may be dropped but may never be stale.
2. **Wall-clock commit budget** (`eccfc72`, `b62f81a`). Even active-zoom-only
   painting is workload-dependent (17.6 ms at 50% with ~10 visible tiles per
   chunk). The commit now accepts an injected time source and a 10 ms
   deadline; painting stops at the deadline and unpainted tiles drop to
   correct fallback. The interactive poll gap is bounded by construction:
   budget + one tile overshoot (~2 ms) + commit tail (~1.5 ms) < 15 ms.
   Typical budget spillage is 1–2 tiles per chunk (24–61 per 33-chunk
   stroke).
3. **Latent world-bounds bug fixed** (`fd4e526`). `PreparedAppend::publish()`
   clears the prepared operation, and both append paths read
   `stored.world_bounds` afterward, so every `IncrementalAppendResult`
   carried an empty rectangle. The product's end-of-gesture refresh and the
   long-gesture gate's zero-fallback check were refreshing an empty region —
   the old "zero fallback" pass was partly vacuous. Caught by the new host
   policy test; pinned by a regression REQUIRE.
4. **Settled-fallback gate contract** (`1848cc6`). With budgeted commits, the
   pen-up refresh may show bounded transient fallback (15,088 px in the
   accepted run); the gate now settles the producer and requires the second
   refresh to show zero. Both totals are printed.

## Accepted trade and residuals

- **Retention price**: strokes drop affected cross-zoom tiles. The gate's
  revisit lines price it: 4/9/16/0 missing tiles per warmed viewport after
  the full ten-stroke session (384 slots), refilled in 0.14–0.26 s per view.
  A full-width XL stroke at 25% drops everything under its band — the
  worst-case revisit after that is a full view refill (~0.6 s). Latency wins
  by the round's accepted priority.
- **Transient blur under heavy ink**: budget spillage means a briefly blurry
  patch can appear under a just-finished heavy stroke until refinement
  settles it. The round-end glass check must confirm this is acceptable in
  practice.
- **The 25% overview band replay (~13.7 ms worst) is the new ceiling.** It is
  uninterruptible inside one commit and sits above the 10–12 ms target
  (though under the 15 ms alarm). Optimizing it is the identified follow-up
  if the target must be met exactly.
- The device is left flashed with the **320-slot** build of `1848cc6`;
  reflash 384 before product glass work.
