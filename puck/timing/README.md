# ESP32-S3 hardware calibration receipts

`calibration-receipt.schema.json` is the strict receipt format. It records raw
32-bit Xtensa CCOUNT endpoints and deltas together with mandatory git,
toolchain, sdkconfig, boot, and counter provenance. The parser also checks
wraparound deltas, panel payload geometry, strip transaction counts, and the
CCOUNT/CPU-frequency relationship. `TINYDRAW_PROBE_CCOUNT` lines from the
panel-probe firmware provide the raw kernel and panel endpoints; samples that
cross cores or report `pass=0` must not become receipts. A hardware receipt
needs at least 100 raw samples; schema fixtures may stay smaller.

The golden fixtures are schema-only sentinel data, marked with
`"captureMode": "schema-fixture"`; they are not hardware measurements and the
production parser rejects them by default.

Run the suite with:

```sh
bun test puck/timing/calibration_receipt.test.ts
```

Verify a captured hardware receipt with machine-readable output:

```sh
bun puck/timing/verify_calibration_receipt.ts path/to/receipt.json
```

## Standalone cycle probe

`./scripts/esp32 timing-probe` builds an independent ESP32-S3 firmware image;
passing a serial port additionally flashes it. The firmware emits prefixed
NDJSON records for 100-sample SRAM, PSRAM, read-only flash-mmap, and safe
instruction-fetch probes in solo and second-core-PSRAM-contention modes. It
never programs or erases data partitions.

Solo samples optionally carry the ESP32-S3's SoC-global IBus and DBus access
and miss counters. The firmware clears them before the sample's start CCOUNT
and reads the registers sequentially after its end CCOUNT, so their window is
slightly wider than the raw cycle interval and is not an atomic snapshot. A
receipt either carries these counters on every sample in a measurement or on
none. Contended samples omit them because the shared registers cannot
coherently attribute core-1 traffic to the core-0 kernel.

The five-pixel RGB565 scalar oracle has separate hot and cold measurements.
An IRAM assembly boundary materializes the target and ABI arguments before the
start CCOUNT, keeps that endpoint live in its caller register window, executes
exactly one `callx8` plus the exact 41-byte body, and reads the end CCOUNT
immediately on return. It stores both endpoints afterward. Generic sampler
dispatch and setup are outside the window. The body is 32-byte aligned and
therefore occupies two instruction-cache lines.
The cold preparation invalidates those two lines after resetting the input and
output, while the hot measurement uses only its declared suite warmup and does
not call the oracle during per-sample preparation.

The benchmark tasks intentionally saturate both cores at high priority, which
starves the idle tasks during long PSRAM and contention probes. This firmware
variant therefore disables the task watchdog while retaining the interrupt
watchdog on both cores. Product firmware still uses the base watchdog config.
The generated timing-probe sdkconfig, including this benchmark-only setting,
is SHA-256 hashed into every receipt so captures with different watchdog or
hardware settings cannot enter the same calibration cohort.

Capture the complete serial log through the `run-complete` record, then turn
it into strict receipts. The collector hashes the captured boot log and uses
the capture file timestamp; it does not synthesize calibration values. The
receipt output directory must be new so an earlier hardware capture cannot be
overwritten.

```sh
bun puck/timing/collect_timing_probe.ts capture.log receipts/
```
