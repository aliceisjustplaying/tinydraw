# Blank-canvas interaction baseline — `3d4bde4`

Captured on the physical ESP32-S3 on 2026-08-13 before the navigation and input-latency changes.
The app started with a completely blank document. The manual workload used an XL brush, rapid and
dense strokes, aggressive panning, and repeated 25% → 100% → 400% zoom cycling.

## Integrity

- 86 committed strokes; all commits and authority checks passed.
- 498 presentations; no presentation failures.
- 147 progressive-fill records; no presentation failures.
- 20 null producer steps were recorded. These remain a diagnostic for the later scheduler work.
- 81 lift records were emitted. Five were intentionally dropped because the next gesture began
  before the prior report could be printed; `reports_dropped` preserved that count.
- The source serial capture contained 268,902 bytes and 1,060 lines. It was temporary and is not
  retained; the compact measurements below are the durable receipt.

All timing values below are microseconds and are shown as minimum / p50 / p95 / maximum.
Percentiles use the nearest-rank definition.

## Lift path

| Phase | min | p50 | p95 | max |
|---|---:|---:|---:|---:|
| Finish live preview | 732 | 1,744 | 3,736 | 3,743 |
| Finish operation builder | 39 | 45 | 46 | 47 |
| Incremental append | 2,244 | 25,857 | 134,805 | 213,211 |
| Full-view refresh wall time | 45,254 | 49,049 | 55,125 | 55,411 |
| Existing stroke diagnostic write | 28,421 | 29,040 | 29,293 | 29,296 |
| Same-loop progressive fill block | 0 | 20,467 | 24,094 | 37,863 |
| Lift detected → next poll start | 87,745 | 122,756 | 230,756 | 320,746 |
| Lift detected → next poll complete | 88,192 | 123,231 | 231,228 | 321,188 |
| Touch read | 442 | 447 | 474 | 475 |
| Unattributed tail | 1,088 | 1,599 | 1,998 | 2,044 |

The logging column is instrumentation overhead, not product work. Append, full-view refresh, and
same-loop progressive refinement are real scheduling costs. This baseline therefore confirms the
planned leaf fix: bounded visible publication, bounded refresh, and no fill work in the commit tick.

## Progressive fill

| Measure | min | p50 | p95 | max |
|---|---:|---:|---:|---:|
| Steps per aggregate | 1 | 7 | 51 | 155 |
| Aggregate compute | 852 | 41,906 | 544,367 | 1,029,290 |
| Longest compute step | 852 | 8,686 | 49,548 | 66,813 |
| Aggregate presentation | 0 | 4,689 | 48,161 | 60,508 |
| Longest presentation | 0 | 2,519 | 8,075 | 8,853 |
| Longest fill tick | 860 | 13,595 | 49,554 | 66,820 |

There were 79 completed fills and 68 superseded fills. The maximum cold fill required about one
second of total compute, but it was already split into steps. The remaining defect is that a single
step can still block input for up to 66.8 ms.

## Pan presentation

Across 456 pan presentations:

| Measure | min | p50 | p95 | max |
|---|---:|---:|---:|---:|
| Compose | 20,252 | 24,677 | 26,983 | 29,740 |
| First submit | 21,294 | 25,928 | 28,307 | 41,135 |
| First complete | 22,099 | 26,733 | 29,111 | 41,939 |

Fallback pixels ranged from 0 to the complete 164,864-pixel canvas. This is expected on a cold view
but should not recur after a committed stroke on an already refined view.

## Manual finding and diagnosis

The user observed that strokes on the blank canvas became visibly pixelated immediately after lift,
then sharpened when progressive replay completed. The same behavior appeared at 100% and 400%.
This was the first full manual test beginning with a blank document.

Root cause: blank refined tiles are represented by the compact uniform-paper catalog. Incremental
append enumerated only raw resident slots, invalidated intersecting uniform entries, and therefore
made the updated 25% overview the temporary source. Earlier seed-document tests mostly exercised
raw ink-bearing slots and did not expose this state.

Fix: `73e1646` includes affected uniform-paper tiles from the visible priority view in the bounded
incremental publication. A host regression models a completely refined blank 400% viewport and
requires zero fallback pixels and zero fallback tiles immediately after drawing.

## Verdict

- **Correctness:** authority and display publication passed, but the blank-paper post-lift fallback
  was a real product defect. Fixed in `73e1646`.
- **Latency:** baseline fails the provisional lift target. Worst observed next-poll latency was
  321 ms. The next implementation batch must remove full-view refresh and same-tick fill from the
  lift path and reduce bounded append work.
- **Navigation:** not evaluated here; this is the unchanged pre-navigation behavior baseline.
