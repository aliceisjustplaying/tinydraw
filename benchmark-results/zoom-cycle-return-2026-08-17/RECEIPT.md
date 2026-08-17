# Zoom-cycle return receipt — 2026-08-17

## Result

The physical zoom-button route now returns to the exact explored origin:

```text
400% (2300,3100) → 25% → 50% → 100% → 200% → 400% (2300,3100)
```

`gate-device.log` records:

```text
TINYDRAW_GATE1_ZOOM_CYCLE_RETURN explored_x=2300 explored_y=3100 returned_x=2300 returned_y=3100 pass=1
```

The final verdict vector also records `zoom_cycle_return=1`. The full harness
reached its final marker without a watchdog, panic, reset, or stack overflow.
Its existing binding `overlap_cold=0` result remains visible; this receipt does
not reclassify that standing cold-render red.

## Defect and implementation

A test-first regression in
`vector_v2/tests/navigation_state_test.cpp` exercises the complete physical
button cycle. Before the implementation, the new exact-origin check failed:
repeated integer focus conversion centered the final 400% view near the
explored point instead of restoring the explored camera origin.

`NavigationState` now remembers the last clamped origin and associated
quarter-world focus for each tiled zoom. A remembered origin is restored only
when:

1. its saved focus matches the retained focus within four quarter-world units
   (the bounded full-cycle integer-conversion error); and
2. its viewport still contains the retained focus.

Otherwise the target zoom centers the retained focus. This preserves the prior
stale-view regression: panning elsewhere at another zoom cannot pull the user
back to an obsolete viewport corner.

The app-lifetime navigation object moved from the main-task stack to static
storage. The diagnostic harness stack increased from 16 KiB to 20 KiB because
its monolithic battery accumulated another persistent verdict; production
remains 16 KiB. The clean product boot reports 6,504 bytes free, while the full
20 KiB gate reports 2,280 bytes free after the battery.

## Validation

### Host

```sh
./scripts/dev release
```

Result: 29/29 tests passed (`host-release.log`). This includes adjacent focus
continuity, stale-view rejection, direct overview round-trip, and the complete
button cycle.

Changed-file static analysis:

```sh
SDKROOT="$(xcrun --show-sdk-path)"
/opt/homebrew/Cellar/llvm/22.1.8/bin/clang-tidy --quiet \
  -p out/build/host-debug \
  --extra-arg=-isysroot --extra-arg="$SDKROOT" \
  vector_v2/src/navigation_state.cpp
```

Result: no diagnostics (`navigation-clang-tidy.log` is empty).

### Physical gate harness

The canonical command rebuilt the gate from a clean directory and flashed over
serial:

```sh
./scripts/esp32 vector-v2-gate-harness /dev/cu.usbmodem101
uv run --script tools/esp32-capture.py \
  /dev/cu.usbmodem101 /tmp/zoom-cycle-device.log 480 \
  --end-marker TINYDRAW_GATE1_AUTOMATED_DONE \
  --failure-regex 'task_wdt|Guru Meditation|TG1WDT_SYS_RST|stack overflow'
```

Receipts:

- generated gate config: `CONFIG_ESP_MAIN_TASK_STACK_SIZE=20480`;
- exact return marker: `pass=1`;
- final vector: `zoom_cycle_return=1`;
- final marker reached with no failure-regex match;
- post-battery stack margin: 2,280 bytes;
- logs: `gate-build-flash.log`, `gate-device.log`.

### Physical product image

The final flash restored the ordinary interactive V2 image rather than leaving
the diagnostic harness installed:

```sh
./scripts/esp32 vector-v2 /dev/cu.usbmodem101
uv run --script tools/esp32-capture.py \
  /dev/cu.usbmodem101 /tmp/zoom-cycle-product-device.log 30 \
  --end-marker TINYDRAW_VECTOR_V2_READY \
  --failure-regex 'task_wdt|Guru Meditation|TG1WDT_SYS_RST|stack overflow'
```

Receipts:

- generated product config: `CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384`;
- startup presentation: `pass=1`;
- `TINYDRAW_VECTOR_V2_READY` reached with 6,504 bytes main-stack margin;
- no failure-regex match;
- logs: `product-build-flash.log`, `product-device.log`.

No USB mass-storage command was run during this work.
