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
| 50% | 747–749 ms | **337–338 ms cache-ready** | physical completion not yet measured |
| 100% | 1.23–1.24 s | **587–605 ms cache-ready** | over target |
| 200% | 902–905 ms | **464 ms cache-ready** | physical completion likely over 500 ms |

First physical strip remained 7–37 ms and complete visible fallback 40–72 ms,
except cancellation-inclusive 50% transitions remained below 50 ms. All 12
transitions succeeded. These settled numbers were later found to stop before
full-viewport submission/completion and therefore must not be treated as the
physical settled gate. Profiling shows 100% remains dominated by about 399 ms
capsule rasterization plus 85 ms compositing and 52 ms publication; reaching a
reliable sub-500-ms 100% pass likely needs geometry reuse across the twelve
bands or a two-core settled rasterizer rather than more scalar micro-tuning.

## Follow-up: correctness review and physical settled endpoints

A GPT-5.6 Sol review (`SOL_REVIEW_ITERATION_1.md`) rejected the initial settled
claim and found correctness gaps in the first LOD, publication endpoint,
post-mutation source ownership, and cross-core display access. The follow-up
iteration now:

- uses an iterative centerline/radius error-bounded LOD and tests loops,
  hairpins, pressure pulses, painter order, and eraser behavior;
- treats malformed or incomplete LOD maps as an atomic raw-geometry fallback;
- measures settled latency only after the final display-transfer completion,
  with generation/revision/view revalidation;
- checks cancellation inside long capsule scan loops;
- keeps the fallback arena pinned across mutations and repairs only affected
  exact source bands before atomically advancing its revision;
- serializes benchmark display mutation, staging, and timing snapshots through
  the cache/display mutex;
- shares the canonical coverage arena with settled rendering, avoiding an
  additional 164,864-byte PSRAM allocation.

`review-fixes-mutation-auto.log` is the latest physical run. All 12 unchanged-
document zoom transitions succeeded. Honest physical endpoints were:

| Zoom | First strip complete | Visible fallback complete | Visible settled complete |
|---:|---:|---:|---:|
| 50% | 7.1 ms typical; 80.2 ms cancellation-inclusive | 48.7 ms typical; 123 ms worst | **390–464 ms** |
| 100% | 7.9–12.8 ms | 43.7–49.2 ms | **676–682 ms** |
| 200% | 65–79 ms cancellation-inclusive | 98–113 ms | **650–651 ms** |

The fallback interaction gates still pass. The settled <500 ms target passes at
50% but not at 100% or 200%.

Grouping all visible bands in one cache cell into a single geometry traversal
reduced segment setup substantially, but physical settled latency only improved
from about 708 to 676–682 ms at 100% and from about 656 to 650–651 ms at 200%.
Pixel coverage and compositing—not repeated document traversal—are now the main
measured bottlenecks. This falsifies the expectation that grouping alone would
reach the settled target.

The same run automatically appended a vector stroke, attempted a zoom while the
pinned source was stale, waited for incremental exact repair, and retried. The
result was:

```text
TINYDRAW_FALLBACK_REPAIRED revision=2
TINYDRAW_AUTO_MUTATION started=1 committed=1 stale_zoom_accepted=0 repaired_zoom_accepted=1
```

Thus the stale source was refused and zoom capability recovered after repair.
This validates one mutation path; it is not yet an exhaustive state-machine
proof for arbitrary cancellation, repeated mutations, erasing, or pan overlap.

## Grok handoff checkpoint

`grok-handoff-auto-hardware.log` records the exact state handed to the next
independent reviewer. It includes the second Sol review fixes: mutation edge
bands are invalid unless the live raster captured the whole band, settled
publication is bound to the viewport snapshot whose readiness was proved,
spatial-index append failure disables candidate culling, cancellation is
bounded during compositing, and every local radius extremum is retained.

Results:

- 12/12 unchanged-document zoom transitions accepted;
- first physical strip: about 7 ms typical, 71 ms worst;
- complete physical fallback: about 42–49 ms typical, 113 ms worst;
- settled physical completion: 50% 396–459 ms, 100% 674–677 ms, 200% 632 ms;
- mutation test: stale zoom refused, exact fallback repair completed, retry
  accepted;
- post-benchmark-allocation PSRAM: 125,208 bytes free, 124,928-byte largest
  block;
- LOD: 19,844 canonical samples to 7,537 settled samples, 90,444 of 98,304
  allocated bytes.

The pressure-extremum correction trades approximately 11 KB and modest settled
work for correctness. It does not change the conclusion: first/full fallback
passes, while settled 100% and 200% remain above the eventual 500 ms goal.
Current automated zoom coverage remains 50/100/200%. Desired production zoom
coverage includes at least 25%, 400%, and ideally 800%; those levels require
separate provenance, quality, memory, drawing, pan, and visual validation and
are not implied by this checkpoint.

## Grok 4.6 x-high review and follow-up

`GROK_4_6_XHIGH_REVIEW.md` is the independent review of `ba6c392`. The reviewer found a wrong-camera publication path in the partially written bottom zoom band, starvation of adjacent pan runway behind settled rendering, fixed-world LOD error growth at high zoom, and canonical cancellation latency.

The follow-up made these changes:

- complete every intersecting edge band before marking it derived;
- materialize a 32-pixel pan runway before settled rendering;
- check cancellation between canonical composite tiles;
- preserve radius-extremum plateaus and validate finite LOD samples;
- reject candidate bitsets too short to represent the document;
- require a real transfer completion before publishing `settled_us`;
- switch one LOD arena between normal 50/100% and tighter 200% maps.

Hardware evidence:

| Run | Purpose | Result |
|---|---|---|
| `grok-fixes-auto-hardware.log` | Edge-band/runway fix before canonical cancellation | Pan assertions passed; cancellation reached ~95 ms |
| `grok-fixes-cancel-auto-hardware.log` | Canonical composite cancellation | Worst cancellation ~14 ms; first feedback 7–21 ms |
| `grok-fixes-final-auto-hardware.log` | One static tight LOD | Better 200% geometry bound, but all zooms paid the larger map |
| `grok-span-auto-hardware.log` | Per-row conservative scanline bounds | Regressed raster time; reverted |
| `grok-dynamic-lod-auto-hardware.log` | One dynamically rebuilt LOD arena | Selected checkpoint before exact-commit validation |

In the dynamic-LOD run:

- all twelve zoom transitions were accepted;
- `down12_ready=1` and `right1_ready=1` for every transition;
- one-pixel lateral runway became valid in 58–66 ms;
- first physical feedback was 7–20 ms;
- complete physical fallback was 40–55 ms;
- physical settled was 489–491 ms at 50%, 799–851 ms at 100%, and 747–748 ms at 200%;
- stale post-mutation zoom was refused, repair completed, and retry succeeded;
- post-allocation PSRAM was 76,056 bytes with a 147,456-byte LOD allocation.

The interaction fallback and small-pan runway pass. Settled 100/200 remains over target. Drawing latency and visual behavior still require an interactive hardware pass; the automated mutation path is vector-only and does not substitute for live pen/eraser testing.

### Exact-commit hardware identity

After committing the Grok follow-up as `4fc345e`, the same automated run was
flashed again. `4fc345e-auto-hardware.log` reports:

```text
App version: 4fc345e
```

Its metrics reproduce the preceding dynamic-LOD run: 12/12 accepted zooms,
7–20 ms first physical feedback, 40–55 ms complete fallback, every edge/runway
assertion passing, and post-mutation refusal/repair/retry passing. This closes
the earlier evidence-chain caveat where logs named `caed9b5-dirty` rather than
the commit under review.

### Diagnostic hardening and publication telemetry run (`12b70da`)

`12b70da-diag-auto-hardware.log` is the first hardware run of the reviewed
diagnostic patches (CO5300 window validation, cooperative shutdown, pan-time
work suppression, coherent view snapshots) plus correlated publication
telemetry. Firmware identity: `App version: 12b70da`.

Results:

- 12/12 zoom transitions accepted; `down12_ready=1` and `right1_ready=1` on
  every cycle.
- Zero `TINYDRAW_PANEL_WINDOW_REJECT` events across the entire run.
- First physical strip 7.0–9.8 ms; complete physical fallback 40.2–51.0 ms.
- Physical settled 490–491 ms at 50%, 792–828 ms at 100%, 737–740 ms at 200%,
  within the prior run-to-run range; no regression from telemetry.
- `TINYDRAW_SETTLED_LOD output=7537`, identical to the pre-patch baseline, so
  the plateau-boundary LOD change does not alter the realistic workload.
- New internal-heap receipt: `internal_free=95240 internal_largest=54272`.
  The planned internal-RAM settled-scratch experiment fits: an 11.8 KB
  368x32 coverage band allocates trivially; a full 35 KB band workspace fits
  the largest block with limited margin, so borrowing `active_coverage_`
  remains the safer option.
- Publication determinism: every repeated same-zoom, same-revision
  publication hashed identically across all cycles (100% fallback
  `17300f6a`, settled `49a8e974`; 50% `964586fc`/`a167c6e9`; 200%
  `b3eb028d`/`bce1c492`).
- Cross-validation: `TINYDRAW_AUTO_FINAL_PIXELS` (`ink=32554
  hash=6c663cc6`) exactly matches publication id=50 computed by the
  independent benchmark-side record.
- Post-mutation: refusal, repair (`FALLBACK_REPAIRED revision=2`), and retry
  all passed; revision 2 flows through subsequent publication records.
- Known benign record: publication id=1 (initialization `set_zoom(100)`)
  hashes a just-cleared white atlas (`ink=0`) and submits nothing to the
  panel; it is the init generation, not a blank publication defect.
- Disclosed cost: settled `publish_us` grew from ~52 ms to ~64–67 ms; the
  difference is the 368x372 visible-region hash now inside the settled
  publication block. Physical settled endpoints are completion-based and
  stayed in range.

Not exercised by this automated run: manual pan (`TINYDRAW_PAN_CONTENT`
expected-content records), live drawing/eraser latency, and the cooperative
shutdown path (`finish_interactive_pan_benchmark`). These need an interactive
session.

### Interactive session with publication telemetry (`12b70da-manual-diag.log`)

Manual pan/zoom/draw/eraser session on the same firmware, ending with a
persisted report (`TINYDRAW_INTERACTIVE_PAN_DONE persisted=1`). Zero
`TINYDRAW_PANEL_WINDOW_REJECT` events. The new telemetry converted every
subjective observation into a measurement:

- "Panning was choppy after eraser": one gesture recorded
  `rejected_views=103 max_missing_pixels=23488` while post-eraser bands were
  invalid (20:51:35). After `FALLBACK_REPAIRED revision=10`, the next pan ran
  86 accepted frames at ~26 ms with zero rejections. First quantified
  hardware receipt for the hard-refusal pan policy.
- "Zoom fails after drawing": two `TINYDRAW_INTERACTIVE_PAN_FAIL` refusals
  (20:50:37 zoom=50, 20:51:27 zoom=200), both while pinned-source repair was
  pending. Repair latency: ~3 s (revision 3), ~4 s (revision 4), and ~12 s
  cumulative for revisions 5-10 during a six-stroke burst, because each new
  stroke re-targets the repair revision. This fails the review's <=2 s
  repair gate and is the strongest hardware argument for the
  overview-fallback production design over refusal.
- "Rendered block by block over seconds": exact canonical refinement is
  directly visible as a band sweep in the publication records (ids 205-214,
  y0=74..362, ~0.4 s per 32-row band, top-down). Settled quality lands
  first; the sweep is the canonical pass repainting settled bands.
- Zoom-to-settled after panning off cell alignment renders two cells per
  supertask: bands=20 (836 ms raster+composite) and bands=26 (983 ms)
  versus 12 bands centered. Multi-cell views nearly double settled cost.
- Tapping the current zoom level re-runs the entire transition (no no-op
  guard in set_zoom), costing a full fallback + settled + exact cycle for a
  redundant tap. The toolbar also starts showing M while the post-auto-run
  zoom is 50%. Both are benchmark-wiring warts, listed for completeness,
  not scheduled for fixing in the disposable coordinator.
- Incremental LOD append worked across all nine mutations (7,537 -> 7,605
  -> 10,185 high-quality samples; capacity 12,288 never exceeded; no raw
  fallback events).
- Live drawing stayed healthy under mutation load: 1.7-2.8 ms average
  raster updates; stroke finish 36-60 ms including capture and LOD.

Conclusion: fallback, panning on valid cache, drawing, and publication
integrity all pass. Repair latency and refusal behavior under mutation
bursts are confirmed architectural limits of the camera-aligned atlas, as
predicted by all three reviews. These receipts close the prototype's
evidence file; the remaining work belongs to the production
overview-plus-tiles design.
