# Committed-overlay / authority-revision split — execution design

Status: implemented; product publication revised 2026-08-20 (author decision
2026-08-16 \#1 in [`SHIP_CONTRACT.md`](../../SHIP_CONTRACT.md)). Source: external review
§8.2–8.4 (archived as `LATEST_tinydraw-review-report.md` in
`archive/v2-correctness-fixes-e14e6e9`, commit `9be7a53`), adopted by
[`EXTERNAL_REVIEW_SYNTHESIS_2026-08-16.md`](../archive/2026-08-code-reviews/2026-08-18-performance-review/EXTERNAL_REVIEW_SYNTHESIS_2026-08-16.md)
item 9 as the Phase 2 execution design.

Update 2026-08-20: foreground publication is now atomic per Stroke. The live
preview remains provisional during Move; TouchUp copies at most 4,097 compact
samples into one authority operation, and the existing cooperative absorber
materializes that operation after lift. Multi-record pending ranges remain a
core compatibility contract for restored legacy authorities and diagnostic
stress gates. References below to publishing and draining 32-sample chunks
describe the superseded product coordinator, not the current input path.

## 1. Problem, with receipts

The felt 400% drawing lag (glass, 2026-08-16) is the synchronous
chunk commit blocking the input poll loop. Current per-chunk maxima
([`gate-dropattr-1.log`](https://github.com/aliceisjustplaying/tinydraw/blob/v2-feature-complete-pre-cleanup/benchmark-results/ink-fallback-observability/gate-dropattr-1.log),
mixed_draw gate, warm multi-zoom cache):

| zoom | append_max | ph_raw | ph_uniform | ph_overview | ph_commit |
|---|---|---|---|---|---|
| 25  | 13.2 ms | 0.0  | 0.1 | **8.9** | **3.6** |
| 50  | 19.3 ms | **12.2** | 5.4 | 3.8 | 1.8 |
| 100 | 16.0 ms | **12.1** | 4.4 | 1.9 | 1.1 |
| 200 | 15.4 ms | **12.2** | 4.5 | 1.1 | 0.9 |
| 400 | 14.1 ms | **11.5** | 3.4 | 0.7 | 0.8 |

Glass receipts add `ph_uniform` up to 18.3 ms at 400% (fresh paper
tiles under real strokes) and a post-lift poll gap of 87–111 ms with a
72–80 ms synchronous lift refresh (review §8.2).

Two structural facts follow:

1. **The budget is unmeetable by tuning.** Visible raw painting alone is
   12 of the 15 ms budget, and it is budget-exempt by design (dropping a
   visible tile is an on-glass blur — falsified as acceptable on glass, and
   re-verified by the fallback-observability receipt: all drop counters
   zero everywhere). The eraser case (uniform ≈ 0) still spends 17.9 ms.
2. **The work is not wasted, just mistimed.** Every phase is needed; none
   of it needs to happen inside the poll loop, because the glass already
   shows the ink (the live preview staged it) and the authority already
   holds it (the log append is cheap: `ph_prepare` ≤ 0.2 ms).

## 2. Target contract

- Chunk commit on the input path does only: log append + pending-range
  bookkeeping. Synchronous cost target ≤ 2 ms worst observed (receipt
  required), against the existing 15 ms alarm with real margin.
- Lift publishes final authority and returns to input; no synchronous
  full-region refresh. Post-lift next-poll delay target ≤ 20 ms
  (from 87–111 ms).
- Materialization (overview replay, uniform materialize+paint, raw paint,
  metadata commit) drains in bounded slices between polls, visible region
  first. Every slice reports time and work counters (§8.4 state machine).
- Final state is bit-identical to today's: same painters, same order, same
  exactness oracles. `authority_match` stays a hard gate.
- No visible regression at any moment: pixels on glass never revert from
  inked to pre-stroke while materialization trails.

## 3. Mechanism

### 3.1 Revision pair + pending range (core)

Split the lockstep `log.current_revision == canvas.current_revision`
invariant (review §8.3):

- `authority_revision` — advances at every log append (chunk commit).
- `materialized_revision` — the revision the canvas (overview + tiles)
  has fully absorbed. Trails authority by the **pending operation range**
  `[first_pending_op, authority_end)`.
- Absorption happens per-operation through `absorb_pending_operation` and the
  §8.4 phase runner.
  Mutation stays fail-safe: a tile that cannot be finished in this slice
  simply is not retained yet.

### 3.2 The committed overlay is the operation log itself (no new cache)

While the pending range is non-empty, any staged presentation region is
patched with the pending operations' ink before submit: rasterize the
pending range's operations clipped to the staged window, through the same
masked window painters the producer uses (H7 window sweeps), directly into
the internal DMA staging buffer — exactly like the transient live tail and
chrome patches today. Eraser operations stage paper color; the painter
already knows.

This obeys the standing authority doctrine: ordered vector operations are
the drawing authority; the overlay is a derived, transient view of the
not-yet-absorbed suffix. There is no new pixel cache, no byte budget, no
invalidation authority, no eviction — the "overlay" disappears naturally when
the pending range drains to empty. Cost is bounded by the staged window
(move updates stage only the old∪new tail region) times the pending-range
length, which drain keeps short.

### 3.3 Drain scheduling

- Reuse the cooperative slice pattern (idle-repair / producer supertasks):
  each poll-loop iteration with input idle runs one bounded drain slice.
- Priority: visible-viewport tiles of the pending ops first, then
  overview rows, then offscreen retention, then metadata commit per
  §8.4's order.
- The drain must also run (throttled) mid-stroke, so a long gesture cannot
  grow the pending range without bound; the existing per-chunk cadence
  becomes the natural batch boundary.

### 3.4 Serialization boundaries

Pan, zoom, Undo/Redo, SVG/PNG export, autosave checkpoint, New, and
document capacity rejection **drain first** at an explicit boundary
(review §8.3). Each boundary emits a drain receipt (ops drained, slices,
wall). Rationale: these paths either recompose large regions (pan/zoom —
overlay-patching arbitrary exposed regions would put unbounded work on the
pan wire path and reopen its optical gates) or need a single authoritative
state (undo/export/save).

### 3.5 Degradation

If drain cannot keep up (receipt: pending-range high-water counter), the
chunk commit falls back to today's synchronous behavior for that chunk.
Today's behavior is the worst case, never worse.

## 4. What this touches (dependency matrix reopens)

| Surface | Change | Reopened gates |
|---|---|---|
| `incremental_document` | phase machine + revision pair | cold exactness (host fuzz/exactness), mixed_draw, INKTRACE replay baseline |
| canvas revision protocol | materialized may trail authority | rerender ledger (drained work classifies as damage — must not create `unexplained`), cache tour |
| app poll loop + lift path | commit = append only; drain slices; no lift refresh | ink latency lane receipts, lift baseline, long-gesture gate |
| presenter staging | pending-ink patch on staged regions | ink presentation receipts; pan stays untouched (drain-before-pan) |

Not touched: pan compose/cadence (drain-before-pan), panel transport, cold
producer, or the autosave worker. Autosave is now enabled in the full device
battery; the final 20-run cold closure remains open.

## 5. Landing plan (one measured hypothesis per revision)

1. **Scaffold, no behavior change:** extract the explicit §8.4 phase runner;
   phase/work counters already exist.
   Receipt: byte-identical gate battery (verdict vector, exactness,
   INKTRACE latencies within noise).
2. **Revision pair + pending range in core, host-first:** absorption
   slices with host exactness tests proving final-state equality for
   interleavings (commit N ops, drain in K-slice permutations, compare
   full-canvas hash vs synchronous baseline; pen/eraser mixes; capacity
   rejection mid-range).
3. **Overlay staging on device:** patch pending ink into staged move/tail
   windows; hypothesis — no visible difference mid-stroke (glass +
   probe: fb counters and drop counters stay zero), commit path cost
   collapses to ≤ 2 ms (`ph_*` receipts).
4. **Lift without synchronous refresh + drain slices:** hypothesis —
   post-lift poll gap ≤ 20 ms, lift-baseline receipt, long-gesture and
   INKTRACE gates green, `authority_match=1` on every stroke.
5. **Boundary drains:** pan/zoom/export/New serialize; receipts per
   boundary; mixed_draw budget gate finally green at every zoom.

## 6. Premortem (pre-registered failure interpretations)

- **Overlay repaint cost blows the move update:** staged windows are
  small, but a pathological pending range over a dense region could cost
  ms-level per update. Counter: per-update overlay-paint time receipt;
  if p95 > 2 ms, cap pending range harder (more mid-stroke drain).
- **A drain slice splits a canvas commit invariant:** phases must end only
  at states where composition is legal ("no composition may observe the
  canvas between edits and commit"). The §8.4 machine pauses only between
  phases; the metadata commit phase stays atomic.
- **Ledger noise:** drained tiles must classify as `damage`; any
  `unexplained` in the cache tour is a red, full stop.
- **Long-gesture memory:** the pending range holds only op indices —
  no growth beyond the log itself. The log's existing capacity rejection
  path erases the uncommitted transient tail only (ship contract §2
  fidelity rules unchanged).
- **Flash-icache layout luck:** any new hot loop in the staging path gets
  measured against the pan strip budget; presentation code stays pinned
  (`esp32/main/linker.lf` pattern).
