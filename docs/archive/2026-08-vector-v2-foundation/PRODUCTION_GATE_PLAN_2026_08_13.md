# Production gate plan — vector canvas migration

Date: 2026-08-13
Branch: `feat/vector-canvas-production`
Status: **committed to Gate 1 only.** No persistence, no Undo, no AA renderer,
no new exclusive apps until Gate 1 reports.

This document synthesizes four independent project reviews (2026-08-13) into
one decision plan. It supersedes the roadmap ordering in
`PRODUCTION_CONTINUATION_HANDOFF_2026_08_12_NIGHT.md` where they conflict; it
does not supersede its correctness contracts, guardrails, or gates.

## Product requirements (user-confirmed 2026-08-13)

1. **Crisp zoom at every level, 25–400%, for detail work on either side of the
   zoom.** Draw at any level; ink stays crisp at every other level. This is
   the product. Raster downscale-as-zoom is rejected as an alternative.
2. **Anti-aliased committed ink is mandatory.** Floor: ≥4-sample-per-pixel
   equivalent edge quality (2× per axis supersample or better). Preferred:
   analytic coverage. **Hard-edged committed ink is not a shippable end
   state.** The old "ship hard-edged if AA fails" retreat is deleted; failure
   branches are: drop zoom levels, cut scope, or stop.
3. 800% stays out of the initial release.
4. Parity target (interaction): draw, erase, colors, all brush sizes, pan,
   committed zooms, ten-step Undo, new drawing, autosave/load, PNG export,
   battery/power, existing toolbar behavior. Demo/recording behavior deferred.

## Where the project stands (verified against receipts)

- **Shipping raster product: intact.** Default firmware is the 3×3 raster app
  with full feature set. It is the safety net; nothing has replaced it.
- **Prototype: finished and rejected.** Proved feasibility (valid-cache pan
  ~26 ms; first fallback strip 7–10 ms; full fallback 40–50 ms), killed the
  camera-aligned atlas, and showed the analytic settled renderer missing the
  500 ms gate (490–828 ms). See `PROTOTYPE_EXIT.md`.
- **Production island: real modules, incomplete experience.** OperationLog,
  MaterializedCanvas, DisplayScheduler, incremental append, replay ranges,
  LOD lifetime experiment, and an opt-in live app
  (`TINYDRAW_PRODUCTION_LIVE_APP`).
- **The missing piece is a high-resolution tile producer.**
  `append_incrementally` only updates tiles that are already resident, and
  nothing in the live path ever publishes one (`publish_tile` is called only
  by the test walk). All zoomed views are nearest-neighbor blowups of the 25%
  overview. The pixelation is not a quality bug; the producer does not exist.
- **Pan at 25% is a geometric no-op** (368×448 level exactly equals the
  screen; `clamp_view_origin` clamps to 0,0). Do not debug pan at 25%.
  Reproduce any pan problem at 100%/400% first.
- **Known corrections to keep straight:**
  - The 4.98 MiB PSRAM figure is the empty-heap probe
    (`756e080-memory-layout.log`), not the live image. The live image has no
    checked-in memory receipt.
  - Live overlay is ribbon-rendered; committed ink is hard capsules. Users see
    a quality cliff on lift today.
  - Toolbar Undo/Export are no-ops in the live app.
  - `PROJECT_STATE.md` predates the live slice (`5dfd793`) and is stale.
  - Brush radii are screen-space and inverse-scaled to world units: a 5 px
    brush drawn at 400% is 1.25 world units → **0.31 px displayed at 25%**.
    25% is the hardest *presence* problem; 200–400% is the hardest
    "still looks like TinyDraw" problem; per-level compute cost is unmeasured
    (prototype receipts were non-monotonic: 100% slower than 200%).

## Locked decisions

| # | Decision | Resolution |
|---|---|---|
| 1 | Pixelated fallback | Acceptable only as a briefly visible transient, progressively replaced. Not a product state. |
| 2 | First renderer | Hard-edged ordered tile producer first — as a risk-closer only, never the end state. |
| 3 | Kill criteria | The existing gates: first valid feedback <100 ms; full fallback <180 ms; valid-cache pan p95 ≤35 ms, zero ordinary refusal; visible settled refinement <500 ms on a realistic 1,000-stroke document; no missing strokes, wrong painter order, or stale revisions. |
| 4 | Zoom levels | 25/50/100/200/400. Drop an individual level only if it individually fails. 800% out. |
| 5 | LOD storage | **Never four independent copies** (rejected by real-touch projection: 226–254K points vs 90K slab, `production/REAL_TOUCH_CHARACTERIZATION.md`). Gate 1 runs on raw source samples; the receipt must say so, so a pass is not secretly conditional on a simplifier. |
| 6 | Undo | Ten-step user contract preserved. Operation history + materialization checkpoints. No synchronous full-document replay. |
| 7 | Parity | Split: *interaction parity* (product engineering, Gate 3) vs *visual parity* (renderer risk, AA gate). Requirement 2 above makes visual parity mandatory. |
| 8 | Provisional quality tier | Hard-edged publications (producer and incremental append path) are labeled `kImmediate`, below `kSettled`. `kSettled` is reserved for AA output. Temporary Gate 1 pixels must never become accepted state. |

**Open decision (due at the AA gate, named now):** subpixel stroke policy at
25%. The rasterizer enforces a 0.75 px minimum screen radius
(`incremental_rasterizer.cpp:13`), which preserves visibility but distorts
true scale. Choose between geometric fidelity (faint/subpixel strokes via AA
coverage) and a minimum-visibility floor (slight thickening). Gate 1's
eyeball test judges *presence only* under the existing floor; the AA gate
judges the chosen policy on glass.

---

## Gate 1 — the tile producer (one focused day)

**Question:** can production create the missing high-resolution tiles from the
operation log fast enough that zoom stops being a pixelated overview blowup?

This is the go/no-go for funding everything else. Hard timebox: 1 day
(1.5 only if flashing/waiting is the limiter).

### Scope

1. **Create missing visible tiles from the log**: hard-edged,
   painter-ordered, **bounds-filtered supertask replay** (scan the log once
   per 2×2 supertask, rejecting ops by level-space bounds). This is honest
   naming: it is not global op-to-tile binning; a persistent bin/index is
   Gate 2 work unless measurements force it. Do not brute-force full replay
   per tile (up to 56 tiles × 1,000 ops).
2. **Progressively replace overview fallback** with produced tiles.
   Publish producer output as a **provisional quality tier** (`kImmediate`,
   below `kSettled`), and relabel the incremental append path's hard-edged
   publications to match. `kSettled` is reserved for AA output. The existing
   quality-downgrade guard (`materialized_canvas.cpp:483`) then guarantees AA
   replaces provisional and provisional can never overwrite AA. Hard-edged
   pixels must not be able to masquerade as accepted settled state.
3. **Measure at 100% and 400% with two workloads**: the deterministic stress
   document for reproducibility/regression, and the **seed-7 realistic
   handwriting corpus** (`core/realistic_workload`) converted to production
   operations for the actual kill verdict. The uniform 20-sample synthetic
   stress doc alone could produce a misleading green. Record p95s against
   the Decision 3 gates. **25% is measured separately** — it has no tiles by
   design; it measures overview presence, quality, and presentation only.
4. **Confirm pan at 100% and 400% as an independent adapter defect** (25%
   pan cannot move by geometry). Overview-fallback pan should already work —
   the `3b69d59` walk composed 100% views from pure fallback. Do not assume
   the tile producer fixes pan; reproduce and fix it early and separately.
5. **One draw-while-fill burst**: draw while tile production runs. Required:
   no stale publication, no corruption, no perceptible live-draw regression.
6. **Measure responsiveness, don't assume it**: record maximum uninterrupted
   supertask time, maximum touch-poll gap, and event-to-submit while filling.
   Single-threading prevents races, not latency. If supertasks exceed
   ~20–30 ms, the producer needs resumable operation batches (finer work
   units, not threads).
7. **Supersample probe (~1 hour)**: rerun the same producer at 2× per axis
   into the reserved 128×128 workspaces for one visible tile set, box
   downsample to 64×64, and time it. This prices 4-sample AA on day 1 with
   zero new renderer code, and feeds the verdict directly.
8. **Eyeball 25% hard-edged on glass**, including strokes drawn at 400%.
   Judge **presence only** against the existing 0.75 px minimum-radius floor
   (`incremental_rasterizer.cpp:13`): nothing may vanish. The
   scale-fidelity-versus-visibility policy (geometric faintness vs enforced
   thickening) is explicitly decided at the AA gate, where coverage
   rendering makes "faint but crisp" possible; the eyeball test needs that
   policy named, not solved.
9. **Receipt must state the geometry assumption**: raw source samples, no
   four-copy LOD.
10. **Do not grow `production_live_app.cpp`** beyond what these steps force.
    No new exclusive apps.

### Kill lines

- Visible high-resolution refinement cannot approach **<500 ms** at 100% on
  the 1,000-stroke document after one focused day → **stop and rescope.**
- Draw or pan regresses while fill runs → **stop and rescope.**

### Verdict (based on measured numbers, not inference)

Use the supersample probe's **measured** SSAA time extrapolated to the full
viewport — do not infer AA cost by multiplying hard-edged time (an earlier
draft claimed ≤150 ms × 3.5–4 fits under 500 ms "by construction"; 150 × 3.5
= 525 ms — it does not).

| Result | Reading | Action |
|---|---|---|
| Hard-edged refinement <500 ms **and** measured 4-sample SSAA <500 ms | **Green.** AA risk substantially closed on day 1. | Proceed to Gate 2. |
| Hard-edged passes; measured SSAA fails | **Yellow.** The cheap SSAA route is dead; AA depends on the analytic span/microtile renderer — the design that already failed once at 490–828 ms. | Run the AA gate spike **immediately**, before Gate 2. |
| Hard-edged cannot approach <500 ms, or interaction regresses | **Red.** Kill line trips. | Continue / cut scope / stop decision with the user. |

### Deliverable

High-resolution tiles on glass plus one receipt with the measurements above.
Not a characterization document. Not a pan archaeology expedition.

---

## If Gate 1 passes: the path forward

### AA gate — own gate, own kill, **must land before Gate 3 starts**

**Question:** can anti-aliased settled rendering (≥4-sample equivalent,
analytic coverage preferred) meet the interaction gates on overview and tiles?

- Route A (if Gate 1 was green): productionize the supersampled producer.
  Verify the overview separately: a 2×-per-axis overview scratch is
  736×896×2 = **1.29 MB** — plausible against the ~3.4 MB post-reserve
  headroom, but needs a real allocation receipt, and incremental append pays
  ~4× per overview update. Alternative: overview keeps analytic coverage
  (it is the level that most needs sub-pixel coverage).
- Route B (if Gate 1 was yellow): timeboxed spike (0.5–1 day) of the
  span/microtile analytic renderer from handoff #56 on one zoom level —
  geometry generated once per supertask, ordered coverage spans, solid
  interiors, coverage math only at edges. Its predecessor measured
  490–828 ms; it must beat that by ~1.6×+.
- Both routes: settlement starts from an explicit epoch/revision checkpoint
  and painter-ordered `replay_range()` — **AA cannot be blended in place over
  hard-edged pixels** (`PROJECT_STATE.md` #56). The overview needs the same
  settlement path as tiles.
- **Kill:** if AA cannot meet the interaction gates, make an explicit
  decision — drop zoom levels, cut scope, or stop. "Just keep going" is
  prohibited. Shipping hard-edged is not an option (Requirement 2).

### Gate 2 — in-memory vector canvas (2–4 days)

- Add 50% and 200% to the live path (identities already exist).
- Tile cache eviction and mutation behavior.
- Repeated draw/erase/pan/zoom cycles; full pen/eraser ordering validation.
- Settle the shared/nested/on-demand LOD representation, judged on 25–50%
  *visual* quality as well as capacity — never four copies.
- AA work may start here; it must have passed its own gate before Gate 3.
- Move the reusable path toward the real application. Stop growing the test
  slice.

Exit: you can actually draw and zoom on device, crisply where AA has landed.
Still not the product.

### Gate 3 — interaction parity in shipping firmware (4–8 days)

Budgeted honestly; persistence is the item with teeth.

- Vector persistence: format, checksums, power-loss recovery, and an explicit
  answer for existing raster saves.
- Autosave/load; ten-step Undo via checkpoints; PNG export; new drawing.
- Toolbar, battery, and power integration through a narrow adapter — no
  third forever-app; strangler rule applies (`production/README.md`
  migration rule).
- Toolbar-at-25% UX: the toolbar obscures overview rows 372–447. Hide or
  overlay it. This is a parity list item, not a renderer test item.
- Remove superseded raster authority as production takes each responsibility.

### Gate 4 — hardening (2–4 days)

- Long-session and capacity tests against representative captured input.
- Power interruption and corrupted-save handling.
- Removal of temporary paths and the opt-in test apps.
- Hardware regression passes; final review.

---

## Timeline (honest)

Assuming Gate 1 passes:

| Milestone | Estimate |
|---|---|
| Useful zoomed canvas (hard-edged, in test slice) | ~1 week |
| Interaction parity in shipping firmware | 2–3 weeks |
| Crisp-at-every-zoom product as defined in Requirements | **2–4 weeks** if AA cooperates |

If the AA gate fails, the timeline is open-ended — which is why the AA
verdict is sequenced to arrive by roughly day 2–4, converting "open-ended"
into a bounded continue/cut/stop decision early.

Estimates in this repository have historically been optimistic (see
`CONTINUATION_HANDOFF_2026_08_12_EVENING_FACT_CHECK.md`); treat lower bounds
as floors, not midpoints.

## Failure branches (pre-committed)

- **Gate 1 fails:** cut zoom and keep the raster product, or accept a
  25%-overview-only feature, or stop. Do not keep inching.
- **AA gate fails:** drop zoom levels, cut scope, or stop. Hard-edged ink is
  not a ship option.
- **Any gate trips:** the kill lines are honored as written. The default
  firmware remains a working product throughout; stopping at any point loses
  nothing that ships.

## Load-bearing references

1. `PROTOTYPE_EXIT.md` — verdict, receipts, rejected mechanisms, gates.
2. `PRODUCTION_CONTINUATION_HANDOFF_2026_08_12_NIGHT.md` — architecture,
   contracts, task order.
3. `production/README.md` — island rules, memory plan, task receipts.
4. `production/REAL_TOUCH_CHARACTERIZATION.md` — LOD capacity rejection.
5. `CONTINUATION_HANDOFF_2026_08_12_EVENING_FACT_CHECK.md` — claim discipline.
6. `production/hardware-receipts/` — all hardware evidence to date.

`PROJECT_STATE.md` should be updated to point at this plan and at the live
slice (`5dfd793`) before Gate 1 work begins.

---

# Appendix: Gate 1 build plan

Grounded in the current interfaces (`materialized_canvas.h`,
`incremental_rasterizer.h`, `operation_log.h`, `incremental_document.h`,
`production_live_presenter.h`). Gate 1 is almost entirely assembly of seams
that already exist; exactly one new module gets written.

## Shape

One new host-tested production module plus thin wiring into the live app.
Nothing else.

```text
        OperationLog (painter-ordered ops, replay_range, epoch)
              │ read-only
              ▼
   ┌─────────────────────────┐   NEW: production/src/tile_producer.cpp
   │  TileProducer            │   (worklist + supertask render + publish)
   └─────────────────────────┘
      │ render               │ publish
      ▼                      ▼
apply_incremental_operation   MaterializedCanvas::publish_tile   ← both EXIST
 (RasterSurface, any zoom,     (revision-validated, makes tile
  arbitrary stride/bounds)      resident → append path maintains it)
                             │
                             ▼
              compose_view → presenter.present   ← EXISTS (progressive refresh)
```

The chicken-and-egg breaks itself: the moment `publish_tile` makes a tile
resident, the existing `append_incrementally` path keeps it current on every
subsequent stroke. Tiles are produced once; maintenance is already built and
hardware-proven.

## What exists vs. what gets written

| Piece | Status |
|---|---|
| Ordered op reads: `OperationLog::operation(i)`, bounds per op | ✅ exists |
| Rasterizer for any level-space rect at any zoom with stride (`RasterSurface`) | ✅ exists (hard-edged — correct for Gate 1) |
| Tile publication with revision validation, LRU slots, pin safety | ✅ exists (`publish_tile`, `choose_slot`) |
| Fallback composition + progressive present | ✅ exists (`compose_view`, presenter) |
| Op→tile binning for the append path | ✅ exists (`affected_tiles`; the cold producer uses bounds filtering, not this) |
| Deterministic 1,000-op stress document | ✅ exists (`append_stress_document`; regression only — not the verdict workload) |
| Realistic handwriting corpus | ⚠️ exists (`core/realistic_workload`, seed 7); needs conversion to production operations |
| Provisional quality tier (`kImmediate`) | ❌ small enum + relabel change (producer and append path) |
| **Viewport worklist + supertask render + publish coordinator** | ❌ the one new module |
| Wiring: call producer in idle slices of the live loop | ❌ ~tens of lines in the live app |

## Core loop (TileProducer)

1. **Worklist:** given the current viewport (zoom, level origin), enumerate
   the covering tile keys — **42 aligned, up to 56 at arbitrary offsets**
   (⌈431/64⌉ × ⌈511/64⌉ = 7 × 8), so up to 16 partially populated
   supertasks, not ~11; subtract already-resident current-revision tiles
   (`lookup`); order **center-out** (retained prototype mechanism). Use the
   worst case for timing budgets.
2. **Supertask:** pop a 2×2 tile group (128×128 px) — deliberately matching
   the two reserved 128×128 renderer workspaces from the Task #53 memory
   plan. Clear to white. Scan the log **once**, in painter order, applying
   every op whose level-space bounds intersect the supertask rect via
   `apply_incremental_operation`. Honest name: **painter-ordered,
   bounds-filtered replay into 2×2 supertasks** — not global op-to-tile
   binning (a persistent bin/index is Gate 2 work unless measurements force
   it). It still honors the `PROTOTYPE_EXIT.md` lesson (generate once per
   supertask, publish smaller tiles) and avoids the banned failure mode
   (per-tile full replay: up to 56 × 1,000).
3. **Publish:** split into up to 4 `publish_tile` calls at **`kImmediate`
   (provisional) quality**, at the revision captured when the supertask
   started. `kSettled` is reserved for AA output; the quality-downgrade
   guard makes replacement one-directional.
4. **Present:** `compose_view` over the supertask's screen rect →
   `DisplayScheduler` → glass. Fallback pixels visibly upgrade region by
   region.
5. Repeat until the worklist is empty.

## Three design decisions that keep it a one-day job

**Concurrency = synchronous first, resumable only if measured latency
fails.** The live app is one loop; run one complete supertask per idle
iteration (no finger down). Single-threading prevents races — it does *not*
guarantee responsiveness: one supertask can block touch polling for as long
as it renders. So Gate 1 measures maximum uninterrupted supertask time,
maximum touch-poll gap, and event-to-submit while filling. If supertasks
exceed ~20–30 ms, split them into resumable operation batches (finer work
units on the same loop — not threads).

**Revision guard = restart, not merge — and be honest about when it is
live.** With fully synchronous supertasks, no stroke can commit between
render and publish, and the restart logic is dead code (keep the
`(epoch, revision)` capture and `publish_tile`'s revision validation as a
cheap backstop anyway). The moment supertasks become resumable, restart
becomes the real mechanism: a stroke committing mid-supertask makes it stale
→ discard and re-render. Bounded (≤1 supertask of waste per stroke), always
correct. Merging deltas is a Gate 2 optimization if measurements demand it.
The draw-during-fill burst verifies both the no-stale-publication property
and that a physically arriving touch is not badly delayed.

**Geometry = raw source samples.** No LOD, no simplifier (locked Decision 5).
The receipt states it.

## Order of work (the day)

1. **Morning — host first** (island rule: prove on host before hardware).
   `TileProducer` + tests. The oracle already exists: for a
   white-baseline document, producing all viewport tiles then `compose_view`
   must byte-equal a single direct `apply_incremental_operation` render of
   the whole viewport rect. Deterministic hash, same style as prior receipts.
2. **Midday — wire and flash.** Idle-slice call in the live loop;
   progressive present. In parallel: reproduce and fix pan at 100%/400% as
   an **independent adapter defect** (25% is a geometric no-op — do not
   debug it there). Overview-fallback pan should already work (`3b69d59`
   composed 100% views from pure fallback); do not assume tile production
   fixes it.
3. **Afternoon — measure and decide.** Load the stress doc for regression
   hashes, then the converted seed-7 handwriting corpus for the verdict;
   cycle 25/100/400; record first-tile-visible and full-viewport-refined
   times, p95 over repeated zoom commits, plus max supertask time, max
   touch-poll gap, and event-to-submit while filling. Draw-during-fill
   burst. Eyeball 25% for presence (0.75 px floor) including strokes drawn
   at 400%.
4. **Supersample probe (~1 hour):** no new renderer and no 800% enum needed —
   for a 100% tile, render the same world rect through the **200% path** at
   2× bounds into a 128×128 workspace, box-downsample to 64×64, time it.
   That is a real 4-sample-AA cost measurement at 100% using existing code.
   (A 400% probe needs a scale-factor tweak to `RasterSurface`; skip unless
   100% is ambiguous.)
5. **Receipt:** numbers, hashes, verdict per the measured-SSAA table (green:
   hard-edged **and** measured SSAA <500 ms / yellow: SSAA fails → analytic
   AA spike jumps the queue / red: hard-edged fails or interaction
   regresses), geometry assumption, workload identity, and the 25% visual
   observation.

## Napkin check that the timebox is sane (motivation only — not evidence)

Warm hard-edged incremental appends measured **~1 ms/op** for bounded regions
(`16dc9b2` receipt). A cold 100% viewport fill is up to 16 supertasks × (ops
intersecting each), with each op typically touching 1–2 supertasks — order of
1,000–2,000 bounded op-applications ≈ low hundreds of ms. Caveat: that 1 ms
figure was one bounded *new* operation against existing materializations;
cold replay has different clipping, sample distributions, and repeated
supertask scans. This estimate justifies attempting the gate. It must not
influence the verdict — only the hardware measurement does.

## Scope warning

`TileProducer` must not know about scheduling policy, pan prediction,
neighbor prefetch, or eviction. Worklist in, publications out. Everything
else is Gate 2.
