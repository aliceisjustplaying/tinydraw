# External review packet

The 2026-08-18 packet is a snapshot of the committed cleanup tree.
It contains the source needed to build the macOS host, Raster V1, Vector V2,
QEMU, and the ESP32 gate harness. It also contains the current contract, design
notes, performance chronicle, compact receipts, tests, fixtures, and a patch
against the recorded Git HEAD.

Start with `READ_FIRST.md` in the archive, then read
`REVIEW_BRIEF.md`, `source/PROJECT_STATE.md`, `source/SHIP_CONTRACT.md`, and
`source/docs/PERFORMANCE_CHRONICLE.md`. The brief distinguishes accepted work
from open release gates and gives file-and-line entry points for each review
question.

## Provenance

`provenance/` records the exact commit, working-tree status, diff statistics,
recent history, and validation results. `MANIFEST.sha256` covers every file
inside the packet. The archive has a separate SHA-256 file beside it.

## Exclusions

The packet excludes credentials, generated builds, managed ESP-IDF components,
raw serial logs, videos, old review archives, local reference clones, unrelated
hardware PDFs, and prior packet archives. Compact receipts retain the decisive
measurements and name any external raw artifacts. `wifi_credentials.local.h`
is never included; only its example file is present.

## Reproduction

From `source/` on macOS:

```sh
./scripts/dev test
./scripts/dev release
./scripts/dev asan
./scripts/dev format-check
./scripts/dev tidy
./scripts/dev cppcheck
```

The first three commands pass in the source snapshot. Current quality-tool
failures are disclosed in `REVIEW_BRIEF.md` and the packet validation record.
ESP-IDF v6.0.2 is pinned by `.idf-version`; `./scripts/esp32 build`,
`./scripts/esp32 raster-v1`, and `./scripts/esp32 qemu` cover the retained
firmware targets. Physical timing numbers require the ESP32-S3/CO5300 board.
