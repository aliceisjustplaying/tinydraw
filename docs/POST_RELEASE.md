# TinyDraw post-release work

Updated: 2026-08-20. This is the active queue after the first Vector V2 release.
The completed release roadmap is archived in
[`V2_RELEASE_ROADMAP_PRE_RELEASE_2026-08-19.md`](archive/2026-08-vector-v2-performance/V2_RELEASE_ROADMAP_PRE_RELEASE_2026-08-19.md).

## Drawing and display

- Polish minimap navigation and optional visibility without changing its
  accepted absolute tap-and-drag behavior.
- Make the export exit presentation feel less abrupt while keeping **EJECT &
  EXIT** contained and preserving automatic host-eject return.
- Add a capture-only ESP32 screen-dump build. Mirror submitted panel rectangles
  into a 368×448 RGB565 shadow buffer and convert the serial dump to PNG on the
  host. Keep the product display path allocation-free.
- Investigate the tiny extreme-magnification raster edge notches that settled
  antialiasing masks in PNG. SVG shared boundaries are already smooth.
- Revisit band-sliced full-frame refresh if the remaining 166–184 ms one-shot
  refresh class becomes visible during interaction.

## Performance

- Continue settled-AA work only with a fresh bounded experiment budget and the
  existing exact checksums. The 2026-08-19 five-attempt campaign is complete.
- Revisit extreme 400% cold and captured-drawing rendering only if a real product
  workload crosses the 500 ms contract.
- Attribute any unexplained revisit render with the gate-only live ledger before
  changing cache policy or capacity.
- After the blog post and demo video, run a bounded 604-vs-620/624-slot device
  A/B. Each added slot costs 8,224 B; preserve at least 200 KiB after the demo
  build's worst-case autosave peak, rerun the autosave/export reserve gates, and
  keep the larger cache only if cold-render or pan behavior measurably improves.
- Close formal optical ink latency if a later renderer change touches the
  accepted input-to-display path.

## Storage and endurance

- Measure committed-work loss under timed physical power cuts. The release
  contract intentionally makes no unverified five-second promise.
- Add two-arena journal compaction and clear capacity/failure UI while preserving
  the last valid recovery point.
- Soak repeated draw, pan, Undo/Redo, autosave, export, eject, NTP, and power
  cycles for hours. Record failures and the exact source revision.
- Characterize longer real documents, journal capacity, cache pressure, and the
  25% overview path.

## Later product work

- Richer semantic/editable SVG as a separate format.
- 800% zoom.
- RP2350 feature parity.
