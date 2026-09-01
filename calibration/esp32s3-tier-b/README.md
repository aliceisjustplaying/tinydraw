# ESP32-S3 Tier-B timing probe draft

This is an unmeasured, review-stage probe image for the Tier-B hardware queue in
esp32sim `docs/STATUS.md`. It targets the exact TinyDraw ESP32-S3 revision 0.2
board configuration under ESP-IDF v6.1. No number emitted by this draft is an
adopted timing claim. Two clean independent boots are required before a result
can enter an evidence receipt.

The unified image covers arbitration aggressors (internal SRAM, flash, and
PSRAM with a core-to-core start barrier and cache counters), PSRAM store hits,
clean and dirty writeback ladders at 1, 2, 4, 8, and 16 cache lines,
instruction-PSRAM hot and cold fetches, and first-line pooling for I-flash,
D-flash, and D-PSRAM. Display-path cells cover panel QSPI payload sweeps, GDMA
and SPI2 transfer sweeps, touch I2C transactions, GPIO 21 edge timing,
`esp_cache_msync` writeback and invalidate sweeps, plus PSRAM and flash
bandwidth under cross-core contention.

`probe-cells.json` is the review inventory. `verify_draft.py` requires the
firmware cell table to match it exactly. The firmware refuses failed setup or
counter checks as typed `refusal` records, and the host validator treats each
refusal as a completed sample without converting it into a cost.

## Review-only build

```text
python3 calibration/esp32s3-tier-b/verify_draft.py
eim run "idf.py -C calibration/esp32s3-tier-b -B out/build/esp32s3-tier-b build"
eim run "idf.py -C calibration/esp32s3-tier-b -B out/build/esp32s3-tier-b-xip \
  -DTINYDRAW_TIER_B_XIP_PSRAM=ON build"
python3 -m unittest tools/test_tier_b_ndjson.py
```

Building does not flash or open the serial port. I-flash cells exist only in the
normal image. Instruction-PSRAM cells exist only in the XIP image because
`CONFIG_SPIRAM_FETCH_INSTRUCTIONS` remaps all flash text. Review both ELF maps
before any hardware session: each instruction ladder range must span its named
number of 32-byte lines, and the XIP map must place those ranges in the PSRAM
instruction copy.

## Maintainer capture session, after review

The command below is documentation only. It flashes hardware and opens serial,
so an agent must not run it without the board owner's explicit session:

```text
eim run "idf.py -C calibration/esp32s3-tier-b -B out/build/esp32s3-tier-b -p PORT flash"
uv run --script tools/tier-b-capture.py PORT results/boot-1/serial.log 900 --cells all
```

Selective recovery uses the same ELF and boot path:

```text
uv run --script tools/tier-b-capture.py PORT results/recovery/serial.log 180 \
  --cells first_line_i_flash,first_line_d_flash,first_line_d_psram
```

The capture tool sends `TIER_B_SELECT`, validates every prefixed NDJSON line as
it arrives, and prints a completeness tally. Malformed, truncated, out-of-order,
or count-inconsistent records exit 2. Offline validation uses:

```text
python3 tools/tier_b_ndjson.py results/boot-1/serial.log
```
