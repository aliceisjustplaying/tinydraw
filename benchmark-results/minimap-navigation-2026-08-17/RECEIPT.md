# Minimap navigation receipt — 2026-08-17

## Result

The visible minimap now owns touch gestures for every selected tool:

- a stationary tap centers the active tiled view on the selected minimap point;
- movement beyond a 4 px intent threshold drags the viewport continuously;
- the gesture remains captured and clamps when the finger leaves the minimap;
- popups, confirmation, and export progress hide and disable the minimap;
- at 25% the fixed `(0,0)` overview absorbs the gesture as a successful no-op.

The physical presenter classifier records:

```text
TINYDRAW_GATE1_MINIMAP_NAV hit=1 threshold_px=4 intent=1 tap_x=552 tap_y=710 tap_complete_us=20164 tap_pass=1 drag_x=626 drag_y=782 drag_complete_us=16529 drag_reused=1 drag_pass=1 pass=1
```

The tap uses a complete overlay-safe refresh. Continuous movement uses the
existing boundary-drained pan path; the classifier proves the drag reused the
canvas ring rather than forcing a full frame.

## Module design

`chrome_minimap_level_point()` is the single panel-to-level projection. It
owns the rendered minimap geometry and clamps coordinates beyond the interior,
so neither the app nor presenter duplicates `272,258,80×98` constants.

`VectorV2Presenter::jump_from_minimap()` centers the normal drawing-area focus
on a projected point and performs a complete refresh.
`VectorV2Presenter::pan_minimap_from()` applies the projected level delta to the
gesture's starting origin, quantizes ring movement to even pixels, delegates
world-edge clamping to `NavigationState`, and uses `refresh_pan()`.

The app interaction loop retains only gesture arbitration state. It drains any
pending committed overlay at the ordinary pan boundary before the first
minimap drag presentation. The minimap consumes touches under its visible frame
instead of allowing accidental pen/eraser authority beneath an interactive
overlay.

## Test-first host receipt

`vector_v2/tests/chrome_test.cpp` was added first. The red build is archived in
`host-red.log`; it failed because the proposed minimap hit/projection interface
did not yet exist.

The final tests cover:

- exact visible-frame ownership and popup suppression;
- 3 px jitter remains a tap, 4 px promotes to drag;
- exact top-left, center, and bottom-right projection at 200%;
- captured movement clamps beyond the minimap frame;
- the existing minimap rendering/cache equivalence suites.

Final host validation is in `host-release.log`: 29/29 CTest targets pass.
The ASan/UBSan build also passes 11/11 targets (`host-asan.log`). Changed-file
clang-tidy produced no diagnostics (`chrome-clang-tidy.log`).

## Physical validation

### Functional/pacing gate

The full serial-flashed gate reached `TINYDRAW_GATE1_AUTOMATED_DONE` with no
watchdog, panic, reset, or stack-overflow marker (`gate-device.log`). The final
vector has:

- `minimap_navigation=1`;
- `zoom_cycle_return=1`;
- `zoom_overlay_pan=1`;
- every general cold, pan, ink, cache, export, and return field at 1;
- the already documented binding `overlap_cold=0` at 50%.

The physical classifier measured 20.164 ms for the tap and 16.529 ms for the
ring-reused drag sample.

### Cold hold-line containment

The first two full minimap builds exposed a persistent instruction-layout
regression in the unrelated frozen 400% cold gate:

| Build | Compute | Wall | 520 ms gate |
|---|---:|---:|---:|
| pre-IRAM run 1 | 452.131 ms | 524.243 ms | red |
| pre-IRAM run 2 | 452.414 ms | 526.063 ms | red |
| tile producer in IRAM | 423.189 ms | 496.693 ms | green |

The project already queued IRAM-pinning because unrelated flash layout moved
this gate by 2–3%. `esp32/main/linker.lf` now places the bounded
`tile_producer.cpp` text object in `noflash_text`. `iram-symbols.txt` records a
6,310-byte object and product addresses in the `0x403...` internal-instruction
range. Gate free internal memory moved from 296,492 to 290,860 bytes, leaving
about 290 KiB free; PSRAM was unchanged.

### Product image left installed

The final command flashed the ordinary interactive V2 product, not the gate
harness:

```sh
./scripts/esp32 vector-v2 /dev/cu.usbmodem101
```

`product-device.log` reached `TINYDRAW_VECTOR_V2_READY` with startup `pass=1`,
6,408 bytes of 16 KiB main-stack margin, and no failure marker. The product map
also places the producer hot functions at `0x403...` IRAM addresses.

## Commands

```sh
./scripts/dev release

./scripts/esp32 vector-v2-gate-harness /dev/cu.usbmodem101
uv run --script tools/esp32-capture.py \
  /dev/cu.usbmodem101 /tmp/minimap-iram-device.log 480 \
  --end-marker TINYDRAW_GATE1_AUTOMATED_DONE \
  --failure-regex 'task_wdt|Guru Meditation|TG1WDT_SYS_RST|stack overflow'

./scripts/esp32 vector-v2 /dev/cu.usbmodem101
uv run --script tools/esp32-capture.py \
  /dev/cu.usbmodem101 /tmp/minimap-product-device.log 30 \
  --end-marker TINYDRAW_VECTOR_V2_READY \
  --failure-regex 'task_wdt|Guru Meditation|TG1WDT_SYS_RST|stack overflow'
```

No USB mass-storage command was run. A human glass-feel check remains useful,
but no sleeping-owner action was required for the automated functional,
pacing, cold, crash, or product-boot receipts.
