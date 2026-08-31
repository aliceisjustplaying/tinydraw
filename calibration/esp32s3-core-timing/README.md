# ESP32-S3 core-timing calibration

CPU-side cost categories the memory-timing harness does not cover: window
overflow/underflow exceptions, straight-line issue rate by instruction
width and dependency, zero-overhead-loop body alignment, and interrupt
entry/resume latency through the ESP-IDF dispatcher.

Same board, same CAL_RECORD line format, same sdkconfig as
`../esp32s3-memory-timing/`. Window and issue trials run at INTLEVEL 15 so
no tick lands inside a measured window. Instruction encodings and loop-body
alignments are verified from the built ELF with objdump before flashing
(plain `or`/`addi` get narrowed by the assembler; the probes use `_or` and
`_addi` no-transform forms, and the loop probes live in
`main/core_timing_loops.S` where layout is controlled from the function
label inside a no-transform region).

## Run

```text
cd calibration/esp32s3-core-timing
eim run "idf.py -B ../../out/build/esp32s3-core-timing-calibration build"
eim run "idf.py -B ../../out/build/esp32s3-core-timing-calibration -p /dev/cu.usbmodem101 flash"
uv run --script ../../tools/esp32-capture.py /dev/cu.usbmodem101 results/boot-N/serial.log 120 \
  --end-marker CALIBRATION_DONE \
  --failure-regex 'Guru Meditation|assert failed|CALIBRATION_FAILED|task_wdt'
```

## Results, 2026-08-31, two boots

Board: TinyDraw ESP32-S3R8 (revision 2), 240 MHz, ESP-IDF v6.0.2, tinydraw
worktree at `9fa31aa0d015bcfc4639a0dd7517a96faafeeb74` (this directory
untracked at capture time). Raw logs:

- `results/boot-1/serial.log`, SHA-256
  `175c451447ba2cb25d30fdc76fbf489e74bd4881fc2cbba350f7ec549e2531cf`
- `results/boot-2/serial.log`, SHA-256
  `ee526ce363d50edb0a49f8a1c246f34d7a29accad735eba6c3d94984be2b6c4e`

Every headline number below is identical in both boots.

### Window overflow/underflow: 35 cycles per spilled frame

Non-tail callx8 recursion, 256 chains per trial, median cycles per full
chain call:

| Depth | Cycles/call | Per added level |
| --- | --- | --- |
| 0 | 20 | |
| 1 to 5 | 36 to 100 | exactly 16 |
| 6 to 32 | 151 to 1,478 | exactly 51 (16 + 35) |

A call level without window pressure costs 16 cycles (callx8 + entry +
add + retw.n in this shape). From depth 6 on, every additional level pays
one WindowOverflow8 plus one WindowUnderflow8, together 35 cycles. The
knee at depth 6 matches the 64-entry physical register file with the
harness frames below the chain. Deep trials show at most 3 distinct sample
values in 9; medians are boot-identical.

### Straight-line issue: 1.000 cycles per instruction, width-insensitive

256-instruction IRAM blocks, 4,096 calls per trial, empty-call baseline
subtracted, cycles per instruction:

| Block | Boot 1 | Boot 2 |
| --- | --- | --- |
| 2-byte `nop.n` | 1.0000 | 1.0000 |
| 3-byte `_or a8,a8,a8` | 1.0005 | 1.0005 |
| alternating 2/3-byte | 1.0003 | 1.0003 |
| serially dependent `_addi a8,a8,1` | 1.0005 | 1.0005 |
| four-way independent `_addi` | 1.0003 | 1.0003 |

Single-issue, in-order, full forwarding: ALU dependency chains and width
mixes cost nothing extra in straight-line IRAM code. The base issue cost of
1 cycle in the pack's `timing.json` generalizes across these shapes.

### Loop-body alignment: +1 cycle per iteration at +3 mod 4

1,024-iteration `loopnez` over eight `nop.n`, 64 calls per trial, body
start residues verified from the ELF:

| Body start mod 4 | Cycles per iteration |
| --- | --- |
| +0 | 8.018 |
| +1 | 8.017 |
| +2 | 8.019 |
| +3 | 9.017 |

A loopback target at +3 mod 4 pays one extra cycle every iteration, a
12.5 percent penalty on this body. The assembler knows: with
transformations enabled, gas re-aligns Xtensa loop targets on its own
(observed while building these probes), so compiled firmware usually sits
in the fast case, and a timing model that ignores alignment is wrong
exactly on the loops the assembler could not fix.

### Interrupt entry and resume: 228 and 142 cycles, deterministic

Software interrupt (INTSET) through the ESP-IDF dispatcher to an IRAM
handler, 33 samples per level, min = median in every cell, identical
across boots:

| Path | Level 1 | Level 3 |
| --- | --- | --- |
| WSR INTSET to handler first instruction | 228 | 223 |
| handler last instruction to interrupted task | 142 | 138 |

About 1.5 microseconds round trip at 240 MHz. These are exact-tier
candidates for the dispatcher-inclusive path real ESP-IDF firmware
actually takes; bare-vector latency without the dispatcher is a separate,
smaller number this probe does not measure.

## Adoption status

These are candidates, `microbenchmarkToArchitecturalCost: unreviewed`, in
the sense of the puck repository's timing-lab discipline. Mapping reviews
belong to that repository's timing lane
(`packs/esp32-s3-touch-amoled-18/timing/`), with decision 0008's tier
vocabulary (window pair and interrupt latencies as `exact` candidates; the
alignment rule as an `exact` conditional cost).

## Board state

This firmware replaces whatever was previously flashed (it was the
`esp32s3-memory-timing` calibration firmware). Restore it with that
project's `run.sh`, which rebuilds and reflashes.
