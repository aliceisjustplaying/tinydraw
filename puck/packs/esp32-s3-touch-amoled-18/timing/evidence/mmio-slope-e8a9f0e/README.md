# ESP32-S3 RTC DATE and MMIO write-slope evidence

This cohort measures read-only `RTC_CNTL_DATE_REG` and matched same-value writes at 2048 and
4096 operations. The capture firmware ran only these ten cells in single-core and core-1
contention modes, so unrelated cache-signature traffic could not invalidate the target receipts.
Commit `e8a9f0e574f0e3f8902ae4c66585d43c9775a098` is the fetchable capture source; the following
evidence commit restores the aggregate runner.

## Fixed configuration

- device: one disposable ESP32-S3 revision 0.2 board; hardware identifier redacted
- CPU: 240 MHz
- PSRAM: 8 MiB octal at 80 MHz
- flash: 16 MiB QIO at 80 MHz
- ESP-IDF: v6.0.2; compiler: GNU 15.2.0
- timing-probe ELF SHA-256: `b483caab054faba176a9a72db5b203d792ba898993b930745cab82b3ef2a8a3e`
- sdkconfig SHA-256: `5befec96cb7e4dbd86a69abccf96828696b0c79cfe0b0c904d5bdc75543d3d68`

## Retained boots

Both runs emitted 20 complete 100-sample measurements and `pass=true`.

| evidence | boot ID | retained host log | log SHA-256 |
| --- | --- | --- | --- |
| `boot-1/` | `device-a-6dca07a4-00016b7b` | `/private/tmp/mmio-slope-rtc-date-final-captures/cohort-e8a9f0e/boot-2.log` | `3fcbbb52f56cb68700f7ed92f3cd8f1cdd56347524fa0cbe1b3f69c4c78f2994` |
| `boot-2/` | `device-a-c73d3613-00016b74` | `/private/tmp/mmio-slope-rtc-date-final-captures/cohort-e8a9f0e/boot-3.log` | `7535d4771d194eaa3f30716ba454bc59febc2276d0fb0e1313df77115b4ab9d6` |

The first otherwise-complete capture was excluded because USB truncated one sample JSON record;
its log SHA-256 is `8b8de0c247062b3e6326f17a62f3d9647ce826e230ab1ff8243a964ac0f3ddb0`.
The retained receipts replace the hardware-derived boot-ID prefix with `device-a`; samples,
configuration, source commit, and raw boot-log hashes are unchanged.

## Exact single-core result

All 100 samples in both retained boots have the zero-access, zero-miss cache-counter signature.

| operation | 2048 cycles | 4096 cycles |
| --- | ---: | ---: |
| matched SRAM same-shape write | 2072 | 4120 |
| same-value `SYSTEM_SYSCLK_CONF_REG` write | 8208 | 16400 |
| same-value `EXTMEM_DCACHE_CTRL1_REG` write | 8208 | 16400 |
| same-value `EXTMEM_ICACHE_CTRL1_REG` write | 8208 | 16400 |

For every safe register and both boots, the additive deltas are `6136` and `12280` cycles.
Therefore `(12280 - 6136) / (4096 - 2048) = 3` exactly, with the stable relation
`register - matched SRAM = 3N - 8`. This supports an address-scoped additive cost of 3 cycles
per aligned 32-bit write for the three same-value boot-register forms only. It does not support
AUTOLOAD, RTC, counter-clear, or value-changing writes.

`RTC_CNTL_DATE_REG` is not scalar. Boot 1 spans 372030..372189 cycles with 49 distinct totals;
boot 2 spans 372018..372186 with 43 distinct totals. Both retain the RTC-class counter signature
`ibus accesses=176, ibus misses=0, dbus accesses=0, flash misses=0, psram misses=0`, matching the
previously excluded RTC STORE1 behavior. The DATE read therefore remains aggregate-only.

Each JSON receipt preserves all samples, checksums, cache counters, boot identity, toolchain,
configuration, capture hash, and source commit. Verify any receipt with:

```sh
bun puck/timing/verify_calibration_receipt.ts <receipt.json>
```
