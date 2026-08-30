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

Capture the complete serial log through the `run-complete` record, then turn
it into strict receipts. The collector hashes the captured boot log and uses
the capture file timestamp; it does not synthesize calibration values. The
receipt output directory must be new so an earlier hardware capture cannot be
overwritten.

```sh
bun puck/timing/collect_timing_probe.ts capture.log receipts/
```
