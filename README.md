# TinyDraw

<p align="center">
  <a href="assets/readme/tinydraw-drawing.jpg"><img src="assets/readme/tinydraw-drawing.jpg" alt="TinyDraw running on the AMOLED board" width="32%"></a>
  <a href="assets/readme/tinydraw-colors.jpg"><img src="assets/readme/tinydraw-colors.jpg" alt="TinyDraw color palette" width="32%"></a>
  <a href="assets/readme/tinydraw-sizes.jpg"><img src="assets/readme/tinydraw-sizes.jpg" alt="TinyDraw brush sizes" width="32%"></a>
</p>

TinyDraw is a small finger-drawing app for Waveshare's 368×448 ESP32-S3 and
RP2350 Touch AMOLED 1.8-inch boards. A macOS app and ESP-IDF/QEMU targets
exercise the same C++20 drawing and UI core.

## Current state

The default firmware is **Raster V1**, the raster-authoritative 3×3 application described below.
**Vector V2** is the accepted vector-authoritative 4×4 successor under construction; its validated
foundation is isolated in `vector_v2/`, but it is not yet feature complete or the default. See
[`PROJECT_STATE.md`](PROJECT_STATE.md) for current direction and
[`vector_v2/README.md`](vector_v2/README.md) for V2 guardrails.

- Variable-width, Perfect Freehand-style ink with 4×4 edge smoothing
- Solid self-overlaps, rounded sharp turns, twelve colors, and four sizes
- ESP32: pan, ten Undos, 3×3 canvas, autosave, battery, and USB PNG export
- ESP32: onboard clock, one-shot NTP correction, demos, and battery-powered off/on
- RP2350: screen-sized ink, eraser, colors, sizes, and confirmed New
- Native replays, exact snapshots, ASan/UBSan, QEMU, and device telemetry

ESP32 long strokes average 2.5–3.4 ms per update. Autosave, charging, and PMU
power-off/on are verified. Export mounts the full 1104×1344 drawing as
`TINYDRAW/DRAWING.PNG` with the RTC-backed local time; macOS Finder is verified.
Wi-Fi shuts down after correcting the clock. The RP2350 averages 1.2–1.4 ms
but has no pan, Undo, or persistence. [`PROJECT_STATE.md`](PROJECT_STATE.md) has details.

## Run the macOS app
```sh
./scripts/bootstrap-macos   # once
./scripts/dev run           # use run-2x or run-3x for a larger demo window
```

The window opens near the panel's physical size on a 14-inch 2021 MacBook Pro.
Draw with the mouse, use `Cmd-Z` to undo, `C` for New, and `Esc` to quit.

## Run the Raster interactive core in Puck
```sh
./scripts/bootstrap-wasm             # once: LLVM 22 + WASI libc/libc++
./scripts/puck /path/to/puck         # builds, audits, tests, and installs emu.wasm
# in the Puck checkout: bun run dev
```

This target compiles the platform-neutral C++20 Raster reducer with libc++ as a raw WASI reactor.
It exposes Puck's framebuffer, touch, deterministic clock, and reported refresh rectangles,
including conservative full-frame UI and state refreshes. The browser
module includes pen/eraser, colors, sizes, pan, Undo, and New. It exposes the board's BOOT and power
buttons in the device diagram, but deliberately leaves their demo-recording and power services as
no-ops and does not claim to simulate persistence, USB export, RTC/NTP, battery state, the physical
touch-controller registers, or Vector V2. The production ESP32 shell has not yet been migrated to
the new shared reducer, so this is an interactive-core integration rather than a byte-for-byte
simulation of every hardware-app service.

## Test and profile
```sh
./scripts/dev test          # native tests and end-to-end replays
./scripts/dev release       # optimized build and tests
./scripts/dev asan          # ASan and UBSan on the shared core
./scripts/dev perf          # sustained XL work and memory traffic
./scripts/dev format-check
```

## Build ESP32 firmware
```sh
./scripts/bootstrap-idf     # once; isolated ESP-IDF v6.0.2
./scripts/esp32 raster-v1
./scripts/esp32 vector-v2 PORT
./scripts/esp32 graphics-test
```

`./scripts/esp32 build` remains an alias for `raster-v1`. Both named commands build in separate
directories; `vector-v2 PORT` also flashes the Vector V2 app. Use an explicit serial port. The same
firmware binary includes driver paths for both released ESP32-S3 board revisions:

| Board label | AMOLED | Touch | Panel X gap | Validation |
| --- | --- | --- | ---: | --- |
| V1 | SH8601 | FT3168 through the FT5x06-family driver | 0 | Builds; physical validation pending |
| V2 | CO5300 | CST820 through the CST816S-family driver | 0x10 | Physically exercised |

At boot, TinyDraw resets the shared display/touch rails and requires exactly one known touch address
to respond before it initializes the paired panel. Address 0x38 selects V1 and 0x15 selects V2;
neither or both fail closed. Diagnostic firmware can explicitly construct `BoardHardware` with
`BoardSelection::kV1` or `BoardSelection::kV2`; the override and probe mismatch are logged. Waveshare
has not published V3 controller details, so V3 is not guessed or claimed as supported. V1 needs a
real-board receipt for cold boot, partial refresh, touch orientation, and TE timing before it should
be treated as production-validated.

Export replaces USB serial with a read-only drive. To flash again on battery, power off, hold BOOT,
and short-press power for a cold
boot. Short BOOT records/replays demos. Hold the lower button four seconds to
power off; short-press it to start. There is no sleep mode yet.

## Build RP2350 firmware
```sh
./scripts/rp2350 bootstrap
./scripts/rp2350 build
./scripts/rp2350 metrics PORT
./scripts/rp2350 trace PORT
```

The RP2350 uses one SRAM framebuffer. Repeated arbitrary SH8601 rectangles were
unreliable; full-width bands stay fast and physically stable. Historical
measurements are archived in
[`FINDINGS.md`](docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md).

## License

MIT
