# CO5300 panel hardware limits (measured)

Measured: 2026-08-15, ESP32-S3 Touch AMOLED 1.8 (V2 hardware, CO5300
controller), panel-probe target (`./scripts/esp32 panel-probe PORT [CLOCK]`).
Receipts: `benchmark-results/blockA-panel-limits/probe-{40,50,60}mhz.log` at
[`v2-feature-complete-pre-cleanup`](docs/EVIDENCE_ARCHIVE.md).

This file records **software-measured physics only**. Nothing here is an
optical tearing verdict; those belong to the Block B camera cells. Every
number below has a serial-log receipt.

## Measured facts

### 1. TE signal (both edges, 350 samples each, 3 boots)

| Quantity | Value | Receipt |
|---|---|---|
| Period p50 | 16,773 µs (59.62 Hz) | `TINYDRAW_PROBE_TE_PERIOD rising` |
| Period spread (min–max) | 16,757–16,789 µs | same |
| High time | 578 µs (3.4% duty) | `TINYDRAW_PROBE_TE_HIGH` |
| ISR→task resume | p50 9 µs, max 15 µs | `TINYDRAW_PROBE_TE_RESUME` |
| Timeouts in 2,100 waits | 0 | `TINYDRAW_PROBE_TE` |

The signal is clean, stable across boots, and task wake latency is
negligible. Scheduling jitter is **not** a plausible tear mechanism at these
magnitudes.

### 2. QSPI clock: requests of 40, 50, and 60 MHz are identical

44-row continuation-stream full-frame walls, p50:

| Requested | Wall p50 |
|---|---|
| 40 MHz | 17,999 µs |
| 50 MHz | 17,998 µs |
| 60 MHz | 17,998 µs |

Identical to measurement noise. The ESP32-S3 GPSPI divider from the 80 MHz
source offers no value between 40 and 80 MHz, so **all historical "50 MHz"
and "60 MHz" configurations ran at 40 MHz actual**. Consequences:

- The published CO5300 envelope (50 MHz max) was never exceeded; the
  "diagnostic overclock" concern was moot.
- Actual wire ceiling: 40 MHz × 4 lanes / 16 bpp = **10.0 Mpixel/s =
  20 MB/s = 27.2 full-width rows/ms**.
- The offline review's P0-2 rate analysis assumed 15 Mpixel/s at 60 MHz;
  the real writer advances at ~27.2 rows/ms against a ~26.7 rows/ms modeled
  beam — **near parity, not 1.53×**. All beam-race margin math must be
  redone from this number if beam racing is ever revisited.

### 3. Full-frame transfer time (368×448 RGB565)

| Path | Strip rows | Transactions | Wall p50 |
|---|---|---|---|
| windowed push (CASET/RASET/RAMWR per strip) | 44 | 11 | 18,999 µs |
| windowed push | 8 | 56 | 23,998 µs |
| continuation stream (window once, RAMWR+RAMWRC) | 44 | 11 | 17,998 µs |
| continuation stream | 8 | 56 | 19,998 µs |

Derived costs:

- Marginal cost per extra color transaction: **~44 µs**
  ((19,998−17,998)/45).
- CASET/RASET window setup: **~90 µs per window**
  ((18,999−17,998)/11 vs stream).
- Raw 40 MHz payload for the full frame: 16,487 µs; measured best wall
  17,998 µs → **~1.5 ms total pipeline overhead** (transaction setup,
  staging stalls, 1 ms-quantized completion poll).
- The folklore "~0.4 ms per transaction" (transport comment) is falsified;
  actual is an order of magnitude lower.

### 4. Staging bandwidth (PSRAM → internal DMA + byte swap)

| Kind | Full frame | Rate |
|---|---|---|
| `memcpy` (std::copy_n) | 20,094 µs | 16.4 MB/s |
| `stage_pixels_swapped` | 9,724 µs | 33.9 MB/s |
| `stage_ring_row` (shifted) | 9,628 µs | 34.2 MB/s |

Staging (34 MB/s) is faster than the wire (20 MB/s), so a pipelined
presenter is **wire-bound**, not staging-bound. Ring de-rotation is free
relative to the plain swap. (The slower `std::copy_n` result is a compiler
artifact of the u16 copy loop; the swap loops use wider accesses.)

### 5. TE-synchronized streaming cadence (rising edge, region height sweep)

| Region rows | Edge→DMA-complete p50 | Frame interval p50/p95 | Sustained rate |
|---|---|---|---|
| 448 (full) | 18,474 µs | 34,000 / 34,000 µs | 29.4 FPS |
| 424 | 17,506 µs | 34,000 / 34,000 µs | 29.4 FPS |
| 400 | 16,604 µs | 17,000 / 33,000 µs | mixed — at the boundary |
| 368 | 15,401 µs | 17,000 / 17,000 µs | **58.8 FPS** |

The single-period boundary sits at ~390–400 rows with the current ~1.5 ms
overhead. **A full-frame edge-synced sweep can never exceed 29.4 FPS at
40 MHz; a ≤368-row region sustains 58.8 FPS.** Shaving overhead (finer
completion wait, pre-staged first strips) could push the boundary toward
~420 rows but can never fit 448 rows in one period (payload alone is
16.5 ms against a 16.77 ms period).

### 6. GETSCANLINE (0x45) — read path non-functional

The QSPI read transaction completes (no error, ~36 µs) but always returns
0. Control reads settle the attribution: RDDID (0x04), RDDST (0x09),
RDDPM (0x0A), RDDMADCTL (0x0B), RDDCOLMOD (0x0C), and brightness readback
(0x52 — written 0xFF at init) **all read 0**
(`probe-40mhz-readcontrol.log` at the archived tag). QSPI register reads are broken or
unsupported as configured (likely missing dummy-cycle insertion in the
`esp_lcd` SPI io path). **No software beam-position oracle exists; optical
capture is the only tear instrument.**

## Falsified prior claims

1. "60 MHz quad QSPI" (transport config/comments, review P0-2 premise):
   never engaged; actual 40 MHz.
2. "~0.4 ms per-transaction setup" (transport comment): actual ~44 µs
   marginal, ~90 µs per window pair.
3. "~11 ms full-frame wire floor" (`PAN_FLOOR_CLOSURE_2026_08_15.md`):
   actual 16.5 ms payload, 18.0 ms measured wall.
4. White-notch attribution to overclock signal integrity: there was no
   overclock; the mechanism is still unknown.

## Product implications (pending optical confirmation)

- 24 FPS pan floor: physically comfortable at any region size.
- 30 FPS pan target: full-frame edge-synced tops out at 29.4 FPS;
  the canvas region (dock excluded) at ≤~390 rows can run one-period
  (58.8 FPS ceiling) if the boundary-sweep policy proves optically clean.
- Rising-edge start gives the writer a 578 µs (~15 row) head start over
  active scan, and the writer then holds ~parity with the beam
  (27.2 vs 26.7 rows/ms). Whether that parity is optically safe is
  exactly what the Block B A/B decides — it cannot be decided in software.

## Open questions for Block B (optical)

1. Does the rising-edge row-zero sweep produce zero tears at 29.4 FPS
   full-frame and at 58.8 FPS canvas-region?
2. Same question for falling edge (control cell).
3. Beam-race control cell at the *real* 40 MHz rates.
4. White-notch reproduction and localization with guard columns.
5. GETSCANLINE validation with an ID-read control; if reads work, a
   software beam oracle replaces most future camera sessions.
