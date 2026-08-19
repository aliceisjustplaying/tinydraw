# TinyDraw project state

Updated: 2026-08-19. Vector V2 is release-ready pending one same-revision final
battery and product reflash after this documentation cleanup. Raster V1 remains
a supported, independently buildable product.

## Release-candidate summary

- The host Debug and Release suites pass 31/31 targets. ASan/UBSan passes 13/13,
  and the format check passes.
- The 604-slot physical battery passed every gate before the final export-label
  polish. The currently flashed product includes that polish, booted normally,
  restored generation 581 with 226 retained operations, and has no failure
  marker. The same-source battery and reflash are the only remaining tag gate.
- Product flash uses a 1.75 MiB app partition, 4 MiB authority journal,
  10.125 MiB export volume, and 64 KiB coredump partition.
- The captured drawing is now part of the physical battery: 102 active
  operations and 2,706 samples replay through production append and absorption.
- Both PNG and SVG export use one pinned authority snapshot. SVG erasing,
  physical-gesture grouping, curve continuity, and extreme-zoom seam teeth are
  fixed. PNG uses production settled antialiasing.
- Host eject and **EJECT & EXIT** both return to drawing and restore USB serial.
  Popup contacts cannot fall through to controls underneath them.

## Current scorecard

| Area | State | Current evidence |
|---|---|---|
| Pan | accepted | Tear-free glass at every zoom; about 29.4 FPS. The optical positive control tore and the rising-edge product cadence stayed clean across 1,495 analyzed frames. |
| Ink | accepted | Zero lost Down/Up in the trace corpus. Ordinary-firmware drawing feel was accepted. |
| Cold rendering | accepted | Every final corpus is below 500 ms. Final 400% walls: overlap 213.970 ms, general 490.747 ms, captured drawing 344.686 ms. |
| Revisit retention | accepted | Pure-revisit amplification is 1.000; accepted revisits return sharp without visible refill while retained. |
| Undo/Redo | accepted | Whole-gesture exactness, retained Redo, and bounded damage pass. Physical interaction work is at most 27.3 ms; first visits may rebuild behind the hourglass. |
| Settled AA | accepted | Appearance and exact checksums are accepted. The five-attempt speed campaign moved its five device workloads from 75.217/87.869/177.282/386.169/959.910 ms to 75.102/87.647/176.885/383.594/946.849 ms. |
| Autosave/recovery | accepted | Authority-only recovery survives truncation and corruption fixtures. A captured physical journal restored exactly; time-bounded destructive power-loss testing is deferred. |
| PNG/SVG export | accepted | Same-snapshot pair, settled PNG, transparent SVG erasers, one path per physical gesture, shared smooth curves, and exact SVG span boundaries. |
| USB export exit | accepted | Two physical host-eject cycles returned serial without screen interaction; explicit exit also passes. Export text is centered, complete, and contained. |
| Touch arbitration | accepted | Active popups own their input layer. Size/history and Export/pencil fall-through regressions are covered. |
| New document | accepted | Product New uses the blank reset path. Only diagnostic firmware restores its test fixture. |
| Color dialog | accepted | Open time fell from 132.466 ms to 27.568 ms. RGB565 modal chrome has one byte-swap boundary. |
| Zoom/minimap | accepted | Focus-centered zoom and absolute minimap tap/drag navigation pass host and glass checks. |
| Clock/power | accepted | One-shot NTP feedback and RTC write are accepted; the four-second hardware shutdown remains active. |

## Final performance record

The cold-render campaign used all five experiments. The last accepted treatment
removed a duplicate visible-tile directory scan:

| corpus | zoom | before | after |
|---|---:|---:|---:|
| general | 50% | 393 ms | 390 ms |
| general | 100% | 386 ms | 379 ms |
| general | 200% | 461 ms | 456 ms |
| general | 400% | 498 ms | 490.747 ms |
| overlap | 50% | 478 ms | 474 ms |
| overlap | 100% | 288 ms | 287 ms |
| overlap | 200% | 269 ms | 267 ms |
| overlap | 400% | 217 ms | 213.970 ms |
| captured drawing | 50% | 118 ms | 117 ms |
| captured drawing | 100% | 134 ms | 133 ms |
| captured drawing | 200% | 200 ms | 199 ms |
| captured drawing | 400% | 347 ms | 344.686 ms |

The maximum cooperative tick was 10.719 ms. This is the longest uninterrupted
piece of background rendering between opportunities to service input and other
work; it is not the total render time.

Undo/Redo used all five experiments. Focused chrome presentation fell from
5.982 ms to 3.003 ms, and dense host history moves fell from 0.172 ms to
0.0169 ms. Settled AA used all five experiments. SVG smoothness used four of
five because the reported curve joins and seam teeth were fixed and accepted.

Every attempted treatment, including rejections and measurements, remains in:

- [`COLD_RENDER_EXPERIMENTS_2026_08_19.md`](docs/receipts/vector-v2/COLD_RENDER_EXPERIMENTS_2026_08_19.md)
- [`HISTORY_PERFORMANCE_EXPERIMENTS_2026_08_19.md`](docs/receipts/vector-v2/HISTORY_PERFORMANCE_EXPERIMENTS_2026_08_19.md)
- [`AA_PERFORMANCE_EXPERIMENTS_2026_08_19.md`](docs/receipts/vector-v2/AA_PERFORMANCE_EXPERIMENTS_2026_08_19.md)
- [`SVG_SMOOTHNESS_EXPERIMENTS_2026_08_19.md`](docs/receipts/vector-v2/SVG_SMOOTHNESS_EXPERIMENTS_2026_08_19.md)
- [`PERFORMANCE_CHRONICLE.md`](docs/PERFORMANCE_CHRONICLE.md)

The release contract is [`SHIP_CONTRACT.md`](SHIP_CONTRACT.md). Deferred work is
[`docs/POST_RELEASE.md`](docs/POST_RELEASE.md). Completed roadmaps, reviews, and
the original detailed contract remain under [`docs/archive/`](docs/archive/).
