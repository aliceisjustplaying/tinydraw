# Ink trace harness — high-level spec

Purpose: make ink latency and fidelity measurable through the **real**
interaction path, so the visual-first ink work (Wave 2) is graded by the
quantity users feel, not by component self-reports. This closes the gap the
2026-08-15 review flagged as P1-3: the existing drawing gate bypasses the
touch sampler, coalescing, app sequencing, presentation, and lift drains.

Scope discipline: this is a measurement harness, not a platform. Ship-contract
gates it serves: §2 (ink latency + fidelity). Anything not needed for those
gates is out of scope.

## 1. Trace format

A recorded, replayable sequence of timestamped touch events:

```
TraceHeader { magic, version, name, source (recorded|synthetic), sample_rate_note }
TraceEvent  { t_us (monotonic, from trace start), kind (Down|Move|Up), x, y }
```

- Binary or CSV; small enough to commit under `testdata/ink-traces/`.
- Canonical traces (committed once, then frozen):
  1. `fast-curve-dense-25` — fast curved stroke on the dense seed-7 document
     at 25% (the worst measured coordinator interaction).
  2. `fast-curve-400` — same gesture at 400%.
  3. `slow-precise-100` — slow deliberate line, catches quantization/stickiness.
  4. `scribble-multistroke` — five rapid strokes with short gaps, catches
     lift-drain backlog and stroke-boundary loss.
  5. `under-overlay` — stroke crossing zoom rail, minimap, toolbar regions.

Capture mode: a firmware flag records real finger input (event timestamps from
the touch sampler) to serial; a host script converts to trace format. One
session of the owner's real scribbling becomes the canonical corpus.

## 2. Replay mode

A gate-harness mode injects trace events into the app at the **touch event
buffer boundary** (not into the document engine), honoring original relative
timestamps. Everything downstream — coalescing, sampler, coordinator ordering,
ribbon, presenter, panel — is the production path.

Determinism requirement: same trace + same build + same document ⇒ same event
consumption sequence. (Panel timing may vary; consumption must not.)

## 3. Measured chain

For every consumed sample, one record with the original event timestamp
carried end-to-end (never loop time — review P1-2):

```
t_event → t_consumed → t_geometry_ready → t_first_submit → t_dma_complete
```

Plus per-stroke counters:

- max consumed-sample time gap (µs) and space gap (px)
- coalesced/received ratio (path-thinning visibility — review P1-1)
- events received vs consumed; Down/Up must be 1:1 with trace
- final committed path vs replayed input: exactness check against authority

Serial output: one `TINYDRAW_INKTRACE` line per stroke with p50/p95/max of
each stage delta, plus a final summary line with the gate verdicts.

## 4. Gates (from SHIP_CONTRACT §2)

- `t_event → t_dma_complete` p95 ≤35 ms guard band (software proxy for the
  45 ms optical gate; DMA-complete precedes optical visibility by ≤1 panel
  period, so software p95 ≤35 ms implies optical p95 ≤~52 ms worst-case —
  the optical spot-check closes the remaining gap).
- Zero lost Down/Up across the corpus.
- Coalescing ratio and max gaps reported; regression vs baseline fails.
- Fidelity: replayed final paths exact vs authority fixtures.

## 5. Optical spot-check (no CV project)

The eyeball-burst method from Block B: film one replayed trace at 240 fps
(or the owner scribbles live), extract consecutive-frame bursts, measure
finger/stylus-to-tail distance in frames. Performed at Wave 2 ink closure and
at ship closure; not per-commit. A torn/laggy positive control (old build)
validates the method once.

## 6. Non-goals

- No camera automation, no per-commit optical runs.
- No scheduler/telemetry framework; fixed structs, serial receipts.
- No synthetic-only grading: at least one canonical trace must come from
  recorded real finger input.

## 7. Implementation order (for the ink worker)

1. Trace format + replay injection at the event-buffer boundary.
2. Timestamp plumbing (t_event end-to-end) + per-stroke receipt line.
3. Record canonical corpus from owner finger session (5 traces).
4. Baseline receipts on current build (pre-visual-first) — this is the
   "before" scorecard.
5. Visual-first reorder + provisional tail (the actual Wave 2 ink work).
6. After receipts + optical spot-check + side-by-side vs Raster V1.
