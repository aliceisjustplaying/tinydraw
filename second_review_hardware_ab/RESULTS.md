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

## Follow-up: center-out strip presentation with completion endpoints

A third run (`strips-auto-zoom.log`) measured the next iteration: zoom
transitions present as center-out 22-row nearest-resampled strips (one strip =
one panel transaction), with ISR-recorded transfer-completion sequence
numbers providing honest physical endpoints. All times are elapsed from the
zoom input event, before cancellation.

| Metric (12 transitions) | Typical | Worst |
|---|---:|---:|
| Cancellation done | 16–989 µs | 49.0 ms |
| First strip ready (resampled) | 5.0–6.7 ms | 54.7 ms |
| First strip physically complete | **6.3–8.1 ms** | 56.2 ms |
| Last visible strip physically complete | 38.9–49.5 ms | 97.4 ms |

The worst case is the cycle whose cancellation had to wait ~49 ms for an
in-flight 200% canonical band to stop; even it met the 100 ms first-valid gate.

A fourth run (`realistic-auto-zoom.log`) repeated the cycle on the realistic
handwriting document (1,000 strokes, 20,153 samples, deterministic seed 7):
first strip physically complete 6.6–16.2 ms, last visible complete 39–57 ms,
12/12 transitions, initial exact atlas 5.36 s versus 4.12 s for the old
fixed-12-sample workload. Cancellation collisions were more frequent (1–9 ms)
because canonical refinement of the heavier document overlaps more of each
cycle, and every transition still passed the gates.
Against the review's gates: input→first completed valid strip p95 ≤ 100 ms now
passes by an order of magnitude (6–8 ms typical), and input→complete visible
valid p95 ≤ 150–180 ms passes at 39–50 ms. The visible-region cost is now
dominated by 17 × ~2.4 ms generic nearest resampling — the specialized
power-of-two resample kernels from the review's P2 list are the next lever if
full-visible time matters. Settled at 200% is intentionally unset pending the
settled-renderer experiment. Visual correctness on the physical panel should be
spot-checked by eye; the strips use the same validity-proven source and
resampler as the previous full-region path.

## Follow-up: pinned complete fallback and settled pass

Adding visible settled work before runway originally caused cycles 3–11 to be
refused: cancellation left the current atlas partial, and the next zoom treated
that partial atlas as its only source. `settled-auto-zoom-diagnostic.log`
reproduces the failure and shows source coverage was proven while raster band
readiness failed.

The no-third-buffer correction pins the initial complete 100% atlas in the
inactive 2.97 MiB arena. Later transitions rewrite the active arena in place
from that immutable source. Offscreen runway may still be canceled, but it can
no longer corrupt or replace the source needed by the next zoom.
`pinned-fallback-runway-auto-zoom.log` is the physical validation:

- 12/12 transitions accepted; no failure/refusal lines;
- first strip physically complete: **7.1–15.8 ms**;
- complete visible region physically complete: **40.2–51.6 ms**;
- visible capsule-settled: 50% **747–749 ms**, 100% **1.23–1.24 s**, 200%
  **902–905 ms**.

The fallback gates pass with wide margin. A document mutation invalidates the
pinned source; zoom must conservatively refuse until that source has been
repaired or rebuilt for the new revision. Production will replace this temporary
invariant with an incrementally maintained complete overview.

## Follow-up: profiled settled LOD

`settled-lod-auto-zoom.log` records the first measured settled optimization.
The settled renderer now consumes an independently generated centerline LOD,
uses incremental capsule scanline math and a squared-distance edge ramp instead
of software `sqrt`, and presents the complete visible settled viewport once
instead of twelve band pushes.

The 19,844 source samples become 6,453 settled samples (77,436 bytes). Physical
results over 12 accepted transitions:

| Zoom | Before | LOD settled range | Result |
|---:|---:|---:|---|
| 50% | 747–749 ms | **337–338 ms** | passes <500 ms |
| 100% | 1.23–1.24 s | **587–605 ms** | 17–21% over target |
| 200% | 902–905 ms | **464 ms** | passes <500 ms |

First physical strip remained 7–37 ms and complete visible fallback 40–72 ms,
except cancellation-inclusive 50% transitions remained below 50 ms. All 12
transitions succeeded. Profiling shows 100% remains dominated by about 399 ms
capsule rasterization plus 85 ms compositing and 52 ms publication; reaching a
reliable sub-500-ms 100% pass likely needs geometry reuse across the twelve
bands or a two-core settled rasterizer rather than more scalar micro-tuning.
