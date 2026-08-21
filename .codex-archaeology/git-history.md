# TinyDraw git-history archaeology

Generated 2026-08-19 from the complete local graph and the current repository. This is an evidence ledger, not article prose.

## Scope and method

- The repository contains 949 commits reachable from all refs, 906 on `main`, spanning 2026-08-09 through 2026-08-19. The narrative below follows the `main` lineage and uses side-branch commits only when they were later merged/cherry-picked or preserve a measured experiment.
- Commit timestamps are author timestamps in `+01:00`. Hashes are abbreviated where unambiguous.
- “Why” and “assumption changed” are drawn from commit bodies and contemporaneous receipts, not inferred from the final architecture alone. Where the only evidence is sequence or a docs edit, that is labeled.
- Benchmark claims here are included only to identify causal transitions. The performance-forensics workstream should remain the authority for publication classification.
- Confidence: **high** = commit plus contemporaneous receipt/test; **medium** = commit sequence plus later retrospective; **low** = plausible label without direct evidence.

## Development phases at a glance

1. **Pre-hardware Raster V1 (Aug 9):** native macOS core, Perfect Freehand port, streaming ribbon raster, dirty tiles, QEMU, PSRAM model, bounded Undo.
2. **Physical Raster V1 (Aug 10–11):** ESP32 display/touch bring-up, hardware-driven memory placement, panning, persistence/export, RP2350 port.
3. **Vector prototype (Aug 11–12):** vector authority, cached raster pan, staged zoom, analytic settled renderer, then rejection of the camera-aligned atlas.
4. **Production Vector V2 foundation (Aug 12–13):** complete overview plus sparse quality-tiered tiles, transactional publication, resumable work, retained multi-zoom views.
5. **Interaction and panel campaigns (Aug 14–16):** cold replay, drawing fanout, ring-buffer pan, tearing falsification, hardware physics, optical validation, cold-compute rewrite.
6. **Mental-model pivot (Aug 16):** authority-only ink commits behind a pending overlay; cross-zoom retention becomes idle work; settled AA becomes a background quality tier.
7. **Product completion and release (Aug 17–19):** SVG/PNG, whole-stroke history, crash-safe autosave, cooperative background work, IRAM placement, final AA/history/cold fixes, release.

## Milestones

### 1. Pre-hardware native loop establishes the real coordinate and replay contract

- **When / commits:** 2026-08-09 18:20–18:42; `4486705` bootstrap, `acfadce` timestamp-aware ink stream, `cf86c0e` replay golden, `88af0d7` Perfect Freehand oracle.
- **What / why:** A C++20 core and macOS SDL shell ran at the real 368×448 resolution before the board arrived. Replayed input produced exact snapshots; ASan/UBSan exercised the SDL-free core. This let work begin against stable input, coordinate, and rendering contracts without hardware.
- **Assumption changed:** The board was not required to validate ink semantics. Platform input became a source of the common `TouchPoint` lifecycle, not part of the drawing engine.
- **What it exposed next:** Retina coordinates were accidentally scaled twice (`03af023`, then `9023ac`), demonstrating that even the host harness needed explicit coordinate-space ownership.
- **Survival:** The platform-neutral core, native harness, replay tests, and physical-size target survive.
- **Evidence:** [`INITIAL_RESEARCH.md`](../docs/archive/2026-08-raster-and-vector-prototypes/INITIAL_RESEARCH.md), [`FINDINGS.md`](../docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md), `git show 4486705..88af0d7`.
- **Confidence:** high.

### 2. Perfect Freehand becomes streamed unionable ribbon geometry

- **When / commits:** 2026-08-09 18:44–20:28; `acf779a`, `bb8ab30`, `ee47cab`, `2ffec2b`, `8f6cbfc`, `c6578d8`, `5a94f13`, `94e326a`, `89b6743`, `c07d183`.
- **What / why:** The reference stroke points and outline were first ported directly, then rendered through grayscale coverage tiles. Self-overlapping geometry forced union-before-color because drawing segments directly produced pale seams/holes; the seam regression moved from 207/255 to 255/255 opacity. Streaming append-stable primitives then finalized old geometry while retaining a short live tail.
- **Assumption changed:** A completed polygon was not an acceptable live-stroke unit. The stable unit was a stream of unionable ribbon primitives with explicit provisional/final boundaries.
- **What it exposed next:** Although geometry became streamable, the raster path still replayed too much prior work on each sample.
- **Survival:** Perfect Freehand-derived velocity/pressure behavior, streaming ribbon geometry, union semantics, explicit Down/Move/Up/Cancel all survive; later V2 changes representation and settled rendering but retains shared ribbon authority.
- **Evidence:** [`FINDINGS.md`](../docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md) “Getting the ink right”; first appearances: `core/src/perfect_freehand.cpp` at `acf779a`, `coverage_tile.cpp` at `2ffec2b`.
- **Confidence:** high.

### 3. Quadratic live-stroke work is replaced by dirty-tile incremental raster

- **When / commits:** 2026-08-09 21:15–21:20; `c2d3d25`, `f336303`, `c45307d`, `5d01808`.
- **What / why:** The first live renderer redrew the whole stroke-so-far. Successive 50-point blocks grew from 41 ms to 1,040 ms; a 500-point XL host stroke took 4,479 ms. Streaming geometry plus a two-point tail and dirty 32×32 tiles reduced that workload to 220 ms (20.4× total-work reduction).
- **Assumption changed:** Bounding only geometry generation was insufficient; raster work and destination traffic also had to be bounded by the newest input.
- **What it exposed next:** The host result did not model ESP32 memory tiers, display transfers, DMA lifetime, or task-stack limits.
- **Survival:** Dirty regions, incremental active-stroke raster, fixed-capacity scratch, and workload gates survive in both generations, though Vector V2’s authoritative storage differs.
- **Evidence:** [`FINDINGS.md`](../docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md) “Keeping stroke cost bounded”; `git show c2d3d25^..5d01808`.
- **Confidence:** high.

### 4. QEMU and the PSRAM traffic model turn memory movement into a first-class metric

- **When / commits:** 2026-08-09 21:43–22:46; `1e685bb`, `39976a2`, `5a39b48`, `cb492d5`, `e28df0d`, `a4b1674`, `17e97a9`, `adae1ea`, `8c66082`, `b633d7d`, `fac7d42`, `2a238d1`.
- **What / why:** The core booted as ESP32-S3 firmware under QEMU, first headlessly and then visibly. QEMU modeled 8 MiB PSRAM. Canvas state was allocated by capability, dirty tiles submitted directly, and raster memory traffic was counted and budgeted. Loading overlapping coverage tiles once removed redundant traffic.
- **Assumption changed:** Algorithmic operation count alone was not a sufficient performance model; PSRAM reads/writes and display submissions were explicit resources.
- **What it exposed next:** Snapshot Undo and large canvases could dominate PSRAM even when drawing itself was bounded.
- **Survival:** Capability-based allocation, QEMU integration, memory-traffic gates, and internal-vs-PSRAM placement discipline survive. QEMU remains functional validation, not hardware performance evidence.
- **Evidence:** commits above; [`FINDINGS.md`](../docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md) “From prototype to physical device.”
- **Confidence:** high.

### 5. Undo moves from whole-canvas snapshots to touched-tile before-images

- **When / commits:** 2026-08-09 22:57–23:07; `0488ab9`, `fe98eb6`, `00c6112`, `0b54533`, `6bdad1c`, `4e3cd96`, `fd7aec2`.
- **What / why:** Ten Undo entries became bounded lists of dirty-tile before-images placed in PSRAM. A 1,000-point trace restored 209,920 bytes; later batching cut 105 display submissions to 14. Physical examples ranged from 10.8 ms for two tiles to 109.2 ms for a full 168-tile canvas.
- **Assumption changed:** History cost should scale with affected area, not canvas size.
- **What it exposed next:** Raster before-images made V1 Undo efficient but tied history to raster state; Vector V2 would later need whole-stroke authority transitions and replay.
- **Survival:** This is the supported Raster V1 history path. It is superseded, not reused, by V2 whole-stroke history (`fea929f`–`20e243e`).
- **Evidence:** [`FINDINGS.md`](../docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md) “UI and Undo”; first `tile_undo_history.cpp` at `0488ab9`.
- **Confidence:** high.

### 6. First real hardware makes input cadence, SRAM, DMA, and panel bounds real

- **When / commits:** 2026-08-10 09:49–12:26; `c2a6dc9`, `d1a2a45`, `45fdd18`, `1c65295`, `4727101`, `3c74673`, `1e8d3ff`, `fdd6d18`, `d7ec88f`, `1d34f09`.
- **What / why:** Waveshare V2 display/touch came up. First physical XL capture averaged 19.9 ms, peaked at 72.9 ms, and had a 105.1 ms lift tail. Coverage moved to internal SRAM; tight display bounds and second-core touch sampling followed. The CST820 produced positions only every 13–14 ms (75–77 Hz), so curve fitting had to bridge sparse hardware samples.
- **Assumption changed:** Host smoothness and compute timings did not predict on-glass feel; touch cadence and display transfer were binding components.
- **What it exposed next:** Optimizations could make either timing or line quality worse. `b63ff9a` diagonal-tile skipping and `0d35b50` sparse-input smoothing were immediately reverted; smoothing was reintroduced as a different row-based method in `d7ec88f`.
- **Survival:** Internal coverage, second-core sampling, tight bounds, hardware telemetry, and touch-aware smoothing survive in Raster V1; V2 later replaces much of the presentation path.
- **Evidence:** [`FINDINGS.md`](../docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md) physical progression table.
- **Confidence:** high.

### 7. The likely “meetup/demo” Raster V1 snapshot is documented, but not named in git

- **When / commits:** 2026-08-10 15:39 `ca41dc2`; 18:42 `53269b3`.
- **What / why:** `FINDINGS.md` was rewritten around “Numbers for the demo” and a “Five-minute demo,” then `DEMO.md` was added. At this point the demonstrable product was physical Raster V1: bounded ink, self-overlap, Undo/New, and measured 4,479→220 ms host work plus 72.9→16.1 ms worst board update.
- **Assumption changed:** This is a presentation checkpoint, not an architecture pivot.
- **What it exposed next:** The demo’s screen-sized canvas immediately led to panning and a larger raster world.
- **Survival:** The exact demo snapshot is historical; V1 remains a supported product generation.
- **Evidence / caveat:** `git show ca41dc2`, `git show 53269b3`; no tracked file or commit contains the literal word “meetup.” Treat identification with a specific meetup as **unverified** unless session logs provide it.
- **Confidence:** medium for demo snapshot; low for meetup attribution.

### 8. Raster panning discovers that moving the camera can be almost free—and that bus wins can be unstable

- **When / commits:** 2026-08-10 16:34–19:44; `3d2708c`, `9050187`, `4041ef2`, `d6af862`, reverted sparse-pan pair `070f3a3`/`08681da` → `ff2252d`/`ed6dd82`, then `a73821d`, `c6c5885`, `d856d8f`, `435cc7d`, `d19fdbd`, `07c7f0b`, `7cd16be`.
- **What / why:** A fixed larger world and pan tool were added. Early full-frame panning cost 71–74 ms. Changing the viewport origin without copying, streaming strided world rows, pairwise byte swaps, larger DMA chunks, and an 80 MHz bus produced 9.8–10.1 ms frames.
- **Assumption changed:** Raster pan did not require framebuffer shifting or rerendering; it could be an address/window change plus display transfer.
- **What it exposed next:** Random colored lines appeared at 80 MHz. Wi-Fi removal did not cure them; the stability build dropped the requested bus to 60 MHz. Later panel characterization proved 40/50/60 requests all physically ran at 40 MHz, while 80 MHz was a distinct and unsafe point.
- **Survival:** Direct world-window streaming is the V1 mechanism. The early sparse dirty-pan design was reverted. V2 inherited the lesson—keep rendering off the warm-pan path—but required a cache and ring presentation.
- **Evidence:** [`FINDINGS.md`](../docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md) “Larger canvas and image export”; [`CO5300_PANEL_LIMITS`](../docs/receipts/hardware/CO5300_PANEL_LIMITS_2026-08-15.md).
- **Confidence:** high.

### 9. Wi-Fi PNG is a dead end; low-memory persistence and USB export reshape the raster memory map

- **When / commits:** Wi-Fi prototype 2026-08-10 17:27–19:38 (`fccb68a` through `44efa5b`, removed by `ec9ad93`); persistence/export 2026-08-11 13:53–19:45 (`78b1b7f`–`db86bc1`, `ef36e6a`, `582f317`, `4347bc1`, `87ef340`, `e9990bf`, `47e684e`, RTC/NTP series).
- **What / why:** Captive Wi-Fi served a full-world PNG but iOS connectivity/caching was unreliable and the firmware removed Wi-Fi. Raster autosave then wrote only dirty 32×32 tiles from a 4 KiB staging buffer. Removing the full-world save shadow allowed a 3×3 world (2,967,552 bytes) plus ten Undos in 8 MiB PSRAM. PNG streamed to flash and appeared as `DRAWING.PNG` on a synthesized read-only FAT16 USB disk.
- **Assumption changed:** Export should be a bounded storage/USB pipeline, not a network service or full encoded image in RAM. Persistence staging must be sector-sized, not canvas-sized.
- **What it exposed next:** Raster authority consumed most of PSRAM and still provided only a fixed 3×3 world; richer pan/zoom and logical SVG/history demanded vector authority.
- **Survival:** Wi-Fi export is dead. Streaming PNG, synthetic FAT16 USB, flash staging, RTC timestamps, and V1 dirty autosave survive; V2 later exports both logical SVG and settled PNG and journals vector authority.
- **Evidence:** [`FINDINGS.md`](../docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md); first `png_encoder.cpp` at `4347bc1`, `fat16_disk.cpp` at `e9990bf`.
- **Confidence:** high.

### 10. The RP2350 port confirms that architecture follows memory and panel behavior

- **When / commits:** 2026-08-11 20:21–23:26; `efc372d` through `2e5f532`.
- **What / why:** A second 368×448 AMOLED target with only 520 KiB SRAM shared the core ink and toolbar. A 329,728-byte framebuffer ruled out the ESP32 canvas/Undo design. Native RGB565, fixed-point coverage, and full-width partial bands reduced average updates from 33.4 ms to 1.17–1.39 ms; arbitrary rectangles corrupted the panel and were removed/replaced.
- **Assumption changed:** “Same UI/core” did not imply the same raster storage or display-update strategy.
- **What it exposed next:** Touch still arrived around 60–68 Hz; 100 Hz mode and several atomic/lift experiments were reverted. Visual evidence outranked a disagreeing USB framebuffer capture.
- **Survival:** RP2350 remains a supported Raster V1 target, deliberately without the ESP32’s large canvas/history/persistence.
- **Evidence:** [`FINDINGS.md`](../docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md) “RP2350 port”; reversal commits `3530a9b`, `1a390d0`, `4f513ab`, `7362d5a`, `e8ac53f`.
- **Confidence:** high.

### 11. Vector authority first appears; profiling immediately identifies software divide/sqrt and PSRAM traffic

- **When / commits:** 2026-08-11 19:47–22:09; `a78590d`, `b8aec07`, `e1ebed6`, `e038f2a`, `99bd634`, `8bf7156`, `15247e3`, `2c09570`, `8242e0f`, `12eef72`, `45a5be8`.
- **What / why:** A bounded `VectorDocument`, camera transform, viewport rebuild, and live vector recording were added behind benchmark firmware. Tile-major compositing, dirty rects, two-core rendering, and geometry hoists followed. The ESP32-S3 FPU has no hardware divide/sqrt; hoisting reciprocals and removing redundant `hypot` cut device rasterization 25–30% without changing golden pixels.
- **Assumption changed:** The document could be compact vector authority, but the user-facing image still needed materialized raster state. The chip’s instruction set—not “floating point” generically—determined which arithmetic was toxic.
- **What it exposed next:** Rebuilding vectors during interaction made panning far slower than Raster V1.
- **Survival:** Vector authority, camera-independent geometry, tile-major rendering, no hot-loop division, and explicit performance matrices survive. The first bounded document and camera-aligned raster are prototype ancestors, not final storage.
- **Evidence:** [`V2_PHASE1_FINDINGS.md`](../docs/archive/2026-08-raster-and-vector-prototypes/V2_PHASE1_FINDINGS.md); commit body `8242e0f`.
- **Confidence:** high.

### 12. The first vector pan is 4–8× slower than Raster V1, forcing a materialized cache

- **When / commits:** 2026-08-11 22:09–23:28; `35d38e7`, `370710b`; 2026-08-12 `cbe314d`, `3aed0aa`, `3f79df9`.
- **What / why:** The vector prototype rendered while panning: typical frames 95–105 ms, handwriting 197–208 ms. Direct measurement showed Raster V1 at 25.445–25.458 ms because it only changed origin and streamed a strided window. The 3×3 raster canvas was reinterpreted as disposable materialized cache; panning inside cached pixels stayed a copy/present operation while background work filled/refined.
- **Assumption changed:** Vector rendering could not sit on the pan path. “Vector canvas” meant vector authority plus disposable raster materializations, not vector replay on every camera move.
- **What it exposed next:** Aggressive pan produced checkerboard misses; zoom switches took seconds; provenance/cancellation of cached content became the hard problem.
- **Survival:** The authority/materialization split is the central V2 architecture. The camera-aligned 3×3 atlas was explicitly frozen after measurement and replaced in production.
- **Evidence:** [`VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md`](../docs/archive/2026-08-raster-and-vector-prototypes/VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md) §§1–3.
- **Confidence:** high.

### 13. Zoom becomes a staged physical-publication problem

- **When / commits:** 2026-08-12 11:47–14:49; `87b475b`, `e00bea3`, review baseline `61dd649`, fixes `22cdb14`, A/B `63d99b9`, center-out strips `1469b8a`, realistic workload `79ded72`/`40f4615`.
- **What / why:** Removing an interaction-time atlas clear and caching validity work saved 91–105 ms per zoom: e.g. 100→50 fell 143→52 ms, 50→100 202→111 ms. Zoom fallback then published as center-out 22-row strips with DMA completion—not queue submission—as the endpoint. On a 1,000-stroke workload, first pixels physically completed in 6.6–16.2 ms and full fallback in 39–57 ms.
- **Assumption changed:** A zoom need not wait for exact reconstruction. Fast valid fallback, visible settled quality, and canonical exact refinement could have distinct completion contracts.
- **What it exposed next:** Cancellation could leave the atlas partial and poison the next zoom’s source; a valid-quality tag without generation/revision/source lifetime was unsafe.
- **Survival:** Multi-tier quality, transfer-completion endpoints, transactional publication, cancellation/provenance checks, and progressive presentation survive. The exact strip/atlas prototype does not.
- **Evidence:** [`VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md`](../docs/archive/2026-08-raster-and-vector-prototypes/VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md) §§4–6.
- **Confidence:** high.

### 14. Analytic settled rendering reveals misleading LOD metrics and rejects the PSRAM-scratch theory

- **When / commits:** 2026-08-12 14:52–21:44; `48ac59b`, `18ee81a`, `7a96158`, `caed9b5`, `ba6c392`, review fixes `87ff28f`, `6b1c406`, prototype close `e311a46`.
- **What / why:** Variable-radius capsules with one-pixel analytic coverage replaced expensive reconstructed-ribbon 4×4 supersampling for settled preview. Initial settled times were ~747–749 ms at 50%, 1.23–1.24 s at 100%, 902–905 ms at 200%. Fixed-spacing LOD appeared to beat 500 ms at 200%, but review showed it could delete loops/hairpins/pressure extrema and the metric stopped before the last physical transfer. Error-bounded LOD and physical endpoints replaced it.
- **Assumption changed:** A faster number was invalid if geometry or the endpoint changed. Adversarial shape preservation and display completion were part of the metric.
- **What it exposed next:** Grouping bands helped only modestly because capsule coverage dominated. A controlled A/B showed internal coverage scratch improved raster 0.36% and wall 1.69%, rejecting a predicted ≥40% win; repeated geometry from banding was worse.
- **Survival:** Capsule/analytic ideas inform later settled AA, but this renderer/atlas was retired. The evidence discipline and adversarial LOD tests survive.
- **Evidence:** [`VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md`](../docs/archive/2026-08-raster-and-vector-prototypes/VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md) §§7–16; [`PRODUCTION_CONTINUATION_HANDOFF`](../docs/archive/2026-08-vector-v2-foundation/PRODUCTION_CONTINUATION_HANDOFF_2026_08_12_NIGHT.md).
- **Confidence:** high.

### 15. Production Vector V2 replaces the atlas with overview + sparse, transactional materializations

- **When / commits:** 2026-08-12 22:38 through 2026-08-13 12:30; `eadc75f`, `e6bacb3`, `285e7f5`, `1edf0a9`, `81ee5f4`, `37f838b`, `c709508`, `29c4d1d`, `ded16de`, `119247d`, `c96a77a`, `6b5f6b0`, `20dbab7`, `3c92797`, `5a047e3`, `5dfd793`, `d172195`, `1916db0`, `b20da12`, `f35bef5`, `0bf15f2`.
- **What / why:** Prototype exit explicitly said not to continue the camera-aligned 3×3 atlas. Production introduced a complete low-resolution overview, sparse world-aligned high-resolution tiles, quality tiers, pinned/read-safe sources, fixed-capacity operation log, epoch-bound replay ranges, transactional publication, bounded display scheduling, and resumable tile production.
- **Assumption changed:** Cached state had to be addressed by world identity and quality/provenance, independent of the current camera. Publication was an ownership transaction, not “write pixels then set ready.”
- **What it exposed next:** The design was safe but only useful if cache retention, visible-first scheduling, and memory reserves worked on device across zooms.
- **Survival:** This is the foundation of released V2. Some seams were later decomposed, but the authority/overview/sparse-tile/transaction model remains.
- **Evidence:** [`PRODUCTION_CONTINUATION_HANDOFF`](../docs/archive/2026-08-vector-v2-foundation/PRODUCTION_CONTINUATION_HANDOFF_2026_08_12_NIGHT.md); [`PRODUCTION_GATE_PLAN`](../docs/archive/2026-08-vector-v2-foundation/PRODUCTION_GATE_PLAN_2026_08_13.md).
- **Confidence:** high.

### 16. Gate 1 proves retained multi-zoom tiles, then formalizes Raster V1 vs Vector V2

- **When / commits:** 2026-08-13 12:30–20:56; `fcf244f`, `be3df84`, `cbc1253`, `3eaffdd`, `b3d7870`, `3a57174`, `91b7b78`, `043d2cf`, `1433b87`, `8ca02ea`, `1893334`, `217eb2a`, `a4de2b8`, `b5a588d`, `e26396b`, `ee38887`, `1b3e27b`, `429be78`, then `3fd6f76`, `d88b594`, `ac52370`.
- **What / why:** Progressive tile fill got measured interaction bounds. Multi-zoom views and disjoint pan destinations stayed resident; paper tiles could be represented without raw slots; overlap pixels could be reused during pan. A 320-slot plan had device memory receipts and retained useful views. With the vector foundation accepted, code/docs explicitly named “Raster V1” and “Vector V2” and quarantined the retired prototype.
- **Assumption changed:** Missing high-resolution tiles should compose from overview, never refuse the camera. Fresh paper need not consume the same memory as raster detail. Product generations needed explicit boundaries rather than one continuously mutated renderer.
- **What it exposed next:** Product-level interaction—touch preservation, cold work, fixed chrome, and tearing—was now the bottleneck, not basic cache feasibility.
- **Survival:** World-aligned cache, paper/uniform specialization, retained views, and V1/V2 product boundary survive. Pool size later rose to 384, 448, and finally 604 after reserve measurements changed.
- **Evidence:** [`GATE_1_RECEIPT`](../docs/archive/2026-08-vector-v2-performance/GATE_1_RECEIPT_2026_08_13.md), [`GATE_1_CACHE_CLOSURE`](../docs/archive/2026-08-vector-v2-performance/GATE_1_CACHE_CLOSURE_2026_08_13.md).
- **Confidence:** high.

### 17. Product integration separates cold compute from display commit and makes tearing measurable

- **When / commits:** 2026-08-13 22:05 through 2026-08-14 02:24; `a91630d`, `369fe30`, `3d4bde4`, `ce214bc`, `8df3584`, `2b02045`, `9e30415`, `3a2949e`, `9b62d75`, `593e21b`, `fe678d9`, `8d422f1`, `2d547f0`, `8894f22`, `c040b62`, `11856ca`, `ec88fb4`, `c49eeda`.
- **What / why:** Deterministic V2 navigation and latency telemetry reached hardware. Cold rasterization became resumable and distinct from display commit; recent zooms got soft protection. CO5300 TE was measured and full-frame updates synchronized to panel scan. V2 chrome was isolated from canvas updates.
- **Assumption changed:** “Render time” needed separate compute, publication, transfer-completion, and tear-synchronization components. Fixed overlays could not contaminate reusable canvas pixels.
- **What it exposed next:** Paced cold replay still had zoom-amplified overlap and long uninterruptible work; later, software TE success proved insufficient to establish optical tear-freedom.
- **Survival:** Resumable background rendering, chrome/canvas separation, physical transfer endpoints, and panel telemetry survive. The first TE synchronization policy was superseded after optical falsification.
- **Evidence:** commits above; [`CORRECTNESS_CLOSURE`](../docs/receipts/vector-v2/CORRECTNESS_CLOSURE_2026_08_14.md).
- **Confidence:** high.

### 18. Cold replay’s first large optimization stack changes from geometric replay to saturation-aware row work

- **When / commits:** 2026-08-14 08:45–16:22; `9542714`, `0560525`, `3eaeadb`, `2203bdc`, `f4bd467`, `0370304`, `7a3ccbd`, `5285648`, `f950e27`, `52ec99a`, `927030d`, `a3ac4fc`, `a42fc21`, `3bc5e95`, `e59784e`, `092f2a3`, `264b60e`, `0cba4ad`.
- **What / why:** A paced hardware corpus reproduced expensive overlapping cold fills. The path gained distance culling, redundant-subdivision removal, division hoists, capsule interior spans, scanline edge tracking, straight-run coalescing, tapered row clipping, finalized-pixel masks, exact row saturation, and in-place chunk commits. The major conceptual step was newest-first replay: once newer paint/eraser fully determined a pixel, older operations could be skipped.
- **Assumption changed:** Cold replay cost was not inherently proportional to document length; painter-order saturation could prove that older work was irrelevant.
- **What it exposed next:** Warm drawing on a multi-zoom cache still synchronously painted every affected resident tile, so a richer cache made drawing slower.
- **Survival:** Bounds/occupancy culling, newest-first replay, finalized masks, saturated row/surface skipping, resumable work, and prepared geometry form the released cold renderer. Some scanline experiments were later rejected/reworked.
- **Evidence:** [`PERFORMANCE_CHRONICLE`](../docs/PERFORMANCE_CHRONICLE.md) §1 and [`LONGSTROKE_COLDRENDER_INVESTIGATION`](../docs/receipts/vector-v2/LONGSTROKE_COLDRENDER_INVESTIGATION_2026_08_14.md).
- **Confidence:** high.

### 19. The first “drawing latency closure” trades cross-zoom retention for responsiveness—and is later invalidated on glass

- **When / commits:** baseline `205fefe` 2026-08-14 22:38; attempted closure `fd4e526`, `a652666`, `eccfc72`, `b62f81a`, `1848cc6` through 23:18; glass fix `da99311` 2026-08-15 10:40.
- **What / why:** A new mixed-zoom gate showed 21–130 ms per chunk because warm-cache commits painted 700–960 tiles per stroke across zooms. Active-zoom-only mutation plus a 10 ms wall budget reduced gated chunks to 11.8–13.8 ms. This dropped non-active tiles and accepted 0.14–0.6 s revisit refills. The gate also exposed a vacuous earlier zero-fallback test caused by empty affected bounds.
- **Assumption changed:** Low input latency could be purchased by invalidating derived cache state. Cache warmth was not unconditionally good; fanout was the cost.
- **What it exposed next:** Product glass still showed 15.5–35.8 ms chunks, 92–143 ms loop gaps, and blur-then-sharpen because visible tiles could be dropped. `da99311` exempted visible tiles and cut chunks to 32 samples, but the deeper synchronous-commit problem remained.
- **Survival:** The gate and explicit damage accounting survive; the priority-only synchronous policy was superseded by committed overlay/deferred absorption.
- **Evidence:** [`PERF_ROUND_2_BASELINES`](../docs/receipts/vector-v2/PERF_ROUND_2_BASELINES_2026_08_14.md), [`DRAWING_LATENCY_CLOSURE`](../docs/receipts/vector-v2/DRAWING_LATENCY_CLOSURE_2026_08_14.md), whose header explicitly marks the closure physically falsified.
- **Confidence:** high.

### 20. Toroidal ring + beam race apparently closes pan, then severe tearing falsifies the model

- **When / commits:** 2026-08-14 23:35–2026-08-15 12:41; `aba02bc`, `5293823`, `b76b992`, `2e07671`, `1cd7f1b`, `4022917`, `da99311`, `efb1586`, reviews `973fd7e`, regression recovery `d82a87f`/`c86f3ac`.
- **What / why:** Baseline warm pan was 67.3 ms: ~15 ms PSRAM memmove, ~7 ms exposed compose, ~8 ms tear wait, ~9 ms staging, ~20 ms present. A toroidal frame ring reduced scroll to pointer math; de-rotation folded into byte-swap staging. Beam racing and just-in-time exposed compose reported 28.1 ms average, p95 32.95 ms. Software gates called it synchronized.
- **Assumption changed:** The renderer/presenter could pipeline work against the scanning beam and hide compose inside DMA.
- **What it exposed next:** Manual glass showed severe tearing and fixed-row overlay corruption despite zero software sync failures. The writer/beam model mishandled the wrapped band, and later measurements showed the bus near beam parity. Review fixes restored correctness but regressed p95 to 47.5–48.6 ms. Several overlay designs then measured 36–69 ms or overflowed and were rejected.
- **Survival:** The ring remains; speculative beam racing does not ship. Full-width ordered staging, explicit chrome composition, and optical evidence replace software-only tear claims.
- **Evidence:** [`PAN_FLOOR_CLOSURE`](../docs/receipts/vector-v2/PAN_FLOOR_CLOSURE_2026_08_15.md) and [`REVIEW_ROUND_CLOSURE`](../docs/receipts/vector-v2/REVIEW_ROUND_CLOSURE_2026_08_15.md), both preserved with falsification headers; [`PAN_DESIGN_EXPERIMENTS`](../docs/receipts/vector-v2/PAN_DESIGN_EXPERIMENTS_2026_08_15.md).
- **Confidence:** high.

### 21. Panel characterization and camera evidence replace folklore with physics

- **When / commits:** 2026-08-15 19:20–2026-08-16 09:32; `6057f9c`, `f7e447c`, `0b776c1`, `58683f0`, `55f7449`, `c70d4f8`, `4048d3a`, `b5bdd78`, reverted overlap attempts `2b8776a`/`2e267ec`, accepted staging invariant `7e9d043`, `2a1a3c8`, `ad28f4f`, `3b4abc4`, then cache-lifetime split `52f63e0`.
- **What / why:** A panel probe measured TE at 16.773 ms/59.62 Hz, actual 40 MHz transfer for every 40/50/60 request, 18.0 ms best full frame, 44 µs marginal transaction cost, and a 29.4 FPS full-frame TE-synced ceiling. GETSCANLINE always returned zero, so optical capture was the only tear oracle. Camera protocol included a known-tearing positive control; later Block B found the rising-edge full-frame cadence clean across 1,495 analyzed frames.
- **Assumption changed:** Software timing flags could not certify tear-freedom. Presenter design had to respect measured wire/beam physics and be closed optically.
- **What it exposed next:** Chrome staging exceeded per-strip wire budgets. Prebuilding dynamic patches, caching chrome, fusing ring de-rotation/byte order, and keeping the ring canvas-pure created an invariant: every measured strip staged within its wire time. Clean glass came at ~20 FPS before later product tuning.
- **Survival:** Panel probe, 40 MHz effective model, rising-edge ordered sweep, canvas-pure ring, cached chrome staging, optical protocol, and “hardware verdict beats model” survive.
- **Evidence:** [`CO5300_PANEL_LIMITS`](../docs/receipts/hardware/CO5300_PANEL_LIMITS_2026-08-15.md), [`blockB protocol`](../benchmark-results/blockB-optical/PROTOCOL.md), [`STAGING_INVARIANT_RECEIPT`](../benchmark-results/wave2-compositor/STAGING_INVARIANT_RECEIPT.md), [`GLASS_OBSERVATIONS`](../benchmark-results/wave2-compositor/GLASS_OBSERVATIONS.md).
- **Confidence:** high.

### 22. Visual-first ink and the cold-compute campaign push toward “mechanistic simplicity”

- **When / commits:** 2026-08-16 09:56–18:34; `a2ad3f8`, `19ebbe3`, frozen baseline `a560d20`, `ed23f9d`, `d2f3988`, `a3e8ff8`, `f2f6da7`, `11edbd7`, `bdd95e7`, `75c9145`.
- **What / why:** Live provisional ink was shown before committed work and extended to the raw fingertip, while committed authority stayed curved. The frozen 910-op/12,157-sample cold corpus started at 1,269.157 ms max at 400%. Stateless windowed spans, device-native floor/ceil/reciprocal math, a padded inverse-sqrt seed used only as a conservative bound, internal-SRAM scratch, once-prepared curve units, operation-level y-sorted chord sweeps, direct publish, and O(1) raw-slot metadata reduced 400% to ~507 ms and put 50/100/200% under 500 ms.
- **Assumption changed:** The winning path was not a clever global structure; it was a sequence of exact local proofs that removed repeated work and matched Xtensa/SRAM/PSRAM mechanics. Memory/code placement could change timing after the algorithm was sound.
- **What it exposed next:** IRAM placement became necessary because unrelated code layout moved a tight strip loop 2–3%; a previously shelved metadata win became safe only after panel transport was pinned. Interaction still failed because synchronous committed ink did too much.
- **Survival:** Nearly the full cold stack survives, including IRAM-pinned kernels later extended in `7f4cdb8`/`0be84f4`. “Demoscene” is explicit in the later external-review brief; “mechanistic simplicity” is a good description but the exact phrase was not found in tracked history.
- **Evidence:** [`COLD_COMPUTE_CAMPAIGN_RECEIPT`](../benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md), [`PERFORMANCE_CHRONICLE`](../docs/PERFORMANCE_CHRONICLE.md) §1, commit bodies `ed23f9d`, `bdd95e7`, `75c9145`.
- **Confidence:** high for mechanisms; medium for the label.

### 23. Recorded ink traces and attribution falsify the visible-pixelation diagnosis

- **When / commits:** 2026-08-16 14:15–20:35; `af9809a`, `8712ac6`, `9e67220`, `7513aa1`, `16d223a`, `7a78dd7`, `f3beb5c`, then `fb2a05b`, `9e364af`, `39352d6`.
- **What / why:** A hardware trace recorder captured the owner’s real strokes and replayed them through the production touch path. Phase timings stopped calling the whole append “budget.” Every rerender/fallback got a cause counter (“déjà-vu oracle”). The alleged mid-stroke pixelation mechanism was falsified: drop counters stayed zero; benchmark views had been unwarmed.
- **Assumption changed:** Visual complaints needed event/geometry/publication attribution, not inference from a nearby red benchmark. Observability became part of architecture because the existing profiler could not see cross-iteration causes.
- **What it exposed next:** Genuine input-path cost remained: synchronous commit still included visible raw painting, materialization, and overview work. The new attribution justified the committed-overlay design.
- **Survival:** Frozen trace corpus, production-path replay, cause ledgers, and named red gates survive; ordinary firmware compiles much of the diagnostic ledger out.
- **Evidence:** [`INK_TRACE_HARNESS`](../docs/INK_TRACE_HARNESS.md), [`ink trace baseline`](../benchmark-results/ink-trace-replay-baseline/BASELINE.md), `git show 39352d6`.
- **Confidence:** high.

### 24. Authority-only commit + pending overlay is the decisive latency model change

- **When / commits:** 2026-08-16 21:01–21:36; `afa3239`, `79ce37b`, `4813547`, `902d016`, `ed3a2c1`.
- **What / why:** `VectorDocument` could advance while materialized canvas lagged by a pending operation range. The input path copied/validated authority only; presenters overlaid pending operations through the same rasterizer; idle polls absorbed operations and performed one exact swap when caught up. Host tests proved trailing-canvas + overlay bit-identical to full replay at every pending depth.
- **Measured change:** Worst mixed-draw append fell 19,324 µs → 173 µs (111×), green for the first time. Work moved to 13–30 ms idle absorption slices. Lift tail fell from 87–199 ms to 10–34 ms, then typically 4–5 ms after drains were restricted to empty-poll iterations.
- **Assumption changed:** Materialized pixels did not need to be current when authority committed; they only needed a coherent revision pair plus an exact presentation overlay.
- **What it exposed next:** The synchronous reason for dropping cross-zoom tiles disappeared. Idle absorption could maintain all remembered views and solve revisit “déjà vu.”
- **Survival:** This is the released ink protocol, including 24-op high-water fallback, pending overlay, coherent authority reads, and cooperative absorption.
- **Evidence:** [`committed overlay receipt`](../benchmark-results/committed-overlay/RECEIPT.md), [`VECTOR_V2_COMMITTED_OVERLAY_DESIGN`](../docs/design/VECTOR_V2_COMMITTED_OVERLAY_DESIGN.md).
- **Confidence:** high.

### 25. Cross-zoom “déjà vu” vanishes once retention moves off the input path

- **When / commits:** 2026-08-16 23:05; `50ba72f` (and duplicate/cherry-pick `d5097b1`), docs `bdd2cae`/`06b6095`.
- **What / why:** Idle absorption repainted affected resident raw tiles at every zoom and materialized fresh-paper tiles inside remembered viewports under a separate 25 ms idle budget. Three iterations were needed: raw retention alone did not fix uniform drops; enumeration had to include remembered views; the old 10 ms budget silently skipped 150–208 tiles per XL stroke.
- **Measured change:** Revisits at 50/100/200% went from 4/9/16 missing tiles and 188.0/319.8/326.3 ms refill to zero missing and ~0.37–0.38 ms.
- **Assumption changed:** An optimization that was correct on the synchronous path—drop other zooms—became wrong after the overlay changed where work could run. Architectural improvements invalidated an earlier tradeoff.
- **What it exposed next:** With interaction and revisit largely solved, visible stroke quality—quantization, smoothing, and anti-aliasing—became prominent.
- **Survival:** All-zoom idle retention, remembered-view materialization, separate idle budgets, and region-only battery refresh survive.
- **Evidence:** [`DEJAVU_FIX_RECEIPT`](../benchmark-results/committed-overlay/DEJAVU_FIX_RECEIPT.md), commit body `50ba72f`.
- **Confidence:** high.

### 26. V1-vs-V2 jaggedness is traced to sample quantization; settled AA becomes an idle quality tier

- **When / commits:** 2026-08-16 21:44–23:46; host probes `f813a8f`, `9646969`; sixteenth-world samples `c6a4933`/`bc5e0e3`; streamline 0.4 `3df6f0f`/`693bcdf`; device settled AA `4ea05db`/`bfebbcd`, speed/presentation `9549e32`/`45da80e`.
- **What / why:** A float reference proved V2’s careful-stroke zigzags came from quarter-world sample quantization—one screen pixel at 400%—not the ribbon algorithm. Sixteenth-world units fit the same `uint16`, giving 0.25 px at 400% with zero storage cost; joint p95 improved 30–40%. Streamline 0.4 improved slow curves, but measurements revealed tens-of-pixels raw trailing gap at 1 kHz; provisional geometry was therefore extended to the raw fingertip while committed/SVG authority stayed filtered.
- **AA change:** Newest-first tapered-capsule analytic coverage ran only after drain/fill/repair were quiet, publishing a settled quality tier. The 40 KiB workspace caused a +9 ms cold regression when allocated mid-heap and none when allocated last—PSRAM cache-set/allocation order mattered.
- **What it exposed next:** AA appearance was accepted, but cooperative progression could still violate slice targets (later 76.416 ms for one tile). Export needed to preserve logical strokes and render settled PNG separately.
- **Survival:** Sixteenth-world storage, visual-first raw tip, streamline 0.4, shared ribbon curves, and settled analytic AA survive. Several AA microprobes were later rejected.
- **Evidence:** [`PERFORMANCE_CHRONICLE`](../docs/PERFORMANCE_CHRONICLE.md) §5; [`settled AA receipt`](../benchmark-results/settled-aa-prototype/RECEIPT.md).
- **Confidence:** high.

### 27. Product features deliberately come after rendering semantics: SVG/PNG, whole-stroke history, vector autosave

- **When / commits:** SVG core first appears 2026-08-16 00:25 at `8209d28`; product integration and the remaining feature sequence run 2026-08-17 00:56–20:28: SVG `f85af8f`, `d1e834e`, `57fdfa7`; minimap/NTP tuning; PNG `6b6cb05`, `64381f0`; coherent reads `6dff44b`; history `fea929f`–`20e243e`; autosave `95f9899`, `d5e2850`, `cf6c60c`, `d497ccd`, `fdd0b5f`, `60682d7`.
- **What / why:** SVG exported one logical path per stroke from vector authority; settled AA world rows produced PNG beside it on the USB volume. Whole-stroke Undo/Redo moved authority and replayed affected state, replacing Raster V1 before-images. Autosave became checkpoints plus an append journal with aligned commits and safe-tail recovery, run in the background.
- **Assumption changed:** Export, history, and persistence all had to consume the same coherent logical authority; raster pixels were derived output. Feature order had been intentionally delayed until authority/revision semantics stabilized.
- **What it exposed next:** These background features introduced new caller-latency and reconstruction work. Whole-stroke history initially required expensive tile rebuilds; autosave checkpoint construction needed cooperative staging.
- **Survival:** All three ship. Later fixes add eraser SVG masks, smooth shared curve subdivision, PNG tap dots, preserved-tile history swaps, and incremental autosave staging.
- **Evidence:** [`svg receipt`](../benchmark-results/svg-export-2026-08-17/RECEIPT.md), [`v2 autosave`](../benchmark-results/v2-autosave-2026-08-17/RECEIPT.md), [`authority undo design`](../docs/design/VECTOR_V2_AUTHORITY_UNDO_DESIGN.md).
- **Confidence:** high.

### 28. Final performance round turns every background subsystem into resumable quanta and spends measured memory honestly

- **When / commits:** 2026-08-18 11:35–23:28; hot-loop/index/fill `6acf46b`, `4c823e8`, `919714b`, ring locality `43f1ecd`; cooperative pipeline `cb5b2d7` through `2a9fc86`; autosave `0a7e0d2`, `2696775`; history/spatial/touched-span/IRAM `2d9b23a`, `7a3534b`, `cc69e12`, `7f4cdb8`, `106495f`, `0be84f4`; pool/flash/history/AA `286fa4b`, `e9abacd`, `541e4a5`, `9c806a6`, `efff39b`, `cdcc8d5`.
- **What / why:** External review and glass runs showed that good totals still hid long uninterruptible slices. View composition resumed by row bands; operation absorption, full refresh, settled rendering, metadata commit, and autosave journals/checkpoints all became resumable or staged. Background work yielded to touch and abandoned unpublished producer work. Spatial candidates bounded sparse history replay; raster/AA kernels moved to IRAM.
- **Memory change:** The tile pool rose to 604 only after replacing a fictional 1.5 MiB export reserve with the measured 291,484-byte export peak and validating sequential autosave→export ordering. Full 16 MiB flash was partitioned. Whole-stroke history began swapping preserved tiles and showing an hourglass for genuine reconstruction.
- **Assumption changed:** Cooperative latency is a property of every caller and commit phase, not merely the main renderer. Memory reserve gates must reflect measured concurrent lifetimes, not attractive round numbers.
- **What it exposed next:** Final torture documents found tap-dot inconsistency, popup contact splitting, SVG eraser/curve issues, and more cold/history work that could be skipped by saturation/occupancy.
- **Survival:** Cooperative background pipeline, 604 slots, IRAM kernels, measured reserve gates, spatial replay, and preserved-tile history ship.
- **Evidence:** [`PERFORMANCE_REVIEW_ROUND`](../docs/receipts/vector-v2/PERFORMANCE_REVIEW_ROUND_2026_08_18.md), [`FINAL_PERFORMANCE_BASELINE`](../docs/receipts/vector-v2/FINAL_PERFORMANCE_BASELINE_2026_08_18.md), [`F20`](../docs/receipts/vector-v2/F20_AUTOSAVE_CALLER_LATENCY_2026_08_18.md).
- **Confidence:** high.

### 29. Release closure fixes torture-document edge cases and records same-revision evidence

- **When / commits:** 2026-08-19 00:01–12:23; edge/saturation AA `a2dce6a`, `040a294`; tap dot `c6be49a`; SVG masks/curves `9473469`, `0eb47b9`, `4f43f7e`, `b6630d6`; AA/history/cold skips `8461ae5`, `2be0a0f`, `ac52a67`, `8bbeb40`, `e3f285c`, `c986e42`, `b0667e5`, `b428691`; product fixes through `a5db58d`; release docs `9d91b81`, `fd05d7d`.
- **What / why:** A journal-derived owner torture drawing exposed one-sample “Schrödinger dots,” split popup contacts, SVG erasers rendered incorrectly, gesture-chunk seams, and history overdraw. Exact dot rendering, SVG masks/shared ribbon curves, opaque-AA fast paths, saturated replay stops, occupancy preservation, and redundant-preflight removal closed these. Several render-group/batch-width/AA probes were measured and explicitly rejected.
- **Release evidence:** At `a5db58d`, 31/31 debug and release targets, 13/13 sanitizer targets, and format passed. Physical 604-slot battery was all green; general cold walls were 389.942/383.159/456.961/492.793 ms at 50/100/200/400%. Product boot restored generation 586 with 229 active/retained operations. `fd05d7d` marks V2 released; annotated tag `v2` dereferences to that commit.
- **Assumption changed:** “Feature complete” (`v2-feature-complete-pre-cleanup`, commit `3f23c09`) was not release: cleanup, same-head revalidation, torture data, and product boot evidence were still required.
- **What it exposes next:** Remaining work is progression performance, high-zoom history timing, rare rerender attribution, one-shot full-refresh poll gaps, and final 20-reset 400% closure under normal journal activity.
- **Survival:** This is current released state.
- **Evidence:** [`VECTOR_V2_RELEASE`](../docs/receipts/vector-v2/VECTOR_V2_RELEASE_2026_08_19.md), [`OVERNIGHT_RELEASE_CLOSURE`](../docs/receipts/vector-v2/OVERNIGHT_RELEASE_CLOSURE_2026_08_19.md), [`PERFORMANCE_CHRONICLE`](../docs/PERFORMANCE_CHRONICLE.md) §8.
- **Confidence:** high.

## First appearances of major ideas

| Idea | First tracked appearance | Notes |
|---|---|---|
| Native/macOS physical-scale loop | `4486705` (Aug 9 18:20) | Before hardware; exact replay tests followed within minutes. |
| Perfect Freehand stroke points | `acf779a` (Aug 9 18:44) | Outline `bb8ab30`; end-to-end `ee47cab`. |
| Coverage/AA tiles | `2ffec2b` (Aug 9 19:19) | Unionable ribbons `8f6cbfc`. |
| Streaming append-stable geometry | `89b6743` (Aug 9 20:25) | Host used stream at `c07d183`. |
| Dirty-tile active raster | `c2d3d25` (Aug 9 21:15) | Incremental fix `f336303`. |
| ESP32/QEMU target | `1e685bb` (Aug 9 21:43) | Visible display `5a39b48`; modeled PSRAM `e28df0d`. |
| Dirty-tile Undo | `0488ab9` (Aug 9 22:57) | First history representation. |
| Physical Waveshare V2 | `c2a6dc9` (Aug 10 09:49) | First real display/touch. |
| Fixed pannable raster world | `3d2708c` (Aug 10 16:34) | Direct no-copy viewport `d856d8f`. |
| Streaming PNG | `4347bc1` (Aug 11 16:46) | Flash `87ef340`; FAT16 `e9990bf`; USB `47e684e`. |
| Vector document | `b8aec07` (Aug 11 19:52) | Camera `e1ebed6`; live vectors `99bd634`. |
| Cached pan/progressive zoom | `35d38e7` (Aug 11 22:09) | Architectural prototype. |
| Analytic capsule settled render | `48ac59b` (Aug 12 14:52) | Later reworked as per-tile settled AA. |
| Production materialized canvas | `eadc75f` (Aug 12 22:38) | Sparse/overview foundation. |
| Quality-tiered visible tile producer | `d172195`/`1916db0` (Aug 13 12:05) | Provisional vs refined. |
| Explicit Raster V1 / Vector V2 boundary | `d88b594` (Aug 13 20:43) | Supported generations. |
| TE measurement | `8d422f1` (Aug 14 00:58) | Optical protocol arrives Aug 15. |
| Toroidal frame ring | `2e07671` (Aug 15 00:05) | Survives after beam-race policy is discarded. |
| Panel physics probe | `f7e447c` (Aug 15 21:03) | Establishes actual 40 MHz/wire/TE facts. |
| SVG geometry/export | `8209d28` (Aug 16 00:25) | Exact ribbon-to-SVG core; product integration `f85af8f`; one logical stroke/path `d1e834e`. |
| IRAM panel transport | `11edbd7` (Aug 16 18:14) | Raster kernels `7f4cdb8`; settled AA kernels `0be84f4`. |
| Pending authority/canvas range | `afa3239` (Aug 16 21:01) | Authority-only commit `79ce37b`. |
| Sixteenth-world packed samples | `c6a4933` (Aug 16 22:11) | Same storage, 4× coordinate resolution. |
| Device settled AA tier | `bfebbcd` (Aug 16 23:26) | Main-line commit; background tile settling. |
| Whole-stroke V2 history | `fea929f` (Aug 17 19:10) | Wired at `20e243e`. |
| V2 checkpoint+journal autosave | `d5e2850`/`cf6c60c` (Aug 17 19:54) | Background integration `fdd0b5f`. |

## Reversals, rejected paths, and corrected claims

1. **Diagonal dirty-tile skipping** — `b63ff9a` reverted by `3bd5c21`; hurt correctness/timing assumptions.
2. **First sparse hardware smoothing** — `0d35b50` reverted by `ee1c6b8`; a different smoothing approach landed as `d7ec88f`.
3. **Sparse pan redraw planning** — `070f3a3`/`08681da` reverted by `ff2252d`/`ed6dd82`; direct world streaming won for V1.
4. **80 MHz display path** — gave 9.8–10.1 ms raster pan but produced colored artifacts; stability returned to the 40 MHz-effective configuration.
5. **Wi-Fi export** — complete prototype, removed at `ec9ad93` because iOS/connectivity UX was unreliable; replaced by flash + USB MSC.
6. **RP2350 partial arbitrary rectangles / input experiments** — multiple immediate reverts (`3530a9b`, `1a390d0`, `4f513ab`, `7362d5a`, `e8ac53f`); full-width bands and IRQ-gated touch survived.
7. **Camera-aligned 3×3 vector atlas** — proved cached pan/progressive zoom, then explicitly frozen at `e311a46`; unacceptable mutation repair/refusal/provenance behavior.
8. **Fixed-spacing LOD and “<500 ms settled”** — invalidated by adversarial geometry and wrong endpoint; not a win.
9. **Internal PSRAM coverage-scratch hypothesis** — predicted ≥40%; measured 0.36% raster/1.69% wall. Rejected.
10. **First drawing-latency closure** — gate reported ≤13.8 ms after dropping cross-zoom tiles, but product glass showed lag/blur; superseded by authority-only commit.
11. **Beam-race pan closure** — software showed ~28 ms/p95 32.95 ms, but glass tore severely; historical receipt now carries a falsification header.
12. **Five overlay/staging pan designs** — row splits, PSRAM scratch, internal scratch, undersized ring backup, and serialized sized backup all lost (36–69 ms or overflow).
13. **Overlap staging during tear wait** — `b5bdd78` reverted by `2b8776a`; bounded retry `c8a4755` reverted by `2e267ec`. Both violated optical/pacing headroom.
14. **Cold-render experiments:** 4-sample SSAA, word-mask scans, scanline recurrence, wide bands, flat row budgets, and publication batching all lost or broke interaction gates. Receipts preserved.
15. **AA microprobes/render grouping** — larger/narrower groups and wider scan batches rejected on Aug 19 (`b087da0`, `391eaab`, `1360781`); divergent SVG subdivision rejected `fd87ec3` in favor of shared ribbon curves.
16. **Feature-complete tag vs release:** `v2-feature-complete-pre-cleanup` points to `3f23c09` (Aug 17), but release required two days of cleanup, same-revision tests, hardware gates, and product boot; `v2` dereferences to `fd05d7d`.

## Historical builds worth demoing or capturing

These are candidates; no historical worktree was built or flashed during this read-only archaeology pass.

| Build(s) | Demonstration value | Caveat / suggested capture |
|---|---|---|
| `ee47cab` vs `c6578d8`/`94e326a` | Direct PF outline vs unioned coverage; show self-crossing seams and join gaps disappearing. | Native snapshot is sufficient. |
| Before `c2d3d25` vs `f336303` | The cleanest “more points make every new sample slower” demonstration: 4,479 ms vs 220 ms total host workload. | Use the frozen 500-point XL replay; label total workload, not per-frame speedup. |
| `c2a6dc9` vs post-`4727101`/`3c74673` | First hardware ink (72.9 ms worst, 105.1 ms lift) vs internal-SRAM/tight-bound path. | Physical hardware/video valuable; exact historical telemetry already exists. |
| `a73821d` vs `d856d8f`/`435cc7d` | V1 full-frame/copy pan (71–74 ms) vs direct world streaming (~10 ms at 80 MHz). | Do not flash 80 MHz for a long session; it was unstable. A 40 MHz-safe descendant is preferable for visual demo. |
| `ec9ad93^` | Short-lived captive Wi-Fi export. | Good product dead-end anecdote; poor live demo because the failure was ecosystem reliability, not a deterministic visual defect. |
| `35d38e7` / `370710b` vs Raster V1 | First vector pan at 95–208 ms vs raster’s 25.45 ms. | Strong architecture demo: show checkerboard/misses, then explain cache pivot. |
| `61dd649` vs `22cdb14`/`63d99b9` | Removing atlas clear saves ~100 ms per zoom. | Hands-free zoom driver gives apples-to-apples logs; screen recording can show first response. |
| `1469b8a` | Center-out strips: first physical feedback in 6.6–16.2 ms, complete fallback in 39–57 ms. | High-frame-rate video could make staged publication legible. |
| `ba6c392` | Early analytic settled renderer and progressive zoom. | Label early LOD/endpoint numbers as invalid; value is qualitative architecture, not benchmark proof. |
| `205fefe` → `1848cc6` → pre-overlay `19ebbe3` | Shows how a benchmark “closure” can still feel laggy/blurred on glass. | Use preserved manual glass logs/video if available; rebuilding alone may not reproduce exact allocation/layout. |
| `1cd7f1b` or `c86f3ac` positive control vs `52f63e0`/release | Tearing is ideal high-speed-camera material: software-green beam race tears; ordered rising-edge compositor is clean. | Follow [`blockB optical protocol`](../benchmark-results/blockB-optical/PROTOCOL.md); avoid naked-eye-only claims. |
| `a560d20` vs `ed23f9d` vs `75c9145` | Frozen cold corpus collapses 1,269→669→~507 ms while pixels remain exact. | Best rendered as an instrumented visualization using `docs/BLOG_NOTES.md`; wall-clock video alone understates why. |
| `19ebbe3` vs `79ce37b` | Synchronous committed ink (19.324 ms worst append) vs authority-only overlay (173 µs). | High-speed touch/ink capture plus serial trace; strongest causal before/after. |
| Before `50ba72f` vs after | Revisit “déjà vu”: 188–326 ms visible refill vs ~0.4 ms. | Very strong human-visible zoom-cycle demo; use same warmed multi-zoom document. |
| Pre-`c6a4933` vs post-`c6a4933` | Quarter-world vs sixteenth-world sample quantization; careful curves lose zigzags at 400%. | Use archived host renders and owner trace corpus; avoid camera confounds. |
| `v2-feature-complete-pre-cleanup` vs `v2` | Feature-complete product vs released/correctness-closed product. | Useful for code/history narrative; visually subtler than the performance pairs. |

## Does the history fit “bottleneck → optimization → next bottleneck”?

Yes, strongly, but with three important qualifications:

1. **Some loops were correctness loops, not speed loops.** Faster LOD, beam racing, and the first drawing closure produced attractive numbers that were invalidated by adversarial geometry, physical endpoints, or glass. Measurement did not merely reveal the next bottleneck; it sometimes revoked the prior win.
2. **Several optimizations changed the economics of earlier decisions.** Authority-only commits made all-zoom retention affordable, directly reversing the active-zoom invalidation trade. The architecture pivot exposed “déjà vu” as obsolete policy, not a new hardware limit.
3. **The final mental model is layered authority and cooperative materialization.** The project starts as “draw a ribbon into a canvas,” becomes “vector document plus cached raster views,” and ends as “logical authority advances immediately; multiple derived quality tiers converge in bounded background quanta; the presenter composes coherent lagging state.” That is a genuine change in model, not only a series of micro-optimizations.

The most defensible causal spine is:

`whole-stroke replay` → streaming/dirty tiles → `hardware transfer/memory` → SRAM/PSRAM placement → `pan` → direct raster window → `fixed raster world` → vector authority → `vector pan too slow` → materialized cache → `zoom misses/provenance` → overview+sparse tiles → `cold replay` → saturation/prepared-row work → `warm-cache draw fanout` → invalidate other zooms → `revisit damage and glass lag` → authority-only overlay → retain all zooms in idle → `quality flaws` → finer samples + raw-tip provisional + settled AA → `background tail latency` → cooperative resumable pipeline.

## Current historical limits and open bottlenecks

- Settled-AA progression can still produce a 76.416 ms tile tail at 25%; appearance is accepted, cooperative slice behavior is open.
- High-zoom Undo/Redo lacks a deterministic timing baseline; affected-region reconstruction remains expensive.
- One-shot full-frame refreshes can create 166–184 ms poll gaps; they are attributed but not yet band-sliced.
- Residual revisit strays need a gate build with the live ledger; product firmware omits that observability.
- The 400% general cold result passes the release battery at 492.793 ms, but the chronicle still asks for 20 reset-separated runs on normal product firmware with real journal activity.
- SVG extreme-zoom seam teeth were fixed, but SVG and PNG are intentionally different products: logical paths/masks vs settled raster output. Exact visual parity is not the contract.
- Touch hardware still limits raw sampling cadence; smoothing and provisional-tip logic conceal rather than remove that physical constraint.

## Commands for verification

```sh
git rev-list --all --count          # 949
git rev-list main --count           # 906
git show <commit>
git log --all --reverse --date=iso-strict --pretty='%h %ad %s'
git show v2-feature-complete-pre-cleanup
git show v2^{commit}
```

Primary synthesis documents used: [`FINDINGS.md`](../docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md), [`VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md`](../docs/archive/2026-08-raster-and-vector-prototypes/VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md), [`PERFORMANCE_CHRONICLE.md`](../docs/PERFORMANCE_CHRONICLE.md), [`VECTOR_V2_RELEASE_2026_08_19.md`](../docs/receipts/vector-v2/VECTOR_V2_RELEASE_2026_08_19.md), plus the commit bodies and per-topic receipts linked above.
