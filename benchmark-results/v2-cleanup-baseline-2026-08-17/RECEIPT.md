# Vector V2 cleanup baseline

Baseline tag: `v2-feature-complete-pre-cleanup`

Commit: `3f23c09bf0c9377851d4c384f494f55917e1b5bb`

## Host validation

- `./scripts/dev release`: 29/29 tests passed in 3.33 s.
- `./scripts/dev asan`: 11/11 tests passed in 82.07 s.

## Firmware builds

- Product: 1,063,024-byte binary (`0x103870`), 32% of the 1.5 MiB app partition free.
- Product SHA-256: `8c21326d56ff5e8d6eb737836c7783fe703fafd540fb4b7601a3bcc2b29b6710`.
- Gate, 384 tile slots: 1,236,016-byte binary (`0x12dc30`), 33% of the 1.75 MiB gate partition free.
- Gate SHA-256: `675537bc87051a244447cdd810e38892e66d42a1c00df4c4a9171074df27d9fc`.

Both firmware builds used ESP-IDF v6.0.2 and completed without errors. The existing deprecated touch-driver warning remains.

## Device allocation and timing reference

The accepted product device record is
[`zoom-cycle-return-2026-08-17/product-device.log`](../zoom-cycle-return-2026-08-17/product-device.log):

- Producer scratch: internal; slot directory: PSRAM.
- Free internal RAM after producer setup: 308,784 bytes.
- Ready-state PSRAM: 2,278,592 bytes free; 2,228,224-byte largest block.
- Live storage: 5,915,800 bytes; overview: 1,318,912 bytes; raw tiles: 3,670,016 bytes.
- TE period: 16,813 us; high interval: 579 us.

No ESP32-S3 serial device was attached while this receipt was created, so the hardware values above are the latest accepted on-device record. Any cleanup that changes allocation order, presenter pacing, IRAM placement, or hot-path budgets must produce a fresh device receipt before its temporary layout pads are reclaimed.
