# TinyDraw V2 Ship Contract

Frozen: 2026-08-15, by product owner decision. Changes require an explicit
owner decision recorded here with a date; nothing below drifts silently.

Stable product priorities live in [`PRODUCT_TENETS.md`](PRODUCT_TENETS.md).
This contract owns numeric gates and resolves any conflict.

The governing rule: **a requirement is closed only when it has a permanent
oracle, a guard band, and a known-good tagged revision. No new change may
reopen a closed requirement. Stretch targets never justify regressing a
required gate.** Software self-reports are never sufficient for glass-visible
requirements (lesson of `tear_synchronized`, 2026-08-15).

## Ship requirements

### 1. Pan — tear-free at ≥24 FPS  (required)

| Gate | Threshold | Guard band | Oracle |
|---|---|---|---|
| Correctness | Zero tears, notches, stale bands, seams on glass | zero | Optical: probe cells + torn positive control; manual glass |
| Pacing | PANSEQ frame p95 ≤41.7 ms (24 FPS) | p95 ≤38 ms | Gate harness PANSEQ receipts |
| Stretch | ~29.8 FPS (2 TE periods/frame) | — | same |

Status: correctness provisionally GREEN on the final invariant build (owner
glass check, 2026-08-16); the same-session torn positive-control closure is
still pending. Pacing is RED at 50.934 ms p95. Hardware ceiling is 29.4 FPS
full-frame (`HARDWARE_LIMITS.md`).

### 2. Ink — no perceptible lag  (required)

| Gate | Threshold | Guard band | Oracle |
|---|---|---|---|
| Optical latency | finger-to-glass p95 ≤45 ms, p99 ≤60 ms | p95 ≤35 ms, p99 ≤50 ms | Ink trace harness (`docs/INK_TRACE_HARNESS.md`) + optical burst spot-checks |
| Feel tiebreaker | Indistinguishable from Raster V1 in side-by-side scribble | — | Human, qualitative (V1 optical baseline was never measured) |
| Fidelity | No lost Down/Up; bounded consumed-sample time/space gaps; final path exact vs authority | zero loss | Trace harness counters + exactness tests |

### 3. Cold rendering & the déjà vu problem  (required)

**Priority order per owner: the déjà vu (re-rendering already-rendered
content) is the primary pre-ship complaint; the cold wait is second.**

| Gate | Threshold | Guard band | Oracle |
|---|---|---|---|
| Revisit retention ("déjà vu") | A view rendered this session re-displays sharp without re-render when revisited, within cache capacity; render amplification ≤1.25 renders per unique required tile-revision | ≤1.15 | Repair/producer work counters (durable keys) |
| Cold wait | Current viewport exact ≤500 ms worst accepted case at 400% | ≤450 ms | Deterministic adversarial corpora, on-device receipts |
| Cold stretch | ~300 ms | — | same |

"Cold complete" means the **current visible viewport** is exact. Halo,
remembered zooms, and background sweeps are quality tiers, not gates.

The closure statistic is the maximum wall time across 20 reset-separated
device runs of the frozen `adversarial_tapered_4x+evil_hairlines` corpus at
400%, origin `(0,0)`, from discarded detail tiles through final exact viewport
publication and DMA completion. The combined authority contains 910 operations
and 12,157 samples. An accepted run has no crash, allocation failure, authority
mismatch, touch-service violation, telemetry overflow, or presentation failure.
Development runs before autosave exists are provisional; the final 20-run
closure has autosave and normal product services enabled.

Current three-run development baseline: **1,269.157 ms maximum wall**
(1,165.354 ms compute, 70.182 ms presentation, 31.526 ms pacing, 2.095 ms touch
service). See
`benchmark-results/wave2-compositor/COLD_GENERAL_BASELINE_RECEIPT.md`. The older
663.829 ms straight-authority receipt is historical and no longer describes the
product renderer or frozen corpus.

Amplification threshold (1.25) is provisional until first measured; owner
review after the first real measurement.

### 4. Anti-aliasing — settled refinement  (required, pending first look)

Live strokes remain crisp/hard-edged while the finger is down; edges
anti-alias during idle settling shortly after lift/settle. Brute-force
supersampling is forbidden by measured physics (808 ms/frame probe).

Constraint binding it to §3: settling must never cause visible
cold-to-sharp cycling on revisit — settled output is cached content and
falls under the déjà vu gate like any other rendered pixels.

Owner acceptance: explicitly provisional until seen on glass ("probably
fine, need to see it in action" — 2026-08-15).

### 5. Undo / Redo  (required)

- Whole-gesture granularity. Redo required.
- Depth: ≥10 guaranteed; opportunistically unlimited within document
  capacity (active-prefix cursor makes depth ~free — no performance trade).
- Oracle: exactness fixtures — undo/redo N steps reproduces pixel- and
  authority-exact states; invalidation scope is bounded (undoing a local
  stroke does not force unrelated re-renders — ties into §3 amplification).

### 6. SVG export  (required)

- **Exact variable-width Perfect-Freehand fidelity. Centerline/uniform-width
  export is forbidden** (owner: "absolutely no").
- Mechanism: per-stroke filled outline paths from the existing ribbon
  geometry; visually identical to glass. File size is unconstrained for v1.
- Delivered over the existing USB export flow; read-only authority snapshot;
  no dependence on tile caches or presentation state.

### 7. Autosave & data safety  (required)

- Authority (operations + samples + revision + view/tool state) persists;
  derived caches are never persisted.
- Power loss loses at most the in-progress gesture plus ≤5 s of committed
  work. (PROPOSED default — owner has not reviewed this number.)
- Recovery is exact and verified by interrupted-write fixtures.
- Autosave must be **enabled during all ink/pan gate measurements** — a
  benchmark with autosave off is not a product benchmark.

### 8. Platform features  (required)

- Power off/on flow: required (V1 parity).
- Onboard clock + one-shot NTP: required (export timestamps).
- Minimap: tap-to-jump required; viewport-drag out of contract.
- Zoom: 25/50/100/200/400%. 800% out of contract.

## Document authority policy

Owner decision, 2026-08-16: V2 documents are **blank baseline plus ordered
vector operations**. The operation sequence and active prefix are the complete
durable drawing authority; raster overviews and tiles are rebuilt derivatives.

Raster V1 documents remain explicitly Raster V1 and accessible through the V1
build. They are never silently reinterpreted as V2 vector authority. A future
flattened import must declare an explicit raster baseline and either embed it in
SVG or refuse vector-only SVG export. The existing raster-only snapshot restore
seam is not a valid V2 persistence or Undo mechanism.

### Post-ship (explicitly deferred, not cut)

- Demo record/replay (owner: "rather nice — after shipping").
- Minimap drag navigation.
- Semantic/editable SVG stroke export as alternate format.
- 800% zoom.

## Out of scope

RP2350 port parity. Settled-AA quality beyond "no visible artifacts and no
déjà vu violations."

## Process rules (earned 2026-08-15, binding)

1. Measure the substrate before optimizing on it.
2. Every optical CLEAN requires a torn positive control in-session.
3. Cheapest sufficient instrument first; automation for receipts after.
4. One variable per experiment; interpretation pre-registered.
5. Attribute before optimizing; decompositions are work orders.
6. Full scorecard before/after every meaningful change; a change that wins
   its target while pushing a closed metric out of guard band is rejected.
7. Revert first, investigate second, when a closed metric regresses.
