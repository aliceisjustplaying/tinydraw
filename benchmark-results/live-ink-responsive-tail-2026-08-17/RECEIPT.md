# Responsive 0.40 ink tail receipt — 2026-08-17

## Result

The product retains `InkConfig::streamline = 0.4F` while its replaceable live
ribbon tail reaches the current raw canvas touch. Smoothed `InkPoint`s remain
the sole operation-builder input, so this does not substitute raw samples into
the document or SVG authority.

The ordinary interactive V2 product at commit `ea94071` is installed. Its boot
reached `TINYDRAW_VECTOR_V2_READY` with `pass=1`, nine TE edges, and 6,312 bytes
of main-task stack margin (`product-boot.log`). No USB mass-storage command was
run.

Author finger-on-glass acceptance remains pending.

## Cause and seam

At 0.40, `InkStream` intentionally low-pass filters each input point. The app
then passed that same filtered point to both `ChainedOperationBuilder` and
`CurvedRibbonStream`; although the ribbon had a replaceable provisional tail,
that tail could only reach the already-lagging filtered point.

`CurvedRibbonStream::append` now accepts an optional visual endpoint. Only its
provisional primitives use that endpoint. Its `first_`, `stable_`, and `last_`
state and all committed primitives continue to use the filtered `InkPoint`.
`process_live_ink_move` makes the distinction explicit by accepting both an
authority point and a visual endpoint.

On lift, the app now passes the last clipped raw canvas touch to
`InkStream::finish`. This exercises the stream's existing `complete` behavior
(alpha 1.0) instead of incorrectly finishing at its own filtered output.

Regression coverage in `tests/ribbon_geometry_test.cpp` runs authority-only and
responsive-preview streams side by side. Every committed primitive must match,
while the provisional cap must reach the raw endpoint. The coordinator test
also proves the visual endpoint is published before ready authority.

## Host validation

- Release: 29/29 CTest targets passed (`host-release.log`).
- ASan/UBSan: 11/11 targets passed (`host-asan.log`).
- Product firmware linked successfully at 1,005,392 bytes with 43,184 bytes
  left in the app partition (`product-flash.log`).

A focused five-run host microbenchmark compared 5,000,000 curved-ribbon appends
at parent `d47627c` with responsive appends at `ea94071`:

| Variant | Median | Range |
|---|---:|---:|
| Prior filtered tail | 68.647 ns/append | 68.435–71.401 |
| Responsive raw visual tail | 68.178 ns/append | 67.804–69.390 |

The -0.68% median difference is noise-level evidence of no geometry-CPU
regression, not a speedup claim (`host-geometry-baseline.log`,
`host-geometry-responsive.log`).

## Physical latency battery

Three complete device runs produced stable green ink traces with no watchdog,
panic, reset, stack-overflow, presentation failure, commit failure, or touch
overflow (`device-gate.log`, `device-gate-run2.log`, `device-gate-run3.log`).
All three runs report `ink_trace=1`.

Median values across the three new runs:

| Trace | Zoom | event→geometry p95 | event→submit p95 | event→display p95 | Verdict |
|---|---:|---:|---:|---:|---|
| fast-curve-dense-25 | 25% | 0.794 ms | 2.627 ms | 3.682 ms | pass |
| fast-curve-400 | 400% | 0.792 ms | 2.812 ms | 4.360 ms | pass |
| fast-curve-400-xl | 400% | 0.796 ms | 2.962 ms | 4.662 ms | pass |
| slow-precise-100 | 100% | 0.795 ms | 1.856 ms | 2.354 ms | pass |
| scribble-multistroke | 100% | 0.793 ms | 3.418 ms | 5.884 ms | pass |

This visual change is not free end to end: the ordinary 400% trace's display
p95 is 4.360 ms versus a 3.941 ms median across four prior 0.40 runs
(`benchmark-results/committed-overlay/streamline40-battery-1.log` and the three
`benchmark-results/minimap-navigation-2026-08-17/*gate*.log` runs), a +0.419 ms
cost from presenting the additional responsive pixels. It remains inside the
ink latency gate, but glass feel decides whether that trade is worthwhile.

## Unrelated full-gate reds

The final verdict also prints `overlap_cold=0`, the standing 50% overlap cold
red already documented in
`benchmark-results/minimap-navigation-2026-08-17/RECEIPT.md`. The export verifier
prints `paths=1 path_only=0` because it still compares SVG paths with operation
chunks after export changed to one path per gesture; this is a stale diagnostic
expectation, not an encoder failure (`device-gate.log`,
`esp32/main/vector_v2/vector_v2_gate_harness.cpp`). Both are outside this ink
change; the ink-specific verdict is green.

## Commands

```sh
cmake --build --preset host-release
ctest --preset host-release --output-on-failure
cmake --build --preset host-asan
ctest --preset host-asan --output-on-failure

./scripts/esp32 vector-v2-gate-harness /dev/cu.usbmodem101 448 verify
uv run --script tools/esp32-capture.py /dev/cu.usbmodem101 device-gate-run2.log 480 \
  --end-marker TINYDRAW_GATE1_AUTOMATED_DONE \
  --failure-regex 'task_wdt|Guru Meditation|TG1WDT_SYS_RST|stack overflow'

./scripts/esp32 vector-v2 /dev/cu.usbmodem101
uv run --script tools/esp32-capture.py /dev/cu.usbmodem101 product-boot.log 45 \
  --end-marker TINYDRAW_VECTOR_V2_READY \
  --failure-regex 'task_wdt|Guru Meditation|TG1WDT_SYS_RST|stack overflow'
```
