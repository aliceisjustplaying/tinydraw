# Vector V2 code-quality cleanup — 2026-08-18

This receipt closes the pre-final overengineering and repository-structure round.

## Scope

- Replaced six overlapping gesture flags with one `InteractionMode`; lift handling now dispatches
  from the completed mode.
- Removed the product-only blank-snapshot and re-render-ledger reservations. The gate retains the
  real diagnostic storage; the product allocates neither it nor inert cache-layout padding. Exact
  fixed PSRAM reclaimed: 357,264 bytes.
- Split uniform detection from the diagnostic payload census. Product code links the early-exit
  classifier; row-run/RLE analysis remains diagnostic-only.
- Split the gate coordinator, presenter, materialized canvas, incremental document, rasterizer,
  chrome, raster census, and rasterizer tests at cohesive boundaries. No Vector V2 or ESP32 Vector
  V2 C++ source/header is 1,000 lines or longer.
- Replaced long-parameter raster, absorption, materialized-canvas, and settled-render calls with
  request objects. Authority-journal validation and replay, spatial queries, staged metadata, and
  settled-render phases now have named helpers with unchanged wire/pixel semantics.
- Removed analyzer suppressions from the affected paths and cleared every clang-tidy finding.

## Acceptance

- Host debug and release: 31/31 passed in each configuration.
- Host ASan/UBSan: 13/13 passed.
- `./scripts/dev tidy`, format check, and `git diff --check` passed with no suppressions.
- QEMU built and replayed successfully with checksum `92d3e6ea`.
- ESP32 product (`0x103b60`) and 448-slot gate (`0x130320`) images built and linked.
- The physical 448-slot automated battery passed every flag with no crash or watchdog marker.
  Settled AA retains its accepted yellow performance classification.
- The normal product image was restored after the gate. Boot reached `TINYDRAW_VECTOR_V2_READY`
  with 2,551,056 bytes free PSRAM, a 2,490,368-byte largest block, and 6,088 bytes of main-stack
  headroom.

The exhaustive cppcheck command still exits nonzero on its established style/flow diagnostics and
required-field aggregate false positives. Clang-tidy, compiler, sanitizer, exactness, QEMU, and
physical-gate validation are green.
