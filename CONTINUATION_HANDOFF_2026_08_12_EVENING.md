# TinyDraw continuation handoff — 2026-08-12 evening

## Purpose

Durable handoff after the external-review integration and hardware-validation
session. It records what is committed, what was measured on hardware, every
verified finding, the product decisions made, and the exact continuation
order. Read together with:

- `TINYDRAW_FRESH_PERFORMANCE_REVIEW.md` (in ~/Downloads; the third external
  review — 20 findings, two patches, both now applied)
- `second_review_hardware_ab/RESULTS.md` (all hardware evidence, including
  today's two runs)
- `SECOND_REVIEW_ARCHITECTURE_ASSESSMENT.md` and
  `VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md` (background)

## Branch and repository state

Branch: `prototype/vector-materialized-cache`. All work committed and pushed
through this handoff's commit.

Session commits, oldest first:

| Commit | Content |
|---|---|
| `87ff28f` | Reviewed core fixes: LOD plateau cliff removal, per-stroke LOD fallback, zero-length capsule radius, Clang/Linux portability. Host-validated: 22/22 test, 22/22 release, 4/4 asan, format clean. |
| `6b1c406` | pan_active gate + even-band `present_job` + final-pixels diagnostics (the user's uncommitted work, hardware-proven by `manual-fixed-live-serial.log`). |
| `02fc7d3` | Reviewed esp32 diagnostic hardening: CO5300 window validation (reject+log), cooperative render-task shutdown, pan-time work suppression + 1-job runway budget, coherent view snapshots, end_pan ordering, internal-heap telemetry. First compile of this patch happened here; both ESP-IDF targets build. |
| `12b70da` | `TINYDRAW_PUBLICATION` correlated records (id, kind, generation, revision, origin, submit-sequence range, ink count, FNV-1a pixel hash) on band/settled/zoom_fallback publications; `expected_strokes` (vector-derived) in `TINYDRAW_PAN_CONTENT`. |
| `4f3e226` | Hardware log + RESULTS.md: automated 12-cycle run at `12b70da`. |
| `5d31931` | Hardware log + RESULTS.md: interactive manual session at `12b70da`. |
| (this commit) | This handoff + `tools/esp32-capture.py`. |

## Product decisions made this session

1. **Canvas: 4×4 screens = 1472×1792 world units** (up from 3×3 = 1104×1344).
2. **Zoom range: 25%–400% committed.** 800% remains an optional stretch tier
   with its own quality gate; if only 800% fails, cap at 400%.
3. Deliberate elegance: at 25%, the whole 4×4 canvas exactly fills the
   368×448 screen, so the overview buffer, the minimum-zoom view, and
   "show everything" are the same object.
4. These are tweakable within the math below, but every production component
   should now be sized from these numbers.
5. **The 3×3 camera-aligned atlas prototype is evidence-complete.** No further
   coordinator investment except measurement (Step 0 below). This is the
   unanimous conclusion of three independent reviews plus today's hardware
   receipts.

## Production memory budget (4×4, 25–400%)

| Component | Budget | Derivation |
|---|---:|---|
| Complete overview (RGB565, 25%) | 330 KB | 368×448×2; = whole canvas at min zoom |
| Raw vector operations | ~650 KB | ~13 B/sample measured (`TINYDRAW_VECTOR_READY bytes=325712` for 24,576 samples); compact/delta encoding target ~6–8 B/sample → ~3,000–4,000 strokes |
| LOD + spatial index | ~700 KB | zoom-bucketed LOD tiers + world-aligned index |
| Sparse RGB565 tile pool | ~1 MB | ~42 visible 64×64 tiles (8 KB each) + prefetch ring; independent of canvas size |
| Committed + live screen rasters | 645 KB | fixed, existing |
| Overlay (after shrinking) | ~100 KB | current full-screen overlay is 322 KB (F-17) |
| Scratch, queues, persistence staging | ~600 KB | |
| Reserve (largest contiguous block, not just total) | 1.5 MB | |
| **Total** | **≈5.5 MB of 8 MB** | closes with ~2.5 MB slack |

Precision: float32 world coordinates suffice for 4×4 at 400% (and even 800%);
the double-precision camera (`core/include/tinydraw/graphics/camera.h`) is
removable cleanup during the production port (review F-11, correctly sized as
per-sample cleanup, not the bottleneck).

Honest trade recorded: the canvas gives 16 screens of space but the vector
budget caps ink at roughly 2–4 screens of dense handwriting. Space to spread
out, not a demand to fill it.

## Hardware evidence from this session (firmware `12b70da`)

### Automated run — `second_review_hardware_ab/12b70da-diag-auto-hardware.log`

- 12/12 zoom transitions accepted; `down12_ready=1`, `right1_ready=1` every
  cycle.
- **Zero `TINYDRAW_PANEL_WINDOW_REJECT`** — CO5300 evenness hypothesis holds.
- First physical strip 7.0–9.8 ms; complete physical fallback 40.2–51.0 ms.
- Physical settled: 490–491 ms (50%), 792–828 ms (100%), 737–740 ms (200%) —
  within prior range; telemetry caused no regression (settled `publish_us`
  grew ~52→~64–67 ms from the disclosed in-block hash; physical endpoints are
  completion-based and unaffected).
- `TINYDRAW_SETTLED_LOD output=7537` — identical to pre-patch baseline; the
  plateau-boundary LOD semantics change does not alter the realistic workload.
- **Internal heap receipt: `internal_free=95240 internal_largest=54272`.**
  An 11.8 KB internal settled-scratch band allocates trivially; a full 35 KB
  band workspace fits the largest block with limited margin → prefer borrowing
  `active_coverage_` (164,864 B internal, idle during settled passes because
  strokes pause the render task; needs an explicit ownership handshake).
- Publication determinism: identical view+revision ⇒ identical hash across all
  cycles (100%: fallback `17300f6a` / settled `49a8e974`; 50%:
  `964586fc`/`a167c6e9`; 200%: `b3eb028d`/`bce1c492`).
- Cross-validation: `TINYDRAW_AUTO_FINAL_PIXELS` (`ink=32554 hash=6c663cc6`)
  bit-identical to publication id=50 from the independent benchmark-side path.
- Mutation: refusal→repair (`FALLBACK_REPAIRED revision=2`)→retry all passed.
- Known benign record: publication id=1 (init `set_zoom(100)`) hashes a
  just-cleared white atlas, submits nothing. Not a defect.

### Interactive session — `second_review_hardware_ab/12b70da-manual-diag.log`

Every subjective observation was quantified:

| User observation | Receipt |
|---|---|
| "Panning choppy after eraser, fast again later" | One gesture: `rejected_views=103 max_missing_pixels=23488`; after `FALLBACK_REPAIRED revision=10`, next pan 86 frames @26 ms, 0 rejections |
| "Zoom fails after drawing" | 2× `TINYDRAW_INTERACTIVE_PAN_FAIL` (20:50:37 zoom=50, 20:51:27 zoom=200), both during pending repair |
| Repair latency | ~3 s (rev 3), ~4 s (rev 4), **~12 s cumulative for revs 5–10** during a 6-stroke burst (each commit re-targets repair). **Fails the ≤2 s review gate.** |
| "Renders block by block over seconds" | Exact canonical sweep visible in publications ids 205–214: ~0.4 s per 32-row band, top-down |
| "M→L slow render 2–4 s" | Off-cell-aligned views render 2 cells per settled supertask: bands=20 (836 ms), bands=26 (983 ms) vs 12 centered |
| Toolbar shows M at 50% after auto run; tapping current zoom re-runs full transition | Benchmark wiring warts; `set_zoom` has no no-op guard. Documented, deliberately not fixed (disposable coordinator). |

Also: zero panel rejects, report persisted (`persisted=1`), live drawing
1.7–2.8 ms updates / 36–60 ms finish under mutation load, LOD appends healthy
across 9 mutations (7,537→7,605→10,185 samples, capacity 12,288 never hit, no
raw fallback).

**Interpretation (three-review consensus + receipts):** fallback, cached pan,
drawing, and publication integrity pass. Repair latency and refusal under
mutation bursts are confirmed architectural limits of the camera-aligned
atlas — the production design removes both (overview-derived fallback instead
of refusal; O(one-stroke) incremental tile updates instead of O(document)
band re-rendering).

## The headline unproven performance claim (pre-registered)

**Hypothesis:** the settled renderer is PSRAM-memory-bound, not compute-bound.

Receipts supporting it: coverage scratch is PSRAM
(`interactive_pan_benchmark.cpp` allocates renderer scratch with
`MALLOC_CAP_SPIRAM`, aliased as `settled_scratch`); destination
`canvas.visible()` is PSRAM (`firmware_canvas.cpp`); working set >400 KB vs
32 KB dcache; measured 76 µs/segment ≈ 150–350 cycles/pixel for ~10 float
ops; precedent at `FINDINGS.md:74` (internal-RAM coverage transformed the
live path). The canonical renderer already keeps per-pixel work internal
(`ViewportRenderer` lanes_); the settled renderer skipped the trick.

**Pre-registered predictions for Step 0** (write results in the chronicle
whether they pass or fail):

- Rendering settled per 32-row band with an *internal-RAM* scratch (existing
  `settled_render_region` API, zero core changes; borrow `active_coverage_`)
  drops `raster_us` by **≥40% at 100%** (471→<283 ms).
- `clear_us`/`composite_us` roughly unchanged in Step 0 (destination still
  PSRAM).
- Known confound: per-band traversal adds ~30–50 ms total (chronicle §9).
- If raster_us barely moves, the hypothesis is dead → go directly to the
  microtile/span renderer (fresh review §11 items 1–2).
- Follow-ups if Step 0 passes: internal band destination + direct atlas
  publish (kills `copy_job_to_cache` and most of `publish_us`), then per-row
  touched spans (targets the ~108 ms `composite_us` and clears). Combined
  target: settled <500 ms at all committed zooms on the 1,000-stroke
  document.

## Verified findings inventory (deduplicated across all three reviews)

Fixed this session: LOD cubic plateau cliff (F-03; host 415 ms→4.1 ms at
n=1200; note the semantics change — plateaus keep only boundary samples),
whole-document LOD fallback (F-06; now per-stroke), zero-length capsule
(F-07), CO5300 boundary enforcement (F-01; reject+count, all current callers
audited parity-safe), unsafe teardown (F-02; cooperative stop, leak on
timeout), pan-time scheduling gaps (F-04; LOD/runway/exact/refinement gated),
incoherent view snapshots in `choose_job`/`map_sample` (F-09 partial),
portability trio (F-20).

Open, deliberately deferred to the production port: hard-refusal pan policy
(F-05 — replaced by overview fallback), display single-owner task (F-08),
coordinator state extraction (F-09), macrogrid permanent global fallback +
shared query scratch (F-10), double-precision camera (F-11; per-sample
cleanup, NOT the bottleneck), large-stroke samples×tiles fallback (F-12),
`present_job` conservatism (F-13), pan holds cache lock through 17 transfers
(F-14), metric endpoint catalog (F-15), `WorldCanvas` bool semantics (F-16),
overlay 322 KB (F-17), bounded-world ingress validation (F-18 — now has its
number: 1472×1792), host-sanitizer gap for coordinator code (F-19).

Diagnosed, no code change needed: generation-36 5.68 s settled event was
viewport-churn (~16.6 repeated supertasks), fixed by the pan_active gate;
NOT an LOD or renderer failure. The multi-second post-zoom convergence the
user observes is the by-design exact sweep (~0.4 s/band; production makes it
invisible, not fast).

## Exact-render scaling (for product intuition)

Visible-viewport exact convergence, current unoptimized canonical renderer at
100%: ~4–5 s at 1,000 visible strokes (measured), roughly linear: ~2–2.5 s at
500, ~0.5 s at 100, ~0.2 s floor. Full-canvas exact of the densest storable
document: ~10–20 s, background-only. Cold start and full-quality export are
the only paths where total-document cost reaches the user; both need explicit
design (persisted overview/tiles; progress UI for export). Canonical has
never had an optimization pass; two-core (~1.7–2×) and analytic interiors
(1.5–3×) are credible but unproven.

## Next steps, in order

1. **Step 0 experiment** (pre-registered above). ~1 day. Decides
   memory-bound vs compute-bound before any renderer rewrite.
2. Depending on result: internal band destination + row spans, or
   microtile/span renderer. Re-run the 12-cycle + mutation hardware suite;
   gate: settled <500 ms.
3. **`PROTOTYPE_EXIT.md`**: questions the prototype answered (with the
   receipts above), mechanisms proven (pinned fallback source, revision-gated
   publication, completion-based endpoints, strip pipelining, publication
   hashing), mechanisms rejected (camera-aligned atlas, interaction clears,
   refusal policy, whole-band repair). Include the PSRAM-layout caveat: the
   benchmark's 76 KB free is a prototype artifact, not a production budget.
4. Consolidate the ~15 top-level findings docs into `docs/chronicle/` with
   one live state doc.
5. **Production build** (fresh review §9 + assessment doc, now sized by the
   4×4/25–400% decision): compact vector log → complete 330 KB overview →
   world-aligned sparse tiles → host-testable coordinator state machine →
   single-owner display task → incremental append updates → settled renderer
   per Step 0's answer. Add zoom levels one at a time with hardware gates.
6. Assembly/PIE only after profiles are compute-bound; first candidate:
   packed RGB565 blend with a scalar bit-exact oracle.

## Tools and workflow notes

- `tools/esp32-capture.py PORT OUT_FILE TIMEOUT_S [--no-reset]` — timestamped
  serial capture; resets the board by default; ends on
  `TINYDRAW_INTERACTIVE_PAN_DONE` / alloc/setup failures (edit END_MARKERS
  for auto runs: `TINYDRAW_AUTO_ZOOM_DONE`). Run inside the IDF env:
  `cd esp32 && eim run "python ../tools/esp32-capture.py /dev/cu.usbmodem1101 /tmp/run.log 300"`.
- Build without flashing:
  `cd esp32 && eim run "idf.py -B '../out/build/esp32-interactive-pan-benchmark' -DTINYDRAW_INTERACTIVE_PAN_BENCHMARK=ON build"`.
- Flash: `./scripts/esp32 interactive-pan-benchmark /dev/cu.usbmodem1101`.
- Validation profile for any core change: `./scripts/dev test`, `dev release`,
  `dev asan`, `dev format-check`, plus both ESP-IDF builds for esp32 changes.
- Review culture that worked this session: verify claims against source and
  logs before accepting them (one of my own attributions — gen-36 = LOD —
  was overturned this way); pre-register performance predictions; keep
  benchmark endpoints physical-completion-based; label diagnostic costs.

## Working rules inherited from the user

- Work autonomously through implement/test/diagnose/fix; commit atomically
  and push sound milestones.
- No further deepening of the 3×3 atlas beyond measurement.
- Slight settled-rendering visual liberties acceptable; painter order, stroke
  presence, eraser semantics, and revision correctness are not negotiable.
- Undo/history redesign comes after rendering behavior settles.
- 4×4 canvas and 25–400% are the working numbers; tweakable, but re-derive
  the budget table if changed.
