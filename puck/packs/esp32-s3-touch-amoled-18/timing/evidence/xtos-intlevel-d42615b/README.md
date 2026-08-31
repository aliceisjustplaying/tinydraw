# ESP32-S3 `_xtos_set_intlevel` replay evidence

This bounded cohort measures the exact full-boot callback at ROM stub PC `0x40001c38`.
The matched IRAM callers both materialize saved PS `0x00040c00` and use one `callx8`
(CALLINC2). The sampler enters at interrupt level 3; each wrapper saves that raised caller PS,
restores it immediately after the callback, and the sampler fails if the restored PS differs.
Commit `d42615bc3145c13c2b5855f53962ecf5e7d25f47` is the fetchable capture source.

## Fixed configuration

- device: one disposable ESP32-S3 revision 0.2 board; hardware identifier redacted
- CPU: 240 MHz; callback caller interrupt level: 3
- PSRAM: 8 MiB octal at 80 MHz
- flash: 16 MiB QIO at 80 MHz
- ESP-IDF: v6.0.2; compiler: GNU 15.2.0
- timing-probe ELF SHA-256: `cc447f5074c27a0fec74f6c9fd4c6491542de04cbf00bf31200b5423a3c11669`
- sdkconfig SHA-256: `5befec96cb7e4dbd86a69abccf96828696b0c79cfe0b0c904d5bdc75543d3d68`

The linked target wrapper has exact instruction encodings
`004136 03e690 046192 f0aca1 f0ad81 0008e0 042192 13e690 002010 0a2d f01d`.
The only caller-shape difference from the baseline is its literal-backed destination;
the target resolves to `_xtos_set_intlevel` at `0x40001c38`.

## Retained boots

Both runs emitted two complete 100-sample measurements and `pass=true`.

| evidence | boot ID | retained host log | log SHA-256 |
| --- | --- | --- | --- |
| `boot-1/` | `device-a-8ce10c68-0001986e` | `/private/tmp/xtos-intlevel-captures/d42615b/boot-1.log` | `ae501e97fc8d06f42f04dd8ac58ae6e175af07c63bee7f870571ea12abc724e0` |
| `boot-2/` | `device-a-af0f04ba-0001986e` | `/private/tmp/xtos-intlevel-captures/d42615b/boot-2.log` | `8195689005023702f6b4e9278a8b68b36260061b1fb51783ef3020e9d3928c2f` |

The retained receipts replace the hardware-derived boot-ID prefix with `device-a`; samples,
configuration, source commit, and raw boot-log hashes are unchanged.

## Exact result and boundary

Every baseline sample is exactly 34 cycles and every target sample is exactly 49 cycles in
both independent boots. The matched callback delta is therefore exactly **15 cycles** across
all 200 pairs. Every sample has zero I-bus accesses/misses and zero D-bus
accesses/flash misses/PSRAM misses.

This scalar applies only to `_xtos_set_intlevel(0x00040c00)` entered at INTLEVEL3 through
the `0x40001c38` CALLINC2 replay shape. The source and linked-disassembly test pin the immediate
PS restore and exact wrapper encodings; this result does not assign costs to other PS values,
caller interrupt levels, or ROM entry paths.

Verify any receipt with:

```sh
bun puck/timing/verify_calibration_receipt.ts <receipt.json>
```

Receipt SHA-256 values, in boot-1 baseline/target then boot-2 order:

- `288b7e957222dfee162f85bdee687a7b25b857e0155ba252cd2825b837ad85f2`
- `de1c65455add28a39f72adcbad67fa9757ff789a3511eaaad634a7c78a5cb33b`
- `ff29ad85b9596c2664dbb626b9263259eb0d5a5e67d709c16bb00c4b95ceb206`
- `f8a6abfc1b96379a1c0bc3e51c07837fd69e6b7e12783acb74bff1e2d2c42fe8`
