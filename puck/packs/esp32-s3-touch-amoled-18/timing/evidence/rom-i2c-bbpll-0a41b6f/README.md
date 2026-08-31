# ESP32-S3 ROM REGI2C write evidence: exact BBPLL replay cell

This bounded cohort measures the full-boot `rom_i2c_writeReg` boundary at ROM entry PC
`0x40005d60`. A read-only preflight first proves that BBPLL block `0x66`, host 1, register 4
already contains the replay value `0x6b`; otherwise firmware stops before any write. The matched
IRAM callers materialize the exact arguments and execute one literal-backed `callx8`. Commit
`0a41b6fede59c0e061a5e5c1901e0d3009b21518` is the fetchable capture source.

## Fixed configuration

- device: one disposable ESP32-S3 revision 0.2 board; hardware identifier redacted
- CPU: 240 MHz; measured caller interrupt level: 3
- PSRAM: 8 MiB octal at 80 MHz
- flash: 16 MiB QIO at 80 MHz
- ESP-IDF: v6.0.2; compiler: GNU 15.2.0
- timing-probe ELF SHA-256: `b881de8a7f73151aba59d47cbabcd47b88c9589495c7b6500ec4ab859030a297`
- sdkconfig SHA-256: `5befec96cb7e4dbd86a69abccf96828696b0c79cfe0b0c904d5bdc75543d3d68`
- both boot preflights: BBPLL block `0x66`, host 1, register 4 = `0x6b`

## Retained boots

Both runs emitted four complete 100-sample measurements and `pass=true`. The evidence retains
the matched single-core pair from each boot.

| evidence | boot ID | retained host log | log SHA-256 |
| --- | --- | --- | --- |
| `boot-1/` | `device-a-e7c7bc9d-0001a3ff` | `/private/tmp/rom-i2c-bbpll-captures/0a41b6f/boot-1.log` | `9767c0e7fbf9a6111796b2acd6248e935e6f0f0373e46ef96f6f9ce48d8cc289` |
| `boot-2/` | `device-a-a694ccde-0001a3ff` | `/private/tmp/rom-i2c-bbpll-captures/0a41b6f/boot-2.log` | `f592aac15fc494b2c87ee89ffd18c58e2478c2afda376092f2fdf63ce2980115` |

The retained receipts replace the hardware-derived boot-ID prefix with `device-a`; samples,
configuration, source commit, and raw boot-log hashes are unchanged.

## Exact single-core result and boundary

Every baseline sample is 28 cycles. In each independent boot, the first exact BBPLL write is
864 cycles and the following 99 writes are 863 cycles. The matched delta is therefore 836 cycles
for the one-shot reset-state invocation and 835 cycles after the internal REGI2C path is warm.
Every retained sample has zero I-bus accesses/misses and zero D-bus
accesses/flash misses/PSRAM misses. All 200 writes preserve the full `0x6b` byte.

The replay boundary is a one-shot boot callback, so the adoption-safe scalar for that event is
the independently reproduced first-invocation delta of 836 cycles. The 835-cycle steady-state
value is retained as a distinct warmed-call observation and is not generalized to other REGI2C
blocks or registers.

Verify any receipt with:

```sh
bun puck/timing/verify_calibration_receipt.ts <receipt.json>
```
