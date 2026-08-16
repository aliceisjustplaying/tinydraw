# Session handover — 2026-08-16 Cold Stage B (+ owner glass session)

Read alongside the Stage B receipt
([`../benchmark-results/cold-stage-b-2026-08-16/RECEIPT.md`](../benchmark-results/cold-stage-b-2026-08-16/RECEIPT.md)
— the full experiment matrix, the strip-staging investigation, and the glass
session are recorded there). The wave-3 A/B recipe
([`../review_findings_2026_08_16_cold_campaign/HANDOVER.md`](../review_findings_2026_08_16_cold_campaign/HANDOVER.md)
§3) remains the operating manual; its §6 device-physics cheat sheet is
amended below.

## 1. What this session did (all pushed to `feat/v2-performance-followup`)

| Commit | Content |
|---|---|
| `f2f6da7` | perf: strided tile publication (no packed staging; −10 ms) |
| `395e230` | docs: session-1 receipt (B2 shelved at the time) |
| `11edbd7` | fix: IRAM-pinned panel transport strip loops (linker fragment; kills flash-icache dice on the pan wire budget) |
| `bdd95e7` | perf: H7 op-level chord sweep (−120 ms at 400%) |
| `75c9145` | perf: O(1) raw-slot metadata, un-shelved after the IRAM fix (−26 ms more) |
| `224f1b5` | docs: Stage B closed, receipts |
| `88d3391` | fix: harness boots into interactive mode even on a red verdict (glass testing was impossible with standing known-reds) |
| `436d84a` | docs: owner glass session recorded |

Cold compute three-run maxima went 434/468/599/587 ms → **356/349/410/432 ms**
(zooms 50/100/200/400). Walls 437.9/428.4/488.0 ms are under the ≤500 line;
400% is 507.0 ms (7 ms over). Every run of every landed variant held the
standing guards: cache-tour ledger `amplification=1.000 stale=0 unexplained=0`,
five `TINYDRAW_INKTRACE pass=1`, verdict vector unchanged.

## 2. Machine and tree state

- Device `/dev/cu.usbmodem101` runs the gate harness at HEAD and now stays
  interactive after the cascade (owner document from the glass session is on
  it until next reflash).
- Build dirs: `esp32-vector-v2-gate-harness` at HEAD; host-census /
  host-debug / host-asan all green (228 tests / 29 ctest / 11 ctest).
- A parallel read-only correctness review (Grok, owner-spawned) produced
  `CORRECTNESS_REVIEW_2026-08-16.md`, `CORRECTNESS_REVIEW_SYNTHESIS_2026-08-16.md`,
  and `review_findings_2026_08_16_correctness/` at the repo root — untracked,
  not mine, needs an owner-directed triage pass.
- Untracked leftovers predate the session (review zip, wave1a dir, datasheet
  PDF, `LATEST_tinydraw-review-report.md`).

## 3. Device-physics cheat sheet — amendments (hard-won today)

- **Flash icache layout moves hot-loop timing ±2–3% per build.** The pan
  strip wire budget ran on ~35 µs of margin and every producer-change build
  consumed it — `byte_swap`, a pure internal-SRAM loop, went 11→18 µs from
  code layout alone. Falsified along the way: slot-assignment, data-placement,
  and dead-code-shift theories. Fix pattern: pin beam-racing code via a
  **linker fragment** (`esp32/main/linker.lf`, `noflash_text`). `IRAM_ATTR`
  on in-class (implicitly inline) methods trips Xtensa `l32r: literal placed
  after use` link errors — use fragments for those.
- **`__builtin_assume_aligned` + fixed-size `memcpy` still emits a `callx8`
  memcpy libcall on GCC-Xtensa.** The objdump gate caught it pre-flash. A
  `typedef std::uint32_t T __attribute__((may_alias))` load compiles to a
  bare `l32i`. Even then: word-mask scans lost 4–5 ms net post-H7 (windows
  too short) — rejected, do not retry without structurally longer scans.
- **Slice budgets must charge measured work, not rows.** A flat 128-row
  producer slice budget blew the idle-repair step contract (18.1 ms worst
  step); charging window-clipped span pixels actually visited restored it
  (6.1 ms) while keeping the H7 win.
- **Panel TE bring-up flake exists**: one boot in ~40 came up with no TE
  signal (`tear_edge_observed=0 … tear_heal_attempted=1`), cascading every
  gate red; the next reset healed it. Don't burn a diagnosis on a single
  all-red run — reset first.
- The `python3 -m esptool` reset trick from the wave-3 recipe no longer works
  in this shell (no esptool module); the capture tool's default RTS reset is
  the reliable path.

## 4. NEXT: owner-approved queue

1. **Mid-stroke pixelation diagnosis** (owner glass report; todo carries the
   full plan). Phase 1 loop: count viewport tiles whose `lookup()` falls to
   overview inside the ink-replay gate (mid-gesture max / at Up / settled),
   print on the `TINYDRAW_INKTRACE` line; then attribute drops
   (`materialize_uniform_as_raw` nullopt vs paint-fail vs budget) with
   counters in the retain passes. I was one edit into this when the session
   pivoted to glass testing — nothing committed.
2. **Settled-AA + arc-length-resampling host prototypes** — owner wants
   rendered before/afters. Constraints already logged: within-op self-overlap
   must UNION coverage; freeze the RGB565 blend model first.
3. **Déjà-vu campaign** — glass-confirmed open. Step 1: live ledger
   cause-histogram receipts during glass sessions; then draw-then-return and
   eviction-pressure gate scenarios; then fix per histogram.
4. **Zoom-cycle return-position fix** (mechanical, roadmap Phase 6).
5. **Correctness-review triage** with the owner.

Convergence note: mixed_draw lag (ph_uniform 18.3 ms bursts, felt on glass at
400%), the pixelation report, the lift hitch, and part of déjà-vu all point
at the same committed-overlay / authority-revision-split design (external
review §8.3–8.4). Expect one design to close several fronts.

## 5. Owner decisions pending

1. **mixed_draw budget vs fix** — now with glass feel + phase attribution.
2. **Stage C authority bundle** (conical capsule + adaptive subdivision) —
   justified by speed only; smoothness case falsified.
3. **Close the last 7 ms of 400% wall now, or after autosave re-measure** —
   candidates listed in the receipt; the 20-run closure statistic is still
   unrun either way.
4. **Settled-AA go** — prototype first (queue item 2).

## 6. Gotchas

- The chord-plan workspace (`operation_chord_plans`, 12,384 B internal) is a
  hard `ready()` requirement of `TileProducer` — every new harness/tool rig
  must fund it (pattern in any test fixture).
- `TileProductionStep.raster_steps/raster_work` are now credited at batch
  completion, not per unit; slices that pause mid-batch report zero rendered
  that call.
- The B2 metadata patch file in the receipt dir is now historical (landed as
  `75c9145`); don't re-apply it.
- `general_cold` in the DONE line IS the adversarial corpus
  (`adversarial_tapered_4x+evil_hairlines`, frozen at `a560d20`); an optional
  rename to `adversarial_cold` was floated with the owner but not approved —
  it would change the DONE-line format that log tooling greps.
- `vector_v2_app.cpp` structural split stays deferred until performance
  gates close (standing guardrail).
