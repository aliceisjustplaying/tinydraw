# Performance round 2 — Phase 0 baselines — 2026-08-14

This note records the complete instrumented baseline for the second
performance round at `205fefe`, before any optimization work. Accepted
priorities for the round: **1) drawing/erasing latency, 2) pan responsiveness
(30 FPS floor), 3) cold refinement (halve the accepted p95s)**. All numbers
come from the physical ESP32-S3 running `vector-v2-gate-harness` builds of
`205fefe`; every gate is deterministic and fingerless.

Raw captures:

- [`205fefe-full-gate-384.log`](205fefe-full-gate-384.log) — full battery,
  384 raw slots (product default);
- [`205fefe-full-gate-320.log`](205fefe-full-gate-320.log) — full battery,
  320 raw slots (`./scripts/esp32 vector-v2-gate-harness PORT 320`);
- [`205fefe-cold-p95-20-runs.log`](205fefe-cold-p95-20-runs.log) — 20 reset
  cycles for the cold distribution.

## 1. Drawing: the mixed-zoom gate reproduces the regression

`TINYDRAW_GATE1_MIXED_DRAW` warms 50/100/200/400% viewports over the same
dense seed-7 world corner, then draws and erases 33-chunk boustrophedon XL
gestures at every zoom through the product chunk policy (48 samples) and the
product in-place commit call. Worst chunk commit per zoom at 384 slots:

| Zoom | Worst chunk | Average chunk | Resident tiles painted per stroke |
|---:|---:|---:|---:|
| 25% | **121.7 / 130.0 ms** (pen/eraser) | 34.1 / 32.0 ms | 821 / 735 |
| 50% | 88.2 / 83.0 ms | 28.2 / 26.6 ms | 957 / 873 |
| 100% | 58.0 / 58.1 ms | 27.0 / 26.6 ms | 924 / 920 |
| 200% | 34.4 / 34.3 ms | 22.5 / 22.2 ms | 788 / 776 |
| 400% | 20.5 / 21.0 ms | 18.8 / 18.9 ms | 714 / 735 |

- The product alarm is 15 ms per chunk; **every zoom breaches it** with a warm
  multi-zoom cache, including 400%, which the existing long-gesture gate
  (cache state after a document reset) passes at 11.6 ms in the same run.
- `published == affected`, `fallback == 0` everywhere: the current policy
  paints every intersecting resident raw tile at every zoom
  (`retain_affected_tiles` gates uniform conversion by priority view but
  paints already-raw tiles unconditionally). Fanout, not slot count, is the
  cost driver.
- The gate matches the manual session (120.1 ms at 25%, 131.8 ms at 100%),
  so the regression no longer needs a human hand to reproduce.
- The post-draw revisit lines price what drawing cost the warm cache under
  the current paint-everything policy: 14 missing tiles at 50%, zero
  elsewhere. A future invalidate-instead-of-paint policy pays in these lines,
  visibly, in the same receipt.
- `mixed_draw` is intentionally red in `TINYDRAW_GATE1_AUTOMATED_DONE` and
  intentionally excluded from the battery's final verdict until the fix
  lands; it then joins the required conjunction.

## 2. The 320-versus-384 mixed draw/pan A/B is settled

Identical mixed-draw numbers at both slot counts (worst 129.96 ms at 320
versus 130.02 ms at 384; every per-zoom figure within noise): **the cache
size does not affect drawing latency**, because commit fanout is bounded by
the warmed viewport footprints, not the pool. Retention still favors 384: the
permanent tour at 320 refills 63 tiles in 411 ms on the return trip versus
zero at 384 (reconfirmed in these runs). The 384-slot pool is now accepted on
mixed evidence, and the drawing fix is a mutation-policy change, not a cache
shrink.

## 3. Pan: current warm-pan frame is ~67 ms (~15 FPS) with chrome

`TINYDRAW_GATE1_PANSEQ` (reconciled from the preserved `feat/v2-warm-pan`
work, including its out-and-back fast-leg fix) drives 24 cached-pan frames at
100% and 400%, all frame-reused, and attributes every microsecond:

| Term | 100% avg | 400% avg | Nature |
|---|---:|---:|---|
| PSRAM scroll (memmove) | 15.1 ms | 15.0 ms | bookkeeping — removable via ring/offset addressing |
| exposed-strip compose | 7.3 ms | 7.7 ms | real work, delta-proportional |
| tear wait | 8.5 ms | 7.9 ms | idle — overlappable with compose |
| byte-swap staging (prepare) | 8.9 ms | 8.9 ms | CPU copy — optimizable/overlappable |
| staging/DMA waits | 10.5 ms | 10.5 ms | physical floor ≈ 11–12 ms QSPI |
| present (submit-to-complete) | 19.7 ms | 19.7 ms | includes the two rows above |
| **frame total** | **67.3 ms avg, 68.1 p95** | **67.3 ms avg, 67.9 p95** | ≈ 14.9 FPS |

The pre-UI-round attribution measured 50.4 ms; the difference is the
changing-minimap chrome and related per-frame work added by the UI round
(`chrome_us=7.8 ms` in the single-frame pan gate, which remains red at about
40.4 ms first-submit). The 30 FPS floor needs the frame at or below 33.3 ms:
eliminating the memmove (−15 ms), overlapping the tear wait (−7 to 8 ms), and
taking chrome off the per-frame path (−8 ms) reach it with margin, before
touching the staging path.

## 4. Cold refinement: fresh 20-run distribution on current head

| Corpus | Zoom | Accepted (6abfa0f) | Fresh p95 | Round target (−50%) |
|---|---:|---:|---:|---:|
| adversarial tapered 4× | 50% | 161 ms | 162 ms | **81 ms** |
| adversarial tapered 4× | 100% | 230 ms | 229 ms | **115 ms** |
| adversarial tapered 4× | 200% | 539 ms | 528 ms | **264 ms** |
| adversarial tapered 4× | 400% | 646 ms | 638 ms | **319 ms** |
| overlapping XL | 50% | 468 ms | 466 ms | 233 ms |
| overlapping XL | 100% | 316 ms | 320 ms | 160 ms |
| overlapping XL | 200% | 315 ms | 319 ms | 160 ms |
| overlapping XL | 400% | 300 ms | 304 ms | 152 ms |
| seed-7 realistic | 400% | 362 ms | 366 ms | 183 ms |

Worst producer tick across all 180 gate executions: 12.7 ms. The UI round's
overlay clipping did not move the cold distribution; the −50% campaign starts
from these numbers.

## 5. Known-red receipts and infrastructure notes

- The single-frame 100%/400% pan gates remain red (~40.4 ms first-submit,
  7.8 ms chrome). The battery now continues past red pan timing gates instead
  of silently skipping every downstream receipt (`b7429af`); pan gates are
  receipts, not state producers.
- The PNG export task-watchdog warning (idle starvation during
  `PNGFindFilter`) reproduced in both full-gate captures, as previously
  disclosed. Still open reliability debt for this round.
- Gate-harness builds now run with a 16 KiB main-task stack
  (`sdkconfig.gateharness.defaults`); the full battery overflowed the product
  10 KiB stack once the new gates joined the harness frame. Product builds
  keep 10 KiB.
- The device was left flashed with the **320-slot** harness build of
  `205fefe`; reflash before drawing product conclusions from the glass.
- The `feat/v2-warm-pan` worktree is fully reconciled (gate commit
  cherry-picked with authorship preserved, uncommitted fast-leg fix folded
  in); the worktree and the unfinished-agent transcript are now redundant.

## Next

Phase 1: change the in-place mutation policy so chunk commits never pay
cross-zoom fanout (invalidate or defer non-priority-view tiles), gated by
`TINYDRAW_GATE1_MIXED_DRAW` going green at 10–12 ms per chunk with the
revisit lines pricing the retention cost. Phase 2: warm-pan ring/offset
addressing plus tear-wait overlap plus chrome off the per-frame path, gated
by `TINYDRAW_GATE1_PANSEQ` at or below 33.3 ms per frame. Phase 3: the cold
−50% campaign from the fresh distribution above.
