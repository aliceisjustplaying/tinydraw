# ESP32-S3 Tier-B timing probe draft

This is an unmeasured, review-stage probe image for the Tier-B hardware queue
in esp32sim `docs/STATUS.md`. It targets TinyDraw ESP32-S3 revision 0.2 under
ESP-IDF v6.1. No emitted number is an adopted timing claim. A result needs two
clean, independent boots before it can enter an evidence receipt.
The repository-wide `.idf-version` remains `v6.0.2`; every Tier-B `eim run`
below supplies `v6.1` explicitly. Omitting that final version argument is an
invalid Tier-B build or hardware session.

The committed `probe-cells.json` is the capture contract. It defines the exact
cells, image variants, and sample counts. Missing records, duplicate records,
refusals, unknown cells, and malformed NDJSON fail with exit 2 and do not
produce a receipt. `gpio21_edge` is an explicit open refusal because this image
cannot timestamp the electrical edge. It is excluded from `all` until a
hardware path can report both edge and ISR timestamps.

The normal and XIP images together cover arbitration aggressors, a PSRAM
store-hit cell with an internal-SRAM baseline and internal-RAM issue block,
clean and dirty writeback
ladders, instruction-PSRAM fetches, first-line pooling, selective cohort
reruns, and display-path and DMA cost families. Counter-dependent cells check
the expected access and miss counters. The hardware cache-counter registers
are shared across both cores. Before concurrent timing, core 1 runs a bounded,
isolated lap over the selected source and records its counters and exact
checksum. Flash and PSRAM laps require their matching access and miss counters;
the internal lap relies on its exact checksum and does not require external-miss
counters. The concurrent phase records runtime iterations and checksum, while the core 0
CCOUNT window contains only the victim traversal. The invalidate cell measures
a clean `M2C | INVALIDATE` operation;
dirty `C2M` writeback is measured separately.

Panel and touch initialization power-cycles their TCA9554 rails between boots
and checks the CST820 identity. The raw SPI2 transfer probe has no chip-select,
so its timed bytes cannot be interpreted by the panel.

## Review-only build and ELF verification

Build and verify both images before any capture. The verifier disassembles the
Tier-B issue blocks, checks their exact encodings, checks every 1, 2, 4, 8, and
16 cache-line ladder span and residue, checks five fresh one-line instruction
targets, and confirms XIP ladder placement. A
mismatch exits 2 and creates no verification result.

```text
python3 calibration/esp32s3-tier-b/verify_draft.py
eim run "idf.py -C calibration/esp32s3-tier-b \
  -B out/build/esp32s3-tier-b build" v6.1
eim run 'python calibration/esp32s3-tier-b/verify_elf.py \
  out/build/esp32s3-tier-b/esp32s3_tier_b_calibration.elf \
  out/build/esp32s3-tier-b/sdkconfig \
  out/build/esp32s3-tier-b/elf-verification.json \
  --variant normal \
  --objdump "$(command -v xtensa-esp32s3-elf-objdump)" \
  --compiler "$(command -v xtensa-esp32s3-elf-gcc)"' v6.1

eim run "idf.py -C calibration/esp32s3-tier-b \
  -B out/build/esp32s3-tier-b-xip \
  -DTINYDRAW_TIER_B_XIP_PSRAM=ON build" v6.1
eim run 'python calibration/esp32s3-tier-b/verify_elf.py \
  out/build/esp32s3-tier-b-xip/esp32s3_tier_b_calibration.elf \
  out/build/esp32s3-tier-b-xip/sdkconfig \
  out/build/esp32s3-tier-b-xip/elf-verification.json \
  --variant xip-psram \
  --objdump "$(command -v xtensa-esp32s3-elf-objdump)" \
  --compiler "$(command -v xtensa-esp32s3-elf-gcc)"' v6.1

python3 -m unittest \
  calibration/esp32s3-tier-b/test_verify_elf.py \
  tools/test_tier_b_capture.py \
  tools/test_tier_b_ndjson.py
```

The verification result pins the repository commit and dirty state, image
variant, ELF and sdkconfig hashes, compiler, objdump, exact issue-block
encodings, ladder spans, residues, and instruction placement. Runtime metadata
must match it and also reports chip model and revision, reset reason, and a
per-boot identity. Fixture-generated verifier results cannot authorize a
capture. Building and verifying do not flash or open serial.

## Maintainer hardware session, after review

These commands are documentation for the board owner. They flash hardware and
open serial, so agents must not run them outside an explicit hardware session.
The capture waits for the firmware READY marker before sending a selection and
observes a failure tail after completion before writing the receipt.
It hashes the exact ELF again and retains it under `~/Archives/esp32s3/tier-b`
before opening serial. Raw captures and receipts in these examples live in the
same archive tree.

Normal boot 1:

```text
eim run "idf.py -C calibration/esp32s3-tier-b \
  -B out/build/esp32s3-tier-b -p PORT flash" v6.1
uv run --script tools/tier-b-capture.py \
  PORT ~/Archives/esp32s3/tier-b/normal/boot-1/serial.log 900 \
  --variant normal \
  --preflight out/build/esp32s3-tier-b/elf-verification.json \
  --elf out/build/esp32s3-tier-b/esp32s3_tier_b_calibration.elf \
  --cells all
```

Normal boot 2:

```text
eim run "idf.py -C calibration/esp32s3-tier-b \
  -B out/build/esp32s3-tier-b -p PORT flash" v6.1
uv run --script tools/tier-b-capture.py \
  PORT ~/Archives/esp32s3/tier-b/normal/boot-2/serial.log 900 \
  --variant normal \
  --preflight out/build/esp32s3-tier-b/elf-verification.json \
  --elf out/build/esp32s3-tier-b/esp32s3_tier_b_calibration.elf \
  --cells all
```

XIP boot 1:

```text
eim run "idf.py -C calibration/esp32s3-tier-b \
  -B out/build/esp32s3-tier-b-xip -p PORT flash" v6.1
uv run --script tools/tier-b-capture.py \
  PORT ~/Archives/esp32s3/tier-b/xip-psram/boot-1/serial.log 900 \
  --variant xip-psram \
  --preflight out/build/esp32s3-tier-b-xip/elf-verification.json \
  --elf out/build/esp32s3-tier-b-xip/esp32s3_tier_b_calibration.elf \
  --cells all
```

XIP boot 2:

```text
eim run "idf.py -C calibration/esp32s3-tier-b \
  -B out/build/esp32s3-tier-b-xip -p PORT flash" v6.1
uv run --script tools/tier-b-capture.py \
  PORT ~/Archives/esp32s3/tier-b/xip-psram/boot-2/serial.log 900 \
  --variant xip-psram \
  --preflight out/build/esp32s3-tier-b-xip/elf-verification.json \
  --elf out/build/esp32s3-tier-b-xip/esp32s3_tier_b_calibration.elf \
  --cells all
```

Selective cohort recovery uses the same verified ELF and boot path:

```text
uv run --script tools/tier-b-capture.py \
  PORT ~/Archives/esp32s3/tier-b/recovery/serial.log 180 \
  --variant normal \
  --preflight out/build/esp32s3-tier-b/elf-verification.json \
  --elf out/build/esp32s3-tier-b/esp32s3_tier_b_calibration.elf \
  --cells first_line_i_flash,first_line_d_flash,first_line_d_psram
```

Offline validation derives the same exact contract from the committed
manifest:

```text
python3 tools/tier_b_ndjson.py ~/Archives/esp32s3/tier-b/normal/boot-1/serial.log \
  --variant normal --cells all
```
