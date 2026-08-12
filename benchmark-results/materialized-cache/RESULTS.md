# Materialized vector-cache hardware result

Firmware: `c38ad05` plus conservative macrogrid fix `be16e3e` in ancestry.
Device: ESP32-S3, 8 MiB PSRAM, 368x448 RGB565 panel.
Workload: coherent 1,000-stroke vector document plus live strokes.

## Recorded metrics

| Gate | Result | Status |
|---|---:|---|
| Direct pan p95 | 50%: 30.674 ms; 100%: 26.018 ms; 200%: 26.013 ms | pass at 100/200; pass under 35 ms at 50 |
| Drawing update p95 | 3.097 ms | pass (<10 ms) |
| Drawing update p99 | 4.680 ms | pass |
| 200% first physical valid | 365.291 ms | fail (<100 ms target) |
| 200% settled coarse | 7.376 s | fail (<500 ms target) |
| 200% center exact | 7.901 s | informational |
| 200% full 3x3 exact | 16.361 s | informational |
| Invalid/missing frames | 50%: 163/215; 100%: 0/333; 200%: 0/144 | 50% fail |

The 50% zoom timings are zero because later zoom requests failed and reset that level's metrics. The visual report says the successful 100% -> 50% transition appeared nearly instant, but this run did not preserve a usable numeric transition result.

## Visual findings

- Live drawing felt fast.
- Selecting Pen exposed stale pre-vector raster only where a toolbar overlay had been restored.
- Color palette was inaccessible in this benchmark configuration.
- 50% zoom appeared fast, then pan and drawing worked, but 163 pan frames crossed invalid cache bands.
- 50% -> 200% displayed a pixelated fallback immediately; it did not refine on screen until a pan caused another display push.
- Returning from 200% to 100% or 50% failed. Serial confirms `TINYDRAW_INTERACTIVE_PAN_FAIL zoom=100` and `zoom=50`.

## Interpretation

The key architectural bets were partially validated:

- Existing raster presentation preserves ~26 ms pan at all tested zoom levels.
- Live raster drawing remains comfortably under its 10 ms budget even with 1,000 vector strokes and background infrastructure.
- A materialized fallback makes zoom feel immediate enough to notice content rather than a checkerboard.

The prototype is not yet a product-quality demonstration:

- The first-valid gate is missed at 200% by 3.65x.
- The coarse pass is far too slow and is not repainted when ready.
- 50% cache validity accounting/refinement fails under pan.
- Toolbar refresh pulls stale raster state into overlays.
- Post-drawing zoom-out requests fail the conservative fallback proof instead of taking a valid render path.

These are implementation and scheduling failures in this prototype, not evidence that vector authority is impossible. The strongest positive evidence is that the two interaction loops users feel continuously—pan and drawing—meet their gates. The remaining work is concentrated in zoom transition publication and cache validity.

## Decision

**CONTINUE, but do not start production integration yet.**

Run one focused zoom-publication iteration before the final go/no-go:

1. Present only from `WorldCanvas`; toolbar close must restore from the active materialization, never `committed` startup raster.
2. Push coarse/exact center bands to the panel when published rather than waiting for user pan.
3. Replace the current all-or-nothing fallback proof with per-band validity and synchronous rendering only for uncovered visible bands.
4. Preserve zoom metrics per attempt and record failed transition reason/cancellation time.
5. Re-run only M -> S, M -> L, and L -> M with several pans after each.

A credible final go requires first valid under 100 ms, visible settled output under 500 ms, and zero invalid visible bands. Pan and drawing already pass.
