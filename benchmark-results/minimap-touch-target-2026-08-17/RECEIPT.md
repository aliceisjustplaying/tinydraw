# Minimap high-zoom touch-target receipt — 2026-08-17

## Result

Owner glass feedback liked minimap navigation but found the tiny 400% viewport
hard to grab. The whole visible 92×114 minimap was already the drag-start hit
target, so expanding its frame would only steal more drawing area. The actual
high-zoom friction was the fixed 4 px movement required to distinguish a drag
from a tap while the 400% viewport indicator is only about 5×5 panel pixels.

Drag intent now scales with zoom:

| Zoom | Drag promotion | Jitter retained as tap |
|---:|---:|---:|
| 25%, 50%, 100% | 4 px | 0–3 px |
| 200% | 3 px | 0–2 px |
| 400% | 2 px | 0–1 px |

The visible viewport remains geometrically truthful, the minimap does not
occlude additional canvas, taps elsewhere still jump, and a claimed drag still
uses the existing captured, boundary-drained minimap pan path.

## Validation

- [`host-release.log`](host-release.log): 29/29 CTest targets pass.
- [`host-asan.log`](host-asan.log): 11/11 ASan/UBSan CTest targets pass.
- [`product-build.log`](product-build.log): the ordinary ESP32-S3 Vector V2
  product image compiles and links successfully.
- [`product-device.log`](product-device.log): the ordinary product image was
  flashed after the owner's SVG test and reached `TINYDRAW_VECTOR_V2_READY`
  with no watchdog, panic, reset, or stack-overflow marker and 6,472 bytes of
  main-task stack margin.
- [`gate-build.log`](gate-build.log): the physical gate-harness image compiles
  and links with both intent thresholds in its classifier.
- [`clang-tidy.log`](clang-tidy.log): changed production source has no
  user-code diagnostic.
- `vector_v2/tests/chrome_test.cpp` proves 4/3/2 px promotion and one-less-pixel
  rejection at 100/200/400%, plus popup suppression.
- The gate classifier now checks both the established 4 px behavior at 100%
  and the new 2 px behavior at 400%.

The first build deliberately left the device available for the owner's physical
SVG USB test. After the owner reset the device and explicitly said it was ready,
the ordinary product firmware was flashed over USB Serial/JTAG and its startup
marker was captured. This validation did not invoke USB mass-storage mode. The
owner glass check of the new 400% threshold remains.
