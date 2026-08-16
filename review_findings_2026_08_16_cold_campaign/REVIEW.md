# Code, architecture, and performance review — 2026-08-16

Scope: Vector V2 cold rendering, ink smoothness, settled AA direction, and
general architecture. Review performed alongside the wave-3 cold compute
campaign; every performance claim in this document has a device receipt in
[`benchmark-results/wave3-cold-compute/`](../benchmark-results/wave3-cold-compute/).

## Headline

The cold 400% kill gate moved from **1,269.157 ms to 668.980 ms** (three-run
maximum, −47.3%) with zero exactness drift, every sibling gate held or
improved, and interaction ticks reduced. The remaining distance to the 500 ms
contract line is ~170 ms of compute, and the receipt ranks the candidates.
Nothing in this campaign required new caches, new memory, or architecture
changes — the wins were all mechanical sympathy inside the accepted design.

## Architecture verdict

This codebase is in the top percentile of embedded projects for measurement
discipline. The frozen corpus + receipt + guardband culture caught two of my
own regressions (word-mask, band shape) that host measurements had approved.
Specific strengths worth preserving:

- The narrow `TileProducer` / `MaterializedCanvas` / `OperationLog` seams
  with caller-funded workspaces made every experiment surgical.
- `covers_pixel` as the single geometry authority is the reason four rounds
  of span-search surgery stayed bit-exact. Guard it jealously.
- The census being compiled out of product builds made attribution free.

## Findings (ranked)

### F1 — Fixed: `./scripts/dev release` was broken at HEAD

`ribbon_renderer.cpp` kept a size precondition alive only inside `assert`,
which `-DNDEBUG` discards; `-Werror` then failed the build. Fixed with
`[[maybe_unused]]` (commit `a9e43eb`). A CI leg that builds host-release
would have caught this; today only host-debug appears to run routinely.

### F2 — Fixed: the census had a blind spot exactly where the cost was

`raster_census.h` said "span search predicate calls not counted" — and the
uncounted calls were 2.5M per cold fill, the single largest compute term.
Lesson recorded in the receipt: when a counter's comment says a path is not
counted, that path is where the next whale hides.

### F3 — Fixed: Xtensa float libcalls in per-row code

The toolchain emits `callx8` for float division, `floorf`, `ceilf`, and
`sqrtf` (verified by disassembly). Any per-row or per-pixel float division
on this target is a design smell. The codebase now has native
`fast_floor`/`fast_ceil`/`conservative_sqrt_upper` helpers and hoisted
reciprocals; new hot-path code should reuse them. Consider a short
`docs/` note on this class of trap (division, floor/ceil/sqrt, unaligned
word loads, non-inlined `memcpy`) so future contributors do not rediscover
it on the device.

### F4 — Fixed: producer paint scratch lived in PSRAM

The hottest pixel memory in cold replay (32 KiB supertask + 8 KiB packed)
was allocated from PSRAM while 319 KiB of internal SRAM sat free. Now
internal with PSRAM fallback and a boot receipt line
(`TINYDRAW_PRODUCER_SCRATCH`). Recommend auditing the remaining
allocations with the same question; `region_scratch` and the chrome cache
are the next candidates worth a measured A/B.

### F5 — Open: `mixed_draw` 50% appends exceed the 15 ms budget

18.8 ms max vs 15 ms. Dating evidence (in the wave-3 receipt) points to
`19ebbe3` (curved committed ink) predating the last green mixed receipt;
the failing state was masked because the gate cascade stopped at the cold
gates. At 25%, with no in-place tile painting at all, appends already
average 11.4 ms — overview replay plus commit bookkeeping consumes ~75% of
the budget before any tile is painted. Owner decision needed: either the
per-append budget reflects a per-chunk cost that curved authority
legitimately raised, or the overview-replay-per-chunk design needs its own
optimization pass (the overview repaints the chunk's full bounds through
the unmasked painters on every 12-sample commit).

### F6 — Open: undocumented load-bearing coupling on `kMinimumScreenRadius`

The warm-start masked search is only correct because adjacent row chords
overlap in x, which holds iff the screen radius is ≥ ~0.5 px. The 0.75
clamp exists in `scaled_sample` for a *visual* reason ("stroke presence
survives every committed zoom") and silently underwrites a *correctness*
property two files away. If anyone ever lowers the clamp, the warm-start
painter can silently miss pixels. I added comments at the painter, but the
constant's own comment should also name this dependent invariant.

### F7 — Minor: dead and near-dead code

- `OperationLodStore` (330 lines + tests) is fully unwired; the roadmap
  guardrail explicitly forbids the four-LOD design. Either delete it or
  mark it as a measured-and-rejected reference implementation.
- `apply_masked_incremental_curve_step` (public API) lost its only product
  caller to the prepared-unit path; it survives as a one-line delegation.
  Fine to keep as a seam, but say so in the header comment.
- `incremental_segment_step_count` always returns 1 and
  `incremental_segment_step_work` ignores `step`; the step abstraction
  outlived its use.

### F8 — Minor: repeated workspace-validation boilerplate

Every `apply_masked_*` entry re-derives `required_mask_bytes`, re-checks
`storage_overlaps`, `valid_surface`, and summary readiness — five nearly
identical blocks. A small `MaskedPaintContext { surface, mask, summary }`
validated once at construction would collapse them and make the validation
cost per-batch instead of per-call. Low priority; the cost is small now
that the producer validates per unit instead of per chord.

### F9 — Observation: `vector_v2_app.cpp` split is already queued

1,300+ lines, already tracked as deferred structural debt in
PROJECT_STATE. Agreed with the existing plan: do not mix it into the
performance campaign. The `AppStorage::allocate()` block would be the
natural first extraction (it is pure memory-plan code).

## Ink smoothness — the four-span experiment deserves a rematch

The owner-visible complaint is live-curve angularity: committed authority
approximates each quadratic with **two chords**, and the four-span (four
chord) variant was rejected as too costly. That cost model is now stale:

- The unit-merged row sweep makes additional chords in a unit nearly free
  on the row axis — chords share the window scan and the row loop, finer
  chords are shorter, and the union row count of a unit does not grow with
  subdivision.
- The prepared-unit path pays subdivision once per endpoint, so doubling
  chord count doubles a cost that is now ~40 ms of the 400% budget, not
  ~130 ms.

Recommendation: re-run the four-span (or better, flatness-adaptive 1–4
span) experiment on top of `a3e8ff8`. Adaptive subdivision would spend the
chords only on tight curvature, which is where the angularity is seen.
Caveat: this changes committed authority geometry, so per the dependency
matrix it reopens cold exactness fixtures, SVG parity, and the frozen
corpus statistics — it needs an owner decision and a re-baseline, but it
is no longer expensive.

## Settled AA — a concrete design that fits the measured physics

SSAA is dead (808 ms measured). The contract wants analytic coverage in
bounded idle work. The existing replay machinery is unusually well shaped
for this:

1. The masked scanline replay already finds **exact span boundaries** per
   row per chord. Interior pixels are full coverage; only boundary pixels
   (~2 per chord-row, ~100–150K per full viewport at 400%) need analytic
   coverage.
2. Replace the 1-bit finalized mask with an **8-bit accumulated-alpha
   mask** (16 KiB internal for a group — measured headroom exists) during
   settle passes only. Newest-first replay then becomes classic
   front-to-back compositing: interior spans finalize at α=255 exactly as
   today (same chunk fills, same skips); boundary pixels accumulate
   `α_i·(1−α_acc)` with capsule coverage `α ≈ clamp(0.5 + (r − d), 0, 1)`
   from the same projection math `covers_pixel` already uses. Erasers
   composite background color with coverage, consistent with current
   opaque-white semantics.
3. Cost model: settle-pass ≈ immediate replay cost × ~1.3 for the group.
   At post-campaign immediate costs (~10–25 ms per group), one group per
   idle slice comfortably fits the interaction tick budget with room to
   spare, and a full-viewport settle completes in well under a second of
   idle.
4. Déjà-vu safety: settled tiles publish under the same revision identity
   at a higher quality tier, so the revisit ledger treats them as cached
   content — no cold-to-sharp cycling on revisit within capacity.

The live path stays hard-edged; nothing in the ink lane changes.

## Process notes

- Two of five device experiments contradicted their host proxies (in both
  directions). Host census is a compass, not a verdict — the existing rule
  "glass and device receipts are authoritative" held up and should stay.
- The gate cascade's skip-on-failure behavior hid a real regression
  (`mixed_draw`) for at least one revision. Consider running all gates and
  reporting a full verdict vector even when early gates fail, so red gates
  cannot shadow each other. It cost this review an hour of attribution.
- The per-boot single-run A/B discipline (same harness, alternating
  builds) resolved 15–20 ms deltas reliably; run-to-run spread on this
  hardware is ~1–3 ms. That is a luxury; exploit it.
