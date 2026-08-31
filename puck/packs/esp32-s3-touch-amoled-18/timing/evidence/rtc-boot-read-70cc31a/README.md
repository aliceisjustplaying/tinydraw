# ESP32-S3 RTC boot-register read evidence

This cohort measures aligned 32-bit reads of the full-boot RTCCNTL cells at `0x600080c0`
(`RTC_XTAL_FREQ` / STORE4) and `0x600081fc` (`RTC_DATE`) against one matched internal-SRAM
cell. Every assembly iteration contains one `l32i` and one checksum `xor`; each target and
baseline runs at 2,048 and 4,096 operations. Commit
`70cc31adb84ff43641d8727a1bcd4be8fbfe7744` is the fetchable strict capture source.

## Fixed configuration

- device: one disposable ESP32-S3 revision 0.2 board; hardware identifier redacted
- CPU: 240 MHz; PSRAM: 8 MiB octal at 80 MHz; flash: 16 MiB QIO at 80 MHz
- ESP-IDF: v6.0.2; compiler: GNU 15.2.0
- timing-probe ELF SHA-256: `5fec98337755dfba56490e32faa99d5a623669dec5fd15582e3839cd0c433d86`
- sdkconfig SHA-256: `5befec96cb7e4dbd86a69abccf96828696b0c79cfe0b0c904d5bdc75543d3d68`

## Retained boots

Both retained runs emitted six complete 100-sample measurements and `pass=true`.

| evidence | boot ID | host log | log SHA-256 |
| --- | --- | --- | --- |
| `boot-1/` | `device-a-560d3850-0001986e` | `/private/tmp/rtc-boot-read-captures/70cc31a/boot-1.log` | `d4bb24af94cbbee2527c8a5c6bc186aed9cea760218a90d8fd41e2743746c122` |
| `boot-2/` | `device-a-6f7b4d3b-0001986e` | `/private/tmp/rtc-boot-read-captures/70cc31a/boot-2-retry2.log` | `2443d51b62ea70db24e9e2db603fb2b00a6ca3b0355a70642de1ea986d453490` |

The retained receipts replace the hardware-derived boot-ID prefix with `device-a`; samples,
configuration, source commit, and raw boot-log hashes are unchanged. USB-torn captures are not
retained.

## Results

SRAM is exact in every sample: 6,162 cycles at 2,048 operations and 12,306 cycles at 4,096,
an exact three-cycle steady-state loop slope. The RTC reads retain a narrow asynchronous
clock-domain distribution:

| boot | register | N=2,048 min / median / max | N=4,096 min / median / max | median additive slope |
| --- | --- | --- | --- | --- |
| 1 | `0x600080c0` | 186100 / 186137 / 186187 | 372109 / 372202 / 372295 | 87.8521 cycles/read |
| 1 | `0x600081fc` | 186091 / 186136 / 186190 | 372021 / 372114 / 372198 | 87.8096 cycles/read |
| 2 | `0x600080c0` | 186154 / 186199 / 186259 | 372271 / 372382 / 372466 | 87.9097 cycles/read |
| 2 | `0x600081fc` | 186136 / 186187 / 186232 | 372282 / 372403 / 372522 | 87.9258 cycles/read |

Every SRAM sample has zero cache-counter activity. Every retained 2,048-operation RTC sample
has exactly 88 IBus accesses and every retained 4,096-operation RTC sample has exactly 176;
all IBus misses and all DBus access/miss counters are zero. The source performs unrecorded,
read-only settling runs before ordinal zero because the controller counter-clear signal crosses
an asynchronous boundary; every retained sample then fails closed on the exact signature.

The repeated-read throughput is approximately 87.8–87.9 additive cycles per RTCCNTL read, but
the two-point slopes and individual samples are not exact integers. This evidence therefore does
not justify a scalar one-shot cost for either full-boot read event. It preserves the register-bus
phase boundary while giving the replay model a measured distribution and exact counter contract.

Verify any receipt with:

```sh
bun puck/timing/verify_calibration_receipt.ts <receipt.json>
```
