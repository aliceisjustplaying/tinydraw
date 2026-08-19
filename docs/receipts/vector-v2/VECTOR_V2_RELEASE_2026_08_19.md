# Vector V2 release — 2026-08-19

Vector V2 passed the same-revision release battery and was restored to product
firmware on the physical ESP32-S3 at `/dev/cu.usbmodem1101`. The tested source
revision is `a5db58d`.

## Host checks

- Debug: 31/31 targets passed.
- Release: 31/31 targets passed.
- ASan/UBSan: 13/13 targets passed.
- Format check: passed.

## Physical battery

`./scripts/esp32 vector-v2-gate-harness /dev/cu.usbmodem1101 604 verify`
completed with every verdict flag equal to one. The gate image was `0x13b090`
bytes with 30% of its application partition free.

| corpus | 50% | 100% | 200% | 400% |
|---|---:|---:|---:|---:|
| overlap | 477.386 ms | 287.974 ms | 267.968 ms | 216.970 ms |
| general | 389.942 ms | 383.159 ms | 456.961 ms | 492.793 ms |
| captured drawing | 118.017 ms | 134.090 ms | 198.952 ms | 344.796 ms |

All cold walls passed their release limits. The largest paced-cold cooperative
tick was 10.605 ms. The captured drawing decoded 102 operations and 2,706
samples. History, export, touch, cache, pan, settled rendering, and stress gates
also passed.

The first post-cleanup gate build exposed a stale embedded-binary symbol after
the fixture rename. No firmware was flashed by that failed build. Commit
`a5db58d` corrected the symbol, and the complete battery then passed.

## Product firmware

The same revision produced a `0x106e00`-byte product image with 41% of the
application partition free and SHA-256
`729a1928dd7f79d5f27b06b4536b1ff144f75621845fe188c71e043b43b0d20e`.
Esptool verified the flash write. Product boot reached
`TINYDRAW_VECTOR_V2_READY`, restored generation 586 with 229 active and retained
operations, and reported no crash, watchdog, or stack failure.

Two optional whole-image readback attempts lost the serial stream before
completion. Both reset back into healthy product firmware; the first boot used
one panel-init attempt, and the final boot recovered one invalid panel response
on its second attempt. The release evidence is the verified write plus both
successful product boots.

## Retained evidence

- [`a5db58d-final-release-gate-604.log.zst`](a5db58d-final-release-gate-604.log.zst), 75,207 bytes decoded, SHA-256 `53763f8385183660f3430eb7ca65d502f928d472b00df4a65542b6d91867d2ab`.
- [`a5db58d-final-product-boot.log.zst`](a5db58d-final-product-boot.log.zst), clean first product boot, SHA-256 `8b108315005fe6a9b3d0128f28b8a2a5e4e3ed3f24e8d8737ac63d48f63a86e5`.
- [`a5db58d-final-product-boot-after-readback.log.zst`](a5db58d-final-product-boot-after-readback.log.zst), final recovered product boot, SHA-256 `1af9285c7a16ec5fcd39cc87fd1dc0529ccf48b0b287e6da322d7a1cc3e4ee07`.

The production firmware, not the gate harness, is flashed now. The drawing and
export data partitions were not erased.
