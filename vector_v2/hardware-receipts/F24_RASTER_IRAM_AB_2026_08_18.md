# F24 raster IRAM A/B — accepted

## Decision

**ACCEPTED.** The linker places the whole `incremental_rasterizer` object in
IRAM. A same-tree physical A/B improved all 11 cold/settled compute cases by
6.93–11.68%, with an 8.70% median and zero regressions. Product and gate builds,
the full hardware gate, input paths, export, and reserve guards are green.

## Linker and image evidence

| Build | Baseline IRAM `.text` / end | Treatment IRAM `.text` / end | IRAM cost | Internal heap change |
|---|---|---|---:|---:|
| Product | `0x19c27` / `0x4038e100` | `0x1c67b` / `0x40390b00` | +10,836 B | about -10,880 B |
| Gate | `0x19d37` / `0x4038e200` | `0x1d06b` / `0x40391500` | +13,108 B | about -13,056 B |

Actual binary size is effectively unchanged: product `0x104e60` → `0x104e90`;
gate `0x132220` → `0x1321c0`.

## Root-tree physical A/B

| Case | Baseline `compute_us` | Treatment `compute_us` |
|---:|---:|---:|
| 1 | 384,548 | 356,193 |
| 2 | 209,028 | 193,339 |
| 3 | 187,580 | 174,575 |
| 4 | 150,683 | 140,048 |
| 5 | 325,196 | 287,197 |
| 6 | 313,090 | 282,271 |
| 7 | 386,304 | 349,950 |
| 8 | 415,692 | 379,544 |
| 9 | 155,339 | 142,634 |
| 10 | 184,432 | 165,092 |
| 11 | 100,280 | 90,191 |

Every case passed. Improvement range was 6.93–11.68%; median improvement was
8.70%.

Free internal heap at producer startup changed from 234,964 to 221,972 bytes
(-12,992); Export changed from 102,024 to 88,776 bytes (-13,248). PSRAM was
unchanged. The full gate returned all ones, including pan, mixed-workload,
Export, and reserve checks. The measured speedup clears the runtime gate while
retaining adequate internal headroom, so the whole-object placement is kept.
