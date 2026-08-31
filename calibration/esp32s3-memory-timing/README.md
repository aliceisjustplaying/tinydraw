# ESP32-S3 memory timing calibration

This standalone ESP-IDF harness records CCOUNT samples for IRAM instruction
execution, internal SRAM reads/writes, mapped-flash cache hit/sequential/random
behavior, and octal-PSRAM cache hit/sequential/random behavior. It does not
link TinyDraw or Puck code.

Run it on the physical board with:

```sh
./calibration/esp32s3-memory-timing/run.sh /dev/cu.usbmodem101 \
  calibration/esp32s3-memory-timing/results/YYYY-MM-DD-physical
```

The serial capture retains every raw CCOUNT sample. `result.json` adds the full
per-operation sample distribution, median, p90, range, latency, and bandwidth.
It also records the flashed application binary's SHA-256 digest and the commit
that last changed the firmware source/configuration inputs.
Random external-memory figures subtract the measured LCG/index-only baseline.
Sequential figures include loop/index overhead. Use medians for emulator
calibration and the raw min-to-p90 range to model timing uncertainty.
The two-byte IRAM nop encoding is verified from the linked ELF symbol size.
