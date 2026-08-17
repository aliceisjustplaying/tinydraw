# Zoom-overlay pan gesture receipt — 2026-08-17

## Verdict

**PASS.** The zoom rail no longer swallows a deliberate pan-tool drag.
Stationary/short gestures remain zoom-control taps; movement reaching 8 pixels
promotes the gesture into the existing canvas pan path from its original Down
point. Draw and eraser tools retain ordinary zoom-control ownership.

## State-machine change

Before this change, any Down inside `chrome_contains()` set
`toolbar_pressed=true`. Every Move was accumulated as a toolbar tap and the
entire gesture was consumed on Up, even with the pan tool selected.

Now `chrome_promotes_pan_drag()` is the platform-neutral intent classifier. The
app preserves the initial toolbar point. When the pan tool is active and a rail
gesture crosses the 8 px threshold, it clears toolbar ownership, runs the same
pending-authority boundary drain used by ordinary canvas panning, starts pan at
the original point/origin, and immediately presents the accumulated delta.
Bottom-toolbar gestures, popup gestures, and non-pan tools cannot promote.

## Evidence

Physical ESP32-S3 gate line from [`device.log`](device.log):

```text
TINYDRAW_GATE1_ZOOM_OVERLAY_PAN tap_zoom=1 drag_pan=1 threshold_px=8 pass=1
```

The preceding startup frame had `pushes=11`, observed the TE edge, and passed.
The capture contains no watchdog, crash, or reset marker. The gate is included
in the complete automated verdict vector.

Host tests cover:

- Zoom In/Out stationary taps remain actions;
- sub-threshold diagonal movement remains a tap;
- horizontal and label-area 8 px movement promotes under the pan tool;
- draw-tool and popup gestures never promote.

Commands:

```sh
./scripts/dev format-check
./out/build/host-release/vector_v2/tinydraw_vector_v2_tests \
  --test-case='*zoom rail*'
./out/build/host-release/vector_v2/tinydraw_vector_v2_tests \
  --test-case='*pan drags promote*'

cd esp32
eim run "idf.py -B '$PWD/../out/build/esp32-vector-v2-gate-harness' build"
eim run "idf.py -B '$PWD/../out/build/esp32-vector-v2-gate-harness' -p /dev/cu.usbmodem101 flash"
cd ..
uv run --script tools/esp32-capture.py /dev/cu.usbmodem101 \
  /tmp/zoom-overlay-device.log 30 \
  --end-marker TINYDRAW_GATE1_ZOOM_OVERLAY_PAN \
  --failure-regex 'task_wdt|Guru Meditation|TG1WDT_SYS_RST'
```

The gate does not enter USB mass-storage mode.
