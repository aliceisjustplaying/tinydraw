# F20 autosave caller-latency receipt — 2026-08-18

Hardware: ESP32-S3 at 240 MHz with 8 MiB octal PSRAM. Measurements used the
production authority capacities, allocation capabilities, wire format, and
encoder. Each result is 20 runs after startup. The treatment queue measurement
transferred the staged-buffer pointer without writing the drawing partition;
it does not measure normal-product flash I/O.

## Attempts

| Treatment | 1,000 ops / 20,000 samples caller p95 | 4,000 ops / 80,000 samples caller p95 | Result |
| --- | ---: | ---: | --- |
| Synchronous bit-at-a-time CRC | 53.389 ms | 213.316 ms | Red |
| 256-entry CRC table | 26.685 ms | 106.952 ms | Red |
| CRC table plus compact-sample bulk copy | 25.344 ms | 101.118 ms | Red |
| Stage synchronously, seal on worker | 8.924 ms | 35.641 ms | Red |
| 16 KiB resumable stage, seal on worker | 1.273 ms | 2.999 ms | Green |

The CRC table remains because it approximately halves both synchronous CRC
measurements and the worker seal cost. The table costs 1 KiB of flash.

## Accepted treatment

Representative authority encoded 176,196 bytes into a 180,224-byte allocation:

- 11 slices; slice p95/max 0.864/0.866 ms
- first-call p95/max 1.273/1.708 ms
- final queue-transfer p95/max 5/12 us
- worker seal p95/max 8.315/8.359 ms
- minimum free/largest PSRAM 2,007,300/1,998,848 bytes

Full authority encoded 704,196 bytes into a 704,512-byte allocation:

- 43 slices; slice p95/max 0.818/0.822 ms
- first-call p95/max 2.999/3.194 ms
- final queue-transfer p95/max 4/6 us
- worker seal p95/max 32.708/32.726 ms
- minimum free/largest PSRAM 1,483,012/1,474,560 bytes

The caller gate is 4 ms and the per-slice gate is 2 ms. Payload budgeting
includes each 16-byte operation record and copied sample bytes, including a
single 65,535-sample operation. Every resume fingerprints the full authority
view. The previous committed journal remains authoritative until complete
staging hands its buffer to the worker.

## Correctness and product impact

Debug, Release, and ASan authority suites each passed 77/77 cases and 25,589
assertions. Coverage includes header-only resumption, stale-view abandonment,
post-submit authority mutation, maximum single-operation slicing, full-capacity
recovery, and corrupt-tail fallback to the preceding recovery point.

The integrated product image is 0x104900 bytes with 0x7b700 bytes (32%) free in
the smallest app partition. This is 3,472 bytes larger than the pre-round
0x103b70 image and includes concurrent performance-lane changes. Product boot
restored 109 active/retained operations at generation 140 and reached Ready
without a watchdog, crash, or stack failure.

The integrated full hardware gate passed every automated verdict after this
treatment. Product firmware was then restored; it recovered generation 140
with 109 active/retained operations and reached Ready without a watchdog,
crash, or stack failure.

The author later completed a single-tap product Export and accepted its glass
latency. A controlled normal-product checkpoint with separate `seal_us` and
`io_us` was not recorded. Journal-full recycling remains open and requires a
two-bank recovery design; the current journal fails closed at capacity.
