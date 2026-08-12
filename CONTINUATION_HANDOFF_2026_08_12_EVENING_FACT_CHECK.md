# Fact check: evening continuation handoff

Status: audited against branch `prototype/vector-materialized-cache` at `8817a88`, committed source, commit history, automated/manual hardware logs, and fresh builds/tests. This document distinguishes receipts from forecasts; it does not replace the handoff's narrative.

## Verdict

**GO to begin the production architecture.** The prototype has answered its architectural questions: raster-cache pan is fast, valid zoom fallback can reach the panel in tens of milliseconds, vector recording can coexist with live raster drawing, and the camera-aligned 3×3 atlas has unacceptable mutation repair/refusal behavior.

This means **ready to start production implementation**, not ready to ship and not yet proof that 25–400% meets every product gate. The largest remaining unknowns are the production memory layout, settled-renderer speed, 25/400% hardware behavior, and the no-refusal overview/tile coordinator.

Do one final bounded renderer experiment (Step 0) because its result directly informs the production renderer's memory layout. Do not do more feature or correctness work on the 3×3 coordinator.

## Claim audit

Legend: **VERIFIED**, **ARITHMETIC**, **SUPPORTED**, **HYPOTHESIS**, **OVERSTATED**.

### Repository and validation

- **VERIFIED:** The listed commits exist in the stated order. `8817a88` is both local and remote branch head, and tracked files were clean at audit time.
- **VERIFIED:** Fresh validation at `8817a88` passed: debug 22/22, release 22/22, ASan 4/4, format check, interactive ESP-IDF build, and normal ESP-IDF build.
- **VERIFIED:** Both committed hardware logs match their Git blobs. The automated log identifies app version `12b70da` and the physical 240 MHz ESP32-S3 with 80 MHz PSRAM.
- **VERIFIED:** `tools/esp32-capture.py` compiles as Python and implements timestamped capture plus reset/no-reset behavior. Qualification: its default end-marker list does not include `TINYDRAW_AUTO_ZOOM_DONE`; the handoff correctly says it must be edited for auto runs.

### Product geometry and memory

- **ARITHMETIC:** 4×4 screens is 1472×1792 world units. At 25%, that maps to 368×448. An RGB565 overview is 368×448×2 = 329,728 bytes (322 KiB, colloquially 330 KB).
- **QUALIFICATION:** The toolbar covers rows 372–447 (`esp32/main/hardware_app.cpp:60`). The 25% framebuffer can contain the whole world, but “show everything” is not all visibly unobscured while the toolbar is present. Production UX must hide/overlay the toolbar or accept that obstruction.
- **VERIFIED:** Current types are `StrokeSample=12`, `VectorStroke=28`, `RectF=16` bytes. The current 24,576-sample/1,100-stroke arena is 325,712 bytes.
- **OVERSTATED:** The 650 KB compact-vector budget and 3,000–4,000-stroke capacity are not derived yet. At the measured synthetic average of about 20 samples/stroke, 4,000 strokes is about 80,000 samples. Even 6–8 bytes/sample consumes 480–640 KB before stroke metadata, operation IDs, bounds, indexing, and allocation overhead.
- **HYPOTHESIS:** The 700 KB LOD/index budget, 600 KB scratch/staging budget, 100 KB overlay target, and compact 6–8-byte encoding are design targets, not implemented allocations.
- **ARITHMETIC with qualification:** A 64×64 RGB565 tile is 8,192 bytes. Forty-two tiles exactly cover a 6×7 full-screen tile grid; a 1 MiB pool holds 128 tiles, so visible coverage plus substantial prefetch is plausible. Edge clipping and metadata are additional but small.
- **OVERSTATED:** “2.5 MB slack” is subtraction from rounded design targets, not a measured allocatable reserve. Runtime/ESP-IDF use, fragmentation, KiB-vs-MB units, and largest-contiguous-block requirements still have to be measured. The 1.5 MB reserve is already included in the 5.5 MB table.
- **SUPPORTED:** Float32 has far more coordinate precision than needed over 1472×1792, even at 800%. Replacing the software-double camera is safe from a precision-budget perspective, but its speedup is unmeasured and it is not the main bottleneck.

### Automated hardware run

Source: `second_review_hardware_ab/12b70da-diag-auto-hardware.log`.

- **VERIFIED:** 12/12 zoom transitions changed successfully; every cycle reports `down12_ready=1 right1_ready=1`.
- **VERIFIED:** No `TINYDRAW_PANEL_WINDOW_REJECT` appears. This proves no invalid panel window reached the guard in these scenarios. It supports, but does not causally prove, every aspect of the CO5300 evenness hypothesis.
- **VERIFIED:** First physical fallback completion was 7.032–9.753 ms.
- **CORRECTION:** Last physical fallback completion was 40.221–49.776 ms. The handoff's 51.0 ms upper bound is the software `fallback_us` endpoint (max 51.027 ms), not last physical transfer completion.
- **VERIFIED:** Physical settled completion was 490–491 ms at 50%, 792–828 ms at 100%, and 737–740 ms at 200%.
- **SUPPORTED, not isolated A/B:** Telemetry did not cause a large physical regression. There was no same-firmware telemetry-off control, and `publish_us` increased by roughly 13–15 ms because hashing occurs inside publication.
- **VERIFIED:** Internal heap receipt was 95,240 bytes free and a 54,272-byte largest block. A 368×32 coverage band is 11,776 bytes. Coverage plus RGB565 for that band is 35,328 bytes and nominally fit at that instant with 18,944 bytes of contiguous margin.
- **QUALIFICATION:** That receipt does not guarantee a future 35 KB allocation after production tasks/queues exist. Borrowing the 164,864-byte internal live-coverage arena is plausible only behind an explicit ownership lease: drawing pauses background rendering, but the buffer is currently private live-stroke state.
- **VERIFIED:** Excluding the documented initialization publication, repeated zoom/revision/phase combinations produced stable fallback and settled hashes.
- **QUALIFICATION:** `TINYDRAW_AUTO_FINAL_PIXELS` is useful cross-path consistency, not an independent reference-render oracle. Both paths read the same raster and use the same FNV algorithm.
- **VERIFIED:** Mutation refusal, repair to revision 2, and successful retry occurred without accepting the stale revision.

### Interactive hardware run

Source: `second_review_hardware_ab/12b70da-manual-diag.log`.

- **VERIFIED:** Normal accepted-cache pan was approximately 26.1 ms. One gesture recorded 103 rejected views and 23,488 maximum missing pixels; after revision-10 repair, an 86-frame gesture ran at about 26.1 ms with no refusals.
- **VERIFIED:** Two zoom attempts failed while fallback repair was pending.
- **VERIFIED with wording correction:** Individual observed repairs took about 3–4 seconds. The roughly 12-second figure is cumulative wall time from the first of a six-stroke burst to the final revision-10 repair; each mutation retargeted the work. It is not a single isolated repair pass.
- **VERIFIED:** Exact publications 205–214 form a visible top-down band sweep at roughly 0.4 seconds per 32-row band.
- **SUPPORTED:** Off-cell views produced 20/26 settled bands and were slower than centered 12-band views. This supports extra-cell cost, but content, candidate counts, and band counts changed together, so “nearly doubles” is not an isolated causal measurement.
- **VERIFIED:** Live update averages were 1.67–2.80 ms and finish calls 36–60 ms in the recorded strokes. These are renderer/update timings, not full finger-to-photon latency; touch sampling and queue delay are separate.
- **SUPPORTED, not directly instrumented:** LOD output grew from 7,537 to 7,605/10,185 and stayed below 12,288 capacity. No log explicitly records “raw per-stroke fallback count=0,” so absence of raw fallback is inferred rather than directly proven.
- **OVERSTATED:** “Every subjective observation was quantified” is rhetorical. Major reported events have receipts, but panel appearance and every visual glitch cannot be reconstructed from software hashes alone.

### Correctness and fixes

- **VERIFIED:** The LOD plateau implementation changed from a long plateau walk to local boundary retention. An independent host reproduction at n=1200 measured old approximately 321 ms versus new approximately 3.7 ms, corroborating the reported 415→4.1 ms result.
- **VERIFIED:** Settled rendering now falls back per stroke when an individual LOD map is zero, out of range, or nonfinite; zero-length variable-radius capsules use the larger endpoint radius.
- **QUALIFICATION:** A full `build_settled_lod` capacity failure still aborts that generation rather than producing a partially populated map. This is separate from the fixed renderer-wide fallback cliff and should be redesigned in production.
- **VERIFIED in source, not hardware-exercised:** Cooperative render-task stop exists. The recent successful session did not exercise normal runtime destruction; the benchmark's finish path pauses/persists but leaves the task alive for app lifetime.
- **VERIFIED:** Pan gating, coherent snapshots at the audited sites, end-pan ordering, revision-gated publication, and panel-boundary rejection exist in source.
- **IMPORTANT LOGIC CORRECTION:** `expected_strokes` is not an expected-visible-ink oracle. It counts every stroke bounds intersecting the viewport, including erasers, fully overpainted strokes, and conservative bounds whose actual pixels may not enter the view (`interactive_pan_benchmark.cpp:524-542`). `expected_strokes>0 && visible_ink==0` is suspicious, not proof of a defect.
- **SUPPORTED:** The historical generation-36 108,754-segment event is consistent with repeated supertasks during viewport churn, and the pan gate removes that mechanism. The old log did not identify every constituent supertask, so “not an LOD failure” is a strong diagnosis rather than a direct measurement.

### Step 0 memory-bound hypothesis

- **VERIFIED premises:** Settled coverage scratch aliases the 164,864-byte `ViewportRenderer` scratch allocated in PSRAM (`interactive_pan_benchmark.cpp:1263-1274,1309`). The destination screen raster is PSRAM (`firmware_canvas.cpp:20-25`). Coverage is read/modified in the capsule loop and read/cleared in compositing (`settled_renderer.cpp:89-165`).
- **QUALIFICATION:** “The hot loops run entirely from PSRAM” is too broad. Geometry and scalar state use registers/internal memory; the large per-pixel coverage and destination arrays are the PSRAM-resident hot data.
- **OVERSTATED:** “Canonical keeps per-pixel work internal” applies to its two 32×32 coverage/working lanes (`viewport_renderer.h:109-114`), not its large primitive/bin scratch arena, which is PSRAM in this benchmark.
- **HYPOTHESIS:** A ≥40% `raster_us` drop from internal coverage is unmeasured. It is credible enough to test, not to plan around.
- **INCORRECT prediction:** `clear_us` and `composite_us` need not stay unchanged. Scratch clearing moves to internal RAM, and composite reads/clears coverage there; destination reads/writes remain PSRAM.
- **BETTER experiment:** Use three variants on the same document/camera and verify identical output hashes:
  1. grouped region + PSRAM scratch (current baseline),
  2. 32-row bands + PSRAM scratch (isolates band/traversal overhead),
  3. 32-row bands + internal scratch (isolates memory placement by comparing 2→3).

## Fact check: proposed full/exact-render speedups

The list is useful as an optimization inventory, but its numeric conclusion is too confident.

### 1. Two-core tile compositing “1.7–2×”

**PARTLY VERIFIED, RANGE OVERSTATED.** The interface and two internal lanes exist (`ViewportRenderOptions::execute`, `ViewportRenderer::lanes_`). Commit `45a5be8` wired a priority-1 second-core worker and physically validated dual-lane canonical rendering. However:

- that commit bundled dual-core compositing with provisional-geometry omission, so Git history does not provide an isolated dual-core A/B;
- geometry remains serial;
- current settled rendering does not use `ViewportRenderer` and has no executor seam;
- both cores can contend for PSRAM bandwidth.

A prior review's **1.2–1.7× canonical end-to-end** range is better supported than 1.7–2×. Applying it to settled rendering first requires partitioning painter-order-safe tiles/microtiles.

### 2. Solid interiors “1.5–3× on dense ink”

**PLAUSIBLE KERNEL/Raster-STAGE estimate, not an end-to-end receipt.** The canonical renderer uses 4×4 coverage sampling, although convex polygons already have a fully-inside shortcut. The current settled capsule renderer is already analytic at a one-pixel edge, but still performs distance/radius math over every pixel in each capsule AABB before discovering that an interior pixel is opaque. Scanline interior spans can remove much of that work.

The 1.5–3× range is credible for the raster stage on suitable thick/dense strokes. It must not be multiplied as a guaranteed whole-render factor; thin handwriting has proportionally less solid interior, and composite/geometry/publication remain.

### 3. Persist overview/tiles: “don't do full renders”

**ARCHITECTURALLY CORRECT, not a renderer speedup.** Persisted materializations can eliminate cold-start replay when revisions and checksums match. Incremental append updates can eliminate ordinary O(document) rebuilds. Exact export, stale/corrupt persistence, cache misses, old-operation undo, and format migrations still require replay or checkpoint recovery.

### 4. Geometry reuse across bands “1.2–1.5×”

**MECHANISM VERIFIED; RANGE UNMEASURED.** Current canonical exact refinement invokes `render_region` separately for every 32-row job (`interactive_pan_benchmark.cpp:1044-1072`), reconstructing ribbon geometry and querying candidates repeatedly. A geometry supertask binned into multiple publication tiles removes that duplication. The settled renderer already groups visible bands within a cache cell, so this lever mainly targets canonical exact work and prevents a naive production tile renderer from repeating the mistake.

The strongest historical receipt is better than the estimate: direct full-viewport dual-core canonical rendering measured **1.777 seconds for 1,000 handwriting strokes at 100%** (`V2_PHASE1_FINDINGS.md:18-35`), while the current coordinator's visible exact band sweep is roughly 4–5 seconds. Workload/code differences prevent a strict comparison, but they show that current band scheduling—not just pixel math—is a major part of the 4–5-second result.

### 5. Internal-RAM band destination “about 1.2×”

**PLAUSIBLE, UNMEASURED.** The canonical renderer already copies each PSRAM tile into an internal 32×32 working buffer, composites there, and copies it back. A production band/tile renderer can keep coverage and destination internal and perform one sequential final PSRAM write, avoiding repeated destination traffic and a later atlas copy. Step 0 tests only internal coverage; destination placement is the follow-up. No 1.2× end-to-end receipt exists yet.

### 6. PIE/assembly RGB565 blend “1.3–2× kernel”

**PLAUSIBLE KERNEL estimate, low-confidence end-to-end gain.** Packed SIMD can accelerate partial-alpha RGB565 blending after data is internal and the algorithm/data layout settle. But fully opaque pixels already bypass blending, and composite was about 100–130 ms of an approximately 800 ms settled result. Even a perfect 2× blend kernel cannot halve the full render. It belongs after profiling spans/microtiles, with randomized scalar-equivalence tests.

### Combined “3–6×; 5 s → 1–1.5 s; typical → 0.2–0.4 s”

**OVERSTATED as a forecast.** The proposed gains overlap, depend on content, and are not statistically independent, so multiplying their upper bounds is invalid. A more honest statement is:

- **Approximately 2 seconds for a 1,000-handwriting exact viewport is already historically demonstrated** by direct full-viewport dual-core canonical rendering.
- Returning the current exact path from repeated 32-row reconstruction toward a supertask/full-viewport shape should plausibly recover much of the 4–5 s → ~2 s gap.
- **1–1.5 seconds is a reasonable research target**, not a promise, after geometry reuse plus measured raster improvements.
- “Typical 0.2–0.4 seconds” needs a defined captured workload and a hardware distribution before it is a claim.
- Exact rendering is idle convergence. The product gate is valid fallback and settled output; persistence/incremental updates are more valuable than forcing canonical replay under 500 ms.

## Step 0 result (completed after this audit)

The controlled hardware experiment is complete; see `PROTOTYPE_EXIT.md` and `second_review_hardware_ab/8817a88-step0-scratch-ab.log`.

- grouped PSRAM: raster 437.398 ms, wall 553.283 ms;
- banded PSRAM: raster 470.262 ms, wall 588.240 ms;
- banded internal: raster 468.570 ms, wall 578.317 ms;
- every variant: `ink=117220 hash=c2c4938d`.

Internal scratch improved raster only 0.36% and total banded wall time 1.69%. The pre-registered ≥40% prediction failed. The PSRAM-scratch hypothesis is rejected for this workload; proceed to ordered spans/microtiles and geometry reuse.

## Production entry order

1. `PROTOTYPE_EXIT.md` now freezes the camera-aligned atlas except for evidence fixes.
2. Start production with two deep modules and host-test their interfaces:
   - **MaterializedCanvas:** complete overview, world-aligned tile keys/slots, revisions, overview-derived misses, incremental append application.
   - **DisplayScheduler:** single owner of staging/submission/completion, overlay composition, slot/version pinning, and physical timing.
3. Before committing the final pool sizes, implement an allocation/memory spike for the 330 KB overview, compact operation log, LOD/index tiers, tile pool, renderer workspace, and 1–1.5 MiB largest-block reserve.
4. Bring up zoom in gated order: 25%, 50%, 100%, 200%, 400%; treat 800% as optional. Measure first physical fallback, full fallback, settled, pan refusal count (must be zero in ordinary use), and full pen event-to-submit separately from touch-to-photon.
5. Add persisted materializations and checkpointed replay after the in-RAM architecture is stable. Optimize assembly only after the production profile is compute-bound.
