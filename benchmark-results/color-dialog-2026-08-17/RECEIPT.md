# Color-dialog rendering receipt — 2026-08-17

## Verdict

**PASS on the attached ESP32-S3.** Opening the 16-swatch color dialog fell from
132.466 ms to 27.568 ms: **4.81× faster overall / 79.2% less wall time**.
Chrome drawing itself fell from 82.364 ms to 9.396 ms: **8.77× faster**.
The final gate holds color-dialog wall time to ≤40 ms.

## Mechanism

`PixelPainter::circle()` previously inspected every pixel in each circle's
bounding square, branched on the radius equation, then called a bounds-checked
single-pixel writer. It now derives the exact monotonic half-width once per row
and fills one clipped horizontal span. `rounded()` uses the same exact extents
and writes each row once instead of overlapping two rectangles and four full
circle passes.

The dialog also re-presents the existing linear derived frame for color-only
open/close/page actions. Palette staging does not mutate that frame, so hidden
canvas no longer needs a 26 ms recompose. If a cached pan has left the frame
ring-addressed, `present_frame_region()` fails closed and the app falls back to
a full `refresh()`.

No new framebuffer or cache allocation was added.

## Physical-device evidence

Baseline ([`baseline-device.log`](baseline-device.log)):

```text
TINYDRAW_GATE1_COLOR_DIALOG wall_us=132466 compose_us=26318 chrome_us=82364 chrome_prepare_us=8 chrome_stage_us=82356 complete_us=96210 pushes=11 pass=1
```

The baseline's `pass=1` meant presentation success; it predates the new timing
threshold.

Final ([`final-device.log`](final-device.log)):

```text
TINYDRAW_GATE1_COLOR_DIALOG wall_us=27568 maximum_us=40000 compose_us=0 chrome_us=9396 chrome_prepare_us=13 chrome_stage_us=9383 complete_us=24547 pushes=11 pass=1
```

Both compared runs had a valid startup frame (`pushes=11`, observed TE edge,
startup `pass=1`). The final capture contains no watchdog, crash, or reset
marker.

## Correctness and validation

- The release suite's exact color/tool/dialog PPM snapshots pass unchanged.
- A direct unit test compares span-raster circles against the previous
  per-pixel radius predicate across radii 0–12, four horizontal clipping
  positions, and four vertical clipping positions.
- The final gate firmware compiles with the ≤40 ms guard in the complete
  automated verdict vector.

Commands:

```sh
./scripts/dev format-check
./scripts/dev release

cd esp32
eim run "idf.py -B '$PWD/../out/build/esp32-vector-v2-gate-harness' build"
eim run "idf.py -B '$PWD/../out/build/esp32-vector-v2-gate-harness' -p /dev/cu.usbmodem101 flash"
cd ..
uv run --script tools/esp32-capture.py /dev/cu.usbmodem101 \
  /tmp/color-final2-device.log 30 \
  --end-marker TINYDRAW_GATE1_COLOR_DIALOG \
  --failure-regex 'task_wdt|Guru Meditation|TG1WDT_SYS_RST'
```

The gate uses serial presentation only and never enters USB mass-storage mode.
