# Combined cold-render baseline

Date: 2026-08-16

Firmware authority: curved committed ink at `19ebbe3`

Device: ESP32-S3, 240 MHz, 8 MiB PSRAM, effective 40 MHz panel clock

Viewport: 400%, origin `(0,0)`, detail tiles discarded before each run

The standard cold corpus now layers Alice's evil hairlines onto the tapered
adversarial document. It contains 910 operations and 12,157 samples:

- tapered adversarial: 128 operations, 4,096 samples;
- evil hairlines: 782 operations, 8,061 samples.

Loading the authority is outside the cold timer. The timed interval begins
after detail-tile discard and ends after the final exact panel publication and
DMA completion. Touch service runs throughout.

| Run | Compute | Present | Pacing | Touch | Wall | Max tick |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1,165.354 ms | 70.182 ms | 31.526 ms | 2.095 ms | 1,269.157 ms | 9.016 ms |
| 2 | 1,165.253 ms | 68.882 ms | 30.761 ms | 2.062 ms | 1,266.958 ms | 10.350 ms |
| 3 | 1,147.320 ms | 67.637 ms | 31.153 ms | 1.057 ms | 1,247.167 ms | 10.147 ms |

Three-run median wall is **1,266.958 ms**. The provisional baseline used for
the contract's maximum statistic is **1,269.157 ms**: 1,165.354 ms compute,
70.182 ms presentation, 31.526 ms pacing, and 2.095 ms touch service. It is
769.157 ms over the 500 ms requirement and needs a 60.6% wall reduction.

No run crashed, overflowed the touch queue, or exceeded the 15 ms interaction
tick limit. This is a three-run development characterization, not the final
20-run closure.

The specialized hairline capacity gate now uses the same progressive panel
publication path. Its first full-presentation receipt was 445.980 ms at 100%
and 327.978 ms at 400%; both passed, and the later 448-slot saturation/repair
check also passed.

The older 663.829 ms receipt is retained as historical evidence for straight
authority replay. It was superseded first by curved committed authority and
then by this combined corpus.
