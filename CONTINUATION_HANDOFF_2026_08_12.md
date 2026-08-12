# TinyDraw vector prototype continuation handoff — 2026-08-12

## Purpose

This is the durable handoff from the predecessor Pi session
`2026-08-12T13-06-51-674Z_019ff615-309a-76be-9212-f623c8f3e062.jsonl`.
It records what is committed, what was measured, the current regression, and
the exact continuation order. It is not a replacement for:

- `review_findings_2026_08_12_noon/RESPONSE.md`
- `SECOND_REVIEW_ARCHITECTURE_ASSESSMENT.md`
- `second_review_hardware_ab/RESULTS.md`

## Branch and repository state

Branch: `prototype/vector-materialized-cache`

The branch and its upstream were both at `18ee81a` when this handoff was
written. All commits through `18ee81a` are pushed.

Recent commits:

- `7e31714` — external review and second assessment
- `22cdb14` — renderer-capacity and coordinator correctness fixes
- `63d99b9` — reviewed-patch hardware A/B harness and results
- `1469b8a` — center-out strip zoom and physical completion endpoints
- `feecff6` — strip-pipelined hardware results
- `79ded72` — deterministic realistic handwriting workload
- `40f4615` — realistic-workload hardware results
- `48ac59b` — capsule-based settled renderer
- `18ee81a` — visible settled pass before runway and exact work

Uncommitted intentional files at handoff time:

- `esp32/main/interactive_pan_benchmark.cpp`: two temporary diagnostic prints,
  `TINYDRAW_RUNWAY` and `TINYDRAW_ZOOM_REFUSED`.
- `second_review_hardware_ab/settled-auto-zoom.log`: first failing settled run.
- `second_review_hardware_ab/settled-auto-zoom-diagnostic.log`: reproduced run
  with the two diagnostics above.
- `.pi/`: local subagent artifacts; do not commit this directory.

## What the predecessor completed

### 1. Reviewed correctness/performance patches

The renderer no longer consumes primitive capacity for off-region primitives,
coverage scratch clearing is reused more efficiently, and fully opaque RGB565
composites use direct assignment. Coordinator fixes include a larger timing
buffer, corrected source halo/revisions, failure resumption, and removal of the
interaction-time full inactive-atlas clear.

### 2. Honest, strip-pipelined zoom measurements

The LCD driver now tracks monotonically increasing submission/completion
sequences. Completion times are recorded by the transfer ISR. Zoom fallback is
nearest-resampled and pushed in center-out 22-row strips while it is generated.

On the old synthetic workload:

- first strip physically complete: typically 6.3–8.1 ms;
- worst first strip: 56.2 ms during a 49 ms cancellation collision;
- complete visible region physically complete: typically 38.9–49.5 ms;
- 12/12 transitions succeeded.

### 3. More realistic 1,000-stroke workload

`populate_realistic_handwriting()` creates deterministic handwriting-like
strokes with a sample-count distribution intended to model the measured touch
cadence: mostly 6–35 samples, some 36–80 sample strokes, and rare 120–200 sample
strokes. Seed 7 generated 1,000 strokes and 20,153 samples. The benchmark sample
arena was enlarged to 24,576.

On that workload, before integrating the settled pass:

- initial exact atlas: 5.36 s;
- first strip physically complete: 6.6–16.2 ms;
- complete visible region physically complete: 39–57 ms;
- 12/12 transitions succeeded.

These results strongly support the claim that prompt approximate zoom is viable.

### 4. Capsule settled renderer

`settled_render_region()` renders each stroke as variable-radius capsules with
analytic one-pixel edge coverage. It preserves painter order and current eraser
semantics but deliberately allows slight visual differences from the canonical
ribbon renderer. Host tests cover approximate canonical placement, painter
order, erasing, region isolation, candidates, cancellation, and validation.

The benchmark schedules visible settled bands before offscreen runway and
canonical exact work.

## Current hardware regression

The settled pass itself completes with plausible, but currently too-slow,
visible times on the realistic workload:

- 50%: about 713 ms;
- 100%: about 1.17 s;
- 200%: about 959 ms.

The first three automated transitions succeed. Cycles 3–11 are then refused.
The reproduced diagnostic run is in
`second_review_hardware_ab/settled-auto-zoom-diagnostic.log`.

Important lines:

- generation 4 finishes runway with `invalid=0`;
- generation 6 is canceled while its runway pass still has `invalid=53`;
- generation 8 is also canceled with `invalid=53`;
- the next 200%→100% request is refused at job 60;
- `proven=1`, meaning world-coverage provenance passes and the refusal is due to
  missing/noncurrent source raster readiness, not the source-world boundary.

The refusal is therefore not evidence that the settled renderer is a dead end.
It is a coordinator policy regression: the new ordering lets visible settled
work consume much of the 500 ms driver interval, runway cannot materialize all
source bands before the next request, and `set_zoom()` still requires every
source band needed by the next visible fallback to be materialized. Once one
request is refused, the short 500 ms retry cadence can keep canceling recovery
work and produce a refusal loop.

A key caveat: the current 3×3 camera-aligned atlas is a disposable mechanism
prototype. Production is still intended to use a complete low-resolution
overview plus sparse world-aligned active-zoom tiles, where an overview-derived
fallback replaces refusal.

## Validation performed during recovery

At `18ee81a` plus the two diagnostic prints:

- `./scripts/dev test`: 22/22 passed;
- `./scripts/dev asan`: 4/4 passed;
- ESP-IDF interactive benchmark build: passed;
- physical diagnostic reproduction: passed as a reproduction of the refusal;
- branch/upstream delta before this document: 0/0.

The device reported about 563 KB free PSRAM after benchmark/vector allocations.
The settled band scratch is only about 11.8 KB; it is not the source of the
readiness regression.

## Next steps, in order

### A. Fix the coordinator regression before optimizing the renderer

Do not weaken validity checks and do not publish invalid raster.

For this disposable benchmark, make zoom progress independently of offscreen
runway completion. Preferred experiment:

1. Preserve a complete, known-valid fallback source instead of overwriting the
   only complete source with a partially materialized successor, or add a
   dedicated complete low-resolution fallback/overview for the benchmark.
2. Keep newly settled/exact active bands publishable, but do not promote a
   partial atlas into the sole source needed for the next transition.
3. Add an explicit state transition for partial versus complete fallback source;
   readiness and document revision must be captured atomically.
4. Retain conservative refusal only when neither the complete fallback nor a
   vector-proven blank can cover the requested visible region.
5. Add host-testable coordinator/state-machine coverage if the logic can be
   extracted; otherwise retain hardware diagnostics until the 12-cycle run is
   stable.

A smaller tactical alternative is to prioritize next-transition source runway
before settled work, but that would hide the issue and compromises the desired
visible-settled priority. The complete fallback source is the better mechanism
experiment and aligns with the production overview architecture.

### B. Re-run the automated hardware cycle

Acceptance:

- 12/12 50↔100↔200 transitions accepted;
- no invalid publication/checkerboard;
- first physical valid strip remains below 100 ms;
- complete visible fallback remains below 150–180 ms;
- settled quality appears for every transition;
- no persistent refusal loop when a generation is canceled.

Capture a new log and update `second_review_hardware_ab/RESULTS.md`.

### C. Optimize settled latency

The current 0.7–1.2 s result misses the under-500-ms settled goal. Profile before
assembly. Likely high-value changes:

1. Remove per-pixel `sqrt` from partial capsule edges using squared-distance or
   a lookup/approximation.
2. Avoid clearing/compositing the union of a long stroke's entire dirty
   rectangle when actual touched pixels are sparse; use row spans or microtile
   dirty bounds.
3. Generate sample-reduced zoom-specific centerline LOD at append time.
4. Replace software-emulated `double` camera transforms with bounded fixed-point
   or local `float` transforms.
5. Render geometry in larger supertasks, then bin/publish smaller bands/tiles to
   avoid repeating sample projection and candidate setup per 32-row band.
6. Re-enable a touch-safe two-core settled executor only after measuring the
   single-core kernels.

The goal is visible settled below 500 ms on the realistic 1,000-stroke document.
Slight edge/curve differences are allowed; painter order, stroke presence,
eraser semantics, and revision correctness are not.

### D. Run the requested Sol review loop

The user explicitly requested repeated `gpt-5.6-sol` high-reasoning code review,
without limiting the number of findings, until only nits remain. Pi subagents
could not authenticate the OpenAI provider in the predecessor session, but
Codex CLI worked:

```sh
codex exec -m gpt-5.6-sol \
  -c model_reasoning_effort='"high"' \
  --sandbox read-only \
  '<review prompt>'
```

Review after the refusal fix and first settled optimization, not before. Ask for
correctness, races, generation/revision provenance, publication validity,
resource lifetime, benchmark honesty, and performance issues. Fix all material
findings, validate, commit/push atomically, and repeat until only nits remain.

### E. Then transition to the production architecture

Once the mechanism benchmarks pass, stop deepening the 3×3 atlas. Build:

- compact vector operation and LOD storage;
- a complete bounded-world low-resolution overview;
- a sparse world-aligned tile ring for active zoom levels;
- incremental append updates to resident overview/tiles;
- checkpointed replay for Undo/history later;
- a dedicated display scheduler with slot/version pinning and completion-based
  metrics.

## Working rules inherited from the user

- Work as autonomously as possible; do not pause between ordinary implementation,
  testing, diagnosis, and fixes.
- Commit atomically and often, and push each sound milestone.
- Spend whatever engineering time is useful; do not reject the architecture
  because it takes longer.
- Slight settled-rendering liberties are acceptable for meaningful performance;
  eventual exact convergence remains required.
- Undo/history redesign can come later.
