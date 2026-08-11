# ESP32-S3 Phase 2 physical reports

Physical 240 MHz ESP32-S3, octal PSRAM at 80 MHz. Reports were persisted to
the last 8 KiB of the `export` partition and read from flash offset `0x90e000`
without opening a serial monitor.

- `esp32s3-phase2-v1-corrected.{txt,bin}` is the load-bearing corrected run.
- `esp32s3-phase2-v1.{txt,bin}` is retained as primary evidence of the first
  run. Its pan strips were accidentally positioned over empty world regions,
  and its floating-point-per-pixel preview was intentionally replaced after it
  measured ~550 ms. Do not use it for the verdict.

SHA-256:

```
0f31e927f9f7b9c078f307a414afd3a2a023c162f1a2c0941402e21818e61f43  esp32s3-phase2-v1-corrected.bin
eff839ff00a1f57796930f6ad6032ef61f45d7fc5c08d3752b705b27ba3eb3bf  esp32s3-phase2-v1-corrected.txt
790eab165d847b80abec7782d383b07c5b880eb7c62312535fcb05594c1731d8  esp32s3-phase2-v1.bin
e79449d94d7ccfb4f18e2c69ab5167025da314db2c50c6691054f99edc9c2ea9  esp32s3-phase2-v1.txt
```

Interpretation: `V2_PHASE2_PROTOTYPE_FINDINGS.md`.
