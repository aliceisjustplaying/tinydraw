# ESP32-S3 ROM callback timing evidence

This cohort measures the exact state-safe TinyDraw replay shapes at ROM entry PCs
`0x4000057c`, `0x400011e8`, and `0x40001a4c`. Each target is paired with an IRAM no-op caller
having identical argument setup, literal load, `callx8`, return-value handling, and measurement
window. Commit `0b187a0d84a75b5a86b9043b0396e1c6eed9fa53` is the fetchable bounded capture source.

## Fixed configuration

- device: one disposable ESP32-S3 revision 0.2 board; hardware identifier redacted
- CPU: 240 MHz
- PSRAM: 8 MiB octal at 80 MHz
- flash: 16 MiB QIO at 80 MHz
- ESP-IDF: v6.0.2; compiler: GNU 15.2.0
- timing-probe ELF SHA-256: `32e568fd772ebb18d39ac9630b685cce1ae57e8a02db060f0ed3762d9893ad25`
- sdkconfig SHA-256: `5befec96cb7e4dbd86a69abccf96828696b0c79cfe0b0c904d5bdc75543d3d68`

## Retained boots

Both runs emitted 20 complete 100-sample measurements and `pass=true`. The evidence retains
the ten single-core receipts from each boot.

| evidence | boot ID | retained host log | log SHA-256 |
| --- | --- | --- | --- |
| `boot-1/` | `device-a-64e53f34-0001871b` | `/private/tmp/rom-callback-captures/0b187a0/boot-2.log` | `336f9e1cf8524529c6fac128b69020d922bf5ce1060164132aa4e14f31c7d4e6` |
| `boot-2/` | `device-a-b6596919-00018722` | `/private/tmp/rom-callback-captures/0b187a0/boot-3.log` | `c7e3c393d1099654c3e2679d4baae9d663324026a3455a5b16d5114c6070ed94` |

The first otherwise-complete capture was excluded because reset interrupted one stale sample
JSON record; its log SHA-256 is
`029f48a4b7cc072a3aa50ae3e1b6931d29a0c4665f8c729d0b5535dd315c586a`. Retained receipts
replace the hardware-derived boot-ID prefix with `device-a`; samples, configuration, source
commit, and raw boot-log hashes are unchanged.

## Exact single-core result

Every retained sample has zero I-bus accesses/misses and zero D-bus accesses/flash misses/PSRAM
misses. The following totals and matched callback deltas repeat exactly across all 100 samples in
both boots:

| replay callback | baseline cycles | target cycles | callback delta |
| --- | ---: | ---: | ---: |
| `memset` length 0 at `0x400011e8` | 26 | 57 | 31 |
| `memset` length `0x52e0` at `0x400011e8` | 26 | 6685 | 6659 |
| set CPU ticks/us to current value at `0x40001a4c` | 23 | 32 | 9 |

The reset-reason callback is not scalar. Both baselines are exactly 25 cycles. Core 0 target
samples span 119..131 cycles in 3-cycle steps, giving callback deltas 94..106. Core 1 target
samples span 116..128 cycles, giving callback deltas 91..103. The distributions repeat across
boots but their modal counts do not, so `0x4000057c` remains aggregate-only.

The callback finalizers prove that both reset results match their boot values, the 0-length
`memset` leaves all `0x52e0` bytes unchanged, the full `memset` zeroes all bytes and returns the
input pointer, and the CPU-tick write preserves the live ticks-per-us value.

Verify any receipt with:

```sh
bun puck/timing/verify_calibration_receipt.ts <receipt.json>
```
