# External review synthesis — adoption plan

Date: 2026-08-16
Input: `LATEST_tinydraw-review-report.md` at archived revision `9be7a53`
(external review of snapshot `a560d20`, pre-wave-3)
Compared against: HEAD `cc5ec29` (wave-3 landed),
[`COLD_COMPUTE_CAMPAIGN_RECEIPT.md`](../../../../benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md),
[`REVIEW.md`](../review-findings/2026-08-16-cold-campaign/REVIEW.md),
[`HANDOVER.md`](../review-findings/2026-08-16-cold-campaign/HANDOVER.md),
[`archived V2 roadmap`](../../2026-08-vector-v2-performance/V2_RELEASE_ROADMAP_PRE_RELEASE_2026-08-19.md).

## 1. Context: the review's premise is stale, its structure mostly is not

The external review reasons from the frozen-corpus baseline of
**1,269.157 ms wall / 1,165.354 ms compute**. Wave-3 (`a9e43eb`, `ed23f9d`, `d2f3988`,
`a3e8ff8`) already cut that to **668.980 ms wall / ~582 ms compute**
(three-run max, exactness intact, all sibling gates held or improved).

Gap arithmetic at HEAD (wave-3 receipt):

- Noncompute (present 66.8 + pacing 18.4 + touch 0.9) ≈ **86 ms**, serial.
- Compute must fall **582 → ~413 ms** for the ≤500 ms required gate,
  ~364 ms for the ≤450 ms preferred gate
  ([current ship contract](../../../../SHIP_CONTRACT.md)).
- Ranked serial candidates already queued (H7 op-level chord sweep 40–60,
  strided publish 10–20, aligned word-mask retest 20–40) sum to
  **~70–120 ms** — even the optimistic end leaves the wall at ~549 ms.

**Strategic conclusion:** the queued authority-neutral candidates alone
probably do not close the ≤500 ms gate. Something structural from the
external review's stack — conical authority, compute/present overlap, or
the dual-core pipeline — is likely required. That is the review's central
claim, and it survives the stale numbers.

## 2. Item-by-item disposition

### Already done by wave-3 (independently converged)

| Review item | Wave-3 equivalent |
|---|---|
| §4.7 / §16.7 `PreparedCurveUnit`, prepare once, hold until consumed | Accepted step 4 "prepared curve units" (~110–120 ms at every zoom); `prepare_incremental_curve_unit`, producer consumes whole units |
| §6.3 "no per-pixel/row float division" half | Accepted step 2: hoisted reciprocals, `fast_floor`/`fast_ceil`, rsqrt seed, exact binary zoom scales (disassembly-verified) |
| §4.8 bbox-area work estimate is a poor proxy (partial) | Producer now gates once per unit; "budget by rows, not step areas" is folded into the H7 plan. Full row/mask-word work units still open |
| §16.5 release-build breakage | `a9e43eb` fixed the `-Werror` break — but only via `[[maybe_unused]]`; the review's *runtime validation* ask remains open (see safety patch) |

### Contradicted or corrected by device evidence — adopt only the amended form

| Review item | Device verdict | Amended adoption |
|---|---|---|
| §6.1 32-bit mask words | Wave-3 rejected: +7–13% on device; GCC-Xtensa emitted `callx8` memcpy per load | Retest **only** with `__builtin_assume_aligned` + painter-entry alignment check + disassembly proof of `l32i` before flashing (HANDOVER §4.6) |
| §5.2-ordering: dual-core as Stage 2, before metadata cleanup | Wave-3 got −47.3% with zero concurrency; cheap serial wins exist and are lower risk | Keep internal ordering: serial candidates first, dual-core as the decisive reserve lever |
| §10.4 forward painter order for settled AA | Wave-3's band-unit rejection proved the newest-first saturation shield is exactly what makes replay affordable; forward replay cannot skip covered work | Prefer the internal AA design (8-bit accumulated alpha over newest-first replay, front-to-back compositing). **But adopt §10.3's constraint**: within one operation, overlapping chords/segments must *union* coverage, never composite twice. The host prototype must include a self-overlap fixture |

### Still open, confirmed in current source, adoptable

Cold path (receipts: file:line at HEAD):

1. **§4.9 double-copy publish** — `tile_producer.cpp:629–647` still copies
   supertask→packed, then `publish_tile` copies packed→slot pool
   (`materialized_canvas.cpp:884+`). Same as HANDOVER candidate 2 (est.
   10–20 ms). Both reviews agree. Adopt.
2. **§4.10 visible-missing count** — `visible_tiles_remaining` rescans at
   `tile_producer.cpp:201,234,601`. Adopt maintained count/bitset.
3. **§7.1 raw-slot directory** — `find_tile` linear scan of 448 slots,
   `materialized_canvas.cpp:822–830`, called from ~10 sites. 27,384-byte
   `raw_slot_by_identity` array (PSRAM fine). Adopt.
4. **§7.2 retained-identity bitset** — `std::find` over `retained_keys` at
   `materialized_canvas.cpp:329,683–684`. ~1.7 KiB bitset. Adopt.
5. **§7.3 free-slot list** — `choose_slot` scan at
   `materialized_canvas.cpp:856+`. Adopt free list only; skip LRU heap;
   profile before more.
6. **§4.5/4.6 conical capsule authority** — not present anywhere in the
   tree (`grep conical` → nothing). Tapered rows still probe every
   unfinalized pixel via `covers_pixel`
   (`incremental_rasterizer.cpp:324,645`). Host evidence: ~10.6% combined
   grouped, 4.2–5.4× forward replay, 0.078% pixel delta on tapered-only,
   combined viewport pixel-identical. The single genuinely new *cold* idea
   in the review. Author-gated (authority version bump). Adopt as an A/B —
   see §4 sequencing.
7. **§13 cache-geometry / -O3 / IRAM A/B** — not in any internal queue.
   Cheap bounded experiments once the algorithmic path settles. Adopt as
   tail candidates with internal-heap and export-reserve receipts.

Ink / lift:

8. **§8.1 dishonest budget comment** — `incremental_document.cpp:305–307`
   still says the deadline "covers the complete commit"; in fact only
   offscreen raw retention is deadline-cut. Adopt: rename + per-phase
   counters. This also directly serves the open `mixed_draw` 18.8 ms author
   decision (F5) — phase attribution says whether overview replay or tile
   paint is the term to move.
9. **§8.3/8.4 authority↔materialization revision split + resumable commit
   phases + committed overlay** — matches roadmap Phase 2's open
   "resumable bounded slices" item, but the review supplies the concrete
   design (revision pair, pending range, overlay lifetime, phase state
   machine). Adopt as the Phase 2 execution design.
10. **§9.2 trace corpus is synthetic** — confirmed: all four committed
    CSVs say `SYNTHETIC placeholder`, `fast-curve-dense-25` missing
    (`testdata/ink-traces/`). Both reviews agree: record the recorded touch corpus
    before any smoothing constant is chosen. This is Stage 0 and blocks
    nothing else.
11. **§9.4 arc-length resampling after pressure estimation** — genuinely
    new fidelity idea; no internal equivalent. Adopt into the fidelity
    phase, gated on recorded traces (10).
12. **§9.5 selective 1/2/4 subdivision** — converges with the internal
    approval-gated four-span rematch (wave-3 destroyed the old cost model).
    Adopt the flatness-adaptive form, hard cap 4, brush-aware tolerance.

Safety / hygiene (§11, §16 first patch set) — all confirmed still open:

13. `RibbonRenderer::render_surface` validation is assert-only
    (`ribbon_renderer.cpp:78–83`; Release can underflow `height-1` and
    overrun an undersized surface).
14. `RibbonPrimitiveBatch::push_back` overflows in Release
    (`ribbon_geometry.cpp:152–155`, capacity 8). **Prerequisite for
    adaptive subdivision (12).**
15. `InkStream` lifecycle assert-only (`ink_stream.cpp:45`).
16. `OperationLog::ready` only checks nonempty (`operation_log.cpp:82`);
    add overlap/capacity/alignment rejection like the LOD store's style.
17. CI legs: host release, ASan, NDEBUG+`-Werror`, both firmware builds,
    self-contained headers. HANDOVER §9 independently asks for this; the
    release build *was* broken at HEAD when wave-3 started.
18. **§12 memory-accounting drift** — `memory_layout.h:12–15` disclaimer
    confirmed; `OperationLodStore` is dead code (HANDOVER: delete or
    label). Adopt: one generated report from the real `AppStorage`
    allocation table; delete or explicitly label the LOD store; never
    spend the phantom ~668 KiB.

### Do not adopt (already litigated, both reviews agree)

Whole-viewport replay, exact three-piece taper decomposition, universal
one-span assumption on the *legacy* taper, strict subpixel subdivision,
full-frame SSAA, 512 slots, the 200 KiB chunk-bounds cache, locks around
`MaterializedCanvas`, hand assembly before representation changes, AA
before hard-edge closure (§14 = internal rejected list; no conflict).

## 3. The one real divergence needing an author decision: dual-core

- The external review (§5) proposes a disciplined core-1 raster worker:
  immutable replay snapshot, job/result with epoch+revision+view
  fingerprint+cancellation generation, single result slot, main-core-only
  publication, go/no-go at ≥1.65× effective scaling (stop <1.45×).
- Standing guardrail: "no speculative second-core concurrency"
  (PROJECT_STATE §guardrails, V2_ROADMAP §guardrails). Wave-3's ranked
  list stops at single-core overlap.
- A gated, receipt-driven pipeline with a stop rule is arguably not
  "speculative," but relaxing the guardrail is the author's call, not
  ours. The gap arithmetic (§1) says we likely need one structural lever;
  dual-core is the largest (582/1.65 ≈ 353 ms compute → wall under 450
  even before overlap credit).

## 4. Proposed campaign (sequenced)

**Stage A — safety + instrumentation patch (no perf risk, land now)**
Items 8, 13–16, 18; plus append/publication phase counters (§16.6) and
the `kMinimumScreenRadius` invariant comment (internal F6). Add CI legs
(17) in the same window. Independent of all perf work.

Also in this window: **wire the ink trace harness and record the five
recorded traces** (10). The format library exists
(`vector_v2/include/tinydraw/vector_v2/ink_trace.h`), but firmware
capture and replay-through-`offer()` injection are absent from
`esp32/main/vector_v2/` — the harness spec'd in
`docs/INK_TRACE_HARNESS.md` is not built. It is the ship contract's ink
oracle, it is instrumentation (no hot-path risk), and the recorded corpus
is a hard prerequisite for the Stage C evidence below. Needs an author
recording session, so schedule it early.

**Stage B — authority-neutral cold serial wins (one hypothesis per flash)**
Order by effort/estimate: strided publish (1) → metadata stack (2–5,
measure as one A/B or two) → H7 op-level chord sweep (already queued
first internally; keep it first if the chord-table work lands easily) →
aligned word-mask retest (amended §6.1, disassembly before flash).
Expected: compute 582 → ~470–510 ms. Re-measure the gap.

**Parallel host track while device A/Bs serialize: the smoothness
prototype.** Render flatness-adaptive 1/2/4 subdivision and the conical
envelope on the recorded recorded traces (host only); produce side-by-side
visual evidence plus chord-count/cost deltas. The four-span idea was
rejected on a cost model wave-3 invalidated — it was never evaluated
visually. This prototype is the missing experiment, and its output is a
required input to the Stage C gate.

**Stage C — author gate: bundled authority version bump (this IS the ink
smoothness stage)**
If Stage B leaves the wall >500 ms (likely), take ONE authority change
event to the author combining:
- conical capsule authority (6) — cold speed + taper-dent removal +
  live/cold envelope consistency;
- flatness-adaptive 1/2/4 subdivision (12) — targets the visible
  angularity;
- checked primitive capacity (14) as prerequisite.
One re-baseline pays the reopen cost (cold exactness fixtures vs a slow
mathematical reference per §6.4, SVG parity, frozen-corpus re-freeze with
an explicit old-vs-new bridge measurement, glass acceptance of the
0.08%-class pixel delta) for both changes. Rationale: both reviews
independently want an authority-touching change; doing them separately
pays the re-baseline twice. The author decision is made with both inputs
on the table at once: the residual cold gap (Stage B receipts) and the
smoothness delta (Stage B host prototype on recorded traces).

**Arc-length resampling (11) lands with or immediately after C**, tuned
once against the recorded traces: resampling changes the geometric
support the subdivision selector sees, so tuning them separately means
tuning twice. Scope note: resampling changes how *future* strokes are
sampled, not how committed operations replay — it does not reopen the
frozen cold corpus — but it changes append cost, so `mixed_draw` and the
ink-latency lane get re-measured.

**Stage D — structural reserve (only if C still falls short)**
In order of increasing blast radius:
1. presentation/compute overlap (single core, ~40 ms ceiling, reopens pan
   optical gates per the dependency matrix);
2. dual-core group pipeline per §5's design with the internal go/no-go
   gates — requires the explicit guardrail decision (§3 above).

**Stage E — lift/latency closure (parallel to B/C, different files)**
Lift-phase instrumentation (8) → resumable commit phases + revision
split + committed overlay (9). This is the *scheduling* half of felt ink
quality (the end-of-stroke hitch: ~87–111 ms post-lift poll gap),
independent of the geometry half that lives in B/C. Resolves the
`mixed_draw` author decision with data instead of a budget edit.

**Stage F — settled AA (unchanged phase, amended design)**
Internal 8-bit-alpha newest-first design, host prototype on one group
first, plus the external review's operation-self-overlap union constraint
and RGB565 blend-model freeze (§10.5). Stays out of the cold gate.

## 5. Open author decisions (consolidated)

1. `mixed_draw` 50% append: raise budget vs optimize overview-replay-per-
   chunk (pending Stage A/E phase data + planned glass check).
2. Authority version bump: conical capsule + adaptive subdivision bundle
   (Stage C) — reopens cold exactness, SVG parity, frozen corpus.
3. Dual-core guardrail: relax "no second-core concurrency" for a gated
   job/result pipeline, or keep single-core overlap as the ceiling.
4. Settled AA prototype go (author already said "need to see it in
   action" — host prototype is the next step, no device cost).
