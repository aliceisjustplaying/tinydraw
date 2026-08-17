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
still pending. Pacing is provisionally GREEN: PANSEQ p95 33.939 ms at 100%
and 33.934 ms at 400% after the chrome-lifetime split
(`benchmark-results/wave2-compositor/`). Hardware ceiling is 29.4 FPS
full-frame (`HARDWARE_LIMITS.md`). Pan-tool drags that start on canvas overlays
must enter pan after an 8 px intent threshold; stationary overlay taps retain
their control action (owner-reported zoom-rail swallowing defect, fixed and
gated 2026-08-17).

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
The final 20-run closure uses normal product firmware and includes real journal
activity, not only autosave service initialization in the gate harness.

Current gate results recorded across the 2026-08-17 cleanup and overlap receipts:
50% **421.787 ms**, 100% **399.498 ms**, 200% **464.071 ms**,
and 400% **515.123 ms** under its 520 ms development guard. The separate
stacked-overlap 50% gate is **476.969 ms** under the 500 ms product line. See
`benchmark-results/overlap-cold-fix-2026-08-17/RECEIPT.md`. The 400% result is
inside the temporary 520 ms development guard but above the
≤500 ms release requirement. The gate runs before the ordinary product loop and
does not measure concurrent journal writes. The 20-run reset-separated normal-
product 400% closure statistic remains open. The original
1,269.157 ms baseline
(`benchmark-results/wave2-compositor/COLD_GENERAL_BASELINE_RECEIPT.md`) and
the older 663.829 ms straight-authority receipt are historical and no longer
describe the product renderer or frozen corpus.

The first pure-revisit measurement is 1.000 amplification with zero unexplained
renders. Residual glass strays still require cause attribution in the gate build;
`TINYDRAW_LIVE_LEDGER` is not compiled into ordinary product firmware.

### 4. Anti-aliasing — settled refinement  (appearance accepted; progression open)

Live strokes remain crisp/hard-edged while the finger is down; edges
anti-alias during idle settling shortly after lift/settle. Brute-force
supersampling is forbidden by measured physics (808 ms/frame probe).

Constraint binding it to §3: settling must never cause visible
cold-to-sharp cycling on revisit — settled output is cached content and
falls under the déjà vu gate like any other rendered pixels.

The analytic boundary-coverage implementation is functionally accepted on
glass. Earlier tiled measurements were 1.7–5.4 ms mean / 9.3 ms maximum, but
they are not a universal current bound: the current 25% gate settled 42 tiles in
152.945 ms and recorded a 76.416 ms maximum tile, breaking the nominal 8 ms
cooperative slice. Settled progression therefore remains an open measured
performance gate even though AA correctness and appearance are accepted.

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
- Mechanism: exactly one filled outline path per physical finger-down/up
  Stroke, even when bounded storage splits it into internal chunks; geometry
  comes from the existing ribbon and is visually identical to glass. The SVG
  has no synthetic background rectangle. File size is unconstrained for v1.
- Delivered over the existing USB export flow; read-only authority snapshot;
  no dependence on tile caches or presentation state.

### 7. Autosave & data safety  (required)

- By owner decision on 2026-08-17, authority (operations + samples +
  active/retained prefixes + generation and epoch) persists; session UI state
  and derived caches never persist. The next Stroke identity is derived from
  restored active authority rather than stored separately.
- Power loss loses at most the in-progress gesture plus ≤5 s of committed
  work. (PROPOSED default — owner has not reviewed this number.)
- Recovery is exact and verified by interrupted-write fixtures.
- Autosave must be **enabled during all ink/pan gate measurements**, and
  committed-Stroke workloads must exercise real queued journal writes. Service
  initialization without product-loop writes is not final product evidence.

### 8. Platform features  (required)

- Power off/on flow: required (V1 parity).
- Onboard clock + one-shot NTP: required (export timestamps). V2's on-demand
  Document → Clock flow and post-teardown feedback landed 2026-08-17. The
  owner accepted unavailable-network handling, successful RTC sync, centered
  terminal feedback, and text size on glass
  (`benchmark-results/v2-ntp-sync-2026-08-17/RECEIPT.md`).
- Minimap: tap-to-jump and viewport-drag navigation required (owner elevated
  drag from post-ship on 2026-08-17). Closed with absolute Down/Move mapping,
  captured/clamped drag across the full right dock, 2 px upward and 8 px
  horizontal/downward promotion, host geometry tests, an exact physical capture,
  and owner glass acceptance
  (`benchmark-results/minimap-absolute-pointer-2026-08-17/RECEIPT.md`).
- Zoom: 25/50/100/200/400%. By owner decision on 2026-08-17, transitions keep
  one world focus centered, derive and clamp the new origin, and retain no
  dormant per-zoom origins. The complete cycle preserves focus within four
  quarter-world units. The older exact-origin receipt describes the superseded
  pre-cleanup model. 800% is out of contract.

## Document authority policy

Owner decision, 2026-08-16: V2 documents are **blank baseline plus ordered
vector operations**. The operation sequence and active prefix are the complete
durable drawing authority; raster overviews and tiles are rebuilt derivatives.

Raster V1 documents remain explicitly Raster V1 and accessible through the V1
build. They are never silently reinterpreted as V2 vector authority. A future
flattened V2 import must declare an explicit raster baseline and either embed it
in SVG or refuse vector-only SVG export. The V1 raster snapshot restore seam is
not a valid V2 persistence or Undo mechanism.

## Owner decisions — 2026-08-16 (post Cold Stage B glass session)

1. **Mixed-draw append lag is a defect, not a budget problem.** The felt
   400% drawing lag is unacceptable; the 15 ms per-append harness budget
   stands. The fix is the committed-overlay / authority-revision split
   (external review §8.3–8.4,
   `EXTERNAL_REVIEW_SYNTHESIS_2026-08-16.md` item 9), adopted as the
   Phase 2 execution design. Diagnosis first: the mid-stroke fallback
   observability pass supplies phase attribution before the design lands.
2. **Cold 400% interim ceiling.** The 507.0 ms three-run development
   maximum (frozen corpus wall, Stage B receipt) is accepted until
   autosave exists; further regression is not. The firmware gate holds the
   line at 520 ms (`kColdViewport400HoldTheLineUs`). Calibration history:
   first set to 510 ms from the ≤1.5 ms within-build spread, then
   recalibrated same-day after four builds measured walls of
   499.95/507.98/508.98/512.27 ms — between-build flash-icache layout
   variance (Stage B receipt, ±2–3%/build; producer cold loops are not
   IRAM-pinned), including +6 ms between builds with no cold-path diff.
   520 = observed max + ~2.5% of compute, so the guard catches real
   ≥~10 ms regressions instead of coin-flipping on layout luck. Producer
   IRAM-pinning landed 2026-08-17 after two unrelated-layout runs crossed the
   hold line at 524.243/526.063 ms; the treated run was 496.693 ms with 290,860
   bytes of free internal memory
   (`benchmark-results/minimap-navigation-2026-08-17/RECEIPT.md`). The
   ≤500 ms requirement in §3 is unchanged and governs the final
   normal-product 20-run closure with real journal writes. The remaining micro-candidates
   (block-granular saturation, PIE fixed-point probing,
   presentation/compute overlap) are parked.
3. **Stage C authority bundle declined.** Conical capsules + adaptive
   subdivision had only a speed justification left after the angularity
   tool falsified the smoothness case on recorded owner input
   (`benchmark-results/ink-angularity-baseline/BASELINE.md`). Smoothness
   work goes to arc-length resampling (external review §9.4) plus settled
   AA instead; committed authority geometry stays frozen.
4. **Settled-AA prototype approved.** The boundary-only analytic-coverage
   design (8-bit alpha over the newest-first masked replay, interiors keep
   exact span fills) proceeds to a host prototype with rendered
   before/afters. On-device go/no-go after owner review of the prototype.
5. **Overlap-50 cold red is binding and sequenced (2026-08-16).** The
   overlap-workload 50% cold gate (628 ms vs 500; red since wave-3,
   surfaced from no-scorecard invisibility the same day) gets fixed — not
   re-scoped — but strictly after: the ink lag fix (committed overlay),
   the settled-AA prototype review, and the déjà-vu fix. Owner was
   explicit that its invisibility through multiple paid cold-render
   sessions is unacceptable; process rule 8 below is the anti-recurrence
   guard. **Closed 2026-08-17:** per-chord finalized-window refresh reduced the
   full-battery result to 476.969 ms and every verdict flag passed.

## Owner decisions — 2026-08-17 cleanup closure

1. **Authority-only persistence.** Durable V2 state is the painter-ordered
   operation/sample authority, active/retained boundary, generation, and epoch.
   Navigation and chrome restart from defaults; next Stroke identity derives
   from restored active authority. The earlier session-state receipt remains
   historical evidence, not the current persistence contract.
2. **Focus-centered zoom.** Navigation stores the current zoom/origin and one
   world focus. Every transition derives its target origin from that focus;
   dormant per-zoom origin arrays and exact-origin restoration are removed.
3. **Functional AA accepted; progression still measured.** Appearance and
   settled-cache correctness are accepted. The current 25% 76.416 ms tile tail
   keeps progression performance open.

### Post-ship (explicitly deferred, not cut)

- Demo record/replay (owner: "rather nice — after shipping").
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
8. Every red flag in the harness verdict vector must have a scorecard row
   in `PROJECT_STATE.md` naming its number, its threshold, and its owner
   decision (fix / hold-the-line / re-scope). "Pre-existing reds only" is
   never an acceptable session summary on its own; an undocumented red is
   a session-stopping finding. (Added 2026-08-16 after the overlap-50 cold
   red rode invisibly through the entire cold campaign.)
