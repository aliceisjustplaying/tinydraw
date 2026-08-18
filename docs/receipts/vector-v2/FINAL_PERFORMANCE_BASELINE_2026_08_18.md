# Final performance baseline — 2026-08-18

## Scope

This is the closeout baseline for the performance-review cycle. Device results
come from the accepted ESP32-S3 runs at 240 MHz with 8 MiB octal PSRAM and the
CO5300 panel at 40 MHz. Host results are labeled separately. Values from
different corpora or authority revisions are not treated as one A/B.

The final source builds a `0x104f10`-byte product image with `0x7b0f0` bytes
(32%) free in the smallest app partition. The final automated hardware gate is
all-ones. Glass acceptance found no tearing or persistent white blocks. Settled
AA remains yellow because its complete 50–400% passes exceed 500 ms.

## Device cold compute

These are the retained F24 treatment values. All eleven cases passed and
improved over the same-tree flash-text build. They measure compute time, not
end-to-end paced wall or panel time.
Source: [`F24_RASTER_IRAM_AB_2026_08_18.md`](F24_RASTER_IRAM_AB_2026_08_18.md).

| Corpus | Zoom | Compute |
|---|---:|---:|
| overlap | 50% | 356.193 ms |
| overlap | 100% | 193.339 ms |
| overlap | 200% | 174.575 ms |
| overlap | 400% | 140.048 ms |
| tapered + evil hairlines | 50% | 287.197 ms |
| tapered + evil hairlines | 100% | 282.271 ms |
| tapered + evil hairlines | 200% | 349.950 ms |
| tapered + evil hairlines | 400% | 379.544 ms |
| seed 7 sparse | 400% | 142.634 ms |
| evil-hairline capacity | 100% | 165.092 ms |
| evil-hairline capacity | 400% | 90.191 ms |

The F24 A/B improvement spans 6.93–11.68%, with an 8.70% median and no
regressing case. Incremental-rasterizer IRAM placement costs 10,836 bytes in
the product link and 13,108 bytes in the gate link.

The last retained paced-wall gate predates the cooperative, F24, F2, and final
F13 images. It measured the photographed 400% hairline corpus at 186.978 ms
with a 9.132 ms worst producer tick; the broader mixed corpus took 502.114 ms
under its 520 ms guard. These are historical wall references, not current-tree
treatment results.

## Device settled AA

This is the same-revision-162 comparison used to accept settled-tile IRAM.
Work counters match and every pass reports zero failures.
Source: [`SETTLED_AA_ALL_ZOOM_2026_08_18.md`](../../archive/2026-08-vector-v2-performance/SETTLED_AA_ALL_ZOOM_2026_08_18.md).

| Zoom | Previous | Current | Gain |
|---:|---:|---:|---:|
| 25% | 407.426 ms | 377.439 ms | 7.36% |
| 50% | 606.535 ms | 561.363 ms | 7.45% |
| 100% | 903.449 ms | 840.370 ms | 6.98% |
| 200% | 1,027.491 ms | 958.249 ms | 6.74% |
| 400% | 790.251 ms | 738.756 ms | 6.52% |

The five current passes total 3.476177 s, down from 3.735152 s (6.93%).
Maximum render slices remain between 1.97 and 2.35 ms. The retained mapping
costs 4,528 bytes of text and 4,608 bytes of internal heap. Revision-162 slice
counts were not recorded.

## Device interaction and presentation

Source: [`PERFORMANCE_REVIEW_ROUND_2026_08_18.md`](PERFORMANCE_REVIEW_ROUND_2026_08_18.md).

| Measurement | Current retained value |
|---|---:|
| warm pan p95, 100% | 33.940 ms |
| warm pan p95, 400% | 33.939 ms |
| cooperative full compose | 56 slices; 0.499 ms maximum |
| largest ring-local submission | 71,240 logical pixels |
| full canvas submission | 136,896 logical pixels |
| mixed pen absorption, 25% | 2.118 ms maximum; backlog 1 |
| mixed eraser absorption, 25% | 2.048 ms maximum; backlog 1 |
| dense 25% absorption trace | 1.896 ms maximum |
| every other accepted absorption trace | at most 1.993 ms |
| latest recorded draw-while-fill poll gap | 4.917 ms |
| touch overflow / resynchronization | 0 / 0 |

These are the last retained accepted interaction numerics, captured on earlier
gate images. The final F2/F13/F24 gate preserved pass and failure status but did
not preserve a same-tree timing table. Current ring-local wall time, final
live-stroke append maxima, final product pan distribution, and final
event-to-submit/display percentiles were not recorded. The table does not use
older falsified beam-race pan numbers.

## Host Undo, Redo, and occupancy

The history index adds no storage. The 4,000-operation benchmark is pixel exact.
Sources: [`F10_HISTORY_SPATIAL_REPLAY_2026_08_18.md`](../../archive/2026-08-vector-v2-performance/F10_HISTORY_SPATIAL_REPLAY_2026_08_18.md)
and [`F2_OCCUPANCY_HISTORY_REBUILD_2026_08_18.md`](../../archive/2026-08-vector-v2-performance/F2_OCCUPANCY_HISTORY_REBUILD_2026_08_18.md).

| Corpus | Previous mean/move | Current mean/move | Result |
|---|---:|---:|---:|
| sparse Undo/Redo | 0.0248–0.0313 ms | 0.000279–0.000405 ms | 77.4–89.4x |
| dense Undo/Redo | 0.1644–0.1656 ms | 0.1557–0.1651 ms | 1.00–1.06x |

The occupancy rebuild uses 1,288 bytes of existing scratch. Rebuild cost is
1.125 us for the representative Undo, 3.375 us for Redo, and 1.542 us for
branch replacement.

| Producer corpus | 100% | 200% | 400% |
|---|---:|---:|---:|
| drifting erase | 170.667 us | 54.875 us | 23.333 us |
| Undo | 136.166 us | 46.459 us | 24.208 us |
| Redo | 219.500 us | 93.166 us | 35.500 us |
| branch replacement | 162.541 us | 56.208 us | 27.291 us |
| adversarial full Undo | 13.917 us | 9.000 us | 11.625 us |

These host figures measure prefix reconstruction and subsequent production.
No final device end-to-end Undo/Redo latency was recorded. Dense hairline and
eraser history still takes the exact full-prefix fallback, invalidates affected
high-zoom detail, presents overview fallback, and repairs detail progressively.

## Host cache commit and repair

The production-shaped cache benchmark uses 448 raw slots and 13,692 uniform
identities. The retained-key treatment adds no storage.
Sources: [`F21_CACHE_COMMIT_SCANS_2026_08_18.md`](F21_CACHE_COMMIT_SCANS_2026_08_18.md)
and the [consolidated F26 receipt](PERFORMANCE_REVIEW_ROUND_2026_08_18.md).

| Workload | Current median |
|---|---:|
| warm settled-AA publication | 0.172 us/tile |
| publication with 280 resident slots | 1.181 us/tile |
| full-pool publication/eviction | 1.815 us/tile |
| local absorption, 56 retained raw tiles | 3.333 us |
| full-world history, 448 retained raw tiles | 59.250 us |
| full uniform catalog | 131.750 us |

At equal 64-tile repair work, pan-directed repair changes avoided refills from
16 to 48 for forward travel, 8 to 8 for reverse travel, 11 to 18 for local
random movement, and 13 to 15 for random navigation. Unused repaired tiles
evicted before use fall from 51 to 49. This is a host policy benchmark; no
current 448-slot device repair timing was recorded.

## Device autosave

The benchmark transfers the staged buffer to the worker but does not include
drawing-partition I/O.
Source: [`F20_AUTOSAVE_CALLER_LATENCY_2026_08_18.md`](F20_AUTOSAVE_CALLER_LATENCY_2026_08_18.md).

| Measurement | 1,000 ops / 20,000 samples | 4,000 ops / 80,000 samples |
|---|---:|---:|
| encoded / allocated | 176,196 / 180,224 B | 704,196 / 704,512 B |
| slices | 11 | 43 |
| slice p95 / max | 0.864 / 0.866 ms | 0.818 / 0.822 ms |
| first-call p95 / max | 1.273 / 1.708 ms | 2.999 / 3.194 ms |
| queue p95 / max | 5 / 12 us | 4 / 6 us |
| worker seal p95 / max | 8.315 / 8.359 ms | 32.708 / 32.726 ms |
| minimum free / largest PSRAM | 2,007,300 / 1,998,848 B | 1,483,012 / 1,474,560 B |

Normal-product seal and flash-I/O timing was not captured as a controlled final
baseline. Journal-full recycling also remains open; the current journal fails
closed at capacity.

## Device memory and export

Source: [`PERFORMANCE_REVIEW_ROUND_2026_08_18.md`](PERFORMANCE_REVIEW_ROUND_2026_08_18.md).

| Point | Free internal | Free PSRAM | Largest/held detail |
|---|---:|---:|---:|
| producer startup | 217,364 B | 2,189,168 B | not recorded |
| after SVG + PNG Export | 84,168 B | 2,187,528 B | export pass |
| 1.5 MiB reserve held | not recorded | 614,660 B | largest 606,208 B; pass |

The owner accepted the final Export interaction. A current combined SVG+PNG
elapsed time and byte count were not preserved, so historical SVG-only and PNG
measurements are not presented as the final baseline.

## Measurement gaps carried forward

- complete post-F24 eleven-case paced cold wall and panel table;
- final product pan distribution and ring-local wall time;
- final live-stroke append and event-to-display percentiles;
- device end-to-end dense Undo/Redo latency;
- current 448-slot idle-repair wall time;
- controlled normal-product autosave flash-I/O and combined Export duration.

These gaps do not invalidate the accepted gates. They mark measurements that a
future performance cycle must collect before claiming improvement in those
areas.
