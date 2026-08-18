# Pan floor closure — 2026-08-15

> **Historical benchmark closure; physical correctness was falsified later on
> 2026-08-15.** The beam-race model and software `tear_synchronized` signal did
> not prevent severe glass tearing. At `c86f3ac`, p95 also regressed to
> 47.5–48.6 ms, above the current required p95 ≤41.7 ms gate. See
> [`c86f3ac-manual-glass.log`](c86f3ac-manual-glass.log) and
> [`espdraw-offline-review-2026-08-15.md`](../../archive/2026-08-code-reviews/external/espdraw-offline-review-2026-08-15.md).
> The body remains unchanged as a dated performance receipt.

Perf round 2, priority 2: panning at a floor of 30 FPS (≤33.3 ms/frame)
with margin. Closed at `4022917` on hardware (ESP32-S3, CO5300 panel,
gate harness, both 384 and 320 tile slots).

## Result

`TINYDRAW_GATE1_PANSEQ` (24-frame continuous mixed-delta pan sequence,
warm cache, per zoom):

| build | frame avg | p50 | p95 | max | tear wait avg | scroll avg |
|---|---|---|---|---|---|---|
| `b76b992` baseline (stage 3) | 50.2 ms | 50.0 | 50.5 | 51.0 | 4.3 ms | 15.0 ms |
| `2e07671` ring | 34.8 ms | 33.96 | 43.0 | 43.9 | 4.2 ms | 10 µs |
| `1cd7f1b` beam race + fused compose | 28.1 ms | 26.95 | 32.95 | 33.95 | ~0.1 ms | 11 µs |
| `4022917` TE heal (384, 3 boots) | 28.1–28.6 ms | 26.95 | 32.9–33.95 | 33.95 | ~0.2 ms | 13 µs |

Round start (before this phase): 67.3 ms/frame. Final: **28.1 ms avg,
p50 26.95 ms (~37 FPS), p95 32.95 ms** — floor met with margin at p50/avg;
worst observed single frame 33.95 ms (~29.5 FPS, 2% over floor, accepted).
320-slot spot check identical (28.4/28.5 ms avg): slot count remains
irrelevant to pan, matching the round-1 A/B.

All 24 battery gates green on every run (`1cd7f1b-full-gate-384.log`,
`1cd7f1b-full-gate-320.log`, `4022917-full-gate-384.log`).

## Mechanisms (in commit order)

1. `aba02bc` — chrome and tear wait off cached pan frames: pan presents
   only the unobscured canvas top-down; tear-wait elision window.
2. `5293823` — minimap tracks every pan frame via row-wise resample with
   a source-column LUT.
3. `b76b992` — one panel drain per frame (deferred per-region
   `wait_for_all`), 16 K-pixel transfer strips.
4. `2e07671` — **toroidal frame ring**: pan advances a 2D ring origin
   (pointer math) instead of memmoving ~294 KiB per frame; de-rotation
   folds into the transport byte-swap staging pass; scheduler validates
   ring strips; minimap composes into a linear scratch over a ring
   backdrop. Plus wild-reuse fixes: fallback pixels and composition-epoch
   drift are quality-only and no longer break the cached-pan identity;
   refinement region presents preserve reusability (the manual glass
   session at `b76b992` measured reused=0 on all 386 real pan frames).
5. `1cd7f1b` — **race the beam**: the push sweep starts at the row the
   beam just passed (wrapped two-band present) instead of waiting for the
   top-of-frame pulse; beam-lap math keeps it tear-safe (writer ~15
   rows/ms vs beam 26.6 rows/ms; lap at ~38 ms vs ~25 ms sweep).
   **Fused exposed compose**: exposed strips compose just-in-time inside
   the sweep, filling transfer-semaphore idle (serial compose ~7.3 ms →
   ~0). Staging byte-swap via aligned 32-bit loads; pan deltas quantized
   to even pixels to keep the ring shift on the aligned path.
6. `4022917` — TE signal heal: TEON re-issued after display-on (the
   vendor init list sent it before sleep-out; 2/6 boots ignored it and
   burned 40 ms tear timeouts every frame), plus rate-limited runtime
   self-heal logging `TINYDRAW_PANEL_TE_HEAL`.

## Frame budget (final, 368×372 canvas, warm)

- Ring scroll: ~0 (pointer math)
- Exposed compose: ~0 serial (fused into sweep idle)
- Tear wait: ~0.1–0.3 ms avg (beam raced; short bounded wait only when
  the beam is in the dock band)
- Push sweep (stage + wire): ~28 ms wall, ~12.9 ms CPU (prepare 6.4 +
  staging 6.4); end-drain ~3 ms included
- Wire floor for a full 368×448 RGB565 frame remains ~11 ms; the sweep
  is now DMA-bound with compose hidden inside it

## Residuals

- Worst single frame 33.95 ms (~2% over the 33.3 ms floor); accepted.
- Beam racing needs a glass tearing re-check (morning list). The math
  says larger margins than the old elision window, but confirm.
- The TE boot flake is fixed at the root as far as 3/3 boots show; the
  heal path covers the residual. Battery does not yet alarm on
  TE_HEAL occurrences.
- Real-world (product) pan reuse after these fixes is unmeasured until
  the next manual session; the wild-reuse fixes were driven by
  `b76b992-manual-glass.log` telemetry.
