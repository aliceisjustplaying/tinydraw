# Wave 2 staging invariant receipt

Recorded: 2026-08-16  
Final measured revision: `3b4abc4`  
Hardware: ESP32-S3 + CO5300 on `/dev/cu.usbmodem101`

This is a software timing receipt, not an optical verdict. Software invariant receipts are green; optical verdict is pending the human glass check.

## Attribution before the fix

Receipt: `gate-strip-before.log` (`7e9d043` instrumentation on the reverted-pacing compositor).

Every strip exceeded its payload wire budget at both zooms. The exposed-canvas work inside the staging callback was the broad offender (`exposed_compose_us` averaged 10,335 µs at 100% and 10,871 µs at 400%). Cached-sprite bands added fixed hotspots:

| Zoom | Worst strip | Staging max | Wire budget | Over budget | Minimap strip 7 over budget |
|---|---:|---:|---:|---:|---:|
| 100% | 0 (y=0) | 8,530 µs | 1,619 µs | 6,911 µs | 5,418 µs |
| 400% | 0 (y=0) | 8,916 µs | 1,619 µs | 7,297 µs | 5,740 µs |

This falsified the narrower expectation that only the minimap viewport was responsible.

## Accepted fix

1. Compose exposed canvas into the canvas-only ring before the row-zero sweep.
2. Regenerate the caller-funded chrome cache only when chrome state, viewport, zoom, or overview revision changes.
3. Include the minimap viewport in that cached sprite, leaving each strip callback as a bounded transparent blit.
4. Fuse ring de-rotation with panel byte swapping, avoiding a second full-strip memory pass; cached sprite pixels are converted to panel order during the bounded blit.

No V2 state module performs a hidden allocation. `ChromeStagingCache` still uses the caller-funded `kChromeStagingCachePixels` span.

## Final invariant

Receipt: `gate-invariant-final.log` at `3b4abc4`.

| Zoom | Strip samples | Staging mean | Absolute max | Tightest strip | Wire budget | Headroom | Invariant |
|---|---:|---:|---:|---:|---:|---:|---:|
| 100% | 216 | 1,193 µs | 1,474 µs | 8 (y=352, 20 rows) | 736 µs | 115 µs | pass |
| 400% | 216 | 1,192 µs | 1,466 µs | 8 (y=352, 20 rows) | 736 µs | 107 µs | pass |

All 432 measured strip instances were strictly under their per-strip wire budgets. The same run reported:

- `all_reused=1` at 100% and 400%;
- `tear_edge_failures=0` at 100% and 400%;
- `TINYDRAW_GATE1_OVERLAY_CANVAS presentation_mutations=0 authority=1 pass=1`;
- no `TINYDRAW_PANSEQ_STRIP ... pass=0` lines.

## Pacing decision

| Build | 100% | 400% | Optical status / decision |
|---|---:|---:|---|
| Pre-compositor clean baseline (`c70d4f8`) | 50.3 ms | 52.8 ms | Human observed clean at about 19.9 FPS |
| Prior burst (`b5bdd78`) | p50 33.9 ms, p95 39.9 ms | p50 33.9 ms, p95 40.9 ms | Human observed a fixed-row tear |
| Accepted invariant build (`3b4abc4`) | p50 49.934 ms, p95 50.937 ms | p50 49.934 ms, p95 50.934 ms | Invariant green; pacing gate red |

A bounded burst variant was remeasured in `gate-strip-final.log`: the invariant remained green, but p95 was 50.932 ms at both zooms and tightest headroom fell to 47/60 µs. It did not meet the ≤41.7 ms pacing gate, so commits `c8a4755`/`2e267ec` record its application and rejection. Correctness headroom won per `SHIP_CONTRACT.md`.

The final harness therefore ends `pass=0` solely because `pacing_pass=0`; the two PANSEQ lines retain `staging_invariant=1`, `all_reused=1`, and `tear_edge_failures=0`. No other `TINYDRAW_* pass=0` gate appears in the final log.
