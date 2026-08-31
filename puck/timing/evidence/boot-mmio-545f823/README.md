# ESP32-S3 boot-register MMIO calibration

This directory preserves twenty strict hardware receipts for the matched IRAM
boot-register cells added by `32bbfcbd2e64d38bd561d5880ce7d0aede8934ec`
and hardened by `545f8236beb0bc12fbe90c4c09268b05f73da17a`.

## Fixed configuration

- ESP-IDF: `v6.0.2`
- compiler: GNU `15.2.0`
- CPU: 240 MHz
- flash: QIO, 80 MHz
- PSRAM: octal, 80 MHz
- ELF SHA-256: `fd6e03f85e60df681942612b23e8350128115b6c3881878137dcca140d4c1602`
- sdkconfig SHA-256: `5befec96cb7e4dbd86a69abccf96828696b0c79cfe0b0c904d5bdc75543d3d68`

Both boots captured the same register values: SYSTEM SYSCLK_CONF `0x000a8400`,
EXTMEM DCACHE_CTRL1 `0x00000000`, DCACHE_AUTOLOAD_CTRL `0x00000008`,
ICACHE_CTRL1 `0x00000000`, and ICACHE_AUTOLOAD_CTRL `0x00000008`.

## Two-boot result

| boot | matched cell | exact distribution | cache counters |
| --- | --- | --- | --- |
| `1cdbd47b85c8-9ef4e067-00016b7f` | five register reads | 45,074 cycles x 100 each | all zero |
| `1cdbd47b85c8-9ef4e067-00016b7f` | matched SRAM read | 12,306 cycles x 100 | all zero |
| `1cdbd47b85c8-9ef4e067-00016b7f` | SYSCLK/DCACHE_CTRL1/ICACHE_CTRL1 same-value writes | 16,400 cycles x 100 each | all zero |
| `1cdbd47b85c8-9ef4e067-00016b7f` | matched same-shape SRAM write | 4,120 cycles x 100 | all zero |
| `1cdbd47b85c8-5048156f-00016b80` | five register reads | 45,074 cycles x 100 each | all zero |
| `1cdbd47b85c8-5048156f-00016b80` | matched SRAM read | 12,306 cycles x 100 | all zero |
| `1cdbd47b85c8-5048156f-00016b80` | SYSCLK/DCACHE_CTRL1/ICACHE_CTRL1 same-value writes | 16,400 cycles x 100 each | all zero |
| `1cdbd47b85c8-5048156f-00016b80` | matched same-shape SRAM write | 4,120 cycles x 100 | all zero |

The read cells add exactly 32,768 cycles over matched SRAM across 4,096
operations: exactly 8 additive cycles/read. The same-value write cells add a
stable 12,280 aggregate cycles. Its quotient, 2.998046875 cycles/write, is not
an integer scalar, so this cohort retains the aggregate and does not promote a
per-write cost. A second operation count is required to separate slope from
fixed intercept.

AUTOLOAD writes are excluded. An exploratory boot that wrote back the observed
AUTOLOAD value `0x00000008` passed its immediate state finalizers but the next
cache preparation failed closed with `ESP_ERR_INVALID_STATE`; that failed log
has SHA-256 `75c6cf7f2d2be619d9db031595704dafb9ec6dca218cab8bc4d2d66a5cfd68bb`.
AUTOLOAD reads remained state-preserving across both retained full-suite boots.

## Capture integrity

Both retained logs reached `run-complete` with `pass: true`. USB tearing
affected only unrelated groups. Their paths and SHA-256 values are:

- `/private/tmp/boot-mmio-register-final-captures/boot-2.log`:
  `ddbaf32674ce77367b967b49b86e88d08cde9ed37217ac38b520246e84c1f1bf`
- `/private/tmp/boot-mmio-register-final-captures/boot-3.log`:
  `df44e9dec4706eb1c38ad746b2150edb560af49f7bbb6ad1c32453d36e6775e5`

Every committed receipt contains 100 raw samples and passes the strict receipt
verifier. Receipt SHA-256 values, in boot-1 then boot-2 alphabetical order:

- `3cc492392d9257e4627c5b9bb193cc440c9cc607ab75ec186692e879b5ff3feb`
- `469ec4d8d5c516d8d92dceb4e9d29de31e7bd4e2cd36a8cc8886675e453d90e2`
- `29c2e16a57ed44f22da84a243f920bc395ccb0c239e1abe6ef2f6c8f8a2bca0f`
- `243a5f82fb1a9efbeaea37ae0b4e11c292637d7d97550d16aaa2833ffedcaaea`
- `c5beaeef7def9ec6e694b1b5e188a2da9076c74ba79f795374f0df15a46a1395`
- `569ff31e92c9bd520d6ca69542d534ecbb9fb045f580ee923f9e49321a0ca721`
- `397753f54aa4f9b4b686e081c2f6a31784be074d4a0de22e8ce9ee84d17c86e9`
- `3f6c3da2ed5cc16621582d4386be73821723b26b9445ac76f9922d9389db5c14`
- `9b98bafca0043f256f729bd106504f6a1b57a577f7c2484ebab53bc8e732764e`
- `50055afebcb776fd4250e4cf5f445e0bdd8535a2728b927cabd80bd9689f9141`
- `58e48f455334b5f256a139a80eabf8b8ce8b3f4f44db45b4e938ced13df50a2a`
- `6bb68a405247b33cabf2186082258f473382167805890d4fd6e8d8612e06f383`
- `3f32a20e08bdf5abd0a08495bbb4b18719e067d12b8f645ffd0c5ee2b367b5b6`
- `f7a4fe537fc5018a084857552788f9032242a6d4f9bcc1503588b969791f40be`
- `64427c5c4974e38ea0c6993d43fda7339b372aafe22ee85e63b71af4227e2e89`
- `a976255ed38b751ec60960d9a73f34961f2883b690cbff1bac77aa68cb370e67`
- `f7df880d2477a386950be30c53998fdabc79016fc851f4852c9cd7d05c8dc4ba`
- `bdb3bc0f555dcb841a7e69bab0eaf08168188312ed66bba9de7690f0cb9ca7da`
- `cac11d2a412ce61e55dc35839e928a55b0c819741a8b216f30343746fd529664`
- `eb2fcd89d899db65f57991836adfdfea64643686e8ee0190e0dc1342352b1e23`
