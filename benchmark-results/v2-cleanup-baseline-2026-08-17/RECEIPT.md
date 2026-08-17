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

Fresh product firmware was flashed and hash-verified on `/dev/cu.usbmodem1101` after the host builds:

- Producer scratch: internal; slot directory: PSRAM.
- Free internal RAM after producer setup: 247,448 bytes.
- Ready-state PSRAM: 2,278,540 bytes free; 2,228,224-byte largest block.
- Live storage: 5,915,800 bytes; overview: 1,318,912 bytes; raw tiles: 3,670,016 bytes.
- Startup compose: 26,101 us; chrome: 13,478 us; transfer wait: 20,540 us; pass.
- TE period: 16,804 us; high interval: 578 us.
- Initial settle: 42 tiles in 293,168 us; no failures.
- Autosave restored generation 53 with 15 retained operations at sequence 57.

The fresh 384-slot gate battery completed without a crash. Every named gate passed except
`overlap_cold`: its 50% cold rebuild took 604,187 us against a 500,000 us limit. The 100%, 200%,
and 400% overlap runs passed; the final settled-AA receipt remains yellow. This is the cleanup
baseline, so Wave 1 must not introduce additional failures and should not claim to have fixed this
performance miss without a dedicated measurement.

Any cleanup that changes allocation order, presenter pacing, IRAM placement, or hot-path budgets must produce a fresh device receipt before its temporary layout pads are reclaimed.
