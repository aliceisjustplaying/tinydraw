# Chrome lifetime split receipt

Date: 2026-08-16

Raw device log: [`gate-chrome-lifetime-split.log`](gate-chrome-lifetime-split.log)

Boundary corpus: [`gate-pan-boundary.log`](gate-pan-boundary.log)

## Change

The existing 53,956-pixel caller-funded allocation now has independent toolbar,
battery, zoom, and minimap-base identities. Camera motion redraws only the
transient minimap viewport lines during strip staging. No allocation grew.

## One-variable PANSEQ result

| Zoom | Baseline p95 | Split p50 | Split p95 | Split max | Chrome avg |
|---|---:|---:|---:|---:|---:|
| 100% | 50.937 ms | 33.925 ms | 33.939 ms | 33.940 ms | 2.362 ms |
| 400% | 50.934 ms | 33.925 ms | 33.934 ms | 33.939 ms | 2.185 ms |

Both sequences completed 24/24 cached frames with zero TE-edge failures and
zero toolbar, battery, zoom, or minimap-base redraws. The ≤41.7 ms requirement
and ≤38 ms guard pass at both zooms.

All 432 staged strips remained faster than their measured wire budgets. The
tightest headroom was 41 µs at 100% and 87 µs at 400%; the staging invariant
therefore remains green. Host tests also prove camera motion leaves cached bytes
unchanged while producing pixel-identical output to an uncached full render.

The automated aggregate remains red only at the pre-existing adversarial cold
gate (`adversarial_cold=0`). This receipt is software/device timing evidence;
the changed cadence requires a fresh same-session glass pass and torn positive
control before pan correctness closes.

The follow-up boundary corpus passed at 100% and 400%: one-pixel samples
accumulated into the expected even-coordinate motion, 94- and 96-pixel deltas
reused the ring, and a 98-pixel delta took the full-refresh fallback.
