# Vector V2 cleanup — Wave 1 receipt

Baseline: tag `v2-feature-complete-pre-cleanup`, commit
`3f23c09bf0c9377851d4c384f494f55917e1b5bb`.

## Change

Wave 1 removed 9,298 lines and added 674. It retired superseded Raster V1 and
prototype firmware, replaced the firmware Boolean matrix with one variant
selector, removed unused vector-core experiments and instrumentation, moved
host experiments behind an opt-in build flag, centralized ink-trace validation,
factored snapshot registration, and narrowed chrome interaction to one app
controller interface. Historical raw evidence was left untouched; future
artifact retention is governed by `docs/ARTIFACT_RETENTION.md`.

## Host validation

- `./scripts/dev release`: 29/29 tests passed in 1.64 s.
- `./scripts/dev asan`: 11/11 tests passed in 77.51 s.
- `git diff --check`: passed.

## Firmware builds

- Product: 1,059,952 bytes (`0x102c70`), down 3,072 bytes from baseline.
- Product SHA-256: `94d8f87b9d9c52c6175de0f51ff4a90c9c786a3420d8880f64240dc1a46c02b5`.
- Gate, 384 tile slots: 1,233,472 bytes (`0x12d240`), down 2,544 bytes.
- Gate SHA-256: `280f18231c9bcf76bb4ee1310ca45128a6b96654ae6c814e23e4da05502a29da`.

Both builds used ESP-IDF v6.0.2. The existing deprecated touch-driver warning
remains.

## Attached-device validation

The 384-slot gate battery completed without a crash. Every functional and
integration gate passed. The sole red remained the baseline `overlap_cold`
performance gate: its 50% rebuild was 609,596 us versus 604,187 us at baseline
and a 500,000 us limit. The 100%, 200%, and 400% overlap cases passed; the
settled-AA receipt remains yellow.

Removing unused slot metadata reduced live storage by exactly 3,584 bytes and
increased ready-state free PSRAM by the same amount. The largest PSRAM block
remained 2,228,224 bytes. Startup compose was 26,429 us, chrome 13,765 us,
transfer wait 20,691 us, and initial settle 294,672 us with no failures. These
remain close to the baseline values, so no cache-layout compatibility pad was
added.

Product firmware was restored after the gate run. It restored autosave
generation 53 with all 15 retained operations active at sequence 57.
