# ESP32-S3 ROM REGI2C write evidence: safe BOD cell

This bounded cohort measures `rom_i2c_writeReg` at ROM entry PC `0x40005d60` with a safe,
non-clock-changing cell. The probe reads the documented brownout block `0x61`, host 1,
threshold register 5 once, then writes that exact full-byte value and reads it back after every
sample. The matched IRAM no-op caller has identical argument setup and one literal-backed
`callx8`. Commit `2d2e9ef320f8efd914e3d36f96239171eff14628` is the fetchable capture source.

## Fixed configuration

- device: one disposable ESP32-S3 revision 0.2 board; hardware identifier redacted
- CPU: 240 MHz; measured caller interrupt level: 3
- PSRAM: 8 MiB octal at 80 MHz
- flash: 16 MiB QIO at 80 MHz
- ESP-IDF: v6.0.2; compiler: GNU 15.2.0
- timing-probe ELF SHA-256: `aeffe5c3fac689a38d32fa8e6c53ebd052ccd6e8f515babae9cf0bda41c6f87f`
- sdkconfig SHA-256: `5befec96cb7e4dbd86a69abccf96828696b0c79cfe0b0c904d5bdc75543d3d68`
- boot-captured BOD threshold register byte: `0x47`

## Retained boots

Both runs emitted four complete 100-sample measurements and `pass=true`. The evidence retains
the matched single-core pair from each boot.

| evidence | boot ID | retained host log | log SHA-256 |
| --- | --- | --- | --- |
| `boot-1/` | `device-a-a39452d6-000198b8` | `/private/tmp/rom-i2c-write-captures/2d2e9ef/boot-1.log` | `968c0f2cfed211b8b3b216df46e078f62a2fa107695484a5ebd101f890872e5d` |
| `boot-2/` | `device-a-a1b7f95c-000198b7` | `/private/tmp/rom-i2c-write-captures/2d2e9ef/boot-2.log` | `b3536d2fbe72ae5544c3eebb8eac75190dbf6aa0be3e651bb603801a50a53d18` |

The retained receipts replace the hardware-derived boot-ID prefix with `device-a`; samples,
configuration, source commit, and raw boot-log hashes are unchanged.

## Exact single-core result and boundary

Every baseline sample is exactly 28 cycles and every ROM-write sample is exactly 864 cycles in
both boots, giving a matched callback delta of 836 cycles. Every sample has zero I-bus
accesses/misses and zero D-bus accesses/flash misses/PSRAM misses. All 200 writes preserve the
full `0x47` register byte.

This is evidence for the safe BOD transaction only. The next full-boot call uses BBPLL block
`0x66`, host 1, register 4, data `0x6b`; this cohort does not assign that clock-sensitive shape a
cost. An exact replay-class probe is permitted only when a read-only preflight proves the live
BBPLL byte is already `0x6b`.

Verify any receipt with:

```sh
bun puck/timing/verify_calibration_receipt.ts <receipt.json>
```
