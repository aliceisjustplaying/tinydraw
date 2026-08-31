# ESP32-S3 hardware calibration receipts

`calibration-receipt.schema.json` is the strict receipt format. It records raw
32-bit Xtensa CCOUNT endpoints and deltas together with mandatory git,
toolchain, sdkconfig, boot, and counter provenance. The parser also checks
wraparound deltas, panel payload geometry, strip transaction counts, and the
CCOUNT/CPU-frequency relationship. `TINYDRAW_PROBE_CCOUNT` lines from the
panel-probe firmware provide the raw kernel and panel endpoints; samples that
cross cores or report `pass=0` must not become receipts.

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
