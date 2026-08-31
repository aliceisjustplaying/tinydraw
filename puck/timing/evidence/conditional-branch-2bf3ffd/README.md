# ESP32-S3 conditional-branch calibration

This directory preserves the six strict hardware receipts for the matched IRAM
conditional-branch cells added by commit
`2bf3ffd861115b95451df1860623618c06e22dcf`.

## Fixed configuration

- ESP-IDF: `v6.0.2`
- compiler: GNU `15.2.0`
- CPU: 240 MHz
- flash: QIO, 80 MHz
- PSRAM: octal, 80 MHz
- ELF SHA-256: `b5db91a7c2692395c8b73aa96c69a5966517d518f904725f95b94096e2fd729c`
- sdkconfig SHA-256: `5befec96cb7e4dbd86a69abccf96828696b0c79cfe0b0c904d5bdc75543d3d68`

## Two-boot result

| boot | kernel | exact distribution | cache counters |
| --- | --- | --- | --- |
| `1cdbd47b85c8-06fabe6f-000128af` | baseline | 28,692 cycles x 100 | all zero |
| `1cdbd47b85c8-06fabe6f-000128af` | not taken | 28,692 cycles x 100 | all zero |
| `1cdbd47b85c8-06fabe6f-000128af` | taken | 36,884 cycles x 100 | all zero |
| `1cdbd47b85c8-8cd46371-000128af` | baseline | 28,692 cycles x 100 | all zero |
| `1cdbd47b85c8-8cd46371-000128af` | not taken | 28,692 cycles x 100 | all zero |
| `1cdbd47b85c8-8cd46371-000128af` | taken | 36,884 cycles x 100 | all zero |

Across 4,096 iterations, the repeated not-taken route is equal to its
same-instruction-count baseline. The repeated taken route adds exactly 8,192
cycles, or 2 cycles per iteration. This conclusion is limited to the exact
four-instruction routes pinned by `conditional_branch_probes_source.test.ts`;
it does not assign costs to unrelated opcodes or branch shapes.

## Capture integrity

The complete boot logs remain outside the repository because each is about
4 MiB. Their SHA-256 values are:

- boot 1: `61dfcf10637037d82db1923238a4141816ac7e411b8afb66967cc82692fe30eb`
- boot 2: `ac88a04d11380a87917be8304e0917a41a84b6bce62c17970e5f1532c486d192`

USB tearing affected only unrelated measurement groups. Complete-measurement
recovery retained all six branch groups, and each committed receipt passes:

```sh
bun puck/timing/verify_calibration_receipt.ts <receipt.json>
```

Receipt SHA-256 values, in boot-1 baseline/not-taken/taken then boot-2 order:

- `1021ca1e83ec36a7eb1ebdd790833da5952daa10005937f19495afedbff539cc`
- `13ad2db188806882b9396923f27610a2b3e8091c7e664f55cb6cb3396ae19a16`
- `ad63983eed643d22d5963e599d52aa02862825aa8c613b350b8349bf610ed820`
- `60a167619682f1c2c772c6ce13a85616f9ba2696ac373c536b4523acedc4a05b`
- `3f71e0390c359385cc31817d8b99715ba4c98d0c5cd98ba4be42355150cfc81a`
- `ce36da5b432c2814462b860a60e345756fc3d585b1cb65e978a124c4fdaf90d6`
