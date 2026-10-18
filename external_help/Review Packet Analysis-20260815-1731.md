# Review Packet Analysis

**User:** Anonymous  
**Created:** 8/15/2026 13:05:36  
**Updated:** 8/15/2026 16:51:46  
**Exported:** 8/15/2026 17:31:06  
**Link:** [https://chatgpt.com/c/6a805610-2b0c-83eb-afa9-82ce05cba11d](https://chatgpt.com/c/6a805610-2b0c-83eb-afa9-82ce05cba11d)  

## Prompt:
8/15/2026, 1:05:35 PM

hi, welcome, please settle in, then read the review packet attached. take your time, don't limit your findings to 4-5 things, whatever you find you find. mechanical sympathy and elegance and demoscene mindset are the keywords here; check REVIEW_BRIEF.md and PRODUCT_TENETS.md first both in the archive then go from there. good luck, you got this!!

[Attachment: offline-review-packet-2026-08-15-c86f3ac-glass-regressions.tar.gz]

## Response:
8/15/2026, 1:05:39 PM

(•̀ᴗ•́)و I’ll read the two framing files first, then map and test the archive while tracing correctness, performance, mechanical sympathy, and product intent with concrete evidence.

(⌐■_■) The tear-sync oracle is circular, while the pan regression adds 12–18 ms by compositing overlays into PSRAM before submission. I’m tracing TE semantics and transport timing.

(ಠ‿ಠ) The datasheet confirms TE does not validate the fixed row model; measured timing is ignored. Separately, the 10 ms drawing budget excludes major work, and host tests are non-hermetic.

Touch-age telemetry hides blocked rendering by replacing coalesced moves and timestamps. Separately, cold rendering repeatedly replays thousands of operations per tile group; an existing spatial index could help.

> **Inspecting tar archive contents**
>
>
> **Inspected review packet archives, verified documents, and read repository context**
>
> 
>
> **Inspecting Pan Design Experiments Documentation**
>
>
> **Planning the review**
>
> I’m examining the attached packet and repository evidence across architecture, performance, rendering latency, repair behavior, and cold-start costs. I’ll produce a thorough, prioritized report with concrete findings and supporting references.
>
> **Planned the review**
>
> 
>
> **Inspected pan design, presentation code, transport semantics, and build tests**
>
> 
>
> **Running AddressSanitizer Build**
>
>
> **Investigating panel timing**
>
> I’m verifying the controller’s tearing-effect semantics before judging the synchronization logic. I’m also tracing the full diff and related drivers so I can distinguish confirmed defects from hardware-specific assumptions.
>
> **Investigated panel timing**
>
> 
>
> **Built and tested TinyDraw with AddressSanitizer, dependency inspection, and Clang workaround**
>
> 
>
> **Searching CO5300 AMOLED controller datasheet TE tearing effect signal 0x35 scanline PDF**
>
> [CO5300 Datasheet V0.01 20230724 | PDF](https://www.scribd.com/document/775454972/CO5300-Datasheet-V0-01-20230724)
> [espressif/esp_lcd_co5300 • v2.1.0 - ESP Component Registry](https://components.espressif.com/components/espressif/esp_lcd_co5300)
> [kodediy/esp_lcd_co5300](https://github.com/kodediy/esp_lcd_co5300)
> [esp_lcd/ - components - espressif/esp-idf](https://sourcegraph.com/github.com/espressif/esp-idf/-/tree/components/esp_lcd)
> [TinyML: Analysis of Xtensa LX6 microprocessor for Neural Network Applications by ESP32 SoC](https://arxiv.org/abs/2106.10652)
>
> **Searched 5 websites**
>
> 
>
> **Reading PDF analysis guidelines**
>
>
> **Read PDF analysis guidelines**
>
> 
>
> **Searching the web**
>
>
> **Tracing review evidence**
>
> I’m continuing through implementation history, performance evidence, and physical-test notes. I’m comparing claimed improvements with measured behavior, especially where green automated checks conflict with visible tearing or latency.
>
> **Searching the web**
>
> 
>
> **Inspecting Datasheet Timing Specifications**
>
>
> **Inspected datasheet timing, rendered pages, and reviewed ESP32 display architecture**
>
> 
>
> **Inspecting TE Telemetry Logs**
>
>
> **Tracing drawing latency**
>
> I’m confirming that the visible lag comes from presentation design, not only scheduling. The renderer deliberately omits the live tail, and each partial transfer waits synchronously, limiting overlap and making fast strokes visibly trail input.
>
> **Traced drawing latency**
>
> 
>
> **Inspecting touch sampler backlog handling**
>
>
> **Inspected telemetry logs, gate harnesses, rendering budgets, touch handling, and regression logs**
>
> 
>
> **Analyzing stroke latency and event coalescing**
>
>
> **Analyzed stroke latency and located extracted log files**
>
> 
>
> **Analyzing manual glass stroke performance metrics**
>
>
> **Inspected review archives, repository history, branches, diffs, and performance metrics**
>
> 
>
> **Inspecting idle-related source files**
>
>
> **Inspected presenter, transport, driver, and dependency source code**
>
> 
>
> **Searching the web**
>
> [espressif/esp_lcd_co5300 - 2.1.0 Versions • ESP Component Registry](https://components.espressif.com/components/espressif/esp_lcd_co5300/versions/2.1.0/versions?language=en)
>
> **Searched 1 website**
>
> 
>
> **Reviewing Regression Logs and Performance Metrics**
>
>
> **Reviewed regression logs and performance metrics**
>
> 
>
> **Parsing regression logs for exact results**
>
>
> **Parsed regression logs, inspected TinyDraw entries, and searched pan definitions**
>
> 
>
> **Inspecting targeted gate harness sections**
>
>
> **Verifying review evidence**
>
> I’m consolidating confirmed findings now: panel timing is modeled incorrectly, drawing budgets exclude major work, and visible strokes intentionally trail input. I’m checking remaining tests and line references before writing the prioritized report.
>
> **Auditing validation gaps**
>
> I’m finding that several green gates measure software intent rather than visible behavior. I’m checking exact line references and test portability next, then I’ll separate correctness blockers from performance debt and propose safe fallbacks.
>
> **Verified review evidence and audited validation gaps**
>
> 
>
> **Inspected gate harness, vector rendering, panning, sampling, and scrolling implementations**
>
> 
>
> **Inspecting Repository Structure and Status**
>
>
> **Inspected repository, built and tested code, and reviewed overlay changes and documentation**
>
> 
>
> **Inspecting Incremental Document and Canvas Bugs**
>
>
> **Inspected document, canvas, transport, scheduler, and gate regressions**
>
> 
>
> **Searching the web**
>
> [CO5300 Datasheet V0.01 20230724 | PDF](https://www.scribd.com/document/775454972/CO5300-Datasheet-V0-01-20230724)
> [espressif/esp_lcd_co5300 • v2.1.0 • ESP Component Registry](https://components.espressif.com/components/espressif/esp_lcd_co5300)
>
> **Consolidating findings**
>
> I’ve finished the main evidence pass: automated tests are green with a portability workaround, but physical tearing, visible lag, invalid synchronization assumptions, and excluded rendering costs remain substantial blockers.
>
> **Prioritizing blockers**
>
> I’m treating synchronization as unproven, not tear-free: fixed beam math, fail-open timeouts, unsynchronized partial presents, stale-frame reuse, and serialized strip staging are the central correctness and performance risks.
>
> **Consolidated findings and prioritized blockers**
>
> 
>
> **Searched 2 websites**
>
> 
>
> **Inspected project documentation and found review packet context documents**
>
> 
>
> **Inspecting Targeted Closure Claims**
>
>
> **Inspected targeted closure claims**
>
> 
>
> **Searching the web**
>
> [espressif/esp_lcd_co5300 • v2.1.0 • ESP Component Registry](https://components.espressif.com/components/espressif/esp_lcd_co5300)
> [CO5300 Datasheet V0.01 20230724 | PDF](https://www.scribd.com/document/775454972/CO5300-Datasheet-V0-01-20230724)
>
> **Searched 2 websites**
>
> 
>
> **Reviewing Product Documents With Line References**
>
>
> **Inspecting run_mixed_zoom_stroke setup**
>
> 
>
> **Inspecting Producer and Idle Repair Logs**
>
>
> **Finalizing report structure**
>
> I’m organizing the review around correctness blockers, performance debt, falsified documentation, recovery options, and physical tests. I’ll keep claims tied to exact code lines and distinguish measured evidence from hypotheses.
>
> **Finalized report structure**
>
> 
>
> **Inspecting ESPDraw review files**
>
>
> **Pinning review blockers**
>
> I’m treating the green host suite as limited evidence: it validates software paths, not physical presentation. The strongest blockers are the unproven beam model, fail-open TE behavior, missing provisional ink, and serial pan composition.
>
> **Pinned review blockers**
>
> 
>
> **Inspecting workspace and review artifacts**
>
>
> **Inspected rendering, transport, repair, harness evidence, and verification artifacts**
>
> 
>
> **Inspecting Gate Threshold Mismatches**
>
>
> **Inspected gate thresholds, code lines, rendering risks, and panel overlays**
>
> 
>
> **Calculating display timing and overlay metrics**
>
>
> **Calculated display timing and overlay metrics**
>
> 
>
> **Inspecting Spatial Indexing and Tile Constants**
>
>
> **Inspected spatial indexing, tile constants, and operation capacity limits**
>
> 
>
> **Computing macrocell bitset memory usage**
>
>
> **Computed macrocell bitset memory usage**
>
> 
>
> **Inspecting Pan Floor Closure Evidence**
>
>
> **Finalized TinyDraw offline review report and validated references**
>
> 
>
> **Checking report for overstrong claims**
>
>
> **Checked report for overstrong claims**
>
> 
>
> **Searching the web**
>
> [espressif/esp_lcd_co5300 • v2.1.0 • ESP Component Registry](https://components.espressif.com/components/espressif/esp_lcd_co5300)
> [CO5300 Datasheet V0.01 20230724 | PDF](https://www.scribd.com/document/775454972/CO5300-Datasheet-V0-01-20230724)
>
> **Reviewing implementation evidence**
>
> I’ve completed the cross-check: the round remains open, with 28 findings spanning physical synchronization, live drawing, panning, cold rendering, documentation, and test hygiene. The central issue is equating successful submission with correctly timed visible pixels.
> 
> I’m recommending removal of unverified mid-frame beam racing, a simpler ring pipeline, provisional ink independent of committed geometry, and exact spatial replay indexing.
>
> **Searched 2 websites**
>
> 
>
> **Verifying review artifacts and final report consistency**
>
>
> **Verified review artifacts and TinyDraw repository findings consistency**
>
> 
>
> Worked for 60m 15s

(￣▽￣)ゞ I finished the packet review. The full write-up contains **25 material findings: 10 P0 blockers, 9 P1 findings, and 6 P2 issues**, with `file:line` references, the decisive hardware experiment, architecture proposal, rollback, target assessment, and physical test matrix.

## Verdict

The current Vector V2 presenter is **not tear-free by construction**. `tear_synchronized=true` means only that the software completed its own TE-age model; it does not establish that the model corresponds to the pixels being scanned. The severe physical tearing with zero synchronization failures falsifies the oracle.

There is also a concrete mechanical problem with the safety argument. At 60 MHz quad QSPI and 16 bpp, the active payload ceiling is approximately **40.8 full-width rows/ms**, while the modeled beam advances about **26.7 rows/ms**. A queued 44-row burst can gain roughly 15 rows on the beam, theoretically consuming the 48-row margin in about three strips. This does not prove that beam catch-up is the sole physical cause—the CO5300’s buffering and TE phase still need measurement—but it proves that average 28–42 ms frame time cannot establish “the writer is slower than scanout.”

The supposedly fail-closed fallback is not actually closed either. A full refresh continues after a TE wait failure, can return `passed=true`, and `refresh()` can then mark that frame reusable. An unsynchronized full frame can therefore become the source of later cached pans.

## Where the time went

The 28.9 → 41.5/42.0 ms pan regression is mostly avoidable overlay preparation. Before submitting the first panel strip, the implementation gathers the canvas beneath each overlay, copies it into internal scratch, redraws the overlay, scatters it back into the PSRAM ring, and later restores the canvas. That serial gather/draw/scatter/restore sequence closely accounts for the missing 12–18 ms.

The more mechanically sympathetic design is a **pure canvas ring plus one ordered DMA-stage compositor**. The staging pass already touches every outgoing pixel to de-rotate and byte-swap it. Fixed chrome and volatile live ink should be patched into those internal buffers during that same pass. This preserves full-width row-major transfers without mutating and restoring PSRAM or reopening the old x-window seams.

## Drawing is independently blocked

The 10 ms commit budget is not a genuine bound. The 25% overview copy and raster are indivisible, visible tile work is exempt, and at 25% there is no priority view—so the only substantial work runs outside the only deadline check.

The coordinator then makes the ordering worse:

- authority commits run before the newest preview is presented;
- the renderer explicitly accepts one smoothing-window of finger lag;
- lift synchronously drains pending chunks before refreshing;
- one live update can perform several separate panel drains after overlay subtraction.

That combination explains the manual 15.5–35.8 ms chunks and 92–143 ms polling gaps. Low surviving touch-event age does not clear the interaction path because consecutive moves are replaced with the newest point; thousands of intermediate moves can disappear while the retained event remains young.

The necessary invariant is: **show the newest consumed touch sample first; materialize authority afterward in genuinely preemptible slices**. A replaceable volatile ribbon tail should remain separate from document authority and be composited in the staging pass.

## Cold rendering and repair

Every missing 128×128 producer group begins by traversing the entire post-baseline operation range newest-first. Bounding-box rejection makes unrelated operations cheaper, but each group still repeatedly fetches and examines thousands of records before it can publish.

The main cold-render lever is therefore an append-time spatial or block replay index, not another scheduling constant. Reaching the 500 ms ceiling requires a **20.4%** reduction from the current 628.2 ms adversarial p95; reaching roughly 300 ms requires **52.2%**.

Idle repair also lacks a crisp user contract. Work is frequently superseded, and related work is not deliberately retained across nearby camera generations. The proposed contract prioritizes the current viewport after a defined quiet boundary, requires it to become exact within 500 ms at 400%—target 300 ms—and only then repairs the velocity-leading halo and remembered zoom views.

## Recommendation

Remove beam racing from the product path now. Keep it only behind an experimental presentation-policy flag.

The safest next Vector V2 implementation is a one-variable physical A/B:

- **A:** current beam-aged, two-band sweep.
- **B:** one boundary-triggered, row-zero, top-to-bottom sweep.
- Identical ring pixels, overlays, strip dimensions, queue depth, and staging.

Both modes need synchronized high-speed video and logic capture of TE, bus activity, ISR/task timing, and strip submissions. Alternating frame IDs and row barcodes should make any mixed frame immediately red. Importantly, B is an experiment—not presumed safe—until that optical evidence is clean.

The safest operational fallback is **Raster V1**, after revalidating the current image on the current board. The packet does not contain a physically proven Vector V2 rollback commit; older V2 paths traded the present tearing for other seams while retaining unproven timing assumptions.

## Target assessment

The current implementation fails all three requested product outcomes:

- Drawing visibly lags.
- Pan is physically incorrect and only around the 24 FPS floor.
- Adversarial 400% cold rendering exceeds 500 ms.

The proposed architecture gives a credible route to non-lagging drawing and 24–30 FPS pan. Sub-500 ms cold rendering is plausible with indexed replay; the ~300 ms cold target still requires measured evidence.

I verified the archive manifest, cloned the bundled clean branch, and left the source untouched. The Vector V2 exactness and fuzz suite passed **201 test cases and 76,624 assertions** after force-including missing standard headers. The unmodified checkout is not host-clean on this toolchain: two tests rely on undeclared transitive includes for `std::clamp` and `std::vector`.

[Read the complete review report](sandbox:/mnt/data/espdraw-offline-review-2026-08-15.md)

## Prompt:
8/15/2026, 2:25:31 PM

do you think the no lag ink + 24 fps tear free panning + under 500ms cold renders + not too many re-renders are viable? i don't want to put too much time into this if the answer is not. if yes i'll continue. my hunch is that it is possible but im curious if your opinion

## Response:
8/15/2026, 2:25:36 PM

(•̀ᴗ•́)و **Yes—I think the four goals are jointly viable enough that I would continue.** I don’t see a fundamental bandwidth/compute contradiction in the packet; I see several architectural choices making the machine do avoidable work in the wrong order.

> **Searched review file for key references**
>
> 
>
> **Assessing viability**
>
> I think this is viable, but I’m treating the goals separately. Lag-free ink looks most achievable; sustained smooth panning is plausible if presentation avoids expensive redraws. I’d continue with staged tests and explicit failure thresholds before committing further.
>
> **Assessed viability**
>
> 
>
> **Evaluated safe cached rendering architecture**
>
> 
>
> **Setting proof gates**
>
> I’d continue, but only through focused proofs: separate provisional ink from authoritative rendering, validate tear-free panning under sustained load, and optimize cold redraws. I’d stop if any proof requires sacrificing correctness, bounded latency, or predictable behavior.
>
> **Set proof gates**
>
> 
>
> **Searching the web**
>
>
> **Searching the web**
>
> 
>
> Worked for 38s

(•̀ᴗ•́)و **Yes. I think your hunch is right, and I would continue.** More specifically, I think **no-lag ink + ≥24 FPS genuinely tear-free panning + <500 ms cold rendering + controlled rerendering are compatible goals on this hardware**. I would not say the same yet about *30 FPS + ~300 ms cold*; those are stretch targets until measured.

What gives me confidence is that these do **not** require four unrelated heroic optimizations. The same architectural cleanup helps all four.

- **No-lag ink: high confidence.** The current lag is largely self-inflicted ordering: authority/materialization happens before the newest visual update, and some supposedly budgeted work isn't actually preemptible. A volatile provisional tail lets the newest consumed touch go straight to presentation while document authority catches up behind it. That's a very conventional and mechanically sound separation. `espdraw-offline-review-2026-08-15.md`

- **24 FPS tear-free pan: medium-high confidence.** You're already around **23.8-24.1 FPS while doing an extra 12-18 ms of avoidable overlay work**. The pre-regression warm path was around **28.9 ms**, which is comfortably inside a 33.3 ms 30-FPS frame budget. So I don't see a bandwidth problem here. The remaining uncertainty is genuinely physical: discovering a presentation policy that the CO5300 demonstrably accepts without tearing. I would not invest in more beam-race math; I'd validate a boundary-based policy optically. `espdraw-offline-review-2026-08-15.md` `espdraw-offline-review-2026-08-15.md`

- **<500 ms cold render: reasonably high confidence; ~300 ms much less certain.** You only need **20.4%** off the measured 628.2 ms p95 to cross 500 ms. The current producer repeatedly walks thousands of operation records for each missing group, so there is a large, identifiable algorithmic lever rather than a mysterious 20% you have to squeeze from instruction scheduling. A spatial/block replay index is exactly the sort of change I'd expect to clear 20%. Getting all the way to ~300 ms requires 52.2%, so I would treat that as a bonus until the index is measured. `espdraw-offline-review-2026-08-15.md`

- **Not too many rerenders: high confidence, provided you make reuse an invariant.** Right now repair gets unnecessarily superseded by camera-generation changes: the packet records 137 fills with 43 superseded, including one 400% fill killed after 8.207 seconds. Work should instead have durable identity like `(revision, zoom, tile/group)` and survive camera changes whenever it still intersects the current or predicted view. Then panning primarily changes *what gets presented and prioritized*, not what already-correct pixels must be rasterized again. `espdraw-offline-review-2026-08-15.md`

And importantly, I think there is an **elegant common architecture** hiding underneath all of this:

**operation log = authority → materializer = resumable cache builder → ring = pure cached canvas → volatile ink/chrome = presentation-only layers → one staging compositor → panel.**

That arrangement has good mechanical sympathy. Each piece does one kind of work. Touch doesn't wait for cache maintenance. Overlays don't make PSRAM round trips. Pan doesn't imply rerasterization. Cold rendering doesn't scan irrelevant history. Partial repair isn't thrown away because the camera moved eight pixels. The review's proposed direction is essentially this same decomposition. `espdraw-offline-review-2026-08-15.md`

The thing that would change my answer is **the panel**. That's the one genuine unknown. If a simple, instrumented presentation policy cannot produce clean glass at ≥24 FPS despite the available transfer throughput, then you may be fighting undocumented controller behavior that isn't worth the product effort. Everything else looks like software architecture you can control.

So rather than committing to a long optimization project, I would give the project a few **hard proof gates**:

1. Get a deliberately boring pan implementation-pure canvas ring, staged overlays, no beam racing-and demonstrate optically clean ≥24 FPS panning. If you cannot, investigate the panel before doing anything else.
2. Make a volatile ink tail bypass authority work and verify that dense-document drawing visibly tracks the newest touch sample. If that doesn't dramatically improve perceived latency, something deeper is wrong.
3. Prototype the simplest spatial replay index and see whether adversarial 400% drops below 500 ms. Don't optimize the index beautifully first; this is a feasibility test.
4. Change repair identity so relevant tile work survives camera replans, then measure actual duplicate raster work rather than just fill completion time.

If **those four prototypes work**, I'd be quite bullish about finishing V2. If #1 fails after the presentation path has been simplified enough that you can actually see what the hardware is doing, *that's* when I would seriously consider stopping.

So if the question is essentially **"am I chasing an impossible combination of requirements?"**: I don't think you are. The current system is making the requirements look more antagonistic than they actually are. The direction I'd pursue makes the design *simpler* as it gets faster, which is usually a very good sign.

## Prompt:
8/15/2026, 2:28:51 PM

can you please research the panel? https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm it's this thing with the v2 revision hardware

but also the raster version is definitely over 24 fps

## Response:
8/15/2026, 2:28:52 PM

(⌐■_■) I’ll trace Waveshare’s V2 docs, schematics, driver source, and CO5300 timing data. Raster V1 exceeding 24 FPS already shifts suspicion from bandwidth toward synchronization/update strategy.

The key new detail: V2 replaces the SH8601 with a CO5300 while retaining the 368×448 QSPI AMOLED, so I’m checking its first-party driver and TE behavior specifically.

Two concrete issues surfaced: CO5300 specifies 50 MHz QSPI while Vector uses 60 MHz, and TE-high denotes blanking while current code launches after blanking ends.

> **Researching panel details**
>
> I’m researching the revision-specific hardware, controller, interface wiring, display timing, and official documentation. I’ll verify the raster-rate correction against primary sources and distinguish confirmed facts from inference.
>
> **Researched panel details**
>
> 
>
> **Searching CO5300 AMOLED controller datasheet TE QSPI 368x448**
>
> [Product Specification](https://tailorpixels.com/wp-content/uploads/2025/04/TOH204XVT-01CG_2.04-inch-Touch-AMOLED-Display-with-Cover-Glass-368%C3%97448-QSPI-MIPI.pdf)
> [waveshareteam/ESP32-S3-Touch-AMOLED-1.8](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8)
> [CO5300 Datasheet V0.01 20230724 | PDF](https://www.scribd.com/document/775454972/CO5300-Datasheet-V0-01-20230724)
> [ESP32-S3-Touch-AMOLED-1.8](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8)
> [CO5300 Datasheet - Driver | CHIPONE](https://datasheet4u.com/datasheets/CHIPONE/CO5300/1577323)
> [ESP32-S3-Touch-AMOLED-2.06 - Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06?srsltid=AfmBOoqSmPoIae9Ii4TnmyljBuEBnrss7aPIrN0VI1OFBk2BexzQbOkX)
> [AMOLED 1.43 inch 466*466 QSPI 24 pins CO5300 Display ...](https://www.lxdisplay.com/Product_details/1349773141777539072.html)
> [espressif/esp_lcd_co5300 • v2.1.0 - ESP Component Registry](https://components.espressif.com/components/espressif/esp_lcd_co5300)
> [ESP32-S3 1.8inch AMOLED Touch - ESPHome](https://community.home-assistant.io/t/esp32-s3-1-8inch-amoled-touch/956270)
>
> **Searched 9 websites**
>
> 
>
> **Inspected repository contents and official documentation**
>
> 
>
> **Searching the web**
>
> [ESP32-S3-Touch-AMOLED-1.8 | WaveShare Documentation Platform](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8)
>
> **Searched 1 website**
>
> 
>
> **Inspected Resources Link 49 and GitHub tree**
>
> 
>
> **Verifying revision differences**
>
> I’m checking the official product page and revision-specific materials to separate V2’s hardware changes from software-path limitations. Your confirmed Raster V1 rate materially strengthens the case for panel and transport capability.
>
> **Revising the assessment**
>
> I’m treating the confirmed Raster V1 rate as strong evidence that V2’s limitation isn’t raw panel bandwidth. I’m narrowing the remaining uncertainty to synchronization, composition, and whether both paths use identical transfer modes.
>
> **Searching the web**
>
> [ESP32-S3-Touch-AMOLED-1.8 | WaveShare Documentation Platform](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8)
> [ESP32-S3-Touch-AMOLED-1.8 - Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.8)
> [GitHub - waveshareteam/ESP32-S3-Touch-AMOLED-1.8: ESP32-S3-Touch-AMOLED-1.8 Engineering Sample Program · GitHub](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8)
>
> **Searched 3 websites**
>
> 
>
> **Cloning official Waveshare repository for inspection**
>
>
> **Cloned the official Waveshare repository for inspection**
>
> 
>
> **Inspected CO5300 display initialization, pin configuration, and SDK source files**
>
> 
>
> **Searching the web**
>
> [GitHub - espressif/esp-bsp: Board support components for Espressif development boards · GitHub](https://github.com/espressif/esp-bsp)
>
> **Searched 1 website**
>
> 
>
> **Assessing bandwidth limits**
>
> I’m treating Raster V1’s confirmed rate as decisive evidence that raw panel throughput isn’t the blocker. I’m separating that proof from unresolved tear-free synchronization and Vector V2 composition costs.
>
> **Refining synchronization**
>
> I’m confirming that CO5300’s TE signal has defined blanking semantics, which improves the synchronization picture. Still, TE alone may not make full-frame writes tear-free unless transfer timing fits the available blanking interval.
>
> **Assessed bandwidth limits**
>
> 
>
> **Inspecting Extracted Archive Contents**
>
>
> **Inspected display source, timing, wiring, and raster performance benchmarks**
>
> 
>
> **Fetched official CO5300 source and inspected driver initialization**
>
> 
>
> **Searching "ESP32-S3-Touch-AMOLED-1.8" "GPIO 13"**
>
> [ESP32-S3-Touch-AMOLED-1.8 | WaveShare Documentation Platform](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8)
> [ESP32-S3 1.8inch AMOLED Touch Display Development Board, 32-bit LX7 Dual-core Processor, 368×448 Pixels, Accelerometer And Gyroscope Sensor, ESP32 With Display | ESP32-S3-Touch-AMOLED-1.8](https://www.waveshare.com/product/esp32-s3-touch-amoled-1.8.htm)
> [GitHub - waveshareteam/ESP32-S3-Touch-AMOLED-1.8: ESP32-S3-Touch-AMOLED-1.8 Engineering Sample Program · GitHub](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8)
>
> **Searched 3 websites**
>
> 
>
> **Searching Latest TE Pulse Measurements**
>
>
> **Searched latest TE pulse measurements**
>
> 
>
> **Searching the web**
>
>
> **Searching the web**
>
> 
>
> **Searching Overlay and Toolbar Position Constants**
>
>
> **Inspected overlay and toolbar position constants plus vector panel bounds**
>
> 
>
> **Assessing viability**
>
> I’m now treating Raster V1’s ~39 FPS measured path as strong evidence that V2 can exceed 24 FPS. The remaining engineering risk is synchronization and compositor overhead, not basic panel bandwidth.
>
> **Testing edge timing**
>
> I’m narrowing the likely tear source to TE polarity: the measured blanking pulse is short, and starting on its falling edge may begin too late. A rising-edge, 50 MHz A/B test is the decisive check.
>
> **Testing edge timing**
>
> I’m narrowing the likely tear source to TE polarity: the measured blanking pulse is short, and starting on its falling edge may begin too late. A rising-edge, 50 MHz A/B test is the decisive check.
>
> **Assessed viability and tested edge timing**
>
> 
>
> **Inspecting panel transport and rendering performance**
>
>
> **Inspected panel performance and located benchmark results**
>
> 
>
> **Searching the web**
>
> [dl.espressif.com](https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf)
>
> **Searched 1 website**
>
> 
>
> **Inspecting Measured TE Values in Logs**
>
>
> **Inspected measured TE values in logs**
>
> 
>
> **Searching the web**
>
> [dl.espressif.com](https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf)
>
> **Searched 1 website**
>
> 
>
> **Searched Waveshare BSP for TE and GPIO mappings**
>
> 
>
> **Searching the web**
>
> [GitHub - waveshareteam/ESP32-S3-Touch-AMOLED-1.8: ESP32-S3-Touch-AMOLED-1.8 Engineering Sample Program · GitHub](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8)
>
> **Searched 1 website**
>
> 
>
> **Researched Waveshare board components and display interface configurations**
>
> 
>
> **Cloning Repository and Searching Display Configuration**
>
>
> **Cloned repository and searched display configuration**
>
> 
>
> **Fetched ESP32 AMOLED touchscreen implementation and board source code**
>
> 
>
> **Searching the web**
>
>
> **Searching the web**
>
> 
>
> **Searching the web**
>
>
> **Searching the web**
>
> 
>
> **Investigated LCD touch-enable pin assignments and CO5300 GPIO configuration**
>
> 
>
> **Reaching a verdict**
>
> I’m concluding the panel is viable: Raster V1 reaches about 39 FPS over 368×372, so V2’s ~24 FPS is not a bandwidth ceiling. The likely defect is TE phase choice plus compositor overhead, especially at out-of-spec 60 MHz.
>
> **Revising panel viability**
>
> I’m concluding the panel is capable: Raster V1’s 372-row path reaches about 39 FPS, so Vector V2’s 42 ms is not a hardware ceiling. I’m treating synchronization and inefficient window setup as the remaining blockers.
>
> **Revising panel viability**
>
> (•̀ᴗ•́)و After researching the CO5300, I’m more confident the panel is viable. Your Raster V1 correction matters: ~25.45 ms for 368×372 updates (~39 FPS enqueue rate) clears the throughput concern. The main suspects are synchronization phase and 60 MHz operation exceeding published timing.
>
> **Revised panel viability and reached a verdict**
>
> 
>
> **Reviewing Raster Pan Benchmark Results**
>
>
> **Reviewed raster pan benchmark results**
>
> 
>
> Worked for 30m 5s

(•̀ᴗ•́)و **Yes-after researching the exact V2 panel, I’m more confident that you should continue.** Your Raster V1 correction materially strengthens the case, and I was too conservative before: **41.5-42.0 ms is a Vector V2 software-path result, not the performance limit of this board or panel.**

The bundled Raster V1 pan receipt supports what you observed:

```text
frames=30 pixels=368 rows=372 delta=32
average_us=25451
```

That is about **39.3 submission cycles per second**. The benchmark ends after the last transfer is queued rather than after final optical completion, so I would not call it proven 39.3 display FPS. But combined with your physical observation, it clearly establishes that **more than 24 FPS is available on this hardware**. The current V2 regression is dominated by its extra overlay gather/copy/scatter/restore work, not by a panel bandwidth wall. `espdraw-offline-review-2026-08-15.md`

## What the V2 panel actually is

Hardware V2 changes the display controller from the original board’s SH8601 to a **CO5300**, while retaining the 368×448 RGB565 QSPI AMOLED. Waveshare’s V2 configuration uses a 16-pixel horizontal panel offset, with QSPI on GPIOs 4-7, clock on 11, and chip select on 12. ([WaveShare](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8)) 

The important controller properties are:

- It contains internal display RAM, which the AMOLED scan engine reads independently while the ESP32 modifies it.
- It supports a TE synchronization output.
- It supports ordinary windowed writes with `CASET`, `PASET`, and `RAMWR`.
- It also supports `RAMWRC`-memory-write continuation-so a large image can be sent as several DMA chunks without reopening the window every time. ([Espressif Download Server](https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf))

That is good news. The controller exposes exactly the primitives needed for a very simple, low-state-change transport.

## I think the current TE edge is probably backwards

This is the strongest panel-specific finding.

With `0x35, 0x00`, the CO5300 is in vertical-blank-only TE mode. According to the datasheet:

- **TE high** corresponds to the interval in which visible scanning is not reading the display RAM.
- **TE low** corresponds to active display scanning.

Your measured signal is consistent with that: roughly a 16.8 ms period with about 0.58 ms high.

But the current transport treats the **falling edge** as the safe frame start and measures beam age from that edge. That means it launches the writer at approximately the moment active scanning resumes-not at the beginning of vertical blanking. `espdraw-offline-review-2026-08-15.md` ([Espressif Download Server](https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf))

Conceptually:

```text
                   vertical blank             active scan
TE              ___/‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾\____________________
                    ^                         ^
proposed start -----+                         |
current start --------------------------------+
```

Starting at the falling edge gives the panel reader and host writer effectively the same starting line. Because the QSPI writer is faster than the display scan, it can catch and cross the reader. That creates exactly the moving boundary between old and new frame contents that appears as a horizontal tear.

That is still a **strong mechanism hypothesis**, not a proven diagnosis. Scan direction, edge-to-row delay, and internal GRAM arbitration need logic capture plus glass. But it fits both the datasheet and the observed failure considerably better than the current modeled-beam explanation.

Starting on the **rising edge** instead gives the writer approximately 0.58 ms of head start before visible scanning begins.

## The 60 MHz clock should not be part of the correctness proof

The current V2 transport runs QSPI at 60 MHz. The CO5300 timing table specifies a minimum serial clock period of 20 ns, corresponding to **50 MHz**. Espressif’s official CO5300 QSPI configuration defaults to 40 MHz. ([Espressif Download Server](https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf)) 

This does not establish that 60 MHz always fails on your particular board. It does mean that 60 MHz is outside the published timing envelope, so:

- tearing or white notches at 60 MHz cannot be cleanly attributed to the presentation algorithm;
- a result that works only at 60 MHz is not a strong product proof;
- signal integrity becomes a credible explanation for the periodic white edge notches.

Fortunately, **you do not appear to need the overclock**.

For the 368×372 moving canvas:

```text
368 × 372 × 2 bytes = 273,792 bytes

50 MHz × 4 QSPI bits / 8 = 25 MB/s

273,792 / 25,000,000 = 10.95 ms raw color payload
```

Even a full 368×448 RGB565 frame has a raw payload time of about **13.19 ms at 50 MHz**. Those figures exclude command, staging, DMA, and scheduling overhead, but they are comfortably below the roughly 16.8 ms panel period.

At 40 MHz, the 372-row payload is about **13.69 ms**, which may also work but leaves less room. So the sensible proof point is 50 MHz, with 40 MHz as a diagnostic comparison.

## The mechanically sympathetic panel transport

I would stop improving the split-band beam racer and implement this deliberately boring path:

1. **Run at 50 MHz.**
2. Keep TE in vertical-blank mode with `0x35, 0x00`.
3. Compose and pre-stage the first DMA buffers before waiting for TE.
4. Program one physical window for the complete moving canvas-preserving the V2 16-pixel x offset-before the edge.
5. On the **TE rising edge**, begin one top-to-bottom stream.
6. Use `RAMWR 0x2C` for the first buffer and `RAMWRC 0x3C` for every continuation buffer.
7. Never split at a modeled beam position, wrap to another band, revisit rows, or reopen the window.
8. Perform one completion wait after the ordered batch.

The CO5300 explicitly defines `RAMWRC` as continuing from the pixel after the preceding `RAMWR`/`RAMWRC`, so this is not relying on accidental behavior. ([dl.espressif.com](https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf?utm_source=chatgpt.com), [dl.espressif.com](https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf?utm_source=chatgpt.com))

The current 372-row path uses nine approximately 44-row windows. Your transport source estimates roughly 0.4 ms of setup cost per transaction, so repeated window setup can consume several milliseconds. One window plus continuation transfers removes nearly all of that state churn. It also gives the white-notch problem fewer boundaries at which to occur.

This aligns very well with the staging-compositor architecture:

```text
pure cached canvas ring
        +
fixed chrome sprites
        +
volatile live-ink tail
        ↓
one internal DMA staging pass
        ↓
one monotonic CO5300 memory stream
```

That is both faster and easier to reason about than mutating PSRAM, restoring it, computing a virtual beam, and issuing two-band sweeps.

## The decisive hardware experiment

I would make this the project’s immediate go/no-go gate:

### A - Current policy, cleaned to 50 MHz

- Falling-edge synchronization
- Existing repeated windows
- Existing split/wrap behavior
- Alternating highly distinct frame IDs

### B - Panel-native policy

- Rising-edge synchronization
- One window
- `RAMWR` followed by `RAMWRC`
- Top-to-bottom only
- Same pixels, staging buffers, clock, and DMA depth

Capture:

- TE
- CS and SCLK
- first and last color payload
- high-speed video of alternating frame numbers and row barcodes

Then repeat B at 40 and 50 MHz. Use 60 MHz only as a diagnostic overclock.

The outcomes are unusually informative:

- **B clean, A tearing:** synchronization phase/window strategy was the problem.
- **50 MHz clean, 60 MHz showing notches:** electrical timing or signal integrity was involved.
- **Both 40 and 50 clean:** use whichever provides the necessary measured product margin.
- **B still tearing:** use the CO5300’s programmable TE scan-line command, `0x44`, to determine scan direction and line-to-edge phase before reconsidering the panel. The controller also exposes `0x45` for scan-line reading, though QSPI read support may require additional transport work. 

One documentation limitation remains: Waveshare’s public V2 pin configuration does not identify a TE GPIO, and I could not find a published V2 board-level schematic. Your stable 60 Hz signal on GPIO 13 is highly consistent with CO5300 TE, but that mapping is presently empirical rather than first-party documented. ([github.com](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.8?utm_source=chatgpt.com))

Also, Waveshare’s public examples do not provide a tear-free reference implementation: its ESP-IDF initialization enables TE but does not consume it, while its Arduino initialization leaves TE enable commented out.  

## Revised viability judgment

| Goal | My view now |
|---|---|
| No-lag visible ink | **High confidence** |
| Tear-free panning at ≥24 FPS | **High enough confidence to continue** |
| Tear-free panning at 30 FPS | **Plausible, not yet proven** |
| Adversarial cold rendering under 500 ms | **Credible with indexed replay** |
| Approximately 300 ms cold rendering | **Still a stretch target** |
| Avoiding excessive rerendering | **High confidence with durable tile work identities** |

The goals are also less mutually antagonistic than they sound because they occur in different operating phases:

- While drawing, the volatile ink tail gets priority and materialization trails invisibly.
- While panning, cached pixels are shifted and presented; expensive exact repair is paused.
- After motion becomes quiet, current-view cold repair gets the CPU/PSRAM budget.
- Repair tasks remain keyed by revision, zoom, and tile identity so nearby camera changes do not discard valid work.

The current implementation unnecessarily serializes these lanes. Separating them makes the system both faster and simpler. The live-ink path already has a credible visual-first solution, the cold producer has an identifiable full-log-scan cost rather than a mysterious limit, and current repair supersedes substantially more work than necessary. `espdraw-offline-review-2026-08-15.md` `espdraw-offline-review-2026-08-15.md` `espdraw-offline-review-2026-08-15.md`

## Bottom line

**I would continue.**

The panel research does not reveal a fundamental reason the target combination is impossible. It reveals a much more encouraging possibility: **the current V2 path is using the right synchronization signal at the wrong phase, while overclocking the interface and paying unnecessary transaction and overlay costs.**

I would not invest more effort in tuning the current beam-racing constants. The next investment should be exactly one clean transport proof:

> **50 MHz, TE rising edge, pre-staged data, one window, `RAMWR` + `RAMWRC`, one monotonic sweep.**

If that path produces clean glass at ≥24 FPS, I would consider the complete product target set viable. If that exact spec-clean path still tears after scan direction is calibrated, then the panel becomes a legitimate reason to reconsider the project. Based on the panel’s documented behavior, your Raster V1 evidence, and the available payload margin, I expect the clean path is more likely to work than not.

---

**Sources:**

- [https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8)
- [https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf](https://dl.espressif.com/AE/esp-iot-solution/CO5300_Datasheet_V0.00.pdf)

## Prompt:
8/15/2026, 3:23:48 PM

i want to avoid this further ballooning - most of the features are done, only things like svg export panning the minimap autosave and undo are missing so my goal is "simply" get this thru the finish line: ink not lagging, no visual glitches, 24-30fps tear free panning, 500ms max cold renders and no excessive rerenders. how can i structure all this work so i do all the important measurements then set up potentially more safeguards and then build the rest the features + get to the performance targets in the right order. i feel like this project keeps ballooning or we hit one performance mark but break others and so on

## Response:
8/15/2026, 3:23:49 PM

(•̀ᴗ•́)و I’d treat this as a closure program: freeze scope, establish one repeatable measurement harness and hard gates, then reject any change that regresses the full scorecard.

> **Structuring the project**
>
> I’m treating this as a scope-control problem: I’ll separate must-have evidence from optional improvements, then sequence safety, measurement, implementation, and performance behind clear exit criteria.
>
> **Structured the project**
>
> 
>
> **Sequencing the work**
>
> I’m prioritizing correctness before speed: lock the measurement baseline, add reproducible safety gates, validate tear-free output at conservative settings, then optimize latency and throughput only within fixed performance budgets.
>
> **Sequenced the work**
>
> 
>
> **Defining latency gates**
>
> I’m translating “no visible lag” into measurable targets: sample-to-visible latency, percentile limits, and spatial finger-to-tail gap. I’d propose p95 ≤35 ms, p99 ≤50 ms, plus a bounded gap during fast strokes.
>
> **Defined latency gates**
>
> 
>
> **Sequencing feasibility work**
>
> I’m separating feasibility from feature completion: first prove the panel path, latency, cold-index behavior, and rerender identity. Then I’ll lock architecture, add safeguards, implement dependencies, and finish integrated tuning.
>
> **Assessed sequencing feasibility**
>
> 
>
> Worked for 8m 5s

(ง •̀_•́)ง **Yes-this needs a closure program now, not another round of open-ended optimization.** The project is ballooning because each target has been treated as a local problem, then the next change invalidates the assumptions behind the previous win.

One correction to your framing: the remaining features are not equally isolated.

- **SVG export** is mostly a read-only leaf feature.
- **Minimap** touches the presentation compositor.
- **Autosave** can disturb interactive scheduling and storage latency.
- **Undo** changes revision, cache-invalidation, replay-index, and repair semantics.
- **Panning** touches the hardest hardware path.

So the right structure is:

> **Prove feasibility once → freeze the architecture → finish the features → do one integrated performance-closure pass.**

Do **not** fully optimize the unfinished product now, add undo/autosave/minimap, and then optimize it all again.

Your Raster V1 result is important. It does not prove the feature-complete V2 path will automatically exceed 24 FPS, but it does prove the panel and basic transport are capable of it. Use Raster V1 as a control throughout: V2 should not be allowed to blame the hardware for overhead that Raster does not incur.

# 1. Lock the finish line before touching more code

Write one short `SHIP_CONTRACT.md`. Treat it as immutable after the first measurement baseline, except for correcting an objectively impossible threshold.

Here is the contract I would use. The exact ink thresholds are proposed values-you should adjust them once based on what feels genuinely immediate, then freeze them.

| Requirement | Ship gate | Internal guard band |
|---|---:|---:|
| Ink latency | Optical p95 ≤45 ms, p99 ≤60 ms; no visible tail separation on the standard fast-stroke trace | p95 ≤35 ms, p99 ≤50 ms |
| Ink fidelity | No missing Down/Up; bounded spatial gap; final path matches authoritative path | Same, with extra margin |
| Pan correctness | Zero tears, white notches, stale bands, overlay seams, or mixed frame IDs in ≥10,000 canonical pan frames | Same |
| Pan floor | p95 optical frame interval ≤41.7 ms; no persistent cadence below 24 FPS | p95 ≤38 ms |
| Pan target | Median ≤33.3 ms and p95 close enough that it visibly behaves as 30 FPS | Stretch, not blocker |
| Cold rendering | Current viewport exact in ≤500 ms for every fixed adversarial case | ≤450 ms |
| Rerender amplification | ≤1.25 rendered tile revisions per unique required tile revision | ≤1.15 |
| Discarded repair work | ≤10% of raster/materialization work discarded | ≤5% |
| Exactness | Zero pixel/state mismatches in the existing exactness/fuzz corpus | Same |
| Data safety | Undo/autosave recovery exact across reset and interrupted-save fixtures | Same |

The internal guard bands matter. A feature should not be merged when a metric is sitting exactly on the product limit. That is how minimap or autosave turns yesterday’s “closed” 24.1 FPS into tomorrow’s 22.8 FPS.

Also define **what “cold render complete” means**: the current visible viewport is exact. Do not wait for a surrounding halo, remembered zooms, or the whole document. Those are background quality tiers, not part of the 500 ms contract.

# 2. Build one small permanent measurement harness

Do not build a telemetry platform. Add only measurements tied to failure modes you have actually encountered.

The current internal signals are insufficient as ship oracles. A young surviving touch event can coexist with heavy move coalescing and visible lag, while the current pan synchronization flag can say “synchronized” without observing the panel at all. `espdraw-offline-review-2026-08-15.md` `espdraw-offline-review-2026-08-15.md`

You need four sources of truth.

## Ink

Record a deterministic touch trace and replay it identically. Track:

```text
touch sampled
touch consumed
volatile geometry ready
panel submission started
DMA completed
optical pixel became visible
```

The last step is the actual result. Software timestamps are for attribution.

Also measure:

```text
maximum time gap between consumed samples
maximum spatial gap
coalesced/received ratio
finger-to-visible-tail distance
final preview-to-authority path error
```

This prevents “low touch-event age” from falsely clearing an under-sampled or lagging stroke.

## Pan

Put unmistakable data into each frame:

```text
alternating full-frame IDs
row-number barcodes
guard pixels at edges
high-contrast overlay boundaries
```

Capture TE plus bus activity and glass. The pass condition is zero mixed IDs or displaced row barcodes-not a software boolean.

Measure optical frame intervals, not only submission duration.

## Cold rendering

For every group, record:

```text
operation IDs considered
operation IDs actually intersecting
PSRAM bytes read
groups published
time current viewport became exact
```

The existing 628.2 ms result is structurally associated with repeatedly walking the replay range, so “operations examined per group” is the key leading metric. `espdraw-offline-review-2026-08-15.md`

## Rerendering

Give every materialization unit a durable identity:

```text
(document revision, zoom level, tile/group coordinates)
```

Then count:

```text
unique required keys
render attempts
completed keys
reused completions
canceled work
completed work later discarded
evictions
```

Your primary statistic should be:

```text
render_amplification =
    total render attempts / unique required keys
```

A camera-generation change should not automatically make work obsolete. The current repair path supersedes substantial work that can remain relevant to the new view. `espdraw-offline-review-2026-08-15.md`

# 3. Use a small canonical workload set-not a giant test matrix

Avoid the Cartesian product of every zoom, feature, direction, document, and gesture. That becomes its own project.

Use about eight deterministic traces chosen for pairwise coverage:

| Trace | What it proves |
|---|---|
| Fast curved ink on dense document at 25% | Worst known coordinator/materialization interaction |
| Fast ink at 400% | Fine-detail preview and tile interaction |
| Four-direction pan with all chrome visible | Normal pan cadence and overlay composition |
| Pan across ring wrap boundaries | Addressing, wrap, and stale-row correctness |
| Pan while current-view repair is active | Foreground/background isolation |
| Adversarial cold entry at 400% | The 500 ms requirement |
| Undo while repair work exists | Revision invalidation and reuse correctness |
| Autosave during drawing and panning | Storage work cannot disturb interaction |

Add one long-session churn trace that repeatedly draws, zooms, pans, undoes, pauses, and resumes. Its purpose is memory leakage, work amplification, stale-cache mistakes, and eventual performance decay-not measuring the best frame time.

Every recorded run should identify:

```text
Git SHA
firmware/config hash
board revision
panel clock
dataset hash
trace hash
feature flags
all scorecard results
```

This removes “I think this build was faster” from the process.

# 4. Run four feasibility spikes before completing features

These are go/no-go experiments. They should be deliberately narrow. Do not productionize or generalize them yet.

## Spike A: panel-native tear-free pan

Build the simplest presentation path you can:

- Pure cached canvas.
- Overlay composition only in internal staging.
- One monotonic panel sweep.
- One synchronization policy.
- One ordered batch and one final completion wait.
- Spec-safe panel clock during the correctness proof.
- Raster V1 run alongside it as the hardware/control baseline.

Exit gate:

```text
zero visual artifacts
p95 ≤41.7 ms
same result with chrome enabled
same result across ring wrap and all pan directions
```

If this minimal path cannot do clean 24 FPS while Raster V1 can, then you have isolated a V2 transport or representation problem. Do not add more features until you understand it.

## Spike B: visual-first ink

Temporarily make authoritative materialization as unimportant as possible. Display a replaceable volatile tail representing the newest consumed sample, then let authority catch up.

The invariant is:

> The panel always represents the newest consumed touch sample; document materialization may trail invisibly.

That direction directly removes the current commit-before-preview conflict. `espdraw-offline-review-2026-08-15.md`

Exit gate:

```text
ink latency inside the guard band
no missing stroke start/end
acceptable geometry under coalescing
dense-document workload does not change visible latency materially
```

If ink still visibly lags with authority work disabled, then the bottleneck is in touch sampling, composition, panel scheduling, or optical scanout. That is a much smaller investigation than tuning the document engine.

## Spike C: cold replay index

Build the smallest candidate-discovery index that can answer:

```text
Which operations could affect this group?
```

It does not need to be memory-optimal or elegant yet. It needs to demonstrate that avoiding full replay scans produces enough wall-clock reduction.

Your current ceiling requires about a 20.4% reduction. I would require the spike to demonstrate **at least 25%** on the adversarial corpus to leave integration margin.

Exit gate:

```text
candidate operations/group falls sharply
exactness remains identical
cold current-view time falls by ≥25%
memory cost is survivable
```

Do not spend time designing a multi-resolution spatial database before this experiment proves that candidate discovery is the right lever.

## Spike D: durable repair work

Change repair identity so nearby camera changes retain already useful tasks and completed groups.

Exit gate:

```text
render amplification ≤1.25
discarded work ≤10%
current viewport still receives first priority
memory use remains bounded
```

If these four spikes pass, the project is viable enough to finish. If one fails, you have a concrete reason to reconsider that requirement before spending time on the remaining features.

# 5. Freeze four architectural invariants

After the spikes, choose the simplest successful implementation and freeze these boundaries:

1. **The operation log is authority.** It does not wait for display presentation.
2. **The PSRAM ring is pure cached canvas.** Chrome, minimap, and volatile ink do not mutate and restore it.
3. **There is one staging compositor.** Any pixel being sent is transformed and overlaid once during the mandatory staging pass.
4. **Background work is resumable and durably keyed.** It does not restart merely because the camera generation changed.

The pure-ring staging approach removes the current extra representation round trip and gives minimap and live ink one obvious place to live. `espdraw-offline-review-2026-08-15.md`

Avoid building a general job framework. A tiny interface is enough:

```cpp
bool step_until(uint64_t deadline_us);
```

Materialization, repair, and autosave can each expose a bounded step. The foreground chooses the next one according to fixed priority:

```text
touch / volatile ink
panel presentation
current-view exact repair
autosave journal flush
velocity-leading repair
optional background repair
```

No background subsystem should contain an unbounded workload-proportional loop while advertising a deadline.

# 6. Finish the features in dependency order

Do not implement them in whichever order feels easiest.

## First: finish panning and the presentation compositor

This includes:

- pan camera behavior;
- ring shifting and wrap;
- final panel transport;
- overlay staging;
- all visual artifact tests.

Panning is not an ordinary feature-it is one of the feasibility foundations.

## Second: add the minimap

Implement it as a pre-rendered or incrementally updated presentation overlay. It should be patched into staged outgoing pixels and should never gather, mutate, or restore the canvas ring.

Immediately rerun the complete pan gate. The minimap is part of the shipped visual load, so a pan result measured without it is not final.

## Third: define and implement undo

Undo must establish:

```text
what constitutes a document revision
whether operations are tombstoned or versioned
which cached tiles remain valid
which repair tasks remain valid
how cold candidate discovery filters inactive history
```

This should happen before productionizing the replay index, because otherwise the index may encode assumptions that undo invalidates.

Test undo not only for correct pixels, but for invalidation scope. Undoing one local stroke should not force unrelated visible and off-screen tiles to rerender.

## Fourth: productionize the cold index and repair retention

Now adapt the successful index spike to the finalized undo/revision semantics.

The production gate is not merely `<500 ms once`. It is:

```text
all fixed adversarial seeds ≤500 ms
current viewport publishes first
undo correctness remains exact
repair amplification remains bounded
camera movement reuses relevant work
```

## Fifth: autosave

Persist authority, not derived presentation caches.

Prefer:

```text
append-only operation/revision journal
periodic compact checkpoint
bounded queue between interactive thread and storage worker
atomic checkpoint replacement
```

Autosave must be deliberately active during ink and pan benchmarks. A benchmark that disables autosave is not a product benchmark.

The interaction lane must never synchronously wait for filesystem housekeeping, compaction, or a complete snapshot.

## Last: SVG export

Export from a stable authority snapshot. It should not depend on tile caches, overview pixels, panel state, or repair completion.

Because it is read-only and off the interaction path, it is the safest feature to merge last. Its main gates are semantic exactness, memory bounds, and cancellation behavior.

# 7. Only then do the final performance-closure pass

This is where you tune the feature-complete product-not before.

Optimize in this order:

1. **Correctness:** no tears, notches, stale rows, seams, lost ink, or invalid undo state.
2. **Ink latency:** newest visual sample first.
3. **24 FPS pan floor:** eliminate serial work and jitter.
4. **500 ms cold current-view exactness:** candidate discovery and publication order.
5. **Rerender amplification:** reuse work and reduce invalidation.
6. **30 FPS and ~300 ms stretch goals:** only after every required gate is green.

Do not chase 30 FPS while cold is still 628 ms, and do not chase 300 ms cold if a change destabilizes the 24 FPS pan floor. A stretch target is not allowed to reopen a required target.

# 8. Add safeguards as each target becomes green

The safeguard should be added **immediately after** the corresponding fix, not at the end.

For every meaningful change:

```text
1. State one hypothesis.
2. Record the full baseline scorecard.
3. Change one mechanism.
4. Record the full scorecard again.
5. Keep it only if the intended metric improves and no closed metric leaves its guard band.
```

Practical repository discipline:

- Keep risky transport, compositor, and indexing paths behind temporary A/B flags.
- Tag every known-good milestone with its evidence bundle.
- Keep Raster V1 available as a control and operational fallback.
- Reject averages-only closure claims.
- Reject software-only claims for visually observable requirements.
- Require exactness tests before hardware performance tests.
- When a metric regresses materially, revert first; investigate second. Do not stack another compensating optimization on top.
- Do not merge a change that “wins” one benchmark by adding a workload-specific exception.

Use two gate levels:

### Fast gate on ordinary changes

- Host exactness and fuzz.
- Deterministic trace replay.
- Candidate counts.
- Render amplification.
- CPU and memory assertions.
- No large regression in software timing.

### Full hardware gate at subsystem milestones

- Optical ink latency.
- Optical pan correctness and cadence.
- Cold current-view completion.
- Autosave/undo interaction.
- Long-session churn.

This gives protection without turning every edit into an elaborate laboratory session.

# 9. Establish explicit stop conditions

This protects you from sunk-cost expansion.

Reconsider the target or hardware only if one of these occurs:

- The minimal, pre-staged panel path cannot produce zero-glitch 24 FPS while Raster V1 still can.
- Volatile ink still fails the latency contract with authority/materialization removed from the critical path.
- A replay-index prototype cannot achieve at least a 25% cold-render improvement while preserving exactness.
- Durable task identities cannot reduce render amplification below 1.25 without unacceptable memory growth.
- Autosave cannot be isolated from interaction without changing the persistence requirement.

Those are meaningful failures. “The current implementation is messy” is not one-the current architecture has identifiable avoidable work.

# The immediate order

Your next work sequence should be:

```text
1. Freeze SHIP_CONTRACT.md.
2. Build the deterministic trace + result-recording harness.
3. Prove the minimal tear-free 24 FPS panel path.
4. Prove visual-first ink.
5. Prove the cold index and durable repair identities.
6. Freeze the architecture and tag the known-good state.
7. Finish pan behavior and minimap.
8. Implement undo.
9. Productionize cold rendering and repair reuse against undo semantics.
10. Implement autosave.
11. Implement SVG export.
12. Run one integrated closure pass with every feature enabled.
```

That sequence avoids two expensive traps: polishing performance before feature semantics are stable, and adding features before the core targets are proven feasible.

The central management rule is simple:

> **Nothing gets called “closed” until it has a permanent oracle, a guard band, and a known-good revision. Nothing new is allowed to invalidate a closed metric.**

## Prompt:
8/15/2026, 4:06:18 PM

this is great. can you sketch out the code for these as much as possible please? take your time. also which of these can be done in parallel. i don't want to go full windfall but i do want to go fast

## Response:
8/15/2026, 4:06:19 PM

(•̀ᴗ•́)و I’m grounding the sketches in the actual V2 classes and call paths, then I’ll separate safely parallel work from the dependencies that must stay serialized.

The existing ring, bounded producer, revisioning, and staging machinery already provides the needed seams. I’ll favor small cross-cutting changes: staging composition plus independently bounded touch, repair, and persistence lanes.

Two scope cuts help: provisional ribbon and minimap machinery largely exist; start with preview-first composition, adding asynchronous authority only if measurements demand it. Undo can remain a latest-gesture timeline cursor.

> **Listing archive contents**
>
>
> **Inspected archive contents, source files, and rendering interfaces**
>
> 
>
> **Planning citation use**
>
> I’m deciding how to support code-specific claims without overcomplicating the answer. I’ll rely on the provided files, cite relevant review findings, and point to source paths or line ranges where useful.
>
> **Planned citation use**
>
> 
>
> **Inspecting vector app flow and transport code**
>
>
> **Inspected vector app, presenter, transport, tile, and undo implementations**
>
> 
>
> **Reworking undo design**
>
> I’m finding that revision numbers are too tightly coupled to operation order for simple undo/redo. I’m comparing cursor-based history with append-only visibility tracking, while keeping replay, tile invalidation, and viewport responsiveness consistent.
>
> **Reworked the undo design**
>
> 
>
> **Separating history state**
>
> I’m separating operation identity from document revision. Undo and redo can then move through active history without rewriting stored records, while new edits after undo create a fresh branch and invalidate prior rendering work.
>
> **Separated history state**
>
> 
>
> **Inspecting Canvas Revision Commit Logic**
>
>
> **Inspected canvas commits, memory exports, and raster autosave implementation**
>
> 
>
> **Structuring implementation lanes**
>
> I’m organizing the remaining work into parallel tracks while protecting shared hotspots. I’ll separate measurement, rendering, indexing, history, persistence, and export, then define integration points and limits on simultaneous edits.
>
> **Structured implementation lanes**
>
> 
>
> **Inspecting ribbon geometry APIs and implementation**
>
>
> **Inspected ribbon geometry APIs and implementation**
>
> 
>
> **Designing the code plan**
>
> I’m grounding the sketches in the current app, presenter, transport, and operation-log boundaries. I’ll separate safe parallel work from shared integration points, with measurement gates before committing architecture.
>
> **Designed the code plan**
>
> 
>
> **Inspecting incremental rasterizer source files**
>
>
> **Inspected rasterizer, vector, revision, minimap, build, and export code**
>
> 
>
> **Inspecting panel staging APIs for composer blueprint**
>
>
> **Inspected rendering, replay, undo, autosave, and blueprint implementations**
>
> 
>
> Worked for 45m 23s

(•̀ᴗ•́)و I mapped the plan onto the `f66c808` source and wrote a detailed, implementation-oriented blueprint with concrete C++ interfaces, state machines, memory estimates, tests, commit boundaries, and parallel work lanes:

[Read the full ESPDraw finish-line code blueprint](sandbox:/mnt/data/espdraw-finish-line-code-blueprint.md)

It is approximately 62 KB of code sketches. It is **not a compile-tested patch**; it is designed to be applied and proven in small commits against the packet source. I deliberately avoided turning it into a generalized scheduler, render graph, job system, or layer framework.

## The most important scope decision

Do **not** begin by making the entire document engine asynchronous.

The first ink fix should be much smaller:

1. Paint newly stable ribbon geometry into the cached frame.
2. Keep only the latest provisional ribbon tail as a presentation-only object.
3. Patch that tail into the internal DMA staging buffer.
4. Submit the newest visual update.
5. Only then perform the existing authoritative commit.
6. Measure optical ink latency.
7. Build a resumable commit state machine only if this still misses the gate.

That changes the critical invariant from:

```text
commit authority → display result
```

to:

```text
display newest consumed sample → commit authority afterward
```

The existing path currently commits before `show_update()`, and the renderer intentionally omits provisional geometry, so this is aimed directly at the known source of visible lag rather than at a speculative scheduler rewrite. `espdraw-offline-review-2026-08-15.md`

## Two interfaces to freeze first

Almost all parallelism becomes safe after these two interfaces are established:

```cpp
struct PanelStagePatch;
struct OperationLogSnapshot;
```

`PanelStagePatch` separates pure cached canvas from presentation-only pixels such as chrome, minimap, and volatile ink.

`OperationLogSnapshot` separates readers such as autosave and SVG export from the mutable operation log and later undo semantics.

A compact version looks like this:

```cpp
struct PanelStageSurface {
  int panel_x = 0;
  int panel_y = 0;
  int width = 0;
  int height = 0;
  int stride = 0;

  // Host-order RGB565. Swap only after presentation layers are painted.
  std::span<std::uint16_t> pixels{};
};

struct PanelStagePatch {
  void* context = nullptr;
  void (*paint)(void*, const PanelStageSurface&) = nullptr;

  void apply(const PanelStageSurface& surface) const {
    if (paint != nullptr) {
      paint(context, surface);
    }
  }
};
```

```cpp
struct OperationLogSnapshot {
  OperationLogEpoch epoch{};
  DocumentRevision revision{};

  std::span<const StoredOperation> operations{};
  std::span<const CompactOperationSample> samples{};

  // Only this prefix is currently visible after undo.
  std::size_t active_operation_count = 0;
  std::size_t active_sample_count = 0;
};
```

Once these are stable, panel work does not need to know undo internals, and autosave/SVG do not need to know how the app stores or presents tiles.

# 1. Panel and compositor code

The product path should expose a panel-frame operation, not a beam-racing operation:

```cpp
enum class TearEdge : std::uint8_t {
  kRising,
  kFalling,
};

struct RingFrameSource {
  const std::uint16_t* pixels = nullptr;
  int stride = 0;
  int shift_x = 0;
  int shift_y = 0;
  int area_width = 0;
  int area_height = 0;
};

struct PanelFrameRequest {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  RingFrameSource source{};
  PanelStagePatch patch{};

  TearEdge edge = TearEdge::kRising;
  std::int64_t edge_timeout_us = 20'000;
};

struct PanelFrameResult {
  bool edge_observed = false;
  bool submitted = false;
  bool completed = false;

  std::uint32_t chunks = 0;
  std::int64_t edge_wait_us = 0;
  std::int64_t prepare_us = 0;
  std::int64_t complete_us = 0;
};
```

Keep rising versus falling edge configurable until the optical A/B identifies the right policy. The product code should not contain a modeled beam row or “writer is probably behind the reader” calculation.

The frame path then becomes:

```cpp
PanelFrameResult Co5300PanelTransport::present_ring_frame(
    const PanelFrameRequest& request) {
  PanelFrameResult result{};

  result.edge_observed =
      wait_for_tear_edge(request.edge, request.edge_timeout_us);

  if (!result.edge_observed) {
    return result;  // Fail closed: not reusable.
  }

  if (!set_frame_window(
          request.x, request.y, request.width, request.height)) {
    return result;
  }

  bool first_chunk = true;
  int next_row = 0;

  while (next_row < request.height) {
    const int rows = std::min(
        kTransferPixels / request.width,
        request.height - next_row);

    std::uint16_t* stage = acquire_transfer_buffer();

    copy_ring_rows_host_order(
        request.source,
        request.x,
        request.y + next_row,
        request.width,
        rows,
        stage);

    PanelStageSurface surface{
        .panel_x = request.x,
        .panel_y = request.y + next_row,
        .width = request.width,
        .height = rows,
        .stride = request.width,
        .pixels = std::span(
            stage,
            static_cast<std::size_t>(request.width * rows)),
    };

    // Chrome, minimap, and volatile ink are painted here.
    request.patch.apply(surface);

    swap_pixels_in_place(surface.pixels);

    const std::uint8_t command =
        first_chunk ? kRamWrite : kRamWriteContinue;

    if (!queue_color_transfer(command, surface.pixels)) {
      return result;
    }

    first_chunk = false;
    next_row += rows;
    ++result.chunks;
  }

  result.submitted = true;
  result.completed = wait_for_all(50'000);
  return result;
}
```

The exact ESP-IDF queue and callback details will need adaptation: each queued transfer must release the correct staging buffer, and the raw IO path must preserve the CO5300 QSPI opcode encoding. That is why I recommend keeping this as an A/B path until it compiles and passes glass.

The presenter’s patch callback remains explicit:

```cpp
void VectorV2Presenter::paint_stage_surface(
    const PanelStageSurface& surface,
    const ChromeState& chrome,
    const ChromeNavigation& navigation,
    bool include_provisional_tail) {
  draw_chrome_strip_overlays(
      {
          .pixels = surface.pixels,
          .width = surface.width,
          .height = surface.height,
          .origin_x = surface.panel_x,
          .origin_y = surface.panel_y,
      },
      chrome,
      navigation);

  if (!include_provisional_tail || !provisional_tail_.valid) {
    return;
  }

  render_intersecting_tail_primitives(
      provisional_tail_,
      surface,
      *renderer_);
}
```

This lets you delete the current pan-time sequence that gathers overlay backdrops from PSRAM, modifies the ring, transfers it, and restores it. That avoidable journey is closely associated with the 28.9 → 41.5/42.0 ms regression. `espdraw-offline-review-2026-08-15.md`

It also means the already-existing minimap renderer can use the same compositor. The minimap is not actually a from-scratch rendering feature: `chrome.cpp` already contains `draw_minimap`, minimap-surface drawing, and strip-overlay integration. Most of the missing work appears to be input/navigation and final product wiring.

# 2. Preview-first ink

Use a fixed-size presentation object; no allocations and no retained layer tree:

```cpp
struct ProvisionalTail {
  std::array<RibbonPrimitive, 8> primitives{};
  std::size_t count = 0;
  std::uint16_t color = 0;
  PixelRect bounds{};
  bool valid = false;

  void clear() {
    *this = {};
  }
};
```

`show_update()` should:

- permanently render `update.committed` into the cached frame;
- replace `provisional_tail_` with `update.provisional`;
- present the union of the old tail, new tail, and newly stable region;
- leave the ring itself free of provisional pixels.

```cpp
LivePresentationTiming VectorV2Presenter::show_update(
    const RibbonUpdate& update,
    std::uint16_t color,
    const ChromeState& chrome,
    std::uint32_t event_us) {
  frame_reuse_.reset();

  const PixelRect old_tail =
      provisional_tail_.valid
          ? provisional_tail_.bounds
          : PixelRect{};

  std::optional<PixelRect> damage;

  if (!update.committed.empty()) {
    renderer_->render(
        update.committed,
        frame_,
        kOverviewWidth,
        chrome_input_bottom(chrome),
        color);

    include_bounds(damage, primitive_bounds(update.committed));
  }

  provisional_tail_.clear();

  if (!update.provisional.empty()) {
    provisional_tail_.count = update.provisional.size();
    provisional_tail_.color = color;

    std::copy(
        update.provisional.begin(),
        update.provisional.end(),
        provisional_tail_.primitives.begin());

    provisional_tail_.bounds =
        primitive_bounds(update.provisional);
    provisional_tail_.valid = true;

    include_bounds(damage, provisional_tail_.bounds);
  }

  if (rect_nonempty(old_tail)) {
    include_bounds(damage, old_tail);
  }

  if (!damage.has_value()) {
    return {.passed = true};
  }

  return present_composited(
      *damage,
      chrome,
      event_us,
      /*include_provisional_tail=*/true);
}
```

The app hot-loop reorder is equally important:

```cpp
const auto operation_point =
    presenter.operation_point(last_ink);

const ChainedOperationStatus add_status =
    builder.add(operation_point);

if (add_status == ChainedOperationStatus::kRejected) {
  reject_current_tail();
  continue;
}

const std::uint16_t color =
    chrome.tool == ChromeTool::kErase
        ? 0xFFFFU
        : selected_color(chrome);

// Produce stable geometry plus the replaceable latest tail.
const RibbonUpdate update =
    ribbon.append(last_ink, /*include_provisional=*/true);

// First: newest sample starts traveling to the panel.
live_metrics.include(
    presenter.show_update(
        update,
        color,
        chrome,
        event_us));

// Second: update authoritative storage.
if (add_status == ChainedOperationStatus::kChunkReady) {
  commit_ready_chunk(
      builder,
      log,
      canvas,
      workspace,
      presenter,
      stroke_world_bounds,
      stroke_chunks,
      stroke_append_us,
      stroke_append_max_us);
}
```

Also pass the true touch timestamp instead of loop-entry time:

```cpp
presenter.show_start(last_ink, color, chrome, event_us);
presenter.show_update(update, color, chrome, event_us);
presenter.pan_from(..., event_us);
```

At lift:

```cpp
const std::uint32_t finished_us = event_us;

ChainedOperationStatus finish_status =
    builder.finish(presenter.operation_point(last_ink));

last_ink = ink.finish({
    .x = last_ink.position.x,
    .y = last_ink.position.y,
    .timestamp_us = finished_us,
});

// Show the latest visual endpoint before draining authority.
live_metrics.include(
    presenter.show_update(
        ribbon.finish(last_ink),
        color,
        chrome,
        finished_us));

while (finish_status == ChainedOperationStatus::kChunkReady ||
       finish_status == ChainedOperationStatus::kFinalChunkReady) {
  finish_status = commit_next_chunk(...);
}

presenter.clear_provisional_tail();
presenter.refresh(chrome, finished_us);
```

Until the authoritative refresh succeeds, the frame must not receive a reusable-pan token.

## Conditional escalation

Only after measuring that version should you consider:

```cpp
class IncrementalCommitJob {
 public:
  bool begin(const OwnedOperationChunk& chunk);
  bool step_until(std::int64_t deadline_us);

  [[nodiscard]] bool done() const;
  [[nodiscard]] bool failed() const;

 private:
  CommitPhase phase_ = CommitPhase::kIdle;
  std::size_t overview_row_cursor_ = 0;
  std::size_t segment_cursor_ = 0;
  std::size_t tile_cursor_ = 0;
};
```

That state machine would split overview raster, affected-tile enumeration, priority painting, and metadata commit into 1-2 ms units. But building it before measuring preview-first ink would be exactly the kind of scope ballooning you are trying to stop.

# 3. Permanent measurements

Keep one fixed-size event buffer rather than logging from hot paths:

```cpp
enum class ClosureEventKind : std::uint8_t {
  kTouchSampled,
  kTouchConsumed,
  kInkGeometryReady,
  kFirstPanelSubmit,
  kLastPanelComplete,
  kFrameEdgeObserved,
  kColdGroupStarted,
  kColdGroupPublished,
  kRepairAttempted,
  kRepairCompleted,
  kRepairDiscarded,
};

struct ClosureEvent {
  std::uint32_t trace_id = 0;
  std::uint32_t sequence = 0;
  std::uint32_t timestamp_us = 0;
  std::int16_t x = 0;
  std::int16_t y = 0;
  ClosureEventKind kind{};
  std::uint8_t flags = 0;
};
```

For ink, also record:

```cpp
std::uint32_t maximum_consumed_time_gap_us = 0;
std::uint32_t maximum_consumed_distance_q8 = 0;
std::uint32_t maximum_sequence_gap = 0;
std::uint32_t coalesced_move_count = 0;
```

This is important because the current queue can replace intermediate move events while the surviving point remains young, so event age by itself does not prove good visible tracking. `espdraw-offline-review-2026-08-15.md`

For repair:

```cpp
struct RepairWorkCounters {
  std::uint32_t unique_required = 0;
  std::uint32_t attempts = 0;
  std::uint32_t completed = 0;
  std::uint32_t reused = 0;
  std::uint32_t canceled_before_work = 0;
  std::uint32_t work_discarded = 0;

  float amplification() const {
    return unique_required == 0
        ? 0.0F
        : static_cast<float>(attempts) /
              static_cast<float>(unique_required);
  }
};
```

# 4. Cold-render index

The smallest promising index is a fixed bitset, not a dynamic spatial database.

With 128×128 world macrocells and blocks of 16 operations:

```cpp
inline constexpr int kReplayCellWorldSize = 128;

inline constexpr int kReplayCellColumns =
    (kWorldWidth + kReplayCellWorldSize - 1) /
    kReplayCellWorldSize;  // 12

inline constexpr int kReplayCellRows =
    (kWorldHeight + kReplayCellWorldSize - 1) /
    kReplayCellWorldSize;  // 14

inline constexpr std::size_t kReplayCellCount =
    kReplayCellColumns * kReplayCellRows;  // 168

inline constexpr std::size_t kReplayOperationsPerBlock = 16;

inline constexpr std::size_t kReplayBlockCount =
    (kOperationCapacity + kReplayOperationsPerBlock - 1) /
    kReplayOperationsPerBlock;  // 250

inline constexpr std::size_t kReplayBlockWords =
    (kReplayBlockCount + 31) / 32;  // 8

inline constexpr std::size_t kReplayIndexBytes =
    kReplayCellCount *
    kReplayBlockWords *
    sizeof(std::uint32_t);  // 5,376 bytes
```

Each macrocell marks which 16-operation blocks might intersect it. A tile group query ORs the handful of relevant cell bitsets, walks only marked blocks newest-first, then keeps the existing exact operation-bounds test as the correctness gate.

That gives:

- no dynamic allocations;
- approximately 5.3 KB fixed memory;
- append-only updates;
- no false negatives when implemented correctly;
- natural support for an undo active prefix;
- cheap handling of very large operations.

The current 628.2 ms cold result repeatedly scans the operation range for each group, so this is aimed at a measured structural cost. `espdraw-offline-review-2026-08-15.md`

I would reject or revise the first index prototype unless it produces at least a **25% adversarial current-view reduction** with exact output unchanged. You need 20.4% for the 500 ms requirement; 25% supplies integration margin.

# 5. Durable repair identity

Do not key active repair work by a whole camera request. Key it by the thing being produced:

```cpp
struct RepairKey {
  DocumentRevision revision{};
  ZoomLevel zoom = ZoomLevel::k50Percent;
  std::uint16_t group_column = 0;
  std::uint16_t group_row = 0;

  bool operator==(const RepairKey&) const = default;
};
```

```cpp
enum class RepairPriority : std::uint8_t {
  kOptional,
  kRememberedZoom,
  kVelocityHalo,
  kCurrentViewport,
};

struct RepairEntry {
  RepairKey key{};
  RepairPriority priority = RepairPriority::kOptional;
  std::uint32_t last_needed_generation = 0;
  std::uint32_t attempts = 0;
  bool active = false;
};
```

A camera move changes priorities and required keys; it does not automatically cancel an overlapping group already being produced. This directly addresses the current behavior in which repair work is frequently superseded, including long-running work that may remain useful. `espdraw-offline-review-2026-08-15.md`

# 6. Undo, autosave, SVG, and minimap

## Undo

Use a whole-gesture active prefix, taking advantage of the existing `gesture_id`.

```cpp
struct OperationLogState {
  std::size_t stored_operation_count = 0;
  std::size_t stored_sample_count = 0;

  std::size_t active_operation_count = 0;
  std::size_t active_sample_count = 0;

  DocumentRevision revision{};
};
```

Undo moves `active_operation_count` backward over all contiguous chunks with the last active `gesture_id`. Redo moves it forward over the next gesture. A new append after undo truncates the redo tail.

Important invariant:

```text
operation storage position is not document revision
```

Document revisions must remain monotonic even when the active prefix moves backward. The replay index can keep indexing stored operations and simply mask queries to the active prefix.

Because one gesture may consist of several operation chunks, any later asynchronous commit queue must preserve gesture order and prevent interleaving.

## Autosave

Use two complete authority slots in the existing 3 MiB drawing partition rather than designing a journal database immediately:

```text
slot A: 1.5 MiB
slot B: 1.5 MiB
```

Each slot contains:

```cpp
struct AutosaveHeader {
  std::uint32_t magic;
  std::uint16_t format_version;
  std::uint16_t header_size;
  std::uint64_t sequence;
  DocumentRevision revision;
  std::uint32_t operation_count;
  std::uint32_t sample_count;
  SavedViewState view;
};

struct AutosaveFooter {
  std::uint32_t payload_crc32;
  std::uint32_t footer_magic;
};
```

Write header and payload first, then the CRC/footer last. On boot, load the highest valid sequence. Copy one 4 KiB authority page while holding a short log lock, release the lock, then perform flash I/O. No flash erase or write should happen while touch/document state is locked.

Persist only:

```text
active operations
active samples
document revision
current view/tool metadata
```

Do not persist tiles, ring pixels, repair queues, or replay-index bitsets.

## SVG export

Generalize the existing one-file FAT exporter from a hard-coded `DRAWING.PNG` name to a configurable 8.3 name such as `DRAWING.SVG`. Do not build a multi-file FAT implementation.

Export directly from `OperationLogSnapshot`:

```cpp
bool export_svg(
    const OperationLogSnapshot& snapshot,
    ByteSink& sink,
    const SvgExportOptions& options);
```

Use one compact path per operation or compatible run. Keep it semantic-pen path, color, width, round cap/join-unless exact pressure-outline fidelity is a product requirement. Exact outline export would be considerably more work and should not silently become part of finish-line scope.

## Minimap

Rendering is already largely present. The initial missing feature can be tap-to-jump:

```cpp
std::optional<NavigationPoint> chrome_minimap_target(
    ChromePoint point,
    const ChromeState& state,
    const ChromeNavigation& navigation);
```

Map minimap coordinates into level coordinates, center the viewport there, and clamp through the existing navigation logic. Ship tap-to-jump first. Minimap dragging can remain outside the finish-line contract unless it is explicitly required.

# What can be done in parallel

Use **three lanes maximum**. More branches will create more integration work than speed.

| Lane | Owns | First sequence |
|---|---|---|
| **A: Glass/presentation** | CO5300 transport, panel staging, presenter pan/full-frame path, optical pan harness | edge API → stage callback → pure-ring overlays → one-window stream → glass proof |
| **B: Authority/cache** | operation log snapshot, replay index, tile producer, repair queue, undo/cache invalidation | snapshot seam → index spike → durable group identity → undo |
| **C: Interaction/leaf** | closure metrics, touch measurements, provisional-tail helper/tests; later autosave, SVG, minimap input | trace schema → tail helper → snapshot consumers |

The ownership boundary matters:

- Lane A owns `VectorV2Presenter` integration while its pan path is being rewritten.
- Lane C can implement and test the provisional-tail data object, but it should not independently rewrite the presenter on another branch.
- Lane B owns both the `TileProducer` cursor change and repair integration. Splitting those between two people will cause repeated merge and state-model conflicts.

## Safe parallel pairs

These can proceed concurrently after the two interface freezes:

```text
panel stream              || replay index
provisional-tail helper   || replay index
pan optical harness       || undo core tests
SVG exporter              || autosave format
minimap input mapping     || autosave implementation
```

These should not be developed independently at the same time:

```text
presenter pan rewrite     X presenter live-ink integration
undo log model            X autosave before snapshot freeze
repair integration        X TileProducer cursor rewrite
final tuning              X unfinished undo/autosave semantics
```

For two developers:

```text
Developer 1:
  panel lane
  then live-ink presenter integration

Developer 2:
  authority/cache lane
  then autosave, SVG, and minimap
```

For solo work, use at most two live worktrees:

```text
closure/glass
closure/document
```

Finish proof-sized commits and merge them before opening separate autosave, SVG, minimap, and undo branches.

# Recommended commit sequence

This is the order I would actually use:

```text
1. closure:
   add contract, trace schema, and baseline result bundle

2. authority:
   add OperationLogSnapshot with no behavior change

3. panel:
   add configurable tear-edge wait and timing capture

4. presenter:
   add PanelStagePatch and remove overlay PSRAM mutation/restore

5. panel:
   add one-window RAMWR/RAMWRC A/B path

6. ink:
   preview-first ordering, replaceable tail, real event timestamps

7. cold:
   add macrocell/block replay index and exactness gates

8. repair:
   key work by revision/zoom/group and retain it across camera replans

9. history:
   active-prefix, whole-gesture undo/redo

10. persistence:
    two-slot authority autosave and recovery

11. features:
    minimap tap navigation and compact SVG export

12. closure:
    run all glass, cold, rerender, undo, autosave, and long-session gates
    with every feature enabled

13. cleanup:
    delete legacy beam-race and temporary A/B paths only after proof
```

Every behavior-changing commit should record the complete scorecard before and after. A commit is rejected when it improves its target but pushes an already-closed metric outside its guard band.

# The first three proof tracks

To go fast without a big-bang rewrite, begin these simultaneously:

### Glass track

```text
configurable TE edge
→ host-order staging callback
→ chrome no longer mutates PSRAM
→ one-window continuation stream
→ ≥10,000-frame optical pan run
```

### Ink track

```text
real event timestamps
→ provisional RibbonStream output
→ presentation-only tail
→ preview before commit
→ dense 25% optical stroke run
```

### Document track

```text
OperationLogSnapshot
→ 5.3 KB replay-block index
→ exactness and fuzz
→ adversarial 400% timing
```

Those three tracks answer the major feasibility questions while committing very little architecture beyond seams you need anyway. Undo, autosave, SVG, and minimap should not begin altering core state until those proof tracks are green and the two shared interfaces are frozen.

The blueprint contains the fuller code for all of these pieces, including the raw CO5300 frame stream, replay cursor integration, repair queue, undo mutation preparation, two-slot autosave state machine, SVG writer, minimap mapping, cooperative work loop, host tests, hardware gates, and the precise file map.



---
Powered by [ChatGPT Exporter](https://www.chatgptexporter.com)