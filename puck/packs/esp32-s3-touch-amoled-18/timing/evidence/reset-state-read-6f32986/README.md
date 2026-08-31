# ESP32-S3 RTCCNTL reset-state read evidence

This cohort measures aligned 32-bit reads of reset-state register `0x60008038` against a matched
internal-SRAM cell. Each assembly iteration reproduces the real ROM `rtc_get_reset_reason` read
body: `memw`, `l32i`, the core-specific six-bit `extui`, and one checksum `xor`. Both core-0
(`extui 0,6`) and core-1 (`extui 6,6`) shapes run at 2,048 and 4,096 operations. Commit
`6f32986088ab04e9e7746af846982d206eefd5de` is the fetchable strict capture source.

## Fixed configuration

- device: one disposable ESP32-S3 revision 0.2 board; hardware identifier redacted
- CPU: 240 MHz; PSRAM: 8 MiB octal at 80 MHz; flash: 16 MiB QIO at 80 MHz
- ESP-IDF: v6.0.2; compiler: GNU 15.2.0
- timing-probe ELF SHA-256: `bfe64054aeb4933aef72cd6a898a2f411ed344a2e09efe3eec9ef30abbf88cce`
- sdkconfig SHA-256: `5befec96cb7e4dbd86a69abccf96828696b0c79cfe0b0c904d5bdc75543d3d68`

## Retained boots

Both retained runs emitted eight complete 100-sample measurements and `pass=true`.

| evidence | boot ID | host log | log SHA-256 |
| --- | --- | --- | --- |
| `boot-1/` | `device-a-58c0423e-00019869` | `/private/tmp/reset-state-captures/6f32986/boot-1.log` | `5ba15820ce2b342502eb8e89d78675303417bd378b05deb4f01d403705e13d6e` |
| `boot-2/` | `device-a-f8aec26d-0001986f` | `/private/tmp/reset-state-captures/6f32986/boot-3.log` | `879e961a05d5c52b900571cc0f53897d819795f0002256a33b780601329c61c2` |

The retained receipts replace the hardware-derived boot-ID prefix with `device-a`; samples,
configuration, source commit, and raw boot-log hashes are unchanged.

## Results

SRAM is exact in every sample: 10,258 cycles at 2,048 operations and 20,498 cycles at 4,096,
an exact five-cycle steady-state loop slope. RTCCNTL measurements retain a narrow asynchronous
clock-domain distribution:

| boot | field | N=2,048 min / median / max | N=4,096 min / median / max | median additive slope |
| --- | --- | --- | --- | --- |
| 1 | core 0 | 186321 / 186354 / 186393 | 372612 / 372717 / 372786 | 85.9976 cycles/read |
| 1 | core 1 | 186306 / 186360 / 186396 | 372633 / 372696 / 372780 | 85.9844 cycles/read |
| 2 | core 0 | 186312 / 186345 / 186390 | 372486 / 372642 / 372726 | 85.9653 cycles/read |
| 2 | core 1 | 186312 / 186357 / 186402 | 372618 / 372708 / 372789 | 85.9917 cycles/read |

Every SRAM sample has zero cache-counter activity. Every 2,048-operation RTCCNTL sample has
exactly 88 IBus accesses and every 4,096-operation sample has exactly 176; all IBus misses and
all DBus access/miss counters are zero.

The repeated-read throughput is approximately 86 additive cycles per RTCCNTL read, but the
two-point slopes and individual samples are not exact integers. This evidence therefore does
not turn the one-shot `rtc_get_reset_reason` callback lattice into one scalar duration. It
separates the register-bus contribution from the exact instruction body and preserves the
clock-domain phase boundary.

Verify any receipt with:

```sh
bun puck/timing/verify_calibration_receipt.ts <receipt.json>
```
