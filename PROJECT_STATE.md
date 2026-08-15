# TinyDraw project state

Last updated: 2026-08-15 (late-night hardware characterization round)

## Resume point

Branch: `feat/v2-performance-followup`
Current commit: `55f7449` (hardware characterization + optical validation round)
Reviewed product commit: `c86f3ac` (`f66c808` added receipts only)
Evidence-preservation commit: `2a62c36`

**2026-08-15 evening: the panel physics were measured for the first time**
(`HARDWARE_LIMITS.md`) and the boundary-synchronized pan sweep was validated
tear-free on glass with a red-capable optical oracle. Key measured facts:

- All historical "50/60 MHz" QSPI configs ran at **40 MHz actual** (divider
  clamp); the writer moves at 27.2 rows/ms vs the 26.7 rows/ms beam.
- TE is clean: 16.773 ms period, 578 µs high, 9 µs ISR→resume, zero timeouts.
- Full-frame TE-synced streaming sustains 29.4 FPS; ≤368-row regions 58.8 FPS.
- The free-running positive control tears on camera; the rising-edge row-zero
  sweep is optically clean (probe cell 1 + manual glass on the product build).
- QSPI register reads return zeros; there is no software beam oracle.

**Raster V1 remains the default/shipping firmware and the operational fallback.**
Vector V2 remains the accepted architecture under construction, but it is not
feature complete and the current presenter is not physically acceptable.

The 2026-08-15 manual glass run observed severe tearing at 100% and 400% and
visible drawing lag while the software battery reported success. That physical
evidence falsifies the current beam-race tear oracle and the claim that the
10 ms commit budget bounds interaction latency. The latest independent review
is [`external_help/espdraw-offline-review-2026-08-15.md`](external_help/espdraw-offline-review-2026-08-15.md),
backed by [`vector_v2/hardware-receipts/c86f3ac-manual-glass.log`](vector_v2/hardware-receipts/c86f3ac-manual-glass.log).

Current measured finish-line status:

- **Pan correctness: GREEN on glass for the boundary-top-sweep policy.**
  Manual pan at 100%/400% shows no tearing; probe cell 1 (rising-edge
  row-zero sweep) is optically clean while the free-running positive control
  tears (`benchmark-results/blockB-optical/`). Fail-closed TE handling and
  reuse-requires-observed-edge landed in `6057f9c`.
- **Pan pacing: RED — 19.9 FPS measured** (`gate-boundary-pan.log`): frame
  p95 52.8/53.2 ms vs the required ≤41.7 ms. Decomposition: 18.4 ms overlay
  PSRAM round trips + 10.2 ms TE wait + 21.6 ms sweep = 3 TE periods.
  Removing the overlay round trips (Wave 2 staging compositor) fits the
  frame in 2 periods ≈ 29.8 FPS.
- **Known bug:** ink drawn under the zoom rail is distorted — the overlay
  backup/restore machinery stomps fresh strokes. Same root cause as the
  pacing gap; the staging compositor removes the mechanism. Acceptance
  test: draw under every overlay, zero distortion.
- **Ink latency: RED.** Authority/materialization runs before preview, the
  existing provisional ribbon tail is intentionally omitted, and glass saw
  15.5–35.8 ms chunks with 92–143 ms loop gaps.
- **Cold 400%: RED.** Adversarial p95 is 628 ms. The next accepted slice must
  improve the isolated cold path by at least 25%, with product p95 ≤471 ms and
  every accepted run below 500 ms.
- **Authority/exactness: GREEN within its current contract.** Host exactness and
  fuzz coverage pass, but snapshot restore can install a raster baseline and
  empty the operation log; operation-only Undo/SVG/autosave authority is not yet
  a valid assumption.
- **Capacity:** current product code uses **448 raw tile slots**, not 384.

Process learnings binding future rounds (earned 2026-08-15):

1. Measure the substrate before optimizing on it (`HARDWARE_LIMITS.md`
   falsified the 60 MHz clock, the 15 Mpx/s writer rate, and the ~0.4 ms
   transaction folklore in one evening).
2. A pass signal must be able to observe failure: every optical CLEAN is
   valid only alongside a torn positive control (probe cell 4).
3. Use the cheapest sufficient instrument first; automate for receipts and
   rare events afterward.
4. One variable per experiment, interpretation matrix pre-registered before
   capture (`benchmark-results/blockB-optical/PROTOCOL.md`).
5. Attribute before optimizing: "slow" is a mystery, "18.4+10.2+21.6 ms" is
   a work order.

## Source-of-truth map

- This file is the concise current-state and resume document.
- [`V2_ROADMAP.md`](V2_ROADMAP.md) is the only forward work queue.
- [`README.md`](README.md) and [`vector_v2/README.md`](vector_v2/README.md) are
  orientation and stable module contracts, not competing status documents.
- `vector_v2/hardware-receipts/` and `external_help/` preserve evidence. Their
  dated conclusions are historical and do not override later physical results.
- The timeline below remains in place for provenance; headings marked
  **Historical** are not current instructions.

## Current finish-line sequence

1. Establish a red-capable optical panel oracle and run a one-variable A/B:
   current beam race versus a selected-TE-edge, row-zero, top-to-bottom sweep.
   Fail closed on stale/missing TE and forbid unsynchronized frame reuse.
2. Once a physically clean policy exists, keep the PSRAM ring canvas-only and
   patch fixed chrome plus the existing provisional ribbon tail in the mandatory
   internal staging pass. One ordered presentation gets one completion drain.
3. Make ink visual-first: present the newest provisional tail before synchronous
   chunk work, carry original touch timestamps, and measure optical tail latency.
4. Only after panel and ink baselines are stable, prototype the exact replay
   block index and repair-retention counters. Do not mix cache work into panel
   diagnosis.
5. Continue with Undo/Redo authority, SVG, autosave/persistence, settled AA,
   lifecycle parity, and final UI polish in the roadmap order.

## Historical accepted foundation

The following records what earlier automated and hardware rounds established.
Later 2026-08-15 glass evidence supersedes any claim here that pan was tear-free,
that drawing was non-lagging, or that DMA completion was physical completion.

On the physical ESP32-S3 with the deterministic seed-7 1,000-stroke document:

- vector operations are authoritative;
- pen and eraser operations, painter order, and revisions remain correct;
- live drawing is responsive at 400%;
- 25%, 50%, 100%, 200%, and 400% identities exist;
- the complete 25% overview and sparse world-aligned detail path work;
- cache misses use current overview fallback rather than checkerboards;
- paper-aware materialization lets the complete 100% world fit as 266 raw tiles
  plus 378 compact uniform identities, with zero fallback;
- the then-tested raw pool used 384 slots; a 16-stop 400% tour A/B measured zero
  return-trip refills at 384 versus 63 tiles and 409 ms of refill at 320;
- ordinary cached pan reuses framebuffer overlap and reaches first physical
  completion in about 30.6–30.7 ms;
- drawing while refinement is active works without stale publication;
- a separate contiguous 1.5 MiB export reserve allocates alongside the live
  document, cache, and workspaces;
- the final aggressive performance glass test committed 45 strokes with zero
  touch errors, presentation failures, authority mismatch, or corruption;
- the production toolbar now reflects the active V1-derived tool icon and uses
  compact tool, size, document, and round-swatch color popups;
- New requires confirmation, and Export shows band-by-band progress plus a
  visible saved state before USB takes over;
- battery status, the 25–400% zoom rail, and a live noninteractive minimap with
  a viewport rectangle render as fixed canvas overlays;
- physical UI checks confirmed reliable minimap updates and no remaining
  cached-pan ghosts around the zoom rail or minimap;
- the deterministic live-overlay circle gate measures 2.940 ms clear versus
  2.928 ms over the overlays, with zero chrome redraw work or failures;
- independent Fable high and Grok 4.6 high reviews found no remaining blocker
  after their findings were fixed.

Evidence:

1. [`vector_v2/GATE_1_RECEIPT_2026_08_13.md`](vector_v2/GATE_1_RECEIPT_2026_08_13.md)
2. [`vector_v2/GATE_1_CACHE_CLOSURE_2026_08_13.md`](vector_v2/GATE_1_CACHE_CLOSURE_2026_08_13.md)
3. [`vector_v2/hardware-receipts/gate1-paper-cache-scroller.log`](vector_v2/hardware-receipts/gate1-paper-cache-scroller.log)
4. [`vector_v2/hardware-receipts/gate1-final-glass.log`](vector_v2/hardware-receipts/gate1-final-glass.log)
5. [`vector_v2/hardware-receipts/PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md`](vector_v2/hardware-receipts/PERFORMANCE_SLICE_GLASS_VERDICT_2026_08_14.md)

The overall rendering-quality verdict remains **YELLOW** because settled
anti-aliasing is still open. The four-sample SSAA probe took about 808 ms and is
not the funded product path.

## Current measured debt

Phase 0 baselines live in
[`vector_v2/hardware-receipts/PERF_ROUND_2_BASELINES_2026_08_14.md`](vector_v2/hardware-receipts/PERF_ROUND_2_BASELINES_2026_08_14.md).
**Phase 1 (drawing latency) is closed at `1848cc6`**
([`vector_v2/hardware-receipts/DRAWING_LATENCY_CLOSURE_2026_08_14.md`](vector_v2/hardware-receipts/DRAWING_LATENCY_CLOSURE_2026_08_14.md)):

- warm-cache chunk commits fell from 130/88/58/34/21 ms worst
  (25/50/100/200/400%) to 13.8/13.5/12.6/12.2/11.8 ms via the active-zoom
  mutation policy plus a 10 ms wall-clock commit budget; the mixed-zoom gate
  is green at both slot counts and now part of the battery's final verdict;
- accepted trades: strokes drop affected cross-zoom tiles (0.14–0.26 s
  revisit refills; a full-width 25% XL stroke costs a full view refill), and
  budget spillage can leave a briefly blurry patch under heavy fresh ink
  until refinement settles it — the round-end glass check must confirm both;
- residual: the uninterruptible 25% overview band replay (~13.7 ms worst) is
  the new commit ceiling, above the 10–12 ms target but under the alarm;
- a latent bug was fixed along the way: `IncrementalAppendResult` carried
  empty world bounds (read after `publish()` cleared them), so end-of-gesture
  refreshes covered an empty region and the old zero-fallback checks were
  partly vacuous;
- the 320-versus-384 mixed draw/pan A/B is settled: identical drawing
  latency at both counts, and 384 keeps its 63-tile/411 ms return-trip
  retention win, so the 384-slot pool stays;
- warm pan is now about 67 ms per frame (≈15 FPS) with attribution
  15.1 ms scroll memmove + 7.3 ms exposed compose + 8.5 ms tear wait +
  19.7 ms present, plus the UI round's per-frame minimap chrome; the
  single-frame pan gates remain red at about 40.4 ms first-submit with
  7.8 ms chrome;
- memory ledger for later features: product boot at 384 slots leaves
  2.97 MiB free PSRAM; the 1.5 MiB export reserve leaves ~1.4 MiB spare
  (942 KiB proven free with the reserve held). SVG export budgets inside
  the existing reserve (streamed text from the resident operation log is
  cheaper than the PNG path's measured 51 KiB internal + 377 KiB PSRAM).
  Undo/Redo comes after the performance round and should ride the
  operation log (gesture ids are already stored per operation), paying in
  recompute rather than new storage; keep part of the spare pool unspent
  until its design is proven;
- the fresh cold 20-run distribution matches the accepted receipt
  (adversarial 400% p95 638 ms); the round target is −50% at every gated
  corpus and zoom;
- the export PNG task-watchdog warning reproduced in both full-gate
  captures and remains open reliability debt;
- anti-aliasing, persistence, Undo/Redo, autosave, low-power lifecycle
  parity, interactive minimap behavior, and final tap-target polish remain
  incomplete;
- SVG export still needs an eraser mask/compositing design; the encoder must
  budget inside the existing contiguous 1.5 MiB export reserve.

These are product and optimization tasks, not evidence for another rewrite.

## Historical: review-round board (2026-08-15 12:45, closed at c86f3ac)

> **Superseded by later glass evidence.** The software battery was green, but
> severe physical tearing and drawing lag falsified its presentation and latency
> closure claims. Preserve this section as the work timeline, not current truth.

Two external code reviews (26 findings) landed across four lanes; see
`vector_v2/hardware-receipts/REVIEW_ROUND_CLOSURE_2026_08_15.md`. All 27
battery gates green under the new, much stricter rules (pan gates and
tear discipline in the verdict, watchdogs fatal, PNG CRCs verified,
`scripts/esp32 ... verify` parses results). Host 76,624 assertions.

Known documented regression: PANSEQ 28.9 -> 41.5 ms avg (~24 FPS) from
the tear-free-by-construction pan presentation; measured split and
recovery plan in the board todo. Correctness first, by explicit call.

Open, in priority order:
1. Glass re-verdict on the new tear discipline (device carries the
   c86f3ac product build at 448 slots).
2. Pan prep cost recovery (bench-first; gates must stay green).
3. Edge white notches (bisect tools now exist: staging host model,
   edge-ink gate).
4. Cold -50% campaign, then flash L3 spike, SVG, Undo/Redo.

## Historical: organized board (2026-08-15 11:30, settled at efb1586)

Device: product build at `8c222e8`, 448 slots. PANSEQ 29.5/29.7 ms avg,
p50 27.95, all 26 gates green (`8c222e8-full-gate-448.log`). Note: the
`efb1586` battery log was captured at 384 slots (the harness script's own
slot default silently overrode the CMake default; found and fixed at
`8c222e8` when the first honest 512 run failed the export-reserve gate
and the pool corrected to 448).

Landed and verified today: draw-under-dock + center zoom (`ab43005`),
tear fixes round 1 (band wrap wait, overlay stale-compose fix, visible
commit blur removed, chunk cap 32) (`da99311`), 512-slot pool + repair
saturation guard + 48-row beam margin + painter/chrome overlay groundwork
(`efb1586`).

Open, in priority order:
1. GLASS RE-VERDICT on this build: vertical-pan tearing (margin 48 should
   fix), overlay-edge artifacts (expected REDUCED but a rare seam tear at
   overlay edges remains a known residual), blur-then-sharpen (should be
   gone), cold re-renders at 100% on dense docs (should stop churning
   after a pause; capacity still bounds dense-world warmth).
2. Edge white notches every N rows in filled areas (todo #22) — readback
   bisect: strip seams (44) vs tile boundaries (64).
3. Evil hairline crosshatch gate (todo #21, Alice's corpus request).
4. Overlay-seam presentation redesign — PARKED, bench-first. Measured
   designs: region-sequential (current) 28.9 ms with rare edge-seam
   revisit tear; row-major x-splits 36.6 (transaction bloat ~1 ms/txn);
   internal scratch strips 48.4 (3x strip traffic); draw-into-ring +
   backup restore 42.0 (overlay prep serialized ~20 ms — likely fixable:
   per-overlay draws currently redraw all three overlays and the minimap
   resample runs per rect; prep should be ~2-3 ms → ~30 ms seamless).
   Groundwork (painter origin, strip-overlay draw, host equivalence
   tests) is landed and pixel-exact.
5. Cold −50% campaign, then flash L3 spike (todo #20), SVG, Undo/Redo.

## Historical: mid-round resume state (2026-08-15, pan phase CLOSED at 4022917)

Pan floor met with margin: 67.3 → 28.1 ms avg (p50 26.95, p95 32.95), both
slot counts, TE boot flake root-fixed with runtime heal. See
`vector_v2/hardware-receipts/PAN_FLOOR_CLOSURE_2026_08_15.md`.

Idle cache repair CLOSED at `24a9fe9` (Alice's strongest glass complaint):
quiet moments now rebuild dropped tiles — neighbors, remembered zooms, and
the full 100% grid — in bounded idle slices; gate
`TINYDRAW_GATE1_IDLE_REPAIR` (588 damaged → 0 remaining, worst slice
7.5 ms) joined the battery verdict. See
`vector_v2/hardware-receipts/IDLE_REPAIR_CLOSURE_2026_08_15.md`. All 25
gates green.

Product findings from the glass session are also fixed at `ab43005`:
the zoom button now always centers the focused point (remembered-origin
reuse removed; regression-tested), and strokes continue under the bottom
toolbar into the hidden world rows (new `chrome_ink_bottom`; preview
rendering still clips at `chrome_input_bottom`). Battery re-run green
(`ab43005-full-gate-384.log`, 90 pass=1 lines, zero pass=0).

The DEVICE is flashed with the PRODUCT build at `ab43005` and boots a
fresh document (TE healthy: te_period_us=16794). Morning glass list:
1. Pan feel + tearing re-check (beam racing is new; math says safe).
2. Pan after a one-second pause must always be sharp (idle repair);
   `TINYDRAW_LIVE_REPAIR` lines should appear in telemetry.
3. Scribble XL at 100% over ink: stutter and transient-blur verdict.
4. New-document reset on this product build (harness artifact explained).
5. Zoom button should now center; strokes should continue under the dock
   (pan down after drawing across it).

Next: cold −50% campaign (Phase 3). Levers from the baselines: an
operation spatial index (~50–100 KiB; cold gates scan ~12,000 ops to
render ~591) and staging-frame overlap. Then export-watchdog receipt,
SVG export, Undo/Redo per the roadmap sequence.
The section below is the historical in-flight record.

## Superseded in-flight record (2026-08-15 early night)

Branch `feat/v2-performance-followup` at `2e07671`; tree clean; host tests
(68,896 assertions), host battery through release, and the ESP harness build
are green for HEAD, but `2e07671` has **not** run asan/tidy/cppcheck or been
flashed yet.

Pan-phase progress, all receipts in `vector_v2/hardware-receipts/`:

- Baseline 67.3 ms/frame → **50.2 ms flat** on hardware at `b76b992`
  (`b76b992-full-gate-384.log`): chrome and tear wait off pan frames
  (`aba02bc`), per-frame fast minimap (`5293823`), one panel drain per frame
  plus 16 K transfer strips (`b76b992`). Single-frame pan gates green again.
  Frame attribution: 15.0 scroll + 7.2 exposed + 4.3 tear + 23.4 push/drain.
- `2e07671` (unflashed) adds the toroidal ring (kills the 15 ms scroll
  memmove; de-rotation folded into transport byte-swap) and keeps cached pan
  engaged in the wild: Alice's manual session (`b76b992-manual-glass.log`)
  measured **reused=0 on all 386 real pan frames** because fallback pixels
  and composition-epoch drift disqualified the frame; both are quality-only
  and no longer break reuse. Expected next PANSEQ ~35 ms; product pans
  should finally take the cached path.
- Tear-wait elision (8 ms window; writer catches wrapped beam only past
  ~13 ms) is **glass-confirmed**: no tearing/ghosting under violent 100%/400%
  scrubbing (Alice, 23:49–00:00 London).

Next steps, in order:

1. Flash `2e07671` harness @384, capture full battery, check
   `TINYDRAW_GATE1_PANSEQ` (scroll_avg should collapse to ~0) and that all
   other gates stay green; then asan/tidy/cppcheck/format plus raster-V1 and
   product builds; commit receipts.
2. Close the remaining gap to the 33.3 ms floor (candidates: strip-fused
   exposed compose during the push sweep, minimap push inside the drain
   window, raising kMaximumCachedPanDelta for fast swipes now that deltas
   are cheap).
3. **Idle cache repair** (Alice's strongest glass complaint — constant cold
   redraws while panning): when input is idle, proactively re-produce
   dropped/evicted tiles around the visited neighborhood at every zoom; the
   full 100% world fits in 384 slots so edge-panning at 100% should never
   cold-render after idle. Also consider a wild-pan gate (pan into cold
   territory) since PANSEQ only covers the prewarmed case.
4. Queued findings from glass: allow drawing under the bottom toolbar
   (commit ink beneath chrome, render clipped); zoom button should anchor at
   viewport center, not top-left; verify New-document reset on a product
   build (the top-left blue artifact was the gate harness's own long-gesture
   test stroke — explained, not a product bug).
5. Morning glass list: ring-build pan feel + tearing re-check, budgeted-
   commit blur verdict while scribbling XL at 100%, New on product build.

Device state: flashed with the `b76b992` gate-harness build at 384 slots;
Alice's drawing from the glass session is on it. The serial port is free
(the manual capture was stopped and saved).

## Historical: pre-regression next sequence

1. Raise warm pan to a 30 FPS floor (33.3 ms frames, margin preferred):
   ring/offset frame addressing (−15 ms memmove), tear-wait/compose overlap,
   and chrome off the per-frame path; acceptance is `TINYDRAW_GATE1_PANSEQ`
   at or below 33.3 ms per frame and the single-frame pan gates green.
2. Run the cold −50% campaign from the fresh distribution (adversarial 400%
   p95 638 ms → 319 ms). Candidate levers: an operation spatial index over
   the log (cold gates scan 12,000 operations to render 591) and a second
   322 KiB staging frame for compose/DMA overlap.
3. Confirm at the round-end glass check that budgeted-commit transient
   fallback and cross-zoom revisit refills feel acceptable while drawing.
4. Capture a clean hardware export receipt proving watchdog-safe encoding;
   reliability matters more than export speed.
5. Investigate SVG eraser semantics, then continue through anti-aliasing,
   persistence, Undo/Redo, lifecycle parity, and remaining UI polish.

## Repository and coexistence decision

Use a Vector V2 island in this repository—a strangler migration, not a blank
rewrite:

- platform-independent V2 modules live in `vector_v2/`;
- V2 ESP adapters and coordination stay separate from legacy `hardware_app.cpp`;
- stable platform-neutral mechanisms are reused from `core/` by dependency and
  are never copied;
- V1 receives only regression/hardware-preservation work while new product
  behavior lands in V2;
- both firmware variants must build and remain testable until promotion;
- the default switches only after V2 feature parity and final comparison;
- retain an explicit legacy raster build after promotion.

Do not mix V2 tile/document state into `WorldCanvas`, `FirmwareCanvas`, or the
3×3 raster coordinator. Do not add V2 `#ifdef` paths to the V1 interaction loop.

## Target architecture

```text
Vector operation log (authoritative)
        │
        ├── complete 368×448 RGB565 overview at 25%
        └── sparse world-aligned materialization at 50–400%
              ├── compact uniform/paper catalog
              └── 448 raw 64×64 RGB565 slots
                    │
             MaterializedCanvas
                    │
             DisplayScheduler + framebuffer reuse
                    │
                  AMOLED
```

Committed geometry:

- world: 1472×1792 units;
- zoom levels: 25%, 50%, 100%, 200%, and 400%;
- 800% is a stretch goal only;
- overview: 368×448 RGB565, 329,728 bytes.

## Nonnegotiable guardrails

- No clean-room rewrite.
- No camera-aligned 3×3 atlas development.
- No V2 state in `hardware_app.cpp`, `WorldCanvas`, or `FirmwareCanvas`.
- No four separately stored simplified stroke versions.
- No hidden allocation in Vector V2 state modules.
- No speculative second-core concurrency without a measured independent need.
- No broad cosmetic V1 refactor before a real replacement seam exists.
- No deleting V1 before V2 feature parity and promotion.
- No new overlapping handoff/source-of-truth documents: update this file and
  `V2_ROADMAP.md`.

## Validation baseline

For production changes:

```sh
./scripts/dev test
./scripts/dev release
./scripts/dev asan
./scripts/dev format-check
./scripts/dev tidy
./scripts/dev cppcheck
git diff --check
```

ESP integration must also build both raster V1 and vector V2 targets. Physical
hardware remains authoritative for touch latency, PSRAM/fragmentation, DMA,
panel behavior, power, and USB export.

## Historical evidence

Prototype decisions and rejected mechanisms remain in:

- [`docs/archive/2026-08-raster-and-vector-prototypes/PROTOTYPE_EXIT.md`](docs/archive/2026-08-raster-and-vector-prototypes/PROTOTYPE_EXIT.md)
- [`docs/archive/2026-08-raster-and-vector-prototypes/SECOND_REVIEW_ARCHITECTURE_ASSESSMENT.md`](docs/archive/2026-08-raster-and-vector-prototypes/SECOND_REVIEW_ARCHITECTURE_ASSESSMENT.md)
- [`docs/archive/2026-08-raster-and-vector-prototypes/VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md`](docs/archive/2026-08-raster-and-vector-prototypes/VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md)
- [`docs/archive/2026-08-vector-v2-foundation/`](docs/archive/2026-08-vector-v2-foundation/)

Prototype-era handoffs and Gate 1 plans are historical inputs. They no longer
override the V2 roadmap.
