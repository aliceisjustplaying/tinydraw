# Second-review hardware A/B: review patches vs. baseline

Date: 2026-08-12. Device: physical ESP32-S3 (`/dev/cu.usbmodem1101`, VID 0x303A verified).
Workload: unchanged coherent 1,000-stroke interactive-pan benchmark document.

## Method

A hands-free driver (`auto-zoom-driver.patch`, applied to `esp32/main/hardware_app.cpp`)
runs the exact toolbar zoom path — `interactive_pan_benchmark_set_zoom` →
`push_world` → `record_zoom_present` — twelve times in the sequence
50, 100, 200, 100 × 3, with 5-second settling gaps, and prints driver-side wall
times. Driver timing starts before `set_zoom`, so cancellation, preparation,
and the display push are all included identically in both firmware builds.
This sidesteps the baseline/patched difference in internal `event_started`
placement.

- Baseline = commit `61dd649` + driver only.
- Patched = baseline + `review_findings_2026_08_12_noon/tinydraw-review-suggested-changes.patch` + driver.
- Raw logs: `baseline-auto-zoom.log`, `patched-auto-zoom.log`.

## Results (12/12 transitions succeeded in both runs)

| Transition | Baseline total | Patched total | Delta | Speedup |
|---|---:|---:|---:|---:|
| 100→50 (×3) | 143.2–143.4 ms | 51.7–51.9 ms | −91.5 ms | 2.8× |
| 50→100 (×3) | 201.8 ms | 110.7 ms | −91.1 ms | 1.8× |
| 100→200 (×3) | 201.4 ms | 110.6 ms | −90.8 ms | 1.8× |
| 200→100 (×3) | 167.9–169.9 ms | 62.3–65.3 ms | −104.6 ms | 2.6× |

Constant components:

- Display push (`push_world`, 368×372, 17 queued chunks): 26.1–26.2 ms in every
  sample of both runs.
- Initial full 3×3 exact atlas: baseline 4.332 s, patched 4.122 s (≈5% faster,
  consistent with the host band-benchmark's modest renderer gain).

## Interpretation

1. The review's central claim is physically confirmed. Removing the
   interaction-time full-atlas clear (plus caching the per-job source-validity
   scan) removes ≈91–105 ms from every zoom transition. The review estimated
   ≈82 ms for the clear alone from PSRAM throughput; the measured delta brackets
   that estimate.
2. Zoom-out transitions (100→50, 200→100) now meet the <100 ms first-valid gate
   outright: 52–65 ms including the push and cancellation.
3. Zoom-in transitions sit at ≈111 ms — just over the gate — still using
   full-region bilinear fallback and a single monolithic push. The review's
   remaining P0 items (nearest-neighbor first preview, strip-pipelined
   submission) target exactly the ≈85 ms prep that remains, so <100 ms
   is now the expected outcome rather than a hope.
4. No regressions: all transitions succeeded, timings are highly deterministic
   (sub-ms spread across repeats), and the patched initial atlas is slightly
   faster.

Caveats: driver-side totals end when the last chunk is queued (queue depth 3),
not at transfer completion — the same endpoint criticism the review makes of
`first_valid_us` applies here, equally to both runs. Settled/exact refinement
and pan behavior were not exercised by this driver; drawing was not exercised.
