# Author-document battery integration — 2026-08-19

## Result

The compact authority captured from the author's real torture document is now
embedded only in the 604-slot gate firmware. The loader validates the `TDOC`
header and exact archived shape, decodes each sample without alignment or host
endianness assumptions, and replays all operations through production append
and absorption. Product firmware size and runtime memory are unchanged.

Physical device (`/dev/cu.usbmodem1101`, ESP32-S3 at 240 MHz):

| zoom | compute | presentation | wall | 500 ms line |
|---:|---:|---:|---:|---:|
| 50% | 53.211 ms | 60.501 ms | 117.195 ms | pass |
| 100% | 67.365 ms | 63.390 ms | 134.328 ms | pass |
| 200% | 128.424 ms | 65.609 ms | 199.061 ms | pass |
| 400% | 262.925 ms | 76.864 ms | 346.948 ms | pass |

The gate decoded 102 operations and 2,706 samples in 77.770 ms. All touch,
watchdog, overflow, and resynchronization counters stayed clean. The final
`TINYDRAW_GATE1_AUTOMATED_DONE` line reported every gate equal to one,
including `owner_document=1`; settled AA retained its expected yellow receipt.

Baseline before integration was clean `8a24436`. The baseline battery and the
treated battery both passed completely. The embedded corpus increased the gate
image from `0x133b40` to `0x1397b0` bytes and left 550,992 bytes free in the
diagnostic app partition.

Command:

```sh
./scripts/esp32 vector-v2-gate-harness /dev/cu.usbmodem1101 604 verify
```
