# USB export exit and chrome follow-up — 2026-08-19

## Verdict

USB mass-storage exit, export-screen text, and popup input layering are fixed.
Two physical macOS-eject loops returned the device to USB Serial/JTAG without
an on-screen Return tap. The complete 604-slot device battery, host Debug, host
ASan/UBSan, host Release, and format gates pass.

## Reproduction and root cause

Three physical red loops preceded the fix:

- `diskutil eject /Volumes/TINYDRAW` succeeded, but no
  `/dev/cu.usbmodem*` device returned within 10 seconds.
- **Return to Drawing** restored the drawing UI, but serial remained absent for
  60 seconds.
- Returning without a host eject failed identically, proving eject itself was
  not the condition that stranded serial.

TinyUSB shutdown deleted the OTG PHY handle but did not route the shared
internal PHY back to USB Serial/JTAG. Espressif documents this runtime handoff
as a new PHY allocation with `USB_PHY_CTRL_SERIAL_JTAG`; the relevant upstream
diagnosis is [IDFGH-10381](https://github.com/espressif/esp-idf/issues/11573),
and the later regression/fix is [IDFGH-16524](https://github.com/espressif/esp-idf/issues/15912)
and [esp-idf#15932](https://github.com/espressif/esp-idf/pull/15932).

Commit `4615a49` now reacquires and retains that PHY after TinyUSB teardown,
releases it before the next mass-storage session, and keeps shutdown retryable
if reacquisition fails. SCSI eject also exits export automatically. Physical
green loops were:

- explicit **Return to Drawing**: serial returned; the 16.979-second wall time
  includes the human tap delay and is not teardown latency;
- macOS eject: serial returned in 581 ms with no screen interaction;
- final flashed product repeat: serial appeared on the second 500 ms poll; the
  3.531-second wall time includes `diskutil` completing the eject.

## Chrome and input fixes

Commit `5ceb203` confines a compact popup contact to its active input layer.
Document-popup taps can no longer resolve as the pencil button underneath, and
size-popup taps cannot reach the history row. It also adds the missing `J`
glyph and centers **SAFE TO RETURN**.

Commit `b0d8ad2` centers **COPY YOUR FILES**. The regression renders every
export state and asserts both glyph presence and exact horizontal pixel bounds,
covering **COPY YOUR FILES**, **DRIVE EJECTED**, and **SAFE TO RETURN**.

## Final export presentation polish

Commit `2093862` renames the progress state from **SAVING** to **EXPORTING**.
The scale-3 label is centered in a widened 208 px toast, and its progress track
is widened to 176 px.

Glass review showed that the widened return button extended beyond the
white dialog. Commit `255f0f6` changes the scale-3 action to **EJECT & EXIT**,
restores the 264 px button fully inside the dialog, and keeps the hit target
identical to the visible button. The shared pixel font now includes an
ampersand glyph.

The final focused regression failed against the overflowing presentation, then
passed all 36 assertions after the change. It checks rendered glyphs, exact
centering, minimum side padding, white dialog pixels outside the button, and
both inside/outside button contacts. The affected Debug and ASan/UBSan
interaction/font targets pass.

The final product image is `0x106e00` bytes with 41% of the application
partition free and SHA-256
`c88907219794ec431db96c1208f398f7a8caabce6a92e2857c5203f124872ee3`.
It was flashed and hash-verified on `/dev/cu.usbmodem1101`. The production boot
reached `TINYDRAW_VECTOR_V2_READY` without a failure marker and restored
generation 581 with all 226 retained operations; the drawing partition was not
written by the firmware flash.

## Verification

- Focused interaction suite: 65/65 cases, 14,403/14,403 assertions.
- Host Debug: 31/31 CTest targets.
- Host ASan/UBSan: 13/13 CTest targets.
- Host Release: 31/31 CTest targets.
- `./scripts/dev format-check`: passed.
- Physical 604-slot battery: all gates true, `failure_marker=False`.
- Final product: `0x106f20` bytes, 41% partition free, flashed and
  hash-verified on `/dev/cu.usbmodem1101`, then hard-reset.
