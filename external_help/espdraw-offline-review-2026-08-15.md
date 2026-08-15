# TinyDraw Vector V2 offline review — physical glass regressions

**Review date:** 2026-08-15  
**Packet:** `offline-review-packet-2026-08-15-c86f3ac-glass-regressions`  
**Reviewed branch / HEAD:** `feat/v2-performance-followup` / `f66c808`  
**Last product-code commit:** `c86f3ac` (`c86f3ac..f66c808` is documentation/receipt-only)  
**Primary comparison:** `8b02da0..c86f3ac`

## Executive verdict

The current Vector V2 pan presenter is **not physically correct by construction**. Its `tear_synchronized` result proves only that its own timing model completed. The model does not prove TE polarity or phase, visible scan position, controller write-to-visible semantics, instantaneous writer speed, or safety of the second wrapped band. The manual glass verdict—severe tearing at both 100% and 400% with zero reported synchronization failures—therefore falsifies the oracle, not the tester. See `REVIEW_BRIEF.md:29-75`, `METRICS.md:49-70`, and `esp32/main/vector_v2/vector_v2_presenter.cpp:573-679`.

There is also a strong mechanical reason to distrust the specific “writer cannot catch the beam” premise. The code drives 60 MHz quad QSPI at 16 bits per pixel. Its active payload ceiling is about **15.0 million pixels/s**, or **40.8 full-width rows/ms**, while the modeled beam advances **26.7 rows/ms**. A 44-row transfer can therefore gain about 15 rows on the beam during its payload interval; a 48-row lead can theoretically disappear in roughly **3.2 strips / 3.4 ms**, before accounting for controller behavior. This does not by itself identify the exact physical tear mechanism, because command gaps, driver pacing, and CO5300 buffering remain unmeasured. It does prove that average frame wall time cannot establish the no-catch invariant. See `esp32/main/co5300_panel_transport.cpp:28-35,142-166,378-402` and `context/PAN_FLOOR_CLOSURE_2026_08_15.md:44-47,63-66`.

Drawing is a product blocker independently of presentation tearing. The advertised 10 ms commit budget is not a true preemption bound: the 25% overview copy/raster is indivisible, visible tile work is deliberately exempt, and 25% supplies no priority view, so the only large work item runs outside the only budget check. The app then commits authority before showing the next live update and intentionally renders only committed ribbon geometry, accepting one smoothing-window of lag. The observed 15.5–35.8 ms chunk maxima and 92–143 ms loop gaps are expected consequences of this ordering, not an unexplained touch failure. See `vector_v2/src/incremental_document.cpp:12-40,225-313,325-360`, `esp32/main/vector_v2/vector_v2_app.cpp:324-341,981-1027,1052-1111`, and `METRICS.md:72-83`.

The 28.9 → 41.5/42.0 ms pan regression is mostly avoidable work: before the first panel submission, every fixed overlay gathers its ring backdrop, copies it to internal scratch, redraws, scatters back into PSRAM, and later restores the saved canvas. That serial preflight closely matches the missing 12–18 ms. The elegant recovery is not another timing exception; it is to keep the ring as pure canvas and patch fixed overlays plus volatile live ink into the internal DMA staging pass that already de-rotates and byte-swaps every outgoing pixel. See `esp32/main/vector_v2/vector_v2_presenter.cpp:519-571,669-675,803-878` and `esp32/main/co5300_panel_transport.cpp:386-395`.

The safest product fallback in this packet is **Raster V1**, after a brief current-build hardware revalidation. There is no demonstrated tear-free Vector V2 commit to roll back to: earlier V2 paths had seams and retained the same unproven panel-timing premise. Raster V1 remains the default/shipping firmware and has 2.5–3.4 ms long-stroke update measurements. See `README.md:13-31`, `context/PROJECT_STATE.md:9-18`, and `context/V2_ROADMAP.md:12-22`.

### Product-target status

| Target | Current state | Verdict | Credible path |
|---|---:|---|---|
| Drawing must not lag | Manual 25% chunks 15.5–35.8 ms; loop gaps 92–143 ms; intentional provisional-tail lag | **Fail / product blocker** | Visual-first volatile tail; authority/materialization as a preemptible state machine; one staged presentation and one drain |
| Pan ≥24 FPS, target 30 FPS | 41.539/42.010 ms averages ≈24.1/23.8 FPS, with severe tearing | **Correctness fail; throughput at floor only** | Remove overlay PSRAM round trips; pure ring + staging compositor; physically validated presentation policy |
| Cold 400% <500 ms worst accepted, target ~300 ms | Adversarial p95 628.2 ms | **Fail** | Spatial/block replay index, retain overlapping work, priority publication; needs 20.4% reduction for ceiling and 52.2% for target |

## Evidence and review scope

I read `PRODUCT_TENETS.md` and `REVIEW_BRIEF.md` first, then traced the source, gate harness, hardware receipts, metrics, manual serial capture, and tester narration. The archive passed its supplied SHA-256 manifest; the bundled Git branch was clean at `f66c808`; and `git diff --check` passed. I did not modify source code.

The packet’s physical observations are authoritative for visible behavior. Software logs remain useful for attribution, but they cannot overrule glass when their pass condition is derived from the disputed model. The exact physical cause of each tear line remains uncertain until TE, bus activity, and visible pixels are captured together. The findings below distinguish what is directly proven from what is a strong, testable mechanism hypothesis.

Native Vector V2 tests did not build unmodified on this Clang 17/libstdc++ toolchain because `vector_v2/tests/chrome_test.cpp:510-513` uses `std::clamp` without `<algorithm>` and `vector_v2/tests/display_scheduler_test.cpp:123` uses `std::vector` without `<vector>`. With those standard headers force-included, all five Vector V2 test/exactness/fuzz targets passed: **201 test cases, 76,624 assertions**, cold replay exactness, 800-case collinear fuzz, 4,000-case regression fuzz, and 150 random documents. This is a reproducibility defect in the test tree, not a plausible cause of the glass regression.

---

# Prioritized findings

## P0 correctness and product blockers

### P0-1 — `tear_synchronized` is a circular software assertion, not a tear oracle

**Evidence.** The presenter hard-codes a 16.8 ms period, a 448-row sweep, and a 48-row margin in `esp32/main/vector_v2/vector_v2_presenter.h:29-38`. During cached pan it derives `beam_row = age * 448 / 16800`, chooses a start row, optionally waits for a modeled wrap, and then assigns `timing.tear_synchronized = true` after that control flow succeeds (`esp32/main/vector_v2/vector_v2_presenter.cpp:573-679`). PANSEQ asserts the returned boolean and reuse state; it does not observe panel pixels (`esp32/main/vector_v2/vector_v2_gate_harness.cpp:138-178,1601-1692`).

The model does not establish:

- which TE edge corresponds to visible frame start;
- the delay from that edge to visible row 0;
- scan direction, porch interval, or whether the active visible height is the modeled 448 rows;
- whether a GRAM write is visible immediately, line-buffered, frame-latched, or reordered internally;
- the latency from task wake to first QSPI data;
- whether row age remains linear through the wrapped second band.

The manual run then reports severe tearing while 1,623 summarized pan frames show zero software failures (`METRICS.md:62-70`). The only sound conclusion is that the current boolean validates implementation conformance to an unvalidated timing model.

**Impact.** Every “tear-free-by-construction” claim and every gate that treats this boolean as physical evidence is invalid.

**Recommendation.** Rename the signal to something such as `beam_model_completed` until there is a separate optical pass signal. A physical oracle must decode mixed frame IDs or row sentinels from camera/photodiode capture; software timing should remain diagnostic metadata.

### P0-2 — The “writer is slower than the beam” proof uses the wrong rate

**Evidence.** The transport is configured for 60 MHz quad SPI, 16 bpp, queue depth 3, and 16,384-pixel transfer buffers (`esp32/main/co5300_panel_transport.cpp:28-35,142-166`). Full-width strips are 44 rows because `floor(16384 / 368)` is rounded even (`esp32/main/co5300_panel_transport.cpp:363-375`). The active wire ceiling is:

- `60 MHz × 4 data lines / 16 bits = 15.0 Mpixel/s`;
- `15.0 Mpixel/s / 368 = 40.76 rows/ms`;
- modeled beam: `448 / 16.8 ms = 26.67 rows/ms`;
- one 44-row payload: about 1.079 ms, during which the beam advances about 28.8 rows;
- potential gain per active strip: about 15.2 rows;
- 48-row lead consumed in about 3.15 strips / 3.41 ms at the active payload ceiling.

The historical receipt instead reasons from roughly 15 rows/ms average frame progress and says the beam cannot be caught (`context/PAN_FLOOR_CLOSURE_2026_08_15.md:44-47`), while the same document states an ~11 ms full-frame wire floor (`:63-66`). Those two statements are incompatible as a no-catch proof.

`push_rect_ring` is explicitly bursty: it waits for one of three DMA buffers, stages a strip, submits it, and repeats (`esp32/main/co5300_panel_transport.cpp:378-402`). Average frame wall time includes CPU staging, semaphore stalls, command setup, and final drain. It says nothing sufficient about the instantaneous writer-visible trajectory.

**Impact.** The 48-row margin is not guaranteed to remain positive. This is a strong candidate mechanism for tearing, though synchronized instrumentation is still required to establish exactly where the writer meets the visible scan.

**Recommendation.** Stop using average rows/ms in safety arguments. Measure the time of every TE edge, command window, first/last pixel burst, DMA completion, and visible row transition. A retained beam race needs a measured worst-case minimum separation, not a mean.

### P0-3 — The full-refresh fallback is fail-open and can become reusable

**Evidence.** The transport contract says `wait_for_safe_frame_start` returns false on timeout and callers should still present (`esp32/main/co5300_panel_transport.h:42-45`). `present_pixels` records the false synchronization result but proceeds with the full transfer; `timing.passed` depends on transfer completion, not synchronization (`esp32/main/vector_v2/vector_v2_presenter.cpp:921-973`). `refresh` then sets `frame_reusable_ = timing.passed` (`:74-109`).

Therefore a TE-timeout full refresh can return `passed=true`, `tear_synchronized=false`, and still seed later cached reuse. This contradicts the cached-pan comment that every degraded state “falls back to a full refresh” so the code will “never reuse an unsynchronized frame” (`:573-578`). It only exits the cached-pan branch; it does not establish a synchronized source frame.

**Impact.** A dead/stale TE signal or failed wait can create a physically mixed frame and then mark that frame reusable.

**Recommendation.** At minimum, any frame whose later reuse assumes synchronization must require both successful transfer and a physically meaningful synchronization result. More importantly, do not label the full top sweep safe until the edge-to-visible relationship is calibrated. Until then, a timeout should invalidate reuse and raise a red diagnostic rather than silently “heal” correctness.

### P0-4 — The TE implementation mixes edges, fixed timing, and scheduler jitter

**Evidence.** TE health in cached pan is refreshed when the **rising-edge** count changes (`esp32/main/vector_v2/vector_v2_presenter.cpp:581-590`), but beam age and frame-start waits use the **falling edge** (`esp32/main/co5300_panel_transport.cpp:242-275`). The transport measures `period_us` and pulse high time (`:230-239`), yet the presenter ignores the measured period and hard-codes 16,800 µs. Waiting converts the microsecond timeout to FreeRTOS ticks (`:264`) and resumes a task through a semaphore; the edge-to-first-write wakeup latency is neither measured nor included in the row model. The discipline then adds 100 µs busy waits (`esp32/main/vector_v2/vector_v2_presenter.cpp:604-608,649-651`).

**Impact.** Even if the chosen edge were semantically correct, the implementation has unquantified phase uncertainty. It also allows a rising edge to keep “health” green while the falling-edge age used by the model is wrong or stale.

**Recommendation.** Use one explicitly calibrated edge throughout. Record ISR timestamp, task-resume timestamp, first command timestamp, and first payload timestamp. Feed the measured period into diagnostics, but do not infer visible row until calibration demonstrates the relation.

### P0-5 — Beam racing should be removed from the product path now

The current path is physically falsified, its proof is circular, and its central rate premise is unsound. More margin or another wrap exception would add heuristics without restoring an invariant.

**Decision.** Keep the implementation only behind an experimental presentation-policy flag for the A/B. Do not expose it as the product mode and do not let it satisfy release gates.

**Evidence required before retaining it later:**

1. calibrated TE edge, polarity, scan direction, and edge-to-visible-row phase;
2. measured controller write-to-visible behavior;
3. measured worst-case writer/beam separation across every strip, including queue bursts, wraps, task jitter, cold/warm boots, temperature, and supply conditions;
4. a red-capable optical test showing zero mixed frame IDs over a large pan corpus;
5. fault injection proving stale/missing TE never produces a reusable “synchronized” frame.

Without all five, a compact top-to-bottom policy is more elegant and safer.

### P0-6 — Pan spends the missing frame budget mutating overlays into and out of PSRAM

**Evidence.** Before the first submission, `refresh_pan` loops over each overlay and:

1. composes any exposed canvas beneath it;
2. copies the ring region to `overlay_backup_`;
3. copies that backup into `strip_scratch_`;
4. draws the overlay on the scratch surface;
5. writes the surface back into the ring;
6. after all strips have been synchronously staged, restores the saved canvas to the ring.

See `esp32/main/vector_v2/vector_v2_presenter.cpp:519-571,669-675`. The first panel submission does not happen until `:625`. Metrics show +12.6 ms at 100% and +13.1 ms at 400% versus the 28.9 ms reference (`METRICS.md:18-23`), and the closeout attributes roughly 15 ms to overlay prep (`context/REVIEW_ROUND_CLOSURE_2026_08_15.md:50-57`).

The transport must already visit every outgoing pixel in internal RAM to de-rotate and byte-swap (`esp32/main/co5300_panel_transport.cpp:386-395`; `esp32/main/vector_v2/vector_v2_presenter.cpp:803-878`). The current design pays a second representation journey solely to make overlays ride full-width strips.

**Impact.** Pan is at the 24 FPS floor instead of the 30 FPS target, and all overlay prep is serial latency before first light.

**Recommendation.** Keep the PSRAM ring permanently canvas-only. Pre-render compact overlay sprites/strips when their state changes, then patch them into the DMA bounce buffer during the mandatory staging pass. This retains one full-width row-major sweep, avoids x-window seams, removes gather/copy/scatter/restore, and makes overlay cost proportional to touched staged pixels with no extra PSRAM round trip.

### P0-7 — The 10 ms drawing budget is not a real latency bound

**Evidence.** The API claims the input gap is bounded “by construction” (`vector_v2/include/tinydraw/vector_v2/incremental_document.h:75-79`). The deadline is computed before the commit (`vector_v2/src/incremental_document.cpp:325-338`), but the overview copy and operation raster in `prepare_overview` contain no deadline checkpoints (`:12-40`). Uniform tiles visible in the priority view are explicitly exempt (`:252-283`), and visible raw tiles are also exempt; the only `over_budget()` check is for off-screen raw tiles at the active zoom (`:286-313`). At 25%, the app passes no priority view (`esp32/main/vector_v2/vector_v2_app.cpp:324-341`), so it paints no raw tiles and never reaches the only material budget check. Metadata enumeration and revision commit then scan after overview work (`vector_v2/src/materialized_canvas.cpp:669-696,787-819`).

The historical receipt already acknowledges an uninterruptible ~13.7 ms 25% overview band (`context/DRAWING_LATENCY_CLOSURE_2026_08_14.md:81-84`); glass reaches 35.8 ms because operation shape, radius, affected overview area, cache/catalog state, and metadata tail matter in addition to sample count.

**Impact.** The constant communicates a guarantee the code does not provide. Increasing or lowering it will not bound the indivisible work.

**Recommendation.** Make preemption points structural. Raster overview work by bounded row/span batches; retain a resumable commit cursor; carry tile enumeration and metadata mutation in bounded batches or direct affected-index lists. Measure the complete coordinator slice, not only a subset of tile painting.

### P0-8 — The app commits authority before showing the newest visual update

**Evidence.** On each move, the builder may synchronously commit a ready chunk (`esp32/main/vector_v2/vector_v2_app.cpp:981-998`). Only after that returns does `show_update` run (`:1017-1027`). On lift, the app renders the finish preview, drains every ready/final chunk in a synchronous loop, and then recomposes/presents the committed region (`:1052-1111`).

**Impact.** A 15–36 ms materialization step is placed directly in front of visible ink; lift can block for multiple chunks plus refresh. This reverses the primary product priority in `PRODUCT_TENETS.md:7`.

**Recommendation.** Split the interaction into two lanes:

- **visual lane:** immediately update a small volatile live-tail layer from the newest sample and submit it;
- **authority/materialization lane:** append compact vector data quickly, then consume overview/tile damage through a resumable bounded state machine.

A lift should mark the operation final and let materialization catch up; it should not drain an arbitrary backlog before the event loop can continue.

### P0-9 — The renderer intentionally accepts one smoothing-window of finger lag

**Evidence.** `esp32/main/vector_v2/vector_v2_app.cpp:1020-1025` explicitly says provisional segments are omitted and accepts “a one-smoothing-window lag behind the finger.” Calls use `ribbon.append(last_ink, false)`, so only committed geometry is shown.

**Impact.** Even a zero-cost commit would still violate “drawing must not lag.” A start cap gives contact acknowledgment but does not provide continuous direct manipulation.

**Recommendation.** Render a replaceable provisional tail. Two mechanically compact options are:

- keep the last few ribbon primitives as volatile overlay geometry and composite them in the DMA staging pass; or
- retain a tiny backing rectangle, restore the previous provisional tail, and draw the newest one.

When authority catches up, retire the corresponding volatile primitives. The invariant should be: **the panel update always represents the newest consumed touch sample, while materialization may trail invisibly.**

### P0-10 — Live ink can wait for the panel multiple times per update

**Evidence.** `show_start` and `show_update` call `present_unobscured` (`esp32/main/vector_v2/vector_v2_presenter.cpp:243-279`). Overlay subtraction can produce several rectangles; each is sent through `present(..., wait_for_completion)` (`:450-482`). The default is `true` (`esp32/main/vector_v2/vector_v2_presenter.h:139-146`), and `present_pixels` calls `wait_for_all` at the end of every such call (`esp32/main/vector_v2/vector_v2_presenter.cpp:960-973`). The comment says multi-region callers can defer completion, but the live path does not.

`wait_for_all` itself polls completion with 1 ms task delays (`esp32/main/co5300_panel_transport.cpp:331-342`), adding coarse scheduling latency.

**Impact.** A small live update intersecting fixed UI can serialize multiple queue drains, inflating tail latency and loop gaps.

**Recommendation.** Stage/submit all pieces and drain once, or preferably patch fixed overlays into one aligned staged region so the live update uses a single ordered window. Replace 1 ms polling with a completion notification/semaphore where practical.

## P1 high-impact architecture, scheduling, and observability findings

### P1-1 — Touch coalescing makes low event age compatible with path loss

**Evidence.** Consecutive move events are replaced by the newest point (`vector_v2/include/tinydraw/vector_v2/touch_event_buffer.h:43-48`; `vector_v2/src/touch_event_buffer.cpp:83-89`). Event age is measured only when the surviving event is popped (`esp32/main/vector_v2/vector_v2_touch_sampler.cpp:61-80`). The first manual stroke reports 1,954 coalesced moves versus 1,424 consumed events while maximum surviving-event age is only 1.243 ms (`evidence/latest/c86f3ac-manual-glass.log:8`). Later reports contain tens of thousands of coalesces and event ages above 100 ms.

**Impact.** “Touch task healthy” is too narrow. A blocked coordinator can repeatedly replace the path with its latest endpoint, keep the retained point young, and still cause visible lag or geometry under-sampling.

**Recommendation.** Add coalescing ratio, maximum consumed-sample time gap, maximum spatial gap, finger-to-visible-tail distance, and final path error. Prefer a compact geometry-aware queue/resampler that preserves extrema and curvature rather than latest-only replacement under load.

### P1-2 — Latency telemetry is anchored to loop time and stops at DMA completion

**Evidence.** The sampled event timestamp is available at `esp32/main/vector_v2/vector_v2_app.cpp:916`, but `show_start`, pan, and `show_update` are passed `loop_us` (`:968,979,1027`). Lift invents a new timestamp (`:1052-1064`) rather than using the Up event timestamp. `present_pixels` derives submit/complete deltas from the supplied value (`esp32/main/vector_v2/vector_v2_presenter.cpp:968-972`). `complete_time_us` is recorded by the SPI color-transfer completion callback, not optical visibility (`esp32/main/co5300_panel_transport.cpp:318-329,487-496`).

The lift report’s “unattributed tail” is also computed by subtracting phase work from a boundary that can precede parts of that work (`esp32/main/vector_v2/vector_v2_app.cpp:441-470`), which explains negative values in logs and makes the field unsuitable as a residual budget.

**Impact.** Current logs can locate CPU and DMA stalls, but they do not measure touch-to-photon. Some values systematically understate queue delay.

**Recommendation.** Carry the original sampled timestamp through every stage. Record sampled, consumed, preview-ready, first-command, first-payload, DMA-complete, and camera-visible timestamps separately. Do not name DMA completion “physical complete.”

### P1-3 — The mixed-draw gate bypasses the product interaction path

**Evidence.** The gate creates synthetic 1,536-sample strokes and directly calls `append_incrementally_in_place` (`esp32/main/vector_v2/vector_v2_gate_harness.cpp:1256-1328`). It presents only once after the entire stroke (`:1330-1337`) and passes on append timing/correctness (`:1403-1412`). It does not exercise the touch sampler, move coalescing, app sequencing, per-move ribbon updates, overlay subtraction, repeated display drains, lift backlog, or physical panel visibility. Its comment still says 48 samples fill a chunk while the product constant is 32 (`:1258-1263`).

**Impact.** A 12.345 ms gate maximum cannot contradict a 35.8 ms product chunk or prove non-lagging drawing.

**Recommendation.** Add an application-level deterministic replay that feeds timestamped Down/Move/Up events through the coordinator and presenter. On hardware, pair it with optical tail tracking. Keep the direct commit gate as a component benchmark, not a product-latency oracle.

### P1-4 — Every cold 128×128 group starts by walking the entire replay range

**Evidence.** `TileProducer` performs exact newest-first replay (`vector_v2/include/tinydraw/vector_v2/tile_producer.h:58-63`). `start_group` establishes an active group with the complete post-baseline operation range (`vector_v2/src/tile_producer.cpp:253-288`). Each operation is fetched, bounded, and rejected or rasterized (`:303-332`); batching limits work per call (`:415-457`), but no tile can publish until the group’s full replay is complete. Bounding-box rejection makes distant operations cheap, not free.

**Impact.** On the adversarial dense document, each missing group repeatedly scans thousands of operation records before publishing one group. The stable 628.2 ms p95 is therefore a structural cost, not scheduler noise.

**Recommendation.** Add an append-time spatial replay index. A practical first experiment is a compact per-macrocell list of operation IDs plus per-block union bounds; a group replays only intersecting IDs newest-first. Large operations need deduplication or segment-range entries so they do not explode memory. Measure PSRAM reads, index bytes/operation, IDs scanned/group, and exactness. The 500 ms ceiling needs a 20.4% reduction; ~300 ms needs 52.2%, so this should be treated as the main algorithmic lever, not a micro-optimization.

### P1-5 — Commit metadata does avoidable full scans and linear membership searches

**Evidence.** `materialized_tiles_intersecting` scans every slot to enumerate affected identities (`vector_v2/src/materialized_canvas.cpp:787-819`). `commit_in_place_revision` scans every slot again and uses `std::find` over retained keys for each affected slot (`:669-696`).

**Impact.** Commit tail grows with slot count and retained-key count, precisely where the API claims a hard interaction budget. This work occurs after the uninterruptible overview raster.

**Recommendation.** Enumerate affected slot indices once, carry generation-checked indices through the commit, and mark retained entries in a bitset or generation table. Mutate exactly those slots; do not rescan the directory or perform O(slots × retained) membership checks.

### P1-6 — Idle repair has a priority list, but not a bounded user-visible pause contract

**Evidence.** The plan prioritizes current/cardinal neighbors, remembered zooms, and a 100% grid (`vector_v2/include/tinydraw/vector_v2/idle_repair.h:12-36`; `vector_v2/src/idle_repair.cpp:34-67`). The app starts repair only after the current fill is complete and input is quiet; publications never present directly (`esp32/main/vector_v2/vector_v2_app.cpp:1244-1294`). A view or revision change marks the active fill superseded and replans (`:1141-1176`). The manual run has 137 fills, 43 superseded, complete 100% fills up to 700.9 ms, and one 400% fill superseded after 8.207 s (`METRICS.md:99-108`).

**Impact.** “Pause for four seconds” does not map to a guarantee. Motion repeatedly discards scheduling context even when some produced tiles remain relevant.

**Recommendation.** Define and test this contract:

1. while the finger is down or motion continues, overview-correct output is immediate and repair receives only a strict micro-slice;
2. after a defined quiet interval, the **current viewport** becomes exact within <500 ms at 400%, target ~300 ms;
3. next, repair the velocity-leading halo; then recent zoom-return views; finally optional broad sweeps;
4. retain tasks keyed by `(revision, zoom, tile/group)` across camera generations and cancel only work outside the new current/forecast set;
5. batch publications and repaint at display cadence rather than allowing publication bookkeeping to dominate.

This makes pause behavior crisp and lets partial work survive motion.

### P1-7 — “Top frame boundary” is an experiment, not yet a proven safe mode

The packet’s proposed B path is the correct smallest A/B, but the current `wait_for_safe_frame_start` contract itself asserts that the falling edge is safe before that has been measured (`esp32/main/co5300_panel_transport.h:42-45`). A top-to-bottom sweep can still tear if TE refers to a different phase, the writer catches the scan, or the controller exposes writes with buffering semantics.

**Recommendation.** Call B “boundary-triggered top sweep,” not “safe sweep,” until the optical result is clean. If B tears, vary controlled inter-strip pacing and phase offsets before changing pixels or composition. That discriminates scan-race behavior from frame-latched/GRAM semantics.

### P1-8 — There is no physically proven Vector V2 rollback in the packet

Earlier V2 pan variants had rare overlay-edge seams and retained beam timing assumptions; the current review packet does not establish a clean V2 revision that is both correct and fast. Rolling back only `c86f3ac` risks swapping severe tears for known seams without restoring a proof.

**Recommendation.** Use Raster V1 as the operational fallback. For V2 development, disable cached live pan or update only at a validated boundary while the A/B and instrumentation are built. Do not represent an older V2 commit as a correctness rollback without a fresh glass run.

### P1-9 — White edge notches remain outside every current pass/fail signal

**Evidence.** The packet explicitly says periodic white notches remain open and host staging/edge tests did not reproduce them (`REVIEW_BRIEF.md:108-114`).

**Impact.** A release can be green while the physical edge artifact persists.

**Recommendation.** Put nonwhite frame-ID sentinels in guard columns/rows and use a fixed camera ROI to classify any white excursion as red. Sweep x/window width, strip height, queue depth, ring shift, and command gaps while logging bus boundaries. Correlation with 44-row transfer boundaries, 64-pixel tile boundaries, or a specific window command will localize the mechanism quickly.

## P2 contract, test, and maintainability debt

### P2-1 — Presenter readiness omits newly required scratch contracts

`VectorV2Presenter::ready()` validates frame, region, scheduler, display, and renderer, but not `strip_scratch_` or `overlay_backup_` (`esp32/main/vector_v2/vector_v2_presenter.cpp:46-49`). The pan path discovers undersized scratch at runtime and silently falls back to refresh (`:545-548`). The product allocation is currently large enough (`esp32/main/vector_v2/vector_v2_app.cpp:718-722`), so this is not the glass cause, but it weakens construction-time invariants and tests.

**Recommendation.** Include both exact minimum sizes in `ready()` and add undersized-construction tests.

### P2-2 — Even-pixel pan quantization may create micro-stickiness

Pan deltas are aligned to even pixels to preserve panel-window and staging alignment (`esp32/main/vector_v2/vector_v2_presenter.cpp:305-335`). This is a sensible throughput trade, but one-pixel finger motion can become a no-op and movement arrives in two-pixel steps.

**Recommendation.** Keep even physical windows but accumulate sub-window residual motion so gesture mapping remains continuous. Include slow one-pixel drags in glass testing.

### P2-3 — Native test success depends on undeclared transitive headers

`chrome_test.cpp` uses `std::clamp` without `<algorithm>` and `display_scheduler_test.cpp` uses `std::vector` without `<vector>`. The packet’s 76,624-assertion result is still useful evidence from its original environment, and the same tests pass here once the headers are supplied, but “host clean” is not reproducible from an unmodified checkout on this toolchain.

**Recommendation.** Add direct standard-library includes and make a clean-container build part of the battery.

### P2-4 — Several closeout documents are now materially false

The manual glass evidence falsifies these claims:

- “beam-lap math keeps it tear-safe” — `context/PAN_FLOOR_CLOSURE_2026_08_15.md:44-47`;
- “interactive poll gap is bounded by construction” — `context/DRAWING_LATENCY_CLOSURE_2026_08_14.md:49-56`;
- “cached pan frames are synchronized by construction” — `context/REVIEW_ROUND_CLOSURE_2026_08_15.md:10-16`;
- “~24 FPS in exchange for tear-free-by-construction pans” — `context/REVIEW_ROUND_CLOSURE_2026_08_15.md:50-57`;
- “tear-free-by-construction pan presentation” — `context/PROJECT_STATE.md:125-127`.

The same documents contain caveats that glass was still pending (`PAN_FLOOR_CLOSURE:70-72`; `REVIEW_ROUND_CLOSURE:65-71`), but the headlines and state summaries overstate what was known.

**Recommendation.** Mark them “software-model-conformant; physically falsified on 2026-08-15” and prevent future closure documents from using physical language without an optical receipt.

### P2-5 — The gate’s stale 48-sample comment is a warning about benchmark drift

`esp32/main/vector_v2/vector_v2_gate_harness.cpp:1258-1263` says 48 samples make one chunk, while `kInteractiveChunkSampleLimit` is 32. The code uses the real constant, so this does not alter execution. It does show that the benchmark narrative can drift from the tested workload.

**Recommendation.** Derive sweep/sample geometry from the chunk constant or assert the intended relationship.

### P2-6 — “All green” obscures explicit yellow and manually unverified states

`METRICS.md:12-15` says all final flags are green while `ssaa_receipt=yellow`. `REVIEW_BRIEF.md:108-114` also keeps settled anti-aliasing yellow, new-document reset pending manual verification, the cold-reduction campaign incomplete, and idle repair without a complete-neighborhood guarantee.

**Impact.** The summary color can imply a release closure that the underlying receipts do not support, repeating the same category error as `tear_synchronized`: a software verdict is broader than its evidence.

**Recommendation.** Use a tri-state release matrix—automated pass, physical pass, and pending/yellow—for every glass-visible contract. An aggregate “green” must require all mandatory physical receipts, not merely nonfatal software flags.

---

# Smallest decisive A/B

Use one deterministic cached-pan input and change exactly one presentation policy.

## A — current behavior

- current two-band beam-aged start;
- current 48-row modeled lead;
- same ring contents, overlay composition, 44-row strips, queue depth, and transfer count.

## B — boundary-triggered top sweep

- wait for one selected TE edge;
- start at row 0;
- one top-to-bottom full-width sweep;
- no mid-frame row estimate and no wrapped second band;
- otherwise identical pixels, overlays, staging, strip size, and queue depth.

## Required synchronized evidence

- high-speed video with alternating frame IDs and row-coded bars;
- logic analyzer on TE, CS, clock/data activity, plus GPIO markers for ISR, task resume, first command, first payload, every strip submit, and final completion;
- at least horizontal, vertical, and diagonal pans at slow/fast motion, 100% and 400%, with repeated cold/warm boots;
- any simultaneously visible rows carrying different frame IDs is an immediate red result.

Interpretation:

- **B clean, A torn:** remove beam racing; do not tune the margin.
- **Both torn, tear line moves with strip size/pacing:** writer/beam trajectory is implicated.
- **Both torn, insensitive to pacing/phase:** investigate controller GRAM/latch semantics and panel mode.
- **B clean only at a calibrated offset from TE:** encode that measured phase as a presentation contract and continue margin testing; do not infer it from pulse shape.

# Recommended safe next implementation

Implement a small `PresentationPolicy` seam and make **boundary-triggered single-sweep** the only enabled Vector V2 development path; keep current beam racing behind a non-product experiment flag. Preserve the exact ring pixels, overlay path, strip dimensions, and transport so the A/B remains discriminating. Simultaneously:

1. make frame reuse require a successful presentation plus the selected policy’s validated synchronization state;
2. mark TE timeout/staleness as red and non-reusable rather than silently fail-open;
3. emit GPIO/serial phase markers and deterministic row/frame sentinels;
4. run the physical A/B before further pan micro-optimization.

This implementation is “safe next” because it reduces variables and stops declaring a falsified mode correct. It is not a claim that B is already safe; until the optical result is clean, Raster V1 remains the product fallback.

# Recommended architecture: one ordered staging compositor

The current design has good ingredients—pointer-only ring scrolling, bounded internal DMA buffers, exact operation authority—but lets representation and scheduling leak across layers. A simpler machine-shaped architecture is:

## 1. Pure canvas ring

The PSRAM ring always contains canvas pixels only. Pan changes ring origin and composes exposed canvas strips. No fixed UI or live provisional ink is ever scattered into and restored from the ring.

## 2. Compact volatile layers

Keep fixed chrome as small cached RGB565 sprites/strip descriptors, regenerated only when battery/minimap/zoom state changes. Keep the newest ribbon tail as a tiny volatile primitive list or sprite with a generation number. Neither layer becomes document authority.

## 3. Mandatory internal staging pass is the compositor

Every outgoing strip already enters internal RAM for ring de-rotation and endian swap. In that same pass:

- copy/de-rotate canvas;
- patch fixed opaque chrome;
- raster the few volatile live-tail primitives that intersect the strip;
- byte-swap and submit.

Each outgoing pixel is touched once in the expensive memory hierarchy. There are no overlay PSRAM backups, x-window splits, or stale provisional pixels.

## 4. One ordered presentation and one drain

A frame/region submission owns an ordered set of strips and drains once. Presentation policy—boundary sweep, experimentally beam-raced sweep, or controller-supported synchronization—is separate from pixel composition. This creates a testable invariant: **the same composed strip sequence can be replayed under different physical policies.**

## 5. Vector authority ahead of materialization

The operation log should be cheap authority; overview/tile materialization should be a resumable consumer. Live visuals must not wait for materialization. A damage queue keyed by operation revision feeds bounded overview rows, affected tile updates, and cold replay/index maintenance. Once a revision is materialized, the corresponding volatile tail can retire.

This is a larger state-model change than lowering the chunk size, but it removes the fundamental commit-before-preview conflict. A smaller transitional implementation can retain the existing revision coupling while turning `append_incrementally_in_place` into a resumable transaction; the visual lane still must run first.

## 6. Spatial replay index

Build a compact append-only macrocell or operation-block index. Cold groups should fetch a short newest-first candidate list, not scan the complete log. Keep exact first-writer-wins masks and saturation exits; change only candidate discovery.

## 7. Deadline means a preemption boundary

No function should advertise a 10 ms budget unless all workload-proportional loops have checkpoints and all tails are bounded. Record worst indivisible units directly. Prefer 1–2 ms slices so touch and display scheduling have headroom rather than designing exactly to a 15 ms alarm.

# Rollback / fallback

**Product fallback:** Raster V1, after verifying the current image on the current board. It is still the default/shipping app (`README.md:13-18`), its long-stroke updates are measured at 2.5–3.4 ms (`README.md:29-31`), and it avoids presenting an unproven V2 timing model as correct.

**V2 degraded fallback for development:** disable cached live pan and repaint only under the boundary-triggered policy, or defer visual pan updates until gesture end if no physically clean policy is available. This may miss the pan target, but it contains physical corruption while the architecture is repaired. Do not use it as the final product answer.

# Physical and deterministic test matrix

| Area | Axes | Signal | Red criterion |
|---|---|---|---|
| TE phase calibration | rising/falling edge; offsets across a full period; top/mid/bottom narrow bands; multiple boots | camera row decode + TE/bus/GPIO capture | observed scan phase contradicts modeled row, changes by boot, or cannot establish stable relation |
| Pan A/B | A vs B; 100/400%; horizontal/vertical/diagonal; slow/fast; one- and two-pixel motion; warm/cold cache | alternating frame ID + row barcode | any frame contains mixed IDs or a white/notched sentinel |
| Writer trajectory | strip heights around 44; queue depth 1–3; optional controlled inter-strip gap; full/partial height | TE-to-each-payload timing + optical tear row | minimum measured separation reaches zero/unknown, or tear location tracks burst cadence |
| TE faults | missing pulse, stale count, delayed task, wait timeout, forced heal | reuse state + optical output | an unsynchronized/timeout frame becomes reusable or passes physical gate |
| Drawing end-to-end | 25/50/100/200/400%; empty/dense; warm multi-zoom cache; hairline/XL; overlay intersections; bottom edge; long gesture | sampled timestamp, latest-tail generation, camera-visible tail | visible tail trails newest consumed sample beyond agreed frame budget; geometry gaps/path error; loop starvation |
| Drawing component | affected overview area/shape, radius, segment count, catalog occupancy, visible uniform/raw mix | full coordinator slice histogram | any indivisible unit exceeds declared budget or budget is checked only after the expensive unit |
| Cold render | adversarial/overlap/seed-7; all zooms; cold/warm; after mutation; repeated 20+ runs | wall p50/p95/max, operations scanned/group, PSRAM bytes | 400% accepted worst ≥500 ms; exactness mismatch; current viewport publication stalls behind unrelated groups |
| Pause/repair | stop after fast pan; reverse direction; small camera changes; zoom away/back; revision change | time from quiet boundary to current viewport exact | current 400% viewport not exact by 500 ms; overlapping work discarded without cause |
| White notches | solid nonwhite guard, edge ink, each x/window alignment, strip seam, ring wrap | fixed camera ROI classifier | any white pixel run above sensor-noise threshold |
| Test reproducibility | clean container/toolchains; release/sanitizer; direct includes | build + tests | unmodified tree fails to compile or test result depends on transitive includes |

For drawing, the product should lock an optical acceptance such as p95 latest-sample-to-visible-tail within one panel period and a strict maximum within two periods under every accepted workload. The exact threshold should be made explicit, but the key requirement is that it measure the latest sample on glass—not submit time, DMA completion, or a component microbenchmark.

# Claims the new evidence falsifies

1. `context/PAN_FLOOR_CLOSURE_2026_08_15.md:44-47`: beam-lap math does not establish tear safety.
2. `context/DRAWING_LATENCY_CLOSURE_2026_08_14.md:49-56`: the poll gap is not bounded by construction; the document itself notes the 25% indivisible ceiling at `:81-84`, and glass exceeds it.
3. `context/REVIEW_ROUND_CLOSURE_2026_08_15.md:10-16`: cached pans are not synchronized by construction.
4. `context/REVIEW_ROUND_CLOSURE_2026_08_15.md:50-57`: the performance regression was not exchanged for tear-free pans.
5. `context/PROJECT_STATE.md:125-127`: “tear-free-by-construction” must be replaced with “software timing model passed; physical result failed.”

What remains trustworthy is narrower but valuable: operation/canvas authority matched manual strokes, bottom-edge input worked, the strict battery ran without watchdog/crash markers, cold timings are stable, and the exactness/fuzz tests pass when the test sources are made self-contained (`REVIEW_BRIEF.md:116-124`).

# Can the proposed architecture meet all three targets?

## Drawing

**Current architecture: no.** It intentionally lags a smoothing window, puts synchronous materialization before preview, has unbounded overview/visible work, and can drain the display several times per update.

**Proposed architecture: credible.** A visual-first volatile tail plus one staged submission can make visible response independent of materialization. Evidence still needed: optical contact-to-tail distribution under dense 25% workload, coalescing/path fidelity, and proof that the largest staging/raster unit is bounded.

## Pan

**Current architecture: no.** It tears physically and averages 41.5–42.0 ms. Removing the measured +12.6/+13.1 ms regression would return the warm path near 28.9 ms in principle, inside the 33.3 ms 30 FPS target. A naive serial wait for the next TE edge can add up to one frame of latency, so composition/staging and boundary scheduling must be pipelined and measured rather than simply prepending another wait.

**Proposed architecture: credible for ≥24 FPS and plausible for 30 FPS.** The pure ring and staging compositor remove the largest avoidable serial cost. Physical synchronization remains the gating evidence.

## Cold 400%

**Current architecture: no.** 628.2 ms p95 fails the 500 ms ceiling.

**Proposed architecture: plausible for <500 ms; ~300 ms remains unproven.** Candidate indexing must save at least 20.4% for the ceiling and 52.2% for the target. Instrument operations scanned/group and PSRAM traffic before choosing the index representation. Retaining overlapping repair work and publishing current-view groups first should improve perceived and measured pause completion, but algorithmic replay reduction is still necessary.

# Final priority order

1. Stop treating beam racing as product-correct; build the one-variable physical A/B and optical oracle.
2. Fix frame reuse so timeout/unsynchronized frames cannot seed cached pan.
3. Make live ink visual-first and render a replaceable provisional tail.
4. Move fixed overlays and live-tail compositing into the mandatory internal staging pass; keep the ring pure.
5. Submit each visual update as one ordered batch with one completion wait.
6. Turn materialization into genuinely resumable work with bounded overview and metadata units.
7. Add a spatial/block replay index for cold groups.
8. Define the pause-to-sharp contract and retain relevant repair work across camera changes.
9. Promote white notches and contact-to-photon latency to optical red signals.
10. Repair test self-containment and relabel falsified closure documents.

The unifying design test is simple: **does each layer expose a physical invariant, or only another timing story?** The current implementation has many careful local optimizations, but the most important paths still rely on timing stories. A pure canvas ring, volatile visual layer, single staging compositor, explicit presentation policy, and indexed/preemptible materializer form a smaller and more mechanically sympathetic system.
