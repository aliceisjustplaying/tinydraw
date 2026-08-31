# ESP32-S3 MMIO calibration

This directory preserves twelve strict hardware receipts for the matched IRAM
MMIO cells added by commits `2a43798b1dc5263aa3bed5e435189fc0e569b889`
and `6f22350a95ccc3eba4eebfbd89bb98582e0087e0`.

## Fixed configuration

- ESP-IDF: `v6.0.2`
- compiler: GNU `15.2.0`
- CPU: 240 MHz
- flash: QIO, 80 MHz
- PSRAM: octal, 80 MHz
- ELF SHA-256: `21ba3dba138373e4f8227bdded35131a4d37012244c36b847c83efe4f72a05da`
- sdkconfig SHA-256: `5befec96cb7e4dbd86a69abccf96828696b0c79cfe0b0c904d5bdc75543d3d68`

Each cell performs 4,096 aligned 32-bit operations. Read peers use the exact
same load/XOR loop; write peers use the exact same store loop and final `memw`.
`mmio_probes_source.test.ts` pins their source, addresses, dynamic instruction
shapes, and built ESP32-S3 encodings.

## Two-boot result

| boot | kernel | exact distribution | cache-counter signature |
| --- | --- | --- | --- |
| `1cdbd47b85c8-23deca5c-00012899` | SRAM read | 12,306 cycles x 100 | all zero |
| `1cdbd47b85c8-23deca5c-00012899` | SYSTEM CPU_PER_CONF read | 45,074 cycles x 100 | all zero |
| `1cdbd47b85c8-23deca5c-00012899` | EXTMEM CACHE_STATE read | 45,074 cycles x 100 | all zero |
| `1cdbd47b85c8-23deca5c-00012899` | RTC_CNTL STORE1 read | 372,053..372,269 cycles; median 372,158; 51 values | IBUS 176 accesses, 0 misses; DBUS zero |
| `1cdbd47b85c8-23deca5c-00012899` | SRAM write | 4,119 cycles x 100 | all zero |
| `1cdbd47b85c8-23deca5c-00012899` | EXTMEM counter-clear write | 16,399 cycles x 100 | all zero |
| `1cdbd47b85c8-52eff8c9-0001289c` | SRAM read | 12,306 cycles x 100 | all zero |
| `1cdbd47b85c8-52eff8c9-0001289c` | SYSTEM CPU_PER_CONF read | 45,074 cycles x 100 | all zero |
| `1cdbd47b85c8-52eff8c9-0001289c` | EXTMEM CACHE_STATE read | 45,074 cycles x 100 | all zero |
| `1cdbd47b85c8-52eff8c9-0001289c` | RTC_CNTL STORE1 read | 372,380..372,542 cycles; median 372,461; 41 values | IBUS 176 accesses, 0 misses; DBUS zero |
| `1cdbd47b85c8-52eff8c9-0001289c` | SRAM write | 4,119 cycles x 100 | all zero |
| `1cdbd47b85c8-52eff8c9-0001289c` | EXTMEM counter-clear write | 16,399 cycles x 100 | all zero |

The SYSTEM CPU_PER_CONF and EXTMEM CACHE_STATE reads each add exactly 32,768
cycles over the matched SRAM peer, or 8 cycles per read. That scalar is safe
only for these repeated aligned read cells. RTC_CNTL STORE1 retains asynchronous
bridge variation: its two-boot additive range over SRAM is 359,747..360,236
cycles across 4,096 reads (about 87.83..87.95 cycles/read), so this evidence
does not support an exact RTC scalar. The counter-clear write adds a stable
12,280 aggregate cycles over SRAM; the non-integral 2.998046875 cycles/write
subtraction is retained as an aggregate result and is not promoted to an exact
per-write scalar.

## Capture integrity

The full boot logs remain outside the repository because each is about 4 MiB.
Their SHA-256 values are:

- boot 1: `825d49cbafacd352b9bb8779ef3ab30eef482cf64f6293a1f5ae9dae9e8f4432`
- boot 2: `59fc17ac47a877c02585d7ede45386aff8a26068aec7c34429834097ab922ca5`

Both logs reached `run-complete` with `pass: true`. USB tearing affected only
unrelated groups. Complete-measurement recovery retained all twelve target
groups, and every committed receipt passes the strict receipt verifier.

Receipt SHA-256 values, in boot-1 then boot-2 alphabetical order:

- `84734d88b14edc6f91b7c6ad8ee630c3e5bfd070592d409404b5ed209a4b1e19`
- `1b24d815e4647a9a7954c53a30e054ed48172da5ee51b67633ea2e8d47f9d09e`
- `d32984a81dfefd2889dda74f39ca1ef8cb7e121fe767cba5067b2f591e7d1a83`
- `10f4a4ea7624fe6b9ebe9dc0fa4f3ff6205b310b69f61104f9051d3233358634`
- `27eba40b96f47ef50dea7dec6867da0cd28606310822f5fe7673fbe6ae3414bd`
- `ad26cbeba6e35a72fbc4c9ac7e17b6cece9d9fbf24233a81073582c427a9f09a`
- `bc83637267e80f147b6daef57b3f2f1b20ab74be2d8aaa1d8f1a6c152f67f212`
- `07a1c47aa9880901829dd7dc4b0d5b6b6fb933d26b8769835bdbe837f0b545c0`
- `5d1a653e4ca942d5edcce09dc07a18333b981f22f84b58adf8658821eb45f135`
- `74bfd5295132cbaa591de519054d4d084bfbc8c963aa8db4a89bd187a80cb560`
- `15d8414657deb59bcdc168b01a82854537b988e1d4e188135bd563d3735c2e83`
- `0ee0c8080ad9e11868ccd00ead35e5b09f2c2bc43fb1e385716bddc1cacdebb9`
