# Vector V2 on-demand NTP receipt — 2026-08-17

## Status

**Host and product firmware builds pass. Physical flash and network behavior are pending because the device remains mounted as the read-only `TINYDRAW` export volume; no mass-storage reset/eject was attempted.**

## Product behavior

The rightmost document button now opens three equal-width actions: New, Export, and a clock glyph. Clock activation:

1. closes the popup and starts one low-priority asynchronous attempt;
2. displays `CONNECTING`, then `SYNCING` after the station receives an address;
3. obtains time from `pool.ntp.org`, applies the configured POSIX timezone, and writes the PCF85063A RTC;
4. stops/deinitializes SNTP, Wi-Fi, its station interface, and NVS before publishing `TIME SET` or `TIME ERROR`;
5. consumes the touch used to dismiss the terminal toast, so that touch cannot also mark or navigate the canvas.

The product polls a small atomic `TimeSyncStatus` interface; the network task does not mutate UI state. Duplicate starts are rejected while an attempt is active. The pre-existing Raster V1 `start_time_sync` entry point remains intact.

The supplied credentials live only in ignored `esp32/main/wifi_credentials.local.h`; `.gitignore` excludes that exact path. The default timezone remains `UTC0` because no local timezone was supplied.

## UI regression contract

`vector_v2/tests/chrome_test.cpp` proves:

- document popup centers `(60,331)`, `(184,331)`, and `(306,331)` map to New, Export, and Sync Time;
- connecting/synchronizing states own all input, expose no canvas input rows, hide navigation overlays, and render distinct feedback.

The focused test executable passes 241/241 cases and 92,405/92,405 assertions as part of the full host runs.

## Validation

- [`host-release.log`](host-release.log): 29/29 CTest targets pass.
- [`host-asan.log`](host-asan.log): 11/11 ASan/UBSan targets pass.
- [`format-check.log`](format-check.log): repository format check passes.
- [`clang-tidy-chrome.log`](clang-tidy-chrome.log): changed platform-neutral Chrome implementation is clean.
- [`product-build.log`](product-build.log): ordinary Vector V2 firmware compiles/links; binary is `0xf4fa0` bytes with `0xb060` bytes left in the 1 MiB product app partition.
- [`gate-build.log`](gate-build.log): gate firmware compiles/links using the gate-only 1.25 MiB app partition.

The first gate rebuild exposed a real diagnostic-layout constraint: linking Wi-Fi/NTP made the trace-heavy gate image `0x11eda0`, overflowing the 1 MiB product app partition by `0x1eda0` ([`gate-build-1m-red.log`](gate-build-1m-red.log)). `partitions.gate.csv` now moves 256 KiB from the gate-only export partition to the app partition while preserving the total layout footprint; product `partitions.csv` is unchanged.

Full repository clang-tidy/cppcheck remain red on pre-existing unrelated findings in `incremental_document.cpp`, `materialized_canvas.h`, `panel_staging.h`, `ink_trace.cpp`, `rerender_ledger.h`, and `svg_export.cpp`; receipts are [`clang-tidy-full-existing-red.log`](clang-tidy-full-existing-red.log) and [`cppcheck.log`](cppcheck.log).

## Pending physical receipt

After the owner ejects the mounted export and resets the board back to serial mode:

- flash the ordinary product image without entering mass-storage mode;
- press Document → Clock;
- capture `TINYDRAW_NTP_PHASE phase=connecting`, `phase=synchronizing`, `TINYDRAW_NTP_OK`, and `TINYDRAW_NTP_DONE success=1 wifi_stopped=1`;
- confirm `TIME SET` on glass and an ordinary retry after dismissing it.
