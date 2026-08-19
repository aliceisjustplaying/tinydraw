# NTP toast text-size receipt — 2026-08-17

## Author request

The author confirmed that unavailable-network handling terminates, successful
sync reaches `TIME SET`, and all labels are centered. The remaining request was
to make `CONNECTING`, `SYNCING`, `TIME SET`, and `TIME ERROR` the same text size
as export's `SAVING` label.

## Red repro and fix

The focused pixel test renders `SAVING` and each successful NTP state, measures
ink-colored glyph bounds, and requires equal height plus horizontal centering.
Before the fix, all three NTP labels were 14 px high while `SAVING` was 21 px;
the test failed three times with `13 == 20` inclusive-coordinate deltas
(`host-focused-red.log`).

NTP text now uses the same integer scale 3 as `SAVING`. `CONNECTING` is too wide
for the export toast's 160 px interior at that scale, so the NTP-only toast is
widened symmetrically from x=104..264 to x=80..288. Its y=70..132 bounds and
center remain unchanged. Export toast geometry and progress rendering are
unchanged.

The fixed focused test passes 16 assertions (`host-focused-green.log`). It
checks all successful labels remain within x=80..288 and centered to within four
pixels. `TIME ERROR` uses the same shared scale/centering helper; its distinct
red color intentionally excludes it from the ink-color measurement.

## Validation and installed product

- Host release: 29/29 CTest targets pass (`host-release.log`).
- ASan/UBSan: 11/11 CTest targets pass (`host-asan.log`).
- Project format check and `git diff --check` pass (`format-check.log`).
- The normal combined product flashed and hash-verified (`product-flash.log`).
- Boot reached startup `pass=1` and `TINYDRAW_VECTOR_V2_READY` with 6,120 bytes
  of main-task stack free (`product-boot.log:36-37`).
- No mass-storage command was run.

The combined product includes the fine 400% minimap mapping from commit
`803bc97`.

## Glass verdict

**Accepted 2026-08-17.** The author observed `CONNECTING`, `SYNCING`, and
`TIME SET`, confirmed unavailable-network handling terminates, and accepted the
larger centered text. Clock sync is done for the current release scope.
