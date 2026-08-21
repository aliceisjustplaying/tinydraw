# TinyDraw archaeology supplement

Research date: 2026-08-19. This file supplies the detailed chronology, forgotten-detail list, recommended historical demonstrations, and unresolved questions that sit underneath the 29-beat causal timeline in `git-history.md`.

Confidence labels: **CONFIRMED** = direct commit/receipt evidence; **HIGH CONFIDENCE INFERENCE** = strongly implied by chronology and contemporary notes; **UNCERTAIN** = evidence is incomplete or conflicting; **MEMORY NEEDED** = only the owner can resolve it.

## Detailed chronology

### 2026-08-09 — native-first Raster V1

- 18:20–18:42 — Native C++20 core, real-size macOS SDL shell, timestamp-aware input, replay golden, and Perfect Freehand oracle (`4486705`–`88af0d7`). **CONFIRMED**
- 18:44–19:36 — Perfect Freehand points and outline are ported; direct geometry becomes coverage-tile rendering; overlap and join seams are fixed (`acf779a`–`e319928`). **CONFIRMED**
- 20:25–20:29 — Append-stable ribbon streaming lands and the host switches to it (`89b6743`, `c07d183`). **CONFIRMED**
- 20:38–21:08 — Toolbar and physical-scale UI are added; this is product/demo work, not a performance pivot (`cd1bba2`–`a293465`). **CONFIRMED**
- 21:15–21:20 — Dirty-tile incremental raster replaces whole-stroke replay (`c2d3d25`–`5d01808`); frozen host XL work later records 4,479→220 ms. **CONFIRMED**
- 21:43–22:44 — Headless then graphical QEMU, 8 MiB modeled PSRAM, direct dirty submissions, and memory-traffic budgets (`1e685bb`–`50c9f30`). **CONFIRMED**
- 22:45–23:07 — Coverage tiles are loaded once; Undo moves to ten dirty-tile before-image entries in PSRAM (`2a238d1`–`fd7aec2`). **CONFIRMED**
- 23:38–23:46 — Physical/QEMU configs split, DMA lifetime is made synchronous-safe, and Undo eviction is hardened (`6a59cf1`–`69852bc`). These commits precede actual board bring-up. **CONFIRMED**

### 2026-08-10 — physical Raster V1 and two display architectures

- 09:49 — First Waveshare V2 display/touch bring-up (`c2a6dc9`). The first physical XL capture averages 19.9 ms, peaks at 72.9 ms, and lifts in 105.1 ms. **CONFIRMED**
- 09:49–11:15 — Dirty presentation, release debounce, internal-SRAM coverage, and tight stroke bounds attack the new device costs (`d1a2a45`–`3c74673`). **CONFIRMED**
- 11:33–12:18 — Diagonal-tile skipping and the first smoothing attempt are reverted; touch moves to core two and cadence is measured before a replacement smoother lands (`b63ff9a`–`d7ec88f`). **CONFIRMED**
- 12:46–15:39 — Hardware-sized controls, New confirmation, region-only overlays, batched Undo presentation, and round joins refine the physical build (`925ee0a`–`ca41dc2`). **CONFIRMED**
- 16:34–17:14 — Fixed world, hand tool, failed sparse-pan plan, no-copy viewport, direct world streaming, paired byte swap, and larger DMA chunks (`3d2708c`–`07c7f0b`). **CONFIRMED**
- 17:27–19:38 — Captive Wi-Fi PNG export is built, buffered/compressed/chunked, then removed (`fccb68a`–`ec9ad93`). Reliability on iOS, not an incomplete encoder, killed it. **CONFIRMED**
- 19:44 — CO5300 transfer stabilization follows random colored lines at the requested 80 MHz point (`7cd16be`). Later probing shows the stable 50/60 settings were both 40 MHz actual. **CONFIRMED**
- 20:21–23:26 — RP2350 port: display/touch, native RGB565, fixed-point coverage, full-width bands, second-core touch; arbitrary partial refresh and several input-rate/lift ideas are reverted (`efc372d`–`2e5f532`). **CONFIRMED**

### 2026-08-11 — Raster V1 completion and vector pivot

- 10:09–10:48 — Bounded demo tape and physical record/replay are added (`3f2362b`–`1e009ab`). **CONFIRMED**
- 13:53–16:25 — Tile-aligned snapshots and dirty-sector background autosave land; removing the canvas shadow makes the 3×3 raster world fit (`78b1b7f`–`5249033`). **CONFIRMED**
- 14:40–15:39 — Battery/PMU status and shutdown consume many small UI commits; odd-width transfer corruption forces aligned bounds (`74817b7`–`1187e2f`). **CONFIRMED**
- 16:41–18:05 — Low-memory PNGenc, streaming flash PNG, synthetic FAT16, USB exposure (`c97b5ca`–`a5a0b37`). **CONFIRMED**
- 18:19–19:45 — RTC initialization and one-shot NTP supply export timestamps (`054149b`–`694b13a`). **CONFIRMED**
- 19:47–20:02 — Infinite-canvas brief, bounded vector document, camera, viewport rebuild, live vector recording, and benchmark matrix (`a78590d`–`8bf7156`). **CONFIRMED**
- 20:11–21:33 — Row copies, tile-major composition, division/sqrt hoists, dirty coverage rectangles, and two-core rendering attack vector rebuild cost (`15247e3`–`45a5be8`). **CONFIRMED**
- 22:09–23:28 — Cached pan/progressive zoom prototype exposes the main architectural mismatch: vector pan is 95–208 ms versus Raster V1 at 25.45 ms (`35d38e7`, `370710b`). **CONFIRMED**

### 2026-08-12 — prototype, falsification, production reset

- 09:10–10:57 — Cancellation and materialized zoom/drawing arrive; hardware results expose multi-second zoom and publication delay (`cbe314d`–`3f79df9`). **CONFIRMED**
- 11:47–14:30 — Valid zoom publication, provenance guard, external architecture review, and removal of interaction-time clear (`87b475b`–`63d99b9`). The review A/B saves 91–105 ms per zoom. **CONFIRMED**
- 14:40–14:49 — Center-out strips plus a realistic 1,000-stroke document establish physical endpoints: first 6.6–16.2 ms, complete fallback 39–57 ms (`1469b8a`–`40f4615`). **CONFIRMED**
- 14:52–17:50 — Analytic settled capsules, cancellation-safe fallback, LOD, review hardening, edge-band provenance, pan runway (`48ac59b`–`4fc345e`). Early “under 500 ms” LOD claims are revoked because geometry and endpoint were wrong. **CONFIRMED**
- 20:31–21:39 — Review fixes and diagnostic telemetry close the prototype; the controlled PSRAM/internal scratch A/B falsifies the predicted ≥40% win (actual wall −1.69%) (`87ff28f`–`e311a46`). **CONFIRMED**
- 21:44 — Handoff explicitly retires the double camera-aligned 3×3 atlas as product architecture (`3dec0ea`). **CONFIRMED**
- 22:38–23:47 — Production begins with a materialized canvas, complete overview fallback, transactional publication, and a hardware overview walk (`eadc75f`–`c3f8e20`). **CONFIRMED**

### 2026-08-13 — production Vector V2 foundation

- 00:08–03:45 — Pin ownership, staged revisions, separated panel/touch transports, incremental operation authority, bounded display scheduling, and stale/in-flight guards are built in small verified steps (`1edf0a9`–`563ee25`). **CONFIRMED**
- 03:53–06:34 — Bounded input operations, ordered replay ranges, owned LOD storage, and bounded overview publication (`fc57df2`–`4ea0620`). **CONFIRMED**
- 08:28–12:38 — Real-touch workload, production integration slice, provisional/refined quality tiers, resumable producer, Gate 1 p95 hardware receipts (`97f2575`–`dd59c6c`). **CONFIRMED**
- 13:50–17:27 — Review fixes, oversized-work bounds, retained multi-zoom views, disjoint pan destinations, grouped publication, and a 320-slot memory receipt (`b3d7870`–`512f424`). **CONFIRMED**
- 18:20–19:43 — Tile census, implicit paper, compact cache mutation, framebuffer-overlap pan, cache/export reserve, reviewed invariants (`170d7ec`–`7e6c81c`). **CONFIRMED**
- 20:30–20:56 — Foundation graduates to V2; Raster V1 and Vector V2 are named and separated; prototype is quarantined (`3fd6f76`–`0bdd822`). **CONFIRMED**
- 22:05–23:55 — Interaction telemetry, navigation, bounded lift, resumable raster steps, protected recent viewports, cold compute/presentation separation (`a91630d`–`593e21b`). **CONFIRMED**

### 2026-08-14 — cold replay, warm drawing, and pan floor

- 00:58–01:35 — TE measurement and full-frame scan synchronization arrive (`8d422f1`, `2d547f0`), but the initial software model will later be falsified on glass. **CONFIRMED**
- 08:45–09:20 — Cold wall is measured; culling, no redundant subdivision, projection hoists, scanline spans, recurrence, collinear coalescing, and removal of fixed delay get below one second (`9542714`–`52ec99a`). **CONFIRMED**
- 10:30–13:01 — Telemetry leaves input path; adversarial 400% corpus, row clipping, second-core V2 touch, cached pan reuse, finalized-pixel/run skipping, rejection reporting, census rig (`9f2234d`–`91f0c85`). **CONFIRMED**
- 13:50–16:22 — Transactional pan reuse, touch-transition preservation, chained strokes, exact row saturation, and in-place interactive chunks close the first cold/long-stroke phase (`57efa44`–`0cba4ad`). **CONFIRMED**
- 17:28–18:11 — V2 PNG export, measured cold deadlines, cache growth to 384, and 400% tour gate (`7302963`–`35dae94`). **CONFIRMED**
- 19:24–21:27 — V2 chrome, minimap, popup and confirmation UI become product-like (`929b75e`–`2cad0f9`). **CONFIRMED**
- 22:25–23:25 — Mixed-zoom draw gate reveals 19 ms synchronous fanout; active-zoom-only mutation and 10 ms budgets make the gate look green but later glass invalidates the closure (`95eb848`–`024f778`). **CONFIRMED**
- 23:35 onward — Chrome/TE leave cached pan, minimap resampling and one drain per frame lower transport overhead (`aba02bc`–`b76b992`). **CONFIRMED**

### 2026-08-15 — ring buffer, tearing falsification, panel physics

- 00:05–00:57 — Toroidal frame ring, beam-race sweep, dead-TE healing, and idle cache repair (`2e07671`–`24a9fe9`). Automated pan improves, but physical correctness is not yet established. **CONFIRMED**
- 10:40 — Glass forces the correction: pan tearing, overlay stripes, and visible commit blur are fixed together (`da99311`). **CONFIRMED**
- 11:27–12:45 — Pool/review fixes introduce performance regressions, the expanded verify gate catches them, and most pan cost is recovered (`efb1586`–`f66c808`). **CONFIRMED**
- 19:20–22:32 — Boundary-synchronized sweep, panel characterization, pre-registered 240 fps optical cells, positive control, and 1,495-frame product classification establish real panel limits (`6057f9c`–`1cea70e`). **CONFIRMED**
- 23:17–23:29 — Chrome moves into the compositor and fixed sprites are staged/cached (`4048d3a`–`1560ce9`). **CONFIRMED**
- 23:53 and 00:29 next day — Two attempts to overlap staging with TE wait are reverted (`b5bdd78`/`2b8776a`, `c8a4755`/`2e267ec`). **CONFIRMED**

#### The Fuji X-T5 session, recovered

The exact conversation is `$HOME/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-15T19-28-19-209Z_01a006e5-8109-7f08-85ca-ae3f19b8d435.jsonl`. At 19:41:57Z the owner realizes the X-T5 can record 1080/240; at 20:42:17Z a flicker-free 1/1024 shutter setup is found; at 20:53:42Z the two-minute `$HOME/Desktop/DSCF0665.MOV` is supplied. Later observations pin a tear near the top edge of the minus button (22:59:32Z), then see it move about two-thirds down the minimap after burst behavior changes (23:06:19Z), and finally report “tearing seems to be gone” (23:41:11Z). **CONFIRMED**

The camera was needed because every GETSCANLINE/control-read probe returned zero and the software beam-race gates could certify their own timing model while the glass visibly tore. The 240 fps footage supplied an independent physical oracle: it falsified the beam-race premise, localized the tear as presentation ordering changed, and supported the boundary/rising-edge full-frame sequence later paired with ordered strip staging and cached chrome. The pre-registered positive control tore, while the accepted product sequence was classified clean across 1,495 analyzed frames. **CONFIRMED**

### 2026-08-16 — cold compute and the authority-only pivot

- 00:25 — Exact ribbon geometry is first streamed as SVG (`8209d28`); product export integration follows on Aug 17. **CONFIRMED**
- 09:20–10:06 — Chrome cache lifetimes split; tear-free glass is recorded; live ink becomes visual-first (`52f63e0`–`931c7cf`). **CONFIRMED**
- 11:32–13:24 — Frozen combined cold benchmark; stateless windows, device-native arithmetic, unit/op row sweeps, caller-specific painters cut 400% from 1,269 to 669 ms (`a560d20`–`abb780c`). **CONFIRMED**
- 14:15–15:24 — Ink trace recorder, owner corpus, under-overlay reference, angularity analysis, rerender causes, and production-path replay supply missing observability (`af9809a`–`0c6ffe1`). **CONFIRMED**
- 15:52–18:34 — Direct supertask publication, one y-sorted sweep per operation, panel loop in internal RAM, O(1) raw-slot metadata close Stage B at about 507 ms (`f2f6da7`–`224f1b5`). **CONFIRMED**
- 20:31–20:35 — Cause counters falsify the suspected mid-stroke fallback-drop explanation; unwarmed benchmark views caused the report (`fb2a05b`–`39352d6`). **CONFIRMED**
- 21:01–21:36 — Pending authority range, authority-only commit, exact overlay, and empty-poll drains change the system model (`afa3239`–`902d016`). Worst append 19,324→173 µs; work moves to bounded idle absorption. **CONFIRMED**
- 21:44–22:35 — Float reference identifies sample quantization as the V1/V2 jaggedness cause; sixteenth-world units and streamline 0.4 land; raw provisional geometry reaches the fingertip (`f813a8f`–`3df6f0f`). **CONFIRMED**
- 23:05 — Idle absorption now retains every remembered zoom; visible revisit refill falls from 188–326 ms to about 0.4 ms (`50ba72f`/`d5097b1`). **CONFIRMED**
- 23:26–23:55 — Settled analytic AA becomes an idle quality pass, then receives faster slices and 25% presentation support (`bfebbcd`–`602fad4`). **CONFIRMED**

### 2026-08-17 — feature completion against logical authority

- 00:56–01:15 — The SVG core becomes a product-integrated detailed export; zoom-overlay pan behavior follows (`f85af8f`, `2c7ef65`). **CONFIRMED**
- 01:09–18:04 — One-frame color dialog, minimap navigation and input routing, on-demand NTP, logical one-path-per-stroke SVG, controlled owner ink capture; several minimap placements are tried/reverted. **CONFIRMED**
- 18:29–19:02 — Settled PNG joins SVG on a two-file FAT16 volume; export exit and progress are integrated (`6b6cb05`–`0d2f0a2`). **CONFIRMED**
- 19:05–19:39 — Coherent reads and whole-stroke authority history replace raster before-images for V2 (`6dff44b`–`f771064`). **CONFIRMED**
- 19:46–20:28 — Checkpoints, journal, aligned commits, background autosave, recovery acceptance (`95f9899`–`60682d7`). **CONFIRMED**
- 20:57–22:12 — Cleanup removes unused paths and tags feature completion (`f0945e4`–`f8873c3`; tag `v2-feature-complete-pre-cleanup` at `3f23c09`). **CONFIRMED**

### 2026-08-18 — cooperative latency, honest memory, external review

- 11:35–11:52 — Spatial replay index, bulk fills, ring-local updates, touch queue resync, touch-preemptive background work (`e1c80d2`–`884ad6`). **CONFIRMED**
- 12:11–13:31 — Composition, absorption, settling, metadata, and publication become resumable; stale work can be abandoned; cooperative closure is recorded (`cb5b2d7`–`2a9fc86`). **CONFIRMED**
- 14:27–15:35 — Autosave staging, pan-direction repair priority, sparse history candidates, bounded membership scans, touched AA spans, raster IRAM placement (`0a7e0d2`–`7ef326a`). **CONFIRMED**
- 16:37–17:41 — Settled kernels move to IRAM; occupancy rebuilds after history; AA microprobes and streamline glass are accepted/rejected with receipts (`0be84f4`–`db5f8ec`). **CONFIRMED**
- 18:40–20:02 — Source/doc cleanup and module decomposition preserve behavior while making the release review tractable (`3b49dc1`–`8ad5cfc`). **CONFIRMED**
- 21:28–22:49 — Measured export peak funds 604 tiles; full flash map; preserved-tile history swap; settled-AA saturation/white fast paths; honest history hourglass (`286fa4b`–`cdcc8d5`). **CONFIRMED**

### 2026-08-19 — torture document and same-revision release

- 00:01–00:32 — Long-chord settled spans and dense aggregation are accepted; a real device journal is added as torture corpus; the one-sample dot defect is documented (`a2dce6a`–`8a24436`). **CONFIRMED**
- 00:56–01:16 — Tap dots, popup contacts, SVG eraser masks, gesture joins, modal byte order, and opaque-AA fast path are fixed (`c6be49a`–`8461ae5`). **CONFIRMED**
- 01:25–02:15 — History overdraw/saturation/occupancy, rejected render groups/batches, fewer cold resumptions, redo occupancy, duplicate cold preflight removal (`2be0a0f`–`b428691`). **CONFIRMED**
- 09:46–11:21 — SVG seam teeth, popup fall-through, USB serial return, phantom top-edge taps, and contained export exit are closed (`b6630d6`–`2c8b876`). **CONFIRMED**
- 11:53–12:23 — Same-revision release record, candidate closure, and `v2` release marker (`ed1047c`, `9d91b81`, `fd05d7d`). The physical revision under test is `a5db58d`. **CONFIRMED**

## “You probably forgot this” list

1. The Retina input bug scaled SDL coordinates twice; a bottom-right click landed near the center before coordinate-space ownership was fixed (`03af023`, `9023ac`). **CONFIRMED**
2. The first curved firmware replay overflowed ESP-IDF’s default task stack, so large ribbon scratch moved out of stack storage (`e319928`; `FINDINGS.md`). **CONFIRMED**
3. The self-crossing-stroke seam was measurable as 207/255 opacity before unioning coverage; it became 255/255 (`FINDINGS.md`). **CONFIRMED**
4. Requested CO5300 50 MHz and 60 MHz modes were both 40 MHz actual. Several older “60 MHz” conclusions use the wrong physical label (`CO5300_PANEL_LIMITS_2026-08-15.md`). **CONFIRMED**
5. GETSCANLINE and every panel control-read probe returned zero. The high-speed camera was necessary because the panel offered no usable scanline oracle. **CONFIRMED**
6. The beam-race build was software-green and physically wrong. It is the cleanest example of instrumentation validating its own false model. **CONFIRMED**
7. The Block B optical protocol used an X-T5 at 1080/240 and a deliberately tearing positive control; the accepted product sequence stayed clean across 1,495 analyzed frames. **CONFIRMED**
8. A 40 KiB AA workspace cost about 9 ms merely by being allocated mid-heap; moving it last removed the loss. PSRAM cache-set placement made allocation order part of the contract (`docs/MEMORY_MAP.md`). **CONFIRMED**
9. Moving the main settled scratch from PSRAM to internal RAM was predicted to save ≥40% and saved only 1.69% wall. The renderer was arithmetic/work-organization bound. **CONFIRMED**
10. The 1.5 MiB export “reserve” was a synthetic malloc/free test. Real concurrent export peak was 291,484 bytes; correcting the premise funded 156 additional tile slots. **CONFIRMED**
11. The first convincing 200% LOD result timed “cache ready” before final physical transfer and used a simplifier that could erase loops, hairpins, pressure peaks, and eraser dabs. It is not a win. **CONFIRMED**
12. A 12-row bottom-band hole could certify old-camera pixels after zoom because 372 visible rows do not divide by 32. Grok 4.6 found it; a 1–12 px downward pan exposed it. **CONFIRMED**
13. A 103-refusal / roughly 12-second mutation-repair episode helped kill the camera-aligned vector atlas. The prototype was not merely memory-heavy; its validity semantics were wrong. **CONFIRMED**
14. The apparent mid-stroke pixelation diagnosis was falsified by all-zero fallback-drop counters. Unwarmed benchmark views created the symptom report. **CONFIRMED**
15. Streamline 0.4 looked cheap in a synthetic metric, but at 1 kHz the filtered tail could lag tens of pixels. The fix was split authority: filtered committed/SVG geometry plus a raw provisional fingertip. **CONFIRMED**
16. “Déjà vu” took three iterations: retaining raw tiles did nothing because uniform tiles were still dropped; remembered views and a separate idle budget were also required. **CONFIRMED**
17. The one-sample stroke was a “Schrödinger dot”: replay/export paths disagreed about whether the tap existed until the owner’s real journal corpus made it reproducible (`ONE_SAMPLE_STROKE_DOT_BUG_2026_08_19.md`). **CONFIRMED**
18. Two whole-image release readbacks lost serial mid-transfer, but each reset into healthy same-revision product firmware. Release evidence therefore relies on verified flash write plus clean boots, not full readback (`VECTOR_V2_RELEASE_2026_08_19.md`). **CONFIRMED**

## Historical builds worth demonstrating (maximum five)

No old firmware was flashed during this archaeology pass. The attached board was left on the released product image; existing receipts already settle the key numerical questions.

| Priority | Historical build | Compare with | Exact recording | Why / risk / effort |
|---:|---|---|---|---|
| 1 | `afa3239` (the parent of `79ce37b`) | `79ce37b` and current `v2` | Same warmed mixed-zoom drawing: pen-down, 32-sample chunks, lift; record 240 fps glass plus serial phase trace | Best visible causal pivot: synchronous committed pixels versus authority-only overlay. Medium effort; app-only flash should preserve data partitions, but use a safe worktree and return to same-revision product. |
| 2 | `c86f3ac` beam-race/tearing state | `52f63e0` or current `v2` | Re-run the pre-registered Block B rising/positive-control cycle at 1080/240 | Shows software-green tearing versus physically clean ordered presentation. High effort, high explanatory value; use the existing protocol and never leave old firmware installed. |
| 3 | Before `50ba72f` | `50ba72f` or current `v2` | Warm all zooms, draw an XL stroke at each zoom, cycle 50/100/200%, capture visible refill | The 188–326 ms revisit “déjà vu” is immediately legible. Medium effort; label the later ~0.4 ms as revisit lookup/presentation with work shifted to idle. |
| 4 | `35d38e7` / `370710b` | Raster V1 at the same commit era and current V2 | Repeat fixed pan gestures on the realistic handwriting document; record checkerboards/refusals and timings | Demonstrates why vector authority required materialized raster cache. Medium-high effort; prototype drivers may need archived scripts/logs from the pre-cleanup tag. |
| 5 | Pre-`c2d3d25` | `f336303` | Native 500-point XL replay with block-time trace and rendered output | Cleanest cheap demonstration of quadratic whole-stroke replay versus bounded dirty work. Low effort and no hardware risk; this was not rebuilt in this pass. |

## Open questions

1. **Which exact commit was the meetup build? — MEMORY NEEDED.** No tracked commit, document, or session-search hit names a meetup. `ca41dc2`/`53269b3` is the likely demo checkpoint, but event attribution is unsupported.
2. **What did “mechanistic simplicity” refer to at the time? — MEMORY NEEDED.** The later design embodies the idea, and “demoscene mindset” appears in the external-review brief, but the exact phrase is absent from tracked evidence.
3. **Was the board physically unavailable for all Aug-9 work, and exactly when did it arrive? — HIGH CONFIDENCE INFERENCE.** Docs say the native loop preceded the board and first physical bring-up is `c2a6dc9` on Aug 10 09:49; delivery time is not recorded.
4. **What was the actual stable Raster V1 pan time after dropping to 40 MHz effective? — UNCERTAIN.** The 9.8–10.1 ms figure belongs to the unstable requested-80 MHz state; the stable path was not recaptured.
5. **What is current same-tree product pan latency after all release changes? — UNCERTAIN.** Historical accepted p95 exists, but final same-revision distribution was not retained.
6. **What is end-to-end optical pen-down-to-photon latency? — UNCERTAIN.** Software endpoints exclude touch-controller scan phase, panel scan position, and optical persistence.
7. **How often do preserved Undo tiles survive real navigation? — UNCERTAIN.** Policy intentionally evicts history before current pan tiles, but no prolonged product-use distribution exists.
8. **Does the final 400% general cold path hold below 500 ms across 20 reset-separated normal-product runs with real journal activity? — UNCERTAIN.** The final gate passes at 492.793 ms; the stronger distribution remains requested.
9. **How much committed work is lost under timed power cuts? — UNCERTAIN.** Recovery is corruption/truncation tested, but destructive time-bounded hardware testing was deferred.
10. **Are any rare white notch, blue dot, or byte-swap artifacts still present? — UNCERTAIN.** Historical incidents have fixes and torture tests; durable recurrence needs optical evidence.
11. **How much AI usage happened in web-only reviews? — MEMORY NEEDED.** Local logs cannot account for external web UI usage or its billing.
12. **What was the actual subscription spend attributable to this project? — MEMORY NEEDED.** Session metadata identifies plan/rate-limit state, not invoices or marginal project cost.
