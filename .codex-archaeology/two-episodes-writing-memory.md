# TinyDraw: two episodes reconstructed for writing

This is a memory pack, not article prose. Dates are London development days; timestamps quoted from sessions are stored as UTC unless noted. “Cold wall” means the elapsed real-world wait from starting a cold rebuild through the completed display update, including deliberate yielding for touch responsiveness, unless a beat names a different endpoint.

## How to read the annotations

The technical record remains intact. Each timeline beat now has three extra notes:

- **PLAIN ENGLISH:** what the technical language means without assuming graphics-programming knowledge.
- **WHY IT HELPED:** the causal link between the change and the measured or visible result.
- **NARRATIVE IMPORTANCE:** **core** means the idea belongs in almost any telling; **supporting** means it explains the result if needed; **optional** means it can be omitted without making the story false.

The importance label is about the blog narrative, not the engineering value.

## Episode 1 — cold rendering

### First, the remembered “overnight ~40% win”

The evidence does **not** show an overnight cold-render win of roughly 40%. The Aug 13 overnight navigation/TE work reduced apparent tearing and then left cold rendering badly regressed on the morning of Aug 14. The large same-day cold wins most likely blended together in memory:

- Aug 14 afternoon: adversarial 400% p95 fell about **53–55%**, from roughly 1.45 s to 0.65–0.67 s.
- Aug 16 daytime: a new, harder combined corpus fell **47.3%**, from a 1,269 ms baseline maximum to a 669 ms maximum.
- Aug 18 late: a different settled-AA saturation optimization improved its own workload by roughly **40%**. It is not the same cold-render measurement.

There *was* overnight autonomous work elsewhere in this period, and Alice explicitly discussed agents returning to regressions while she slept. It just is not the source of the principal cold-render percentage.

**PLAIN ENGLISH:** The memory is real but combines three different events: a 53–55% general cold win, a later 47.3% general cold win, and an approximately 40% improvement to the separate final edge-smoothing pass. None was the result of the Aug 13 overnight run.  
**WHY IT HELPED:** Separating them keeps the human memory while preventing three workloads from becoming one fictional benchmark.  
**NARRATIVE IMPORTANCE:** **Core correction.** It is worth stating directly because the remembered overnight surprise shaped the requested story.

### Timeline

#### 1. Aug 12 afternoon — the first attractive cold number is revoked

**WHEN:** Analytic-renderer prototype, before production Vector V2.  
**WHAT I WAS EXPERIENCING:** Settled views were already taking roughly 0.75–1.24 s depending on zoom; visibly settling under 500 ms was the desired experience.  
**WHAT I THOUGHT / SAID:** The exact contemporary reaction is less well preserved than later campaigns, but “under 500 ms” became a remembered proof point.  
**WHAT THE AGENTS THOUGHT:** Fixed-spacing level-of-detail simplification appeared to make high zoom fast enough; PSRAM scratch placement was also predicted to be a large lever.  
**WHAT WE TRIED:** Simplified the stroke geometry, timed cache readiness, and moved coverage scratch into internal RAM.  
**WHAT ACTUALLY HAPPENED:** The simplifier could erase loops, hairpins, pressure extrema, and eraser detail; timing stopped before final physical transfer. The controlled SRAM move improved total wall only 1.69%, not the predicted large amount.  
**MENTAL MODEL CHANGE:** Fidelity and the completion endpoint were part of the benchmark. “Fast” could not mean altered geometry or pixels merely ready in a cache.  
**WHAT HAPPENED NEXT:** The prototype atlas was retired. Production V2 adopted vector authority, overview fallback, sparse world-aligned tiles, transactional publication, and physically meaningful endpoints.  
**SOURCE / CONFIDENCE:** [Git archaeology, milestone 14](git-history.md#14-analytic-settled-rendering-reveals-misleading-lod-metrics-and-rejects-the-psram-scratch-theory); [performance archaeology](docs-performance.md#measurement-conflicts-and-supersessions). **High.**

**PLAIN ENGLISH:** The drawing’s real master copy was the list of strokes, not the screen pixels. The early “fast” version cheated twice: it simplified away real parts of those strokes and stopped the timer before the result reached the display.  
**WHY IT HELPED:** This did not make rendering faster. It stopped the project from optimizing against a false finish line.  
**NARRATIVE IMPORTANCE:** **Core.** It establishes the rule that later wins had to preserve the drawing and measure what Alice actually saw.

#### 2. Aug 13 night → Aug 14 morning — cold rendering becomes an emergency

**WHEN:** First production navigation/TE integration, after overnight work.  
**WHAT I WAS EXPERIENCING:** Tearing looked “basically gone,” but dense overlapping marks rebuilt more slowly with every zoom step. At 400%, Alice watched the image creep in for more than ten seconds.  
**WHAT I THOUGHT / SAID:** “This is rough.” “Now it just takes 10 seconds.” “This is not. This is absolutely not.” The requirement became unambiguous: cold rendering had to be under one second.  
**WHAT THE AGENT THOUGHT:** The new resumable producer was doing vast amounts of repeated geometric and already-finalized work. The existing pleasant seed-7 document did not represent overlapping strokes.  
**WHAT WE TRIED:** First measured an overlap corpus at all zooms rather than treating Alice’s stopwatch impression as the number.  
**WHAT ACTUALLY HAPPENED:** The deterministic baseline was 6.164 / 5.829 / 11.189 / **23.663 s** at 50/100/200/400%. The regression was worse than the spoken “ten seconds.”  
**MENTAL MODEL CHANGE:** Cold cost was zoom- and overlap-amplified, and a realistic-looking corpus could miss it. An exact overlap torture case became necessary.  
**WHAT HAPPENED NEXT:** The morning became a focused emergency optimization stack.  
**SOURCE / CONFIDENCE:** [Session archaeology, Aug 12–14](session-history.md#5-aug-12-14-reviewer-specialization-and-cache-architecture), user messages in the production-island session around `8d1926ae`; checked 20-run overlap receipt. **High.**

**PLAIN ENGLISH:** When TinyDraw had to rebuild a dense drawing from its stored strokes, it kept reconsidering huge amounts of work, especially where strokes overlapped. Zooming in made that waste explode.  
**WHY IT HELPED:** Measuring a deliberately overlapping drawing exposed the real 23.66-second failure. The pleasant test drawing had hidden it.  
**NARRATIVE IMPORTANCE:** **Core.** This is the inciting incident for the cold-render story.

#### 3. Aug 14, about 08:45–09:20 — the first major production cold campaign

**WHEN:** Immediate morning response to the 23.66 s overlap result.  
**WHAT I WAS EXPERIENCING:** Alice made a document “so fucking evil,” cycled through zooms, and found 50–200% good and 400% only a little over a second. Fast panning during an active rebuild could still be swallowed.  
**WHAT I THOUGHT / SAID:** “Cold renders are much better now… we’re back on track here.” She also asked for a corpus at least two to four times more evil.  
**WHAT THE AGENT THOUGHT:** The producer was repeating obvious work: distant segments, needless subdivision, division inside loops, individual pixel tests for solid interiors, and fixed pacing delay.  
**WHAT WE TRIED:** A compact stack: spatial rejection, remove redundant subdivision, hoist projection math, fill scanline interiors, track capsule edges incrementally, coalesce straight runs, remove the fixed delay.  
**WHAT ACTUALLY HAPPENED:** Reset-separated p95 became about **771 / 782 / 873 / 971 ms** at 50/100/200/400%. Exactness and bounded ticks held.  
**MENTAL MODEL CHANGE:** Tens of seconds were not inherent to vector replay. Several mechanically small exact changes could compound by more than an order of magnitude.  
**WHAT HAPPENED NEXT:** Better results exposed a harder tapered corpus and a separate long-stroke freeze: every 64 samples, ink could stop for roughly 70 ms.  
**SOURCE / CONFIDENCE:** [Git archaeology, milestone 18](git-history.md#18-cold-replays-first-large-optimization-stack-changes-from-geometric-replay-to-saturation-aware-row-work); messages `371a4ba2`, `a0f9873b`; receipt `492f2ef-overlap-cold-p95-20-runs.log`. **High.**

**PLAIN ENGLISH:** The renderer stopped doing several kinds of obvious busywork. It ignored strokes that could not reach the current patch of screen, stopped chopping simple lines into needless pieces, moved repeated calculations out of inner loops, and filled solid horizontal runs in one go.  
**WHY IT HELPED:** Each change removed work performed thousands or millions of times. Together they turned a 23-second rebuild into roughly one second.  
**NARRATIVE IMPORTANCE:** **Core.** This is the first dramatic comeback. The individual tricks are **supporting**; the important point is that several small exact changes compounded.

#### 4. Aug 14, 13:50–16:22 — saturation supplies the first deep idea

**WHEN:** Fable xhigh’s funded long-stroke + adversarial-cold session.  
**WHAT I WAS EXPERIENCING:** Cold was no longer catastrophic, but a long line visibly froze in chunks and seemed to cold-render marks just drawn. The tapered 400% adversarial case still had p95 **1.452 s**.  
**WHAT I THOUGHT / SAID:** The prompt asked for “mechanical sympathy and elegance, demoscene mindset.” On seeing the result: “These are really good numbers. I mean, minus 53% is incredible.”  
**WHAT THE AGENT THOUGHT:** Newer opaque paint or eraser often fully determined pixels or rows, yet replay kept visiting older operations to prove the same thing again. Tiny work units also paid excessive scheduling delay. Long-stroke copies bought transactionality after all failure points were already known.  
**WHAT WE TRIED:** Newest-first finalized-pixel/row/group saturation; honest work budgeting; validate-first, in-place interactive chunk commits.  
**WHAT ACTUALLY HAPPENED:** Adversarial 400% fell about **1.449 s → 0.671 s** in the first run and to a 20-run p95 of **0.675 s**; steps fell 4,018→904. Overlap cases improved 14–31%. Seed-7 400% regressed about 6%, which was retained as an explicit tradeoff. Worst long-stroke chunks fell below 15 ms.  
**MENTAL MODEL CHANGE:** Cold replay did not need to scale with document history. Painter order could *prove* that older work could no longer affect the final pixel. Saturation, not approximate geometry, became a durable organizing principle.  
**WHAT HAPPENED NEXT:** Richer cross-zoom caches made synchronous drawing fan out over hundreds of resident tiles. The cold win exposed drawing latency as the more important blocker.  
**SOURCE / CONFIDENCE:** [Long-stroke/cold receipt](../docs/receipts/vector-v2/LONGSTROKE_COLDRENDER_INVESTIGATION_2026_08_14.md); messages `500fd477`, `6f4ca3c2`. **High.**

**PLAIN ENGLISH:** TinyDraw painted the newest strokes first. Once a newer stroke had completely decided a pixel, row, or tile, it skipped every older stroke hidden underneath. Separately, long live strokes stopped copying whole tiles out and back before committing each chunk.  
**WHY IT HELPED:** Dense drawings contain lots of buried paint. Proving that buried work could not affect the final image removed most of the replay steps. Avoiding defensive tile copies removed the periodic 70 ms ink freezes.  
**NARRATIVE IMPORTANCE:** **Core.** “Stop drawing what is already covered” is the main conceptual breakthrough of the first campaign. The tile-copy detail is **supporting** because it explains why cold and live ink were worked on together.

#### 5. Aug 14 evening–Aug 15 — cold deliberately leaves center stage

**WHEN:** After the first cold target was met; UI and then the second performance round.  
**WHAT I WAS EXPERIENCING:** The richer cache made ink lag; later glass showed 15.5–35.8 ms chunks, 92–143 ms input gaps, and blur-then-sharpen. Panning remained visibly slow and then tore.  
**WHAT I THOUGHT / SAID:** Priority was stated explicitly: drawing must never lag; panning should be at least 30 FPS; cold should later be cut again. At one point: “fuck it I’m kind of bored with performance work,” document the known regressions, do UI, then return.  
**WHAT THE AGENT THOUGHT:** The cold renderer itself was no longer the only determinant of experience. Warm cache fanout and display presentation could erase its gains.  
**WHAT WE TRIED:** Active-zoom-only mutation and latency budgets looked green in a harness, but the physical display invalidated that closure. Attention moved to drawing and panning/tearing.  
**WHAT ACTUALLY HAPPENED:** Cold remained around the accepted subsecond range, but the project stopped treating it as independently optimizable. Some cold-friendly cache behavior had to be traded or moved off the input path.  
**MENTAL MODEL CHANGE:** The product objective was a balanced latency system, with glass outranking a local benchmark. A warm cache could be a liability when updated synchronously.  
**WHAT HAPPENED NEXT:** Ring-buffer pan and the Fuji tearing episode consumed Aug 15; cold resumed on Aug 16 with a frozen combined corpus.  
**SOURCE / CONFIDENCE:** [Session archaeology, priority dispute](session-history.md#6-aug-14-15-priority-order-and-benchmark-definition-disputes); messages `4b5b30a1`, `902d4b6f`, `7b5d046d`, `02a7d4d0`. **High.**

**PLAIN ENGLISH:** A faster rebuild did not automatically make the app feel fast. Keeping many zoom levels ready meant every new stroke could trigger too much immediate cache work, and the display still had its own panning problems.  
**WHY IT HELPED:** Pausing cold work forced the project to judge the whole interaction instead of celebrating one benchmark while drawing or panning regressed.  
**NARRATIVE IMPORTANCE:** **Core transition.** It explains why the cold story stops, detours through tearing, and resumes later.

#### 6. Aug 16, 10:33–13:24 — Wave 3 returns to cold with a harder battery

**WHEN:** After the optical pan closure and visual-first ink groundwork.  
**WHAT I WAS EXPERIENCING:** Alice still wanted cold under 500 ms and smoother ink and felt she was running out of ideas. A combined tapered + “evil hairlines” authority contained 910 operations and 12,157 samples.  
**WHAT I THOUGHT / SAID:** Again: “mechanical sympathy, elegance and demoscene mindset.”  
**WHAT THE AGENT THOUGHT:** There was no remaining single gross bug. Cost lay in millions of row probes, repeatedly prepared curves, PSRAM scratch, overlapping chords within one curve unit, and one painter forced to serve incompatible cold and warm access patterns.  
**WHAT WE TRIED:** Stateless masked-window search, internal-SRAM producer scratch, prepare curve units once, merge a unit’s chords into one row sweep, and keep separate cold and warm painter paths.  
**WHAT ACTUALLY HAPPENED:** The reset-separated maximum fell **1,269.157 → 668.980 ms (-47.3%)**. Every sibling exactness/fuzz gate stayed green. Several plausible ideas lost on-device and were reverted: summary bitmaps, word mask loads, a 6×2 replay band, and a shared hybrid painter.  
**MENTAL MODEL CHANGE:** The route forward was a stack of local exact proofs aligned with this CPU and memory hierarchy. Host neutrality or elegance did not predict ESP32 performance.  
**WHAT HAPPENED NEXT:** The remaining ~169 ms demanded instruction-level arithmetic and publication/metadata work, not a new cache architecture.  
**SOURCE / CONFIDENCE:** [Wave 3 receipt](../benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md); [performance ledger row 8](docs-performance.md). **High.**

**PLAIN ENGLISH:** The second campaign attacked smaller repeated costs: start each row search near the only pixels still missing, keep the temporary working image in faster memory, calculate each curve once, process overlapping pieces together, and use different routines for rebuilding versus live drawing.  
**WHY IT HELPED:** No single remaining mistake dominated. Removing five medium-sized costs cut the harder test almost in half. Failed device tests also showed that code which looked faster on the Mac could be slower on the ESP32.  
**NARRATIVE IMPORTANCE:** **Core.** This is where the story changes from one big insight to a pile of hardware-aware local wins.

#### 7. Aug 16 — floating-point library calls and the old-school inverse-square-root trick

**WHEN:** Inside Wave 3, while making the stateless row-window idea win at every zoom.  
**WHAT I WAS EXPERIENCING:** A mathematically sensible span bound initially improved 400% while regressing lower zooms.  
**WHAT I THOUGHT / SAID:** No distinct user quote is preserved for the arithmetic diagnosis; the emotional context was the insistence that every zoom matter.  
**WHAT THE AGENT THOUGHT:** Disassembly showed Xtensa emitted `callx8` library calls for float division, `floor`, `ceil`, and `sqrt`—four calls per row in one candidate. The “cheap math” was not cheap on ESP32-S3.  
**WHAT WE TRIED:** Hoisted reciprocals per segment, native truncation-based floor/ceil helpers, exact binary zoom constants, and a padded fast inverse-square-root-style bit hack to seed a conservative search interval.  
**WHAT ACTUALLY HAPPENED:** The arithmetic change flipped the row-window work from a mixed result into a win at every zoom. Crucially, the approximate inverse square root never decided pixel coverage; exact `covers_pixel` remained authoritative and expanded/probed from the conservative seed.  
**MENTAL MODEL CHANGE:** Approximation was safe only as a search hint around an exact oracle. Instruction selection mattered as much as the algebra.  
**WHAT HAPPENED NEXT:** With row search cheaper, curve preparation, row visits, publication copies, and metadata became visible enough to optimize.  
**SOURCE / CONFIDENCE:** [Wave 3 receipt, accepted change 2](../benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md#accepted-changes); [performance conflict ledger](docs-performance.md#dead-ends-failed-hypotheses-and-measurement-traps). **High.**

**PLAIN ENGLISH:** Innocent-looking decimal math was calling slow software helper functions because this chip cannot perform division and square root directly in hardware. The old fast-inverse-square-root trick gave the renderer a cheap place to begin looking, then the exact routine still checked every pixel that mattered.  
**WHY IT HELPED:** It removed several function calls from every rendered row without allowing approximate math to change the picture.  
**NARRATIVE IMPORTANCE:** **Core and colorful.** This is one of the few low-level details worth explaining because it captures the “demoscene mindset” and has a clean safety story.

#### 8. Aug 16 afternoon — first real brush with the 500 ms finish line

**WHEN:** Cold Stage B after Wave 3.  
**WHAT I WAS EXPERIENCING:** The general battery was close enough that a few milliseconds and unrelated code layout were now visible in the verdict.  
**WHAT I THOUGHT / SAID:** “As long as cold doesn’t regress again we hit the targets. Yes sure it’s a few ms over 500 but I’ll take it.”  
**WHAT THE AGENT THOUGHT:** Reduce traversal and publication overhead: one operation-level y-sorted chord sweep, direct/strided publish, O(1) raw-slot metadata. A 520 ms development guard would distinguish real regressions from known ±2–3% build-layout movement while preserving the 500 ms release requirement.  
**WHAT WE TRIED:** Those Stage B changes, plus pinning the *panel transport* hot loop in IRAM after unrelated code moved byte-swap timing from about 11 to 17–18 µs.  
**WHAT ACTUALLY HAPPENED:** 50/100/200% walls were about **438 / 428 / 488 ms**; 400% was about **507 ms**. The target was functionally reached but not yet the release proof.  
**MENTAL MODEL CHANGE:** At this stage, code placement and flash layout were performance inputs. A 2–3% change could cross the entire remaining margin.  
**WHAT HAPPENED NEXT:** The scorecard itself was audited—and revealed a missed 50% overlap failure.  
**SOURCE / CONFIDENCE:** [Cold Stage B receipt](../benchmark-results/cold-stage-b-2026-08-16/RECEIPT.md); message `9c671876`. **High for measurements; “accepted” was a development decision, not final closure.**

**PLAIN ENGLISH:** After drawing pixels became cheaper, copying finished tiles, looking them up, and moving display bytes started taking a noticeable share of the remaining time. TinyDraw removed extra copies and repeated searches. It also put one display routine in faster instruction memory so unrelated code changes stopped making it randomly slower.  
**WHY IT HELPED:** These changes recovered the last large chunk before the 500 ms line. They also reduced timing variation between builds.  
**NARRATIVE IMPORTANCE:** **Supporting.** The 507 ms near-miss matters; the exact names of the copying and lookup changes are optional.

#### 9. Aug 16 evening — a missing corpus invalidates the feeling of closure

**WHEN:** Finish-line handoff later the same day.  
**WHAT I WAS EXPERIENCING:** A synthetic stack of overlapping XL strokes still took about 622 ms at 50%, but several hours of cold-specific work had not shown that red gate in the scorecard.  
**WHAT I THOUGHT / SAID:** “I am very disappointed… several hours of work on cold render specifically and this never came up. I keep losing so much time and money and tokens.”  
**WHAT THE AGENT THOUGHT:** The combined “general” corpus and overlap corpus exercised different saturation shapes; one could not stand in for the other.  
**WHAT WE TRIED:** Documented the red gate and deferred its fix behind inking, AA, and déjà vu as Alice requested.  
**WHAT ACTUALLY HAPPENED:** The few-milliseconds-over-500 story remained true for the general corpus, but it was no longer allowed to imply that every cold case passed.  
**MENTAL MODEL CHANGE:** Benchmark batteries were product requirements, not scenery. An omitted row could undo hours of confidence even when every reported number was honest.  
**WHAT HAPPENED NEXT:** On Aug 17 a narrower finalized-pixel window attacked this specific overlap shape.  
**SOURCE / CONFIDENCE:** [Overlap fix receipt](../benchmark-results/overlap-cold-fix-2026-08-17/RECEIPT.md); messages `fc35f59d`, `377afdb9`. **High.**

**PLAIN ENGLISH:** One difficult test had simply been left out of the dashboard. The reported numbers were real, but the dashboard did not represent every requirement.  
**WHY IT HELPED:** Finding the omission did not speed up the renderer. It prevented a partial success from becoming the project’s official story and forced a targeted fix.  
**NARRATIVE IMPORTANCE:** **Core.** This is the human cost of bad benchmark coverage and one of the clearest examples of why Alice kept demanding harder tests.

#### 10. Aug 17 — the overlap regression is closed without pretending it is the whole story

**WHEN:** Feature-completion day, between minimap/export/history work.  
**WHAT I WAS EXPERIENCING:** The 50% overlap case was the embarrassing outlier even though ordinary/general cold felt acceptable.  
**WHAT I THOUGHT / SAID:** The priority list explicitly kept “the 50% cold stroke thing” for the final optimization work.  
**WHAT THE AGENT THOUGHT:** Partially finalized rows were still searching too broad a horizontal window; the useful unset region was much narrower.  
**WHAT WE TRIED:** Restrict each search to the exact unfinalized window while retaining the exact coverage predicate.  
**WHAT ACTUALLY HAPPENED:** 50% overlap wall fell **585.821 → 476.969 ms**, and steps 235→90. Provenance was a measured uncommitted treatment tree, so archaeology classifies it “probably valid,” not a release statistic.  
**MENTAL MODEL CHANGE:** Saturation was not a one-time optimization. Its summaries/windows had to match the granularity of the remaining holes.  
**WHAT HAPPENED NEXT:** Feature work landed; then the final system-wide performance round revisited instruction placement and pathological documents.  
**SOURCE / CONFIDENCE:** [Overlap fix receipt](../benchmark-results/overlap-cold-fix-2026-08-17/RECEIPT.md); [ledger row 10](docs-performance.md). **High mechanism confidence; medium-high publication confidence.**

**PLAIN ENGLISH:** Even on a partly finished row, the renderer was searching across too much empty or already-completed space. It learned the exact horizontal gap that still needed work and searched only there.  
**WHY IT HELPED:** The overlap case required 90 work steps instead of 235 and finally dropped below 500 ms.  
**NARRATIVE IMPORTANCE:** **Supporting.** It closes the missing-test incident. The “search only the remaining holes” explanation is enough for the blog.

#### 11. Aug 18 afternoon — whole-rasterizer IRAM produces a late universal win

**WHEN:** Final cooperative-latency and performance review, after the core algorithm was already mature.  
**WHAT I WAS EXPERIENCING:** Individual cold cases were already near the threshold; random build layout could move them by the remaining margin.  
**WHAT I THOUGHT / SAID:** Alice asked to push the limits as long as cold did not regress. The excitement is best understood relative to the remaining few-percent margin, not as a dramatic visual transformation by itself.  
**WHAT THE AGENT THOUGHT:** The incremental rasterizer’s hot instruction footprint was still competing in flash cache. Moving the whole object into IRAM could remove layout dice across many callers.  
**WHAT WE TRIED:** Same-tree physical A/B with the full incremental rasterizer in IRAM, spending roughly 13 KiB internal heap. This is distinct from the earlier panel-transport IRAM pin.  
**WHAT ACTUALLY HAPPENED:** All 11 cold/settled compute cases improved **6.93–11.68%**, median **8.70%**, with zero regressions and unchanged pixels.  
**MENTAL MODEL CHANGE:** A universal 7–10% was huge once algorithmic wins were exhausted and the release margin was single digits. Code placement had become an explicit design resource.  
**WHAT HAPPENED NEXT:** The remaining work concentrated on real owner journals, dense crossings, history/AA saturation, and small redundant passes.  
**SOURCE / CONFIDENCE:** [F24 IRAM A/B](../docs/receipts/vector-v2/F24_RASTER_IRAM_AB_2026_08_18.md); [ledger row 19](docs-performance.md). **High.**

**PLAIN ENGLISH:** The ESP32 normally executes this code from flash through a small cache. Moving the whole hot drawing routine into scarce on-chip instruction memory made it run consistently faster.  
**WHY IT HELPED:** At this late stage, a universal 7–10% improvement was larger than the remaining release margin, and it removed some luck caused by where the linker happened to place code.  
**NARRATIVE IMPORTANCE:** **Core.** The exact memory cost is supporting detail; the important point is why a seemingly modest percentage felt enormous.

#### 12. Aug 18 night–Aug 19 — evil hairlines become permanent, and local tricks finish the job

**WHEN:** Torture-document/release closure.  
**WHAT I WAS EXPERIENCING:** Thin, dense, crossing strokes repeatedly broke results that looked good on tapered or ordinary documents; a phantom/tap-dot defect made the pathology visible as correctness as well as speed.  
**WHAT I THOUGHT / SAID:** “We probably need to add something to [the] battery that checks what I refer to as ‘evil hairlines’ where a lot of thin strokes are dense and cross each other.” This was also a familiar manual habit: deliberately drawing evil hairlines whenever a build looked too comfortable.  
**WHAT THE AGENT THOUGHT:** Remaining cost was overdraw, occupancy discovery, redundant cold preflight, and settled-AA work on already-final destinations. Several broad grouping ideas would expose more unsaturated area and lose.  
**WHAT WE TRIED:** Owner-journal torture corpus; occupancy and saturated replay stops; fewer resumptions; remove duplicate preflight; exact opaque/saturated AA skips. Measured grouping/batch experiments were reverted when slower or when they hit watchdog/memory limits.  
**WHAT ACTUALLY HAPPENED:** The *settled-AA* saturated-destination skip, after an earlier invalid comparison, was re-run on like revisions and won about 36–43% across zooms. It must not be conflated with general cold. The general cold battery reached release below 500 ms at all zooms.  
**MENTAL MODEL CHANGE:** “Pathological” meant “a shape Alice actually draws to test the product.” Late performance came from many exact, corpus-specific local proofs—not one final architecture reveal.  
**WHAT HAPPENED NEXT:** A bounded five-experiment closeout rejected risky batching and kept only honest work slicing plus redundant-preflight removal, then ran the release battery at one revision.  
**SOURCE / CONFIDENCE:** [Release milestone](git-history.md#29-release-closure-fixes-torture-document-edge-cases-and-records-same-revision-evidence); [settled saturation supersession](docs-performance.md#conflicts-and-supersessions); messages around `05b24f5e`. **High.**

**PLAIN ENGLISH:** The final tests used the dense, thin crossing strokes Alice naturally drew when trying to break a build. TinyDraw added more exact ways to stop once old paint was irrelevant, remembered which areas could contain ink, and removed one duplicated “is there work left?” scan. Several larger batching ideas failed and were thrown away.  
**WHY IT HELPED:** The torture drawing exposed costs that friendly documents hid. Small safe cuts were enough to put every released general-cold result below 500 ms.  
**NARRATIVE IMPORTANCE:** **Core closure.** Evil hairlines, rejected last-minute gambles, and the final sub-500 result belong in the ending.

### Exact publication numbers — keep separate from the timeline

| What can safely be published | Before | After | Qualification |
|---|---:|---:|---|
| Aug 14 overlap emergency, 400% p95 | 23.663 s | 0.971 s | Same overlap protocol; earliest production cold rescue. |
| Aug 14 adversarial tapered, 400% p95 | 1.452 s | 0.675 s | 20 reset-separated runs; -53.5%. Seed-7 400% regressed ~6.4%. |
| Aug 16 frozen combined corpus, 400% max | 1,269.157 ms | 668.980 ms | Development characterization; -47.3%; not the final release distribution. |
| Aug 18 whole-rasterizer IRAM | — | -6.93% to -11.68% compute | Same-tree 11-case A/B; median -8.70%; compute, not paced wall. |
| Aug 19 released general cold walls | — | **389.942 / 383.159 / 456.961 / 492.793 ms** | 50/100/200/400%; release battery at `a5db58d`. A requested 20-reset final-product 400% distribution was not run. |

**PLAIN ENGLISH:** These rows are different chapters, not one continuous benchmark. The 23.663→0.971 s result is the emergency rescue; 1.452→0.675 s is the harder same-day saturation campaign; 1,269→669 ms is the later combined-corpus campaign; IRAM is a controlled percentage improvement; 492.793 ms is the released 400% result.  
**WHY IT HELPED:** Keeping the figures separate prevents a true number from being attached to the wrong test, endpoint, or stage of the project.  
**NARRATIVE IMPORTANCE:** **Core reference.** The article probably needs the emergency before/after, one later campaign, the IRAM percentage, and the final result. It does not need every intermediate number in running prose.

### What survived versus what taught us and was discarded

This includes the general cold-rebuild path, techniques that reduced how often a cold rebuild occurred, and the adjacent settled-AA path where it repeatedly informed the cold work. “Survived” means the technique or its refined descendant is in the released architecture; it does not mean every early implementation survived unchanged.

| Area | Survived into the released architecture | Instrumental, superseded, or rejected |
|---|---|---|
| Representation | Logical vector authority with a complete low-resolution overview plus sparse, world-aligned refined tiles. Missing detail always had a coherent fallback. | Whole-document replay on every sample; the camera-aligned 3×3 atlas, which accumulated rejected pan requests and multi-second repairs. |
| Fidelity and endpoint | Exact geometry, checksum equality, adversarial-shape tests, and timing through physical display completion. | Fixed-spacing LOD that appeared to get under 500 ms by deleting loops/hairpins/pressure/eraser detail; cache-ready timing that stopped before final DMA. |
| Basic geometric rejection | Operation/segment/tile bounds, distance culling, removal of redundant subdivision, clipped row ranges, and rejection before expensive painting. | Treating every operation and every subdivided segment as relevant to every tile. The first census also omitted span-search calls and therefore understated the true work. |
| Scanline painting | Solid capsule interiors as spans, conservative edge narrowing, straight/collinear run coalescing, and later exact row-window narrowing. | Per-pixel full-box walks; an early conservative per-row span design whose `sqrt`/setup overhead made 50–200% much slower; a later scanline-recurrence variant that regressed device wall 3.3%. |
| Painter order | Newest-first replay with exact finalized-pixel masks. Newer opaque paint/eraser can certify that older work cannot affect the result. | Forward-style replay that repeatedly repainted or re-proved pixels already determined by newer operations. |
| Saturation hierarchy | Exact pixel/run, row, row-range, operation/segment, group, and later occupancy/surface early exits. Narrow unfinalized windows closed the overlap-50 case. | A separate summary-bitmap row probe: 400% improved 2.2%, but 50% regressed 4%; per-row summary probing inside painters also cost more than it saved. |
| Mask scanning | Byte-granular `l8ui` scans over short unfinalized windows. This matched the actual post-H7 window length on Xtensa. | Word-mask loads, tried twice: ordinary loads emitted `callx8`; explicitly aligned `l32i` still lost 4–5 ms because branch/alignment overhead exceeded the gain. |
| Span search | Stateless cold search bounded by both conservative chord geometry and the row’s exact unset window; exact `covers_pixel` remains the oracle. | The historical warm-start search on fragmented cold masks; a single hybrid warm/seeded algorithm, which regressed cold about 5% and also worsened appends. |
| Arithmetic | Per-segment reciprocal hoists, native truncation-based floor/ceil, exact binary zoom constants, and a padded inverse-square-root bit seed used only for a conservative bound. | Float division, `floor`, `ceil`, and `sqrt` in the row loop—four Xtensa `callx8` library calls per row in one candidate. Approximate math was never accepted as pixel authority. |
| Geometry preparation | Prepare curve units once per endpoint and reuse their subdivision/bounds within the active operation. | Re-deriving quadratic subdivision in count, bounds, and apply; later, a persistent ~100 KiB prepared-geometry cache cut setup time but produced no reliable release-wall gain and was removed. |
| Row traversal | Merge the chords of a curve unit, then use one operation-level y-sorted active-chord sweep. One row/window pass handles the union of same-color chords. | A 6×2 tile band intended to amortize setup; broad rows rarely saturated and host 400% became 2.7× slower. Wider bands without block-granular saturation exposed buried work. |
| Cold versus warm callers | Separate cold and interactive painter paths: fragmented newest-first masks use stateless windows; fresh append masks retain warm-start behavior. | One universal painter/search path. The access patterns were different enough that sharing the policy lost on both sides. |
| Producer surface shape | The 2×2 supertask remained the useful balance of shared setup, saturation locality, and 32 KiB internal scratch. | 2×4 doubled scratch to 64 KiB and broke internal-memory bootstrap before timing; 1×2 duplicated enough setup to trip the five-second watchdog. |
| Memory placement | The 32 KiB producer surface and packed tile in internal SRAM with fallback; allocation order chosen to avoid PSRAM cache-set penalties. | The early claim that moving the old main coverage scratch to SRAM would yield ≥40%; controlled wall improvement was only 1.69%. A 40 KiB settled workspace placed mid-heap later cost ~9 ms and had to move last. |
| Code placement | Panel transport was pinned first to stop presentation-layout dice; later the complete incremental rasterizer moved to IRAM, winning 6.93–11.68% across all 11 cases. | Leaving hot loops at the mercy of flash-cache layout when unrelated code could move cold timing 2–3%. IRAM was not free: the raster move spent roughly 13 KiB of internal heap. |
| Publication | Strided/direct publish from the producer surface, avoiding an intermediate packed-copy path; O(1) raw-slot metadata replaced repeated directory work. | Exact publication batching as the main cure: best wall improvement was only ~1.6%, and broader batches violated the 15 ms interaction bound. |
| Work slicing | Deadlines and budgets based on honest visited span pixels; later 22k masked-work slices reduced resumptions while max ticks stayed ~10.6 ms. | One tiny producer action per poll with fixed sleeps; flat 128-row slices produced an 18.1 ms idle-repair step; 96-op scan batches raised max tick to 12.57 ms with essentially flat wall time. |
| Producer discovery | A missing-group search also returns the nearest group, so release removed the redundant second preflight directory scan. | Re-scanning the full visible tile directory twice, and historically scanning 320 slots on each of 4,018 slices despite a comment claiming otherwise. |
| Spatial candidate index | Append-maintained world-cell index with exact fallback, used only when it rejects at least 25%. It helps sparse documents and history while avoiding dense indirection. | Treating the block index as the magic adversarial-cold solution: seed-7 scanned 90% fewer ops but wall improved only 10.9%; the dense adversarial corpus pruned 0% because all 1,038 candidates genuinely intersected. |
| Cache retention / avoiding cold work | Idle absorption updates retained tiles at every remembered zoom; remembered-view materialization and the eventual 604-slot pool made revisits stay warm. | Synchronously maintaining every zoom while drawing, which caused 21–130 ms chunks; active-zoom-only invalidation solved input latency temporarily but created 188–326 ms “déjà vu” refills. |
| Background architecture | Authority-only commits plus exact pending overlay; materialization, absorption, repair, settling, metadata, and publication run as resumable, touch-preemptible work. | Forcing materialized pixels current before authority could commit, or treating a good total time as proof that no individual slice blocked input. |
| Memory budget for retention | Measured modal lifetimes: actual export peak 291,484 bytes funded 604 tile slots, with autosave/export sequenced rather than reserved concurrently. | The synthetic 1.5 MiB “export reserve,” which reduced cache capacity without representing measured concurrent need. |
| Late dense/pathological work | Evil-hairline/owner-journal corpora; occupancy preservation; saturated replay stops; fewer resumptions; constant-capsule certificates where exact preconditions hold. | Broad render-group/batch ideas without a saturation proof; improvements on tapered or seed-7 documents assumed to generalize to dense crossings. |
| Settled AA, related but separate | Exact newest-first alpha accumulation, long-chord row narrowing, saturated-destination/white fast paths, dense saturation aggregation, and a modest opaque-first composite path. | 4-sample brute-force SSAA (~808 ms); adaptive AA bands that regressed dense 400% by 83%; blank-pixel final-fold (+2–11%); row-local touched-span merge (+10–20% dense); shift/add `/255`, which the compiler already optimized. |
| Measurement discipline | Frozen combined and overlap corpora, reset-separated receipts where available, exact checksums/fuzzers, all zooms, max-tick gates, same-tree physical A/B, and explicit per-corpus scorecard rows. | Seed-7 as a universal proxy; the omitted overlap-50 red row; host speed as device truth; unlike-revision comparison that initially caused the successful settled saturation skip to be rejected; the chronologically stale Aug 18 “final” baseline. |

The broad pattern was: exact rejection and saturation survived; approximation was allowed only to narrow a search whose final decision stayed exact; host-only wins, wider work units, extra summaries, and “obviously faster” word operations repeatedly lost on the ESP32.

#### Plain-English map for the cold technique table

| Area | What it means in ordinary English | Why it helped | Narrative importance |
|---|---|---|---|
| Representation | Store the actual strokes as the master drawing. Keep a cheap complete preview and add detailed square patches where needed. | The screen always had something correct to show, even while detail was still rebuilding. | **Core concept.** Explain once; omit the names of the cache layers unless useful. |
| Fidelity and endpoint | A speed result counted only if the drawing stayed identical and the pixels had actually reached the screen. | It ruled out fake wins caused by deleting detail or stopping the clock early. | **Core rule.** |
| Basic geometric rejection | Before drawing a stroke into one square patch, ask whether the stroke can touch that patch at all. | Most strokes are irrelevant to most patches, so rejecting them early avoids deeper work. | **Supporting.** “Skip strokes too far away” is enough. |
| Scanline painting | A thick line is a pill-shaped region. On each screen row, fill its continuous middle in one run instead of asking about every pixel separately. | One range fill replaces many individual geometry tests. | **Supporting and explainable.** This is the answer to “solid capsule interiors as spans.” |
| Painter order | Work from the newest stroke backward. | New paint hides old paint, so TinyDraw can stop considering history underneath it. | **Core breakthrough.** |
| Saturation hierarchy | Track whether one pixel, a row, a group of rows, or a whole patch is already completely decided. | The renderer can skip at the largest proven level instead of checking every covered pixel again. | **Core mechanism**, but one example is enough. |
| Mask scanning | Keep a small yes/no map of which pixels are already decided and scan it one byte at a time. | The remaining gaps were short, so the simplest CPU instruction beat supposedly clever word-at-a-time code. | **Optional color.** Useful as a hardware-surprise example. |
| Span search | Begin looking where geometry says the stroke might be and where the unfinished pixels actually are. | It avoids starting at the edge of every row and walking through completed space. | **Supporting.** “Search only the remaining gap” is enough. |
| Arithmetic | Precompute divisions, use instructions the chip handles directly, and use approximate math only to choose a safe starting point. | It removes slow software math calls from the innermost loop while preserving exact pixels. | **Core** because of the inverse-square-root anecdote. |
| Geometry preparation | Calculate the small line pieces that make up a curve once, then reuse them. | Earlier code recreated the same curve several times for counting, bounds, and painting. | **Supporting.** |
| Row traversal | Handle all the overlapping pieces of one stroke together while moving down the image row by row. | Shared rows and joins are visited once rather than once per little curve piece. | **Supporting.** |
| Cold versus warm callers | Rebuilding an old drawing and adding a fresh live stroke have different patterns, so they use different search routines. | A compromise routine was slower for both jobs. | **Supporting.** It illustrates measurement defeating code-sharing neatness. |
| Producer surface shape | Render four neighboring tiles together in one temporary work area. | Four was the measured balance between sharing setup, finishing covered areas early, and fitting in fast memory. | **Optional.** |
| Memory placement | Put the temporary pixels used constantly by the renderer in scarce fast on-chip memory. | Once the algorithm improved, external-memory traffic became noticeable. | **Supporting.** Contrast it with the earlier failed “move everything to fast memory” theory. |
| Code placement | Put the hottest instructions themselves in fast on-chip memory. | The processor no longer waited on flash cache, and unrelated code placement stopped changing the result as much. | **Core late-stage win.** |
| Publication | Copy each finished tile directly into its final cache slot and look up that slot in constant time. | It removes an intermediate copy and repeated searches after the expensive drawing work is done. | **Optional.** The blog can call this “removing copying and bookkeeping.” |
| Work slicing | Let background rendering do a meaningful amount of real work, then yield before touch feels blocked. | Tiny slices wasted time on scheduling; oversized slices froze input. Measured useful work found the middle. | **Supporting.** Important if discussing responsiveness as well as total speed. |
| Producer discovery | Search once for the next missing patch and reuse the answer. | The old code searched the same directory twice before starting work. | **Optional final cleanup.** |
| Spatial candidate index | Keep a rough map of which world areas each stroke can touch, but use it only when it removes enough candidates to pay for itself. | It helps sparse drawings. Dense crossings still require nearly every stroke, so the fallback avoids extra indirection there. | **Supporting caution.** It is useful mainly because the hoped-for magic index failed on the hardest corpus. |
| Cache retention | Update or prepare remembered zoom areas while the user is idle and keep more detailed tiles in memory. | Revisiting an area no longer triggers another visible rebuild. | **Core to the felt experience**, though it changes frequency rather than speed of one cold render. |
| Background architecture | Record the stroke immediately, show it as a temporary overlay, and let cached pixels catch up in interruptible background steps. | Drawing no longer waits for every cached view to be rebuilt. | **Core system idea**, especially if the article joins cold rendering to ink latency. |
| Memory budget for retention | Measure how much export memory is truly needed, then spend the remainder on more cached drawing tiles. | More remembered detail means fewer cold renders. | **Supporting human/engineering story.** |
| Late dense/pathological work | Turn Alice’s dense crossing hairlines into a permanent test and add exact skips tailored to what they exposed. | Friendly drawings no longer defined “fast.” | **Core closure.** |
| Settled AA | After the hard-edged image appears, refine edge transparency in the background and skip areas already visually final. | It improves final quality without putting all anti-aliasing work on the immediate cold path. | **Optional in the cold narrative.** Include only if discussing the misleading ~40% memory. |
| Measurement discipline | Freeze the test drawing, test every zoom on the device, compare the same code revision, and record both total time and longest blocking step. | It prevents corpus changes, Mac-only wins, and omitted red cases from manufacturing progress. | **Core theme.** |

**PLAIN ENGLISH:** Most surviving techniques fall into four ideas: reject strokes that cannot matter, stop once newer paint has settled the answer, arrange memory and math for the ESP32, and measure the result on a difficult fixed drawing.  
**WHY IT HELPED:** The renderer became fast by removing repeated work at many levels. Attempts to process larger chunks or add clever summaries often exposed more work than they saved.  
**NARRATIVE IMPORTANCE:** **Core summary.** The full table is research backup. A blog explanation can organize the campaign around those four ideas.

### Memory joggers

- Waking to “good news, tearing basically gone; bad news, cold rendering regressed,” then watching a dense 400% rebuild crawl for ten-plus seconds.
- Saying “This is absolutely not” when the remembered subsecond renderer had turned into a slideshow.
- Making the first manual document “so fucking evil,” then immediately asking for the official test to be two to four times worse.
- The 23.66-second instrumented baseline making the subjective ten seconds look optimistic.
- Giving Fable the phrase “mechanical sympathy and elegance, demoscene mindset,” leaving it alone, and returning to a roughly 53% adversarial win that same afternoon.
- Being delighted by the percentage, then immediately asking why other zooms had barely changed.
- Getting bored with performance, knowingly switching to UI work, and recording the regressions so the project could return to them.
- Learning that four innocent float operations per row were hidden Xtensa library calls; the 1990s-looking inverse-square-root bit trick was only allowed because exact pixel coverage stayed in charge.
- Accepting 507 ms with “it’s a few ms over 500 but I’ll take it,” then discovering the missing 50% overlap red row and being furious that hours of cold work had omitted it.
- Drawing evil hairlines whenever a result looked too neat; by release, that personal sabotage gesture had become a permanent corpus.
- A late 8.7% median IRAM win feeling enormous because unrelated layout alone could move the build 2–3% and the remaining margin was tiny.

**PLAIN ENGLISH:** These are emotional and physical prompts, not claims that all belong in the final post.  
**NARRATIVE IMPORTANCE:** Pick two to four that restore the scene: the ten-second rebuild, “so fucking evil,” the missing overlap test, and the late IRAM win form a useful spread.

### Private glossary

| Concept | TinyDraw meaning / why it mattered | Avoid this misunderstanding | Narrative use |
|---|---|---|---|
| Cold render | Rebuilding a view whose refined raster tiles are absent/stale from vector authority. In plain English: TinyDraw recreates detailed pixels from the stored strokes after zoom, pan, or history makes the old pixels unusable. | Not ordinary live ink latency, and not the later edge-smoothing pass unless named. | **Core term.** Define it near the start. |
| Tile | One square patch of the large drawing, cached separately. | A tile is not a stroke and not the whole screen; one stroke can cross many tiles. | **Supporting basic term.** “Cached picture patch” works in prose. |
| p95 / maximum | p95 is the time that 95% of measured runs met or beat; maximum is the slowest recorded run in the stated set. | Do not silently turn a single run, p95, and maximum into the same statistic. | **Core number-reading aid.** Define p95 only if the article publishes one. |
| Authority | Logical operations/samples that define the drawing. In plain English: the strokes are the master copy; screen pixels can be thrown away and rebuilt. | A cached tile being ready is not completion if the display has not physically finished. | **Core concept.** “Master copy” is clearer than “authority” in blog prose. |
| Materialization | Turning authority into overview/raw/settled pixels for a particular world tile and revision. In plain English: build one cached picture patch from the master strokes. | It is not necessarily synchronous with committing a stroke in the final architecture. | **Supporting.** Prefer “rebuild cached pixels” unless the precise distinction matters. |
| Newest-first saturation | Replay newer paint/eraser first; once a pixel/row/group is final, older operations provably cannot change it. In plain English: stop looking backward once newer paint completely hides the past. | It is exact skipping, not approximate LOD or lossy occlusion. | **Core breakthrough.** |
| Work slice | A bounded unit of background work between touch polls. In plain English: render for a short measured burst, then give touch input another chance to run. | Fewer steps can improve paced wall even if raw compute barely moves. | **Supporting** when explaining responsiveness. |
| PSRAM / internal SRAM | Large slow external memory versus scarce fast on-chip memory. Scratch placement mattered after the algorithm was sane. | “Move it to SRAM” was not a universal cure; the early controlled scratch A/B was only +1.69%. | **Supporting hardware context.** Define only if discussing memory placement. |
| IRAM | Scarce on-chip memory used for instructions, avoiding flash-cache/layout variability in hot code. In plain English: move the hottest code into a smaller, faster place. | Panel-transport IRAM and whole-rasterizer IRAM were separate changes. | **Core late-stage technique.** |
| Conservative rsqrt seed | Fast approximate inverse square root used to start an interval search. In plain English: a cheap estimate says where to begin looking, then exact code verifies the answer. | It never decided pixels; exact `covers_pixel` did. | **Core anecdote**, optional terminology. |
| Evil hairlines | Dense, thin, crossing real/manual torture geometry incorporated into later batteries. In plain English: Alice’s habitual attempt to break a nice result with lots of tiny intersecting strokes. | Do not call the early general/tapered corpus equivalent to the later combined battery. | **Core human/test detail.** |

### Things not to accidentally say

- “An overnight agent cut cold rendering by ~40%.” The large cold wins were daytime Aug 14 (~53–55%) and Aug 16 (47.3%); the later ~40% result was settled-AA saturation.
- “Cold first became serious on Aug 12 because the renderer was under 500 ms.” The early under-500 claim was invalid; the product crisis was Aug 14’s 23.66 s overlap result.
- “One architectural breakthrough solved cold rendering.” The production solution was saturation plus many exact local/mechanical changes, arithmetic choices, memory placement, and later occupancy/preflight work.
- “Fast inverse square root approximated the drawing.” It only seeded a conservative search; exact coverage remained authoritative.
- “Moving scratch from PSRAM to SRAM gave a huge early win.” The controlled Aug 12 move improved total wall 1.69%; an Aug 16 internal producer scratch helped as one part of a different stack.
- “We got every cold case under 500 ms on Aug 16.” General 50–200% passed and 400% was ~507 ms; the omitted overlap-50 case was still red.
- “The 520 ms line replaced the 500 ms target.” It was a temporary development guard for layout variance; release stayed ≤500 ms.
- “The ~40% saturated-destination skip proves general cold improved 40%.” That result belongs to settled AA on a later like-revision corpus.
- “The final 492.793 ms is a 20-run p95.” It is the release battery’s 400% wall; the stronger final-product reset distribution remained open.

**PLAIN ENGLISH:** This list marks the places where a technically true phrase can create a false story by changing the workload, endpoint, date, or mechanism.  
**NARRATIVE IMPORTANCE:** Use it as a fact-check after drafting. It is not material to reproduce in the article.

---

## Episode 2 — panning, tearing, and the Fuji X-T5

### Timeline

#### 1. Aug 14 night — panning becomes the next interaction target

**WHEN:** After the first cold/long-stroke campaign and UI pass.  
**WHAT I SAW / EXPERIENCED:** Warm panning was about 67.3 ms per frame—roughly 15 FPS—and incurred obvious full-frame movement cost even when the drawing was cached.  
**WHAT I THOUGHT / SAID:** Drawing could never lag; panning’s bare minimum should be 30 FPS. Alice also complained about repeated cold redraws while panning: “demoscene mindset!”  
**WHAT THE AGENT THOUGHT:** The frame path had attributable chunks: ~15 ms PSRAM memmove, ~7.5 ms exposed compose, ~8 ms tear wait, ~9 ms staging, ~19.7 ms physical present, plus chrome/minimap. Removing the scroll copy was the clean first lever.  
**WHAT WE TRIED:** Separate reusable canvas pixels from fixed UI and prepare a ring-based pan path.  
**WHAT THE EVIDENCE SHOWED:** There was enough non-wire work to approach 30 FPS if scroll and composition were not serialized.  
**MODEL REPLACED:** “Panning means shift an entire framebuffer” gave way to preserving the frame and changing its logical origin.  
**WHAT HAPPENED NEXT:** A toroidal frame ring landed just after midnight.  
**SOURCE / CONFIDENCE:** [Pan milestone 20](git-history.md#20-toroidal-ring--beam-race-apparently-closes-pan-then-severe-tearing-falsifies-the-model); `PAN_FLOOR_CLOSURE` baseline. **High.**

**PLAIN ENGLISH:** Every pan physically shifted almost the entire cached screen image through slow external memory, then rebuilt newly exposed pixels and added the interface before sending the frame to the display.  
**WHY IT HELPED:** Breaking the 67 ms frame into named costs showed that the display transfer was only part of the problem. The full-image memory shift was removable.  
**NARRATIVE IMPORTANCE:** **Core.** This supplies the slow starting point and the reason the ring buffer existed.

#### 2. Aug 15, 00:05–00:57 — the toroidal ring removes the scroll copy

**WHEN:** First overnight pan implementation.  
**WHAT I SAW / EXPERIENCED:** Cached pan became much faster in software measurements.  
**WHAT I THOUGHT / SAID:** Alice was about to sleep and allowed autonomous work after one last finger-on-glass check. The next morning she initially said the work sounded incredible.  
**WHAT THE AGENT THOUGHT:** Store the frame as a torus; a pan updates an origin and only composes newly exposed strips. De-rotate the ring while performing the mandatory panel byte swap.  
**WHAT WE TRIED:** Toroidal/ring buffer, origin arithmetic, exposed-strip composition, fused de-rotation/staging.  
**WHAT THE EVIDENCE SHOWED:** The ring path reduced a roughly 50.2 ms stage to 34.8 ms before more aggressive presentation work.  
**MODEL REPLACED:** Bulk PSRAM memmove was not intrinsic to pan.  
**WHAT HAPPENED NEXT:** With the CPU work smaller, the agent tried to race the panel’s scanning beam.  
**SOURCE / CONFIDENCE:** [First-appearance table](git-history.md#first-appearances-of-major-ideas); commits beginning `2e07671`. **High.**

**PLAIN ENGLISH:** The cached screen became a wraparound sheet. Panning changed the coordinates of its top-left corner and drew only the newly uncovered edge. When sending it to the display, TinyDraw read across the wrap as though the sheet were continuous.  
**WHY IT HELPED:** It replaced a 294 KiB memory move on every frame with a small coordinate change.  
**NARRATIVE IMPORTANCE:** **Core.** This technique genuinely survived and must be kept separate from the beam-racing idea layered on top of it.

#### 3. Aug 15 early morning — beam racing looks like the answer in software

**WHEN:** Ring-buffer pan optimization before manual glass invalidation.  
**WHAT I SAW / EXPERIENCED:** Automated results looked excellent: about **28.1 ms average**, p95 **32.95 ms**, near the 30 FPS target.  
**WHAT I THOUGHT / SAID:** No decisive positive glass reaction survives before the later test; the build’s measurements suggested closure.  
**WHAT THE AGENT THOUGHT:** Observe a TE edge, estimate the currently scanned row, begin just behind it, wrap in bands, and compose exposed pixels during DMA idle. The writer was assumed unable to catch the beam.  
**WHAT WE TRIED:** A wrapped beam-race presenter with software `tear_synchronized` gates and healing/fallback logic.  
**WHAT THE EVIDENCE SHOWED:** Every software timing/gate condition could be green.  
**MODEL REPLACED:** Temporarily, ordered frame presentation was replaced by a modeled writer-versus-beam race.  
**WHAT HAPPENED NEXT:** Alice touched the product and immediately saw what the gate could not.  
**SOURCE / CONFIDENCE:** [Performance ledger row 15](docs-performance.md); `PAN_FLOOR_CLOSURE` preserved with falsification warning. **High measurement confidence; result is explicitly do-not-use as a success.**

**PLAIN ENGLISH:** The display reveals a frame from top to bottom. Beam racing tried to start writing just behind that moving reveal line, continue downward, wrap around, and finish before the display caught the new pixels in an inconsistent state.  
**WHY IT HELPED / FAILED:** It overlapped drawing, copying, and display transfer, producing a software result close to 30 FPS. It failed because the estimated screen position and writer-speed assumptions did not match the physical panel.  
**NARRATIVE IMPORTANCE:** **Core failed idea.** Readers need enough of this model to understand why the green numbers were seductive and why the later physical tear mattered.

#### 4. Aug 15, 09:22–09:46 — the glass says the green oracle is wrong

**WHEN:** Morning manual test of the autonomous build.  
**WHAT I SAW / EXPERIENCED:** Severe tearing, white/UI stripes and corruption, blur/sharpen, and visible rerenders. At both 100% and 400%, software continued to report zero synchronization failures.  
**WHAT I THOUGHT / SAID:** “Tearing is back… it’s tearing badly.” Later, simply: “my god, it tears.”  
**WHAT THE AGENT THOUGHT:** The gate only proved that its own timing model completed. It knew that an edge occurred; it did not know which row was visible or when writes became visible.  
**WHAT WE TRIED:** Correctness/review fixes and several overlay staging designs: row splits, PSRAM scratch, internal scratch, ring backups, and serialized overlay prep.  
**WHAT THE EVIDENCE SHOWED:** Beam-race correctness was physically falsified. The alternative designs measured roughly 36–69 ms or overflowed; the more conservative build regressed p95 to ~47.5–48.6 ms and could still tear.  
**MODEL FALSIFIED:** `tear_synchronized == true` was not a tear oracle. Average frame timing could not prove the writer never crossed the visible scan.  
**WHAT HAPPENED NEXT:** The project paused speculative presenter work and characterized the panel itself.  
**SOURCE / CONFIDENCE:** [Fuji session prelude](session-history.md#why-software-evidence-was-insufficient); messages `5c584fc6`, `5443f4b1`; [pan experiments](../docs/receipts/vector-v2/PAN_DESIGN_EXPERIMENTS_2026_08_15.md). **High.**

**PLAIN ENGLISH:** The software declared success because it had seen the expected electrical pulse and completed its own schedule. It could not see the pixels on the panel. Alice could, and they were visibly split and corrupted.  
**WHY IT HELPED:** The failure changed the definition of evidence. Timers could diagnose the pipeline, but only the glass or a camera could certify the visible result.  
**NARRATIVE IMPORTANCE:** **Core.** This is the central conflict of the tearing episode: the program validated its own wrong model.

#### 5. Aug 15, 19:20 onward — stop optimizing folklore; measure the panel

**WHEN:** Scientific reset after an external review and an earlier unclassified 120 fps clip.  
**WHAT I SAW / EXPERIENCED:** Alice had watched agents infer “about 20 FPS” from a poor handheld recording and felt the project was flailing.  
**WHAT I THOUGHT / SAID:** “I want to finish it and work scientifically instead of flailing.” Start with hardware limits because the app had been optimized without knowing them.  
**WHAT THE AGENT THOUGHT:** Camera should answer only optical correctness; serial timestamps should answer cadence. First measure TE, bus, staging, and readback.  
**WHAT WE TRIED:** Minimal panel probe, one variable at a time, without the document engine or overlays.  
**WHAT THE EVIDENCE SHOWED:** TE period **16.773 ms / 59.62 Hz**, high **578 µs**, ISR→task p50 **9 µs**, zero timeouts. Requested 40/50/60 MHz were all actually **40 MHz**. Full payload was 16.487 ms and best measured full-frame wall **17.998 ms**, for a **29.4 FPS** rising-edge full-frame cadence. Writer ~27.2 rows/ms and modeled beam ~26.7 were near parity.  
**MODEL FALSIFIED:** The earlier 60 MHz / ~11 ms wire floor and 1.53× writer advantage did not exist. TE jitter was not the explanation.  
**WHAT HAPPENED NEXT:** The agent tried to obtain the one missing fact—the actual scan position—from the controller.  
**SOURCE / CONFIDENCE:** [Panel limits receipt](../docs/receipts/hardware/CO5300_PANEL_LIMITS_2026-08-15.md); messages `6b70e446`, `472f0a14`. **High.**

**PLAIN ENGLISH:** The panel refreshed about 60 times per second, but a full TinyDraw frame took about 18 ms to transmit, longer than one 16.8 ms refresh period. Settings labelled 50 and 60 MHz were not faster at all; the hardware divider kept the bus at 40 MHz. The write speed and the screen’s scan speed were almost equal.  
**WHY IT HELPED:** These measurements destroyed the comfortable assumption that TinyDraw could easily outrun the display. They also established a real maximum of about 29.4 complete frames per second for the safe full-screen pattern.  
**NARRATIVE IMPORTANCE:** **Core.** The exact pulse width and interrupt latency are optional; “the supposed 60 MHz mode was actually 40, and writer and screen were neck-and-neck” carries the story.

#### 6. Aug 15 evening — display readback fails, making the camera necessary

**WHEN:** Panel characterization, immediately before the Fuji protocol.  
**WHAT I SAW / EXPERIENCED:** Nothing visible changed; this was the frustrating instrumentation dead end.  
**WHAT I THOUGHT / SAID:** Alice was willing to repeat handheld video if there was no better way.  
**WHAT THE AGENT THOUGHT:** CO5300 `GETSCANLINE` (`0x45`) might turn modeled beam position into a software measurement. Known-nonzero control registers would validate the read path.  
**WHAT WE TRIED:** GETSCANLINE plus six control-register reads.  
**WHAT THE EVIDENCE SHOWED:** Every read returned zero, including registers that could not truthfully be zero. The QSPI read path was broken or unsupported; no usable software scanline oracle existed. Software could timestamp its writes and TE, but not know what row the glass was displaying or the controller’s write-to-visible semantics.  
**MODEL FALSIFIED:** Register readback would not rescue software-only tear certification.  
**WHAT HAPPENED NEXT:** An external optical instrument became mandatory, not decorative.  
**SOURCE / CONFIDENCE:** [Session archaeology, software characterization](session-history.md#why-software-evidence-was-insufficient); [performance facts](docs-performance.md#hardware-timing-and-memory-forensics). **High.**

**PLAIN ENGLISH:** The display controller appeared to offer a command that should say which row it was currently showing. TinyDraw tried that command and several ordinary register reads. Every answer was zero, including answers known to be nonzero, so the read connection itself could not be trusted.  
**WHY IT HELPED:** This dead end proved there was no software shortcut to the missing physical fact. The camera was now the simplest instrument that could observe the screen independently.  
**NARRATIVE IMPORTANCE:** **Core setup for the Fuji.** The command name `GETSCANLINE` is supporting detail; the all-zero controls make the failure easy to explain.

#### 7. Aug 15, 19:41–20:53 — “actually, I have a Fuji X-T5”

**WHEN:** Designing the optical test after the readback failure.  
**WHAT I SAW / EXPERIENCED:** A tiny 368×448 device, no tripod, a mid-telephoto lens with a ~50 cm minimum focus distance, and the prospect of holding a camera through a multi-cell loop.  
**WHAT I THOUGHT / SAID:** At 19:41:57: “actually i just realized i have a fuji xt5… i think it can do 240fps at 1080p… no tripod.” Later: “god i hope all this hassle is worth it.” On delivery: “this is over 2 minutes fingers crossed it has what you need do your thing.”  
**WHAT THE AGENT THOUGHT:** At 240 fps, one 16.8 ms panel period spans about four camera frames. A persistent panel tear and a one-frame camera/scanout straddle could therefore be distinguished. Large colored fields and fiducials would survive handheld drift.  
**WHAT WE TRIED:** X-T5 1080p/240 high-speed; Sigma 56 mm f/1.4 (about 85 mm equivalent); handheld at f/4; manual focus; fixed/daylight white balance; dim room; flicker-free **1/1024** shutter; roughly 50 cm; all fiducials visible.  
**WHAT THE EVIDENCE SHOWED:** `$HOME/Desktop/DSCF0665.MOV` contained 37,920 frames, about 158 s at 240 fps.  
**MODEL REPLACED:** Aesthetic video became a calibrated pass/fail instrument.  
**WHAT HAPPENED NEXT:** The test was preregistered so a clean-looking case could not be accepted unless the setup also detected a known tear.  
**SOURCE / CONFIDENCE:** [Exact Fuji reconstruction](session-history.md#users-camera-offer-and-protocol); messages `59d9de80`, `fd95a1b5`, `426a5808`, `e096894c`, `cbea275b`. **High.** The remembered device-propped-against-laptop and laptop-brightness detail is **personal recollection only**; it is not stated in the surviving session. The transcript does support handheld/no tripod and a dimmed room.

**PLAIN ENGLISH:** The Fuji recorded four camera frames during one screen refresh. That was fast enough to tell a split that remained stuck on the display from the normal moving boundary a camera sees while the screen changes from one frame to the next.  
**WHY IT HELPED:** The footage supplied information the software and broken register reads could not: what the pixels physically did over time.  
**NARRATIVE IMPORTANCE:** **Core human scene.** Camera model, no tripod, 240 fps, fast shutter, and “I hope this is worth it” matter. Lens specifications are optional.

#### 8. Aug 15 — the positive control makes the camera result scientific

**WHEN:** Before filming the one-take cell sequence.  
**WHAT I SAW / EXPERIENCED:** A looping panel probe alternated large red/green fields, cell IDs, corner markers, guard columns, and blue interstitials.  
**WHAT I THOUGHT / SAID:** Alice wanted one large video file because handheld f/4 was the best practical setup.  
**WHAT THE AGENT THOUGHT:** A clean result was meaningless if blur/exposure/framing made the instrument unable to reveal tearing. A deliberately free-running unsynchronized cell therefore had to tear.  
**WHAT WE TRIED:** Preregistered order **4 → 1 → 5 → 2 → 3**. Cell 4: unsynchronized positive control, must tear. Cell 1: TE-rising, row-zero, full 448-row sweep at measured 29.4 FPS, must remain optically clean. Camera judged tears/anomalies; serial logs judged timing.  
**WHAT THE EVIDENCE SHOWED:** The experiment had a red-capable control and a declared interpretation before the footage existed.  
**MODEL REPLACED:** “No tear seen” became “no tear seen by an instrument that demonstrably sees the positive control tear.”  
**WHAT HAPPENED NEXT:** The clip arrived; an ambitious automated classifier then became its own detour.  
**SOURCE / CONFIDENCE:** [Block B protocol](../benchmark-results/blockB-optical/PROTOCOL.md); [session reconstruction](session-history.md#users-camera-offer-and-protocol). **High.**

**PLAIN ENGLISH:** The video began with a deliberately bad display mode that should tear. If the camera failed to show that known tear, any later clean result would be worthless. Only after the bad case visibly failed could the clean-looking case count.  
**WHY IT HELPED:** It proved the camera setup was sensitive enough to detect the defect rather than merely too blurry, badly exposed, or poorly framed to see it.  
**NARRATIVE IMPORTANCE:** **Core scientific idea.** This is what turns an improvised handheld video into a real experiment.

#### 9. Aug 15, about 20:53–21:16 — the classifier flails; contact sheets answer the question

**WHEN:** Immediately after `DSCF0665.MOV` was supplied.  
**WHAT I SAW / EXPERIENCED:** Roughly forty minutes disappeared into frame registration, rotation metadata, ID-strip location, and a frame-counter bug.  
**WHAT I THOUGHT / SAID:** “We’ve been at this for… over an hour?” Then the sharper time audit: started around 6:30, agents had consumed hours, now it was 10:15.  
**WHAT THE AGENT THOUGHT:** Full automation would produce a durable classifier, but the gross physical discriminator did not require it.  
**WHAT WE TRIED:** Fell back to two 24-frame contact sheets, then ran a focused automated slice for the accepted cell.  
**WHAT THE EVIDENCE SHOWED:** Cell 4 showed a red/green sandwich in every frame with a wandering split—the instrument could see a tear. Cell 1 showed a single boundary moving monotonically down over roughly four camera frames, then a solid field: normal camera sampling of a clean scan, with no frozen split, double boundary, or notch. The focused **1,495-frame cell-1 analysis found 0 tears and 0 anomalies**.  
**MODEL FALSIFIED:** The expensive full-video classifier was not necessary to settle the product decision. It also reinforced that camera straddle and persistent panel tear have different temporal signatures.  
**WHAT HAPPENED NEXT:** The simple rising-edge row-zero sweep replaced beam racing as the product correctness mechanism.  
**SOURCE / CONFIDENCE:** [Capture observations](session-history.md#capture-and-classifier-observations); [protocol result](../benchmark-results/blockB-optical/PROTOCOL.md#append-only-results). **High for cells 4 and 1. Cells 2/3/5 were not fully classified.**

**PLAIN ENGLISH:** In the deliberately bad mode, every camera frame contained old red pixels and new green pixels at once, with the split jumping around. In the candidate good mode, one boundary travelled steadily down the screen over about four camera frames and then vanished. That is what a camera should see when it catches a clean top-to-bottom refresh in progress.  
**WHY IT HELPED:** The positive control proved “tear” was visible; the 1,495-frame candidate analysis found none. Contact sheets answered the main question even though the grand automated classifier had bogged down.  
**NARRATIVE IMPORTANCE:** **Core climax.** The red/green sandwich and moving clean boundary are the most concrete way to explain what the camera discovered.

#### 10. Aug 15, about 21:16–22:32 — boundary sweep is clean, but product composition is slow

**WHEN:** Immediate implementation after the optical verdict.  
**WHAT I SAW / EXPERIENCED:** The product stopped obviously tearing, but a stroke-distortion issue near the zoom controls remained and panning felt slower than the minimal probe.  
**WHAT I THOUGHT / SAID:** “Other than that i do think that tearing is fixed,” followed by concern that the product needed at least 24 FPS.  
**WHAT THE AGENT THOUGHT:** The minimal policy was enough: wait for TE rising, start at row zero, stream top-to-bottom. The product’s app/chrome composition—not the clean panel sequence—was now the pace bottleneck.  
**WHAT WE TRIED:** Flash the boundary-top/rising/actual-40-MHz product path; retain the toroidal ring; demote beam race.  
**WHAT THE EVIDENCE SHOWED:** Optical correctness was clean, but product PANSEQ was only about **19.9 FPS**, versus the minimal full-frame ceiling of 29.4 FPS.  
**MODEL FALSIFIED OR REPLACED:** Beam race/wrap/heal was unnecessary for correctness. The footage did **not** need cell 3 to prove this: glass had already falsified beam race, while cell 1 proved the simpler alternative clean.  
**WHAT HAPPENED NEXT:** A staging compositor attempted to recover the missing composition time without changing the accepted scan order.  
**SOURCE / CONFIDENCE:** [Hypothesis and follow-on](session-history.md#hypothesis-falsified-and-model-that-followed); message `6d934d90`. **High.**

**PLAIN ENGLISH:** The successful rule was simple: wait for the screen’s frame-start pulse, begin writing at the top, and continue downward in the same order the screen refreshes. No guessed mid-screen starting point and no wraparound race were needed.  
**WHY IT HELPED:** It gave the display one predictable stream that the camera had shown to be clean. The remaining 20 FPS problem came from preparing the full TinyDraw interface, not from the safe transfer rule itself.  
**NARRATIVE IMPORTANCE:** **Core resolution.** This is the mechanism the article must name as the replacement for beam racing.

#### 11. Aug 15, 22:59–23:06 — the tear that stayed in one UI detail, then moved

**WHEN:** Wave 2 compositor, after fast staging recovered roughly 29.5 FPS.  
**WHAT I SAW / EXPERIENCED:** Panning was fast again, but a tear sat at one precise place near the **top grey edge of the minus button**. Reverting burst pacing made the app slower and moved the tear to roughly two-thirds down the minimap instead of removing it.  
**WHAT I THOUGHT / SAID:** “Panning is fast now, but it’s tearing… one exact spot.”  
**WHAT THE AGENT THOUGHT:** If the defect were only burst timing, reverting the burst should remove it. A row that moved with composition ordering implicated deterministic per-strip staging cost, particularly the dynamic minimap patch.  
**WHAT WE TRIED:** Changed burst/overlap behavior while keeping the physical observation fixed. Then instrumented every strip and prebuilt/cached expensive chrome/minimap work.  
**WHAT THE EVIDENCE SHOWED:** The moving fixed tear was a writer/beam crossover marker. Average staging time was irrelevant if one strip took longer than its own wire budget.  
**MODEL FALSIFIED:** “Burst pacing alone causes the tear.” Replaced with a per-strip invariant: each strip’s pixels must be ready before the wire reaches it.  
**WHAT HAPPENED NEXT:** A Sol high handoff enforced staging-before-wire for all 432 measured strips and rejected later pacing experiments that broke the invariant.  
**SOURCE / CONFIDENCE:** Messages `1ff48c60`, `b726fd7e`, `fc08b05f`; [glass observations](../benchmark-results/wave2-compositor/GLASS_OBSERVATIONS.md). **High.**

**PLAIN ENGLISH:** TinyDraw sends the screen in horizontal strips. One strip containing expensive interface work took too long to prepare, so the display caught up at that exact row and showed a tear. Changing the schedule moved the slow strip and moved the tear with it.  
**WHY IT HELPED:** The moving defect identified the real rule: every strip must be completely prepared before its own turn on the wire, regardless of the frame’s average speed.  
**NARRATIVE IMPORTANCE:** **Core post-camera lesson.** The minus-button edge and minimap are memorable physical clues; “per-strip deadline” is the only technical phrase worth retaining.

#### 12. Aug 15 23:41 → Aug 16 morning — clean glass, then near-30 FPS with cached chrome

**WHEN:** Per-strip closure and next-morning cache-lifetime split.  
**WHAT I SAW / EXPERIENCED:** The conservative invariant build was visually clean but initially only ~20 FPS. After chrome/minimap cache work, aggressive pan remained clean at 50/100/200/400%.  
**WHAT I THOUGHT / SAID:** At 23:41: “yes tearing seems to be gone,” followed by a request that the agent stop trying to send her to sleep. On Aug 16: “as far as I can tell… tearing is fixed.”  
**WHAT THE AGENT THOUGHT:** Keep the ring canvas-pure; stage exposed canvas before the sweep; cache toolbar/battery/zoom/minimap base independently; redraw only transient minimap viewport lines; fuse ring de-rotation and byte swap in the bounded strip blit.  
**WHAT WE TRIED:** Proved every strip inside its wire budget, then split chrome cache identities so camera motion did not rebuild stable UI.  
**WHAT THE EVIDENCE SHOWED:** Accepted historical PANSEQ p95 became about **33.94 ms** at 100% and 400%—roughly **29.5 FPS**—with owner glass clean at every zoom. A final same-tree release pan distribution was not retained, so 33.94 ms is a historical accepted value, not a final-release statistic.  
**MODEL REPLACED:** Tear safety became ordered boundary synchronization plus a local per-strip readiness proof, closed optically.  
**WHAT HAPPENED NEXT:** Panning left the critical path; work returned to cold rendering, ink, AA, retention, and product features.  
**SOURCE / CONFIDENCE:** [Pan milestone 21](git-history.md#21-panel-characterization-and-camera-evidence-replace-folklore-with-physics); [ledger row 17](docs-performance.md); messages `bb799818` and Aug 16 optical confirmation. **High for historical acceptance; final current-tree distribution remains unmeasured.**

**PLAIN ENGLISH:** TinyDraw kept the cached drawing free of buttons and other interface pixels. It prepared the exposed drawing area before transmission, reused prebuilt interface images, added them while copying each strip, and combined the ring-buffer unwrapping with a byte conversion the display already required.  
**WHY IT HELPED:** Each strip became cheap and predictable enough to meet its local deadline. Reusing stable interface pieces then recovered nearly 30 FPS without returning to beam racing.  
**NARRATIVE IMPORTANCE:** **Core closure.** The blog needs the rule and the clean near-30 FPS result. The cache identities and byte-order fusion are optional implementation detail.

### What survived versus what taught us and was discarded

| Survived into the released architecture | Instrumental or rejected |
|---|---|
| Toroidal/canvas ring and logical origin | Beam-race/wrapped-band presenter |
| TE-rising, row-zero, top-to-bottom ordered sweep | Software `tear_synchronized` as a physical oracle |
| Actual 40 MHz transport model | Requested 50/60 MHz assumptions and ~11 ms full-frame floor |
| Canvas-pure ring; fixed chrome added during staging | Mutating overlays into the ring plus backup/restore schemes |
| Cached toolbar/zoom/minimap base and transient viewport patch | Several row-split, PSRAM/internal scratch, and ring-backup designs |
| Fused ring de-rotation + byte swap | Overlapping staging/burst pacing when it violated strip headroom |
| Every strip stages before its own wire deadline | Average frame time as a tear-safety proof |
| Positive-control optical protocol | GETSCANLINE/readback as an oracle—the path returned all zeros |

#### Plain-English map for the panning technique table

| Technique | What it means in ordinary English | Why it helped or failed | Narrative importance |
|---|---|---|---|
| Toroidal canvas ring | Treat the cached drawing like a sheet whose opposite edges join. Move the logical window instead of shifting all pixels. | It removed the large memory copy on every pan. | **Core survivor.** |
| Ordered boundary sweep | Wait for the display to begin a new refresh, then write from the top downward in the same direction. | The Fuji showed this simple order was physically clean. | **Core final mechanism.** |
| Actual 40 MHz model | Use the speed the hardware truly delivered, not the speed requested in software. | It revealed that the writer barely matched the screen rather than easily outrunning it. | **Core correction.** |
| Canvas-pure ring and staged chrome | Keep the cached drawing free of buttons and minimap pixels; add stable interface images while preparing display strips. | It stopped UI ghosts and avoided repeatedly modifying and restoring the cached drawing. | **Supporting survivor.** |
| Per-strip staging deadline | Finish preparing each horizontal strip before the display needs that strip. | It prevented one expensive row of interface work from causing a fixed tear even when averages looked good. | **Core final invariant.** |
| Beam racing | Guess where the display is currently scanning and write just behind it, wrapping around at the bottom. | It looked fast in software but tore on the physical screen because the guess and timing model were wrong. | **Core discarded idea.** |
| Software synchronization flag | Record that software saw the expected pulse and completed its planned sequence. | It verified only the program’s model, not the pixels; glass visibly contradicted it. | **Core measurement failure.** |
| GETSCANLINE/readback | Ask the controller which row it is showing. | Every register returned zero, so the read path supplied no trustworthy information. | **Core reason for the camera.** |
| Positive control | Film a deliberately tearing mode before accepting a clean-looking mode. | It proved the camera could reveal the defect. | **Core scientific method.** |
| Rejected overlay designs | Copy interface regions into and out of the cached drawing, split rows into transactions, or use extra temporary buffers. | They remained slow, overflowed memory, or complicated ordering without proving physical correctness. | **Optional detail.** Their existence explains why the final canvas-pure design was earned. |
| Rejected overlap/burst pacing | Prepare or transmit work during the waiting period to recover speed. | The fast version created a fixed tear; a bounded retry added risk without improving p95. | **Supporting failed experiment.** |

**PLAIN ENGLISH:** The final system combined one lasting speed trick, the wraparound ring, with one lasting correctness rule, write from the top in order and have every strip ready on time.  
**WHY IT HELPED:** The ring removed unnecessary memory work. The ordered sweep and strip deadlines matched what the physical display could safely consume.  
**NARRATIVE IMPORTANCE:** **Core summary.** Beam racing belongs in the story as the convincing wrong answer, not as part of the solution.

### Memory joggers

- Waking to a giant autonomous pan improvement, touching the device, and saying first “tearing is back” and then “my god, it tears” while the logs stayed green.
- The ugly white stripes/overlay corruption and the brief blur-then-sharpen making it obvious that a software pass bit was arguing with your eyes.
- Being annoyed that an earlier handheld video had been turned into a dubious “20 FPS” claim and insisting that the next round separate optical correctness from cadence.
- Realizing requested 60 MHz had never been 60 MHz; all three settings were the same 40 MHz hardware divider result.
- The complete absurdity of `GETSCANLINE` and every known-nonzero control register returning zero, leaving no software way to ask the panel where its beam was.
- “Actually i just realized i have a Fuji X-T5,” followed by discovering 1080p240, then the 1/1024 flicker-free setting.
- No tripod, f/4, manual focus, roughly 50 cm away, holding the camera through a looping two-minute test and thinking “god i hope all this hassle is worth it.”
- Your remembered physical scene—tiny device against/propped by the laptop and the laptop display turned down—is plausible and useful personal memory, but not recorded in the transcript. The recorded protocol says dim room.
- Watching the deliberately bad cell produce a torn red/green sandwich in every frame: proof the camera could say “tear.”
- Watching the accepted cell’s boundary travel down normally across about four 240 fps frames, then disappear; the weird-looking motion was evidence of a clean scan, not a persistent split.
- Losing roughly forty minutes to a classifier before two contact sheets answered the gross question.
- The tiny grey edge above the minus button becoming a physical oscilloscope trace: change pacing, and the tear moves two-thirds down the minimap.
- “Yes tearing seems to be gone,” immediately followed by telling the agent to stop trying to put you to sleep.

**PLAIN ENGLISH:** These prompts recover the physical comedy and frustration around an otherwise abstract display-timing problem.  
**NARRATIVE IMPORTANCE:** The strongest sequence is: green software plus visible tear, broken readback, handheld Fuji, positive control tears, accepted case stays clean, tiny minus-button tear moves, final clean verdict.

### Private glossary

| Concept | TinyDraw meaning / why it mattered | Avoid this misunderstanding | Narrative use |
|---|---|---|---|
| FPS | Complete screen updates per second. Around 15 FPS felt slow; the accepted historical path reached about 29.5 FPS. | Camera recording at 240 FPS did not mean the display refreshed at 240 FPS. | **Core performance term.** |
| TE (tearing effect) signal | Panel pulse marking a frame boundary or phase. In plain English: an electrical “a new screen refresh is starting” cue. Measured at 16.773 ms. | Seeing an edge does not reveal the visible row or guarantee a tear-free write. | **Core**, but call it the panel’s frame-start signal after defining TE once. |
| Toroidal ring | Cached canvas stored with a wraparound origin. In plain English: move the viewing coordinates and redraw the exposed edge instead of shifting the whole image. | The ring survived; the beam-race policy layered on it did not. | **Core survivor.** |
| Beam racing | Starting and wrapping writes around a guessed current scan position to stay behind the display. | It produced attractive software numbers and severe physical tearing; it did not ship. | **Core failed idea.** |
| Boundary sweep | Wait for TE rising, start at row zero, stream monotonically top-to-bottom. In plain English: begin at frame start and write in the screen’s own order. | It was optically clean at a 29.4 FPS full-frame cadence, not 60 FPS. | **Core final mechanism.** |
| Presentation staging | Copy, unwrap, and convert cached drawing pixels into small fast buffers; add the interface before sending each strip. | A good average was insufficient; every individual strip needed headroom. | **Supporting.** “Prepare each strip” is usually enough. |
| Per-strip invariant | Each strip must finish preparation before its own transmission deadline. | It is a local correctness proof, not merely a performance target. | **Core final rule.** |
| Positive control | Deliberately unsynchronized mode that must visibly tear. | A clean test is invalid if the setup cannot show this control tearing. | **Core scientific idea.** |
| Camera straddle | One exposure catches a normal display refresh partway through, so the red/green boundary moves in successive high-speed frames. | A steadily moving boundary is not the same as a persistent panel tear. | **Supporting**, useful when explaining the footage. |
| GETSCANLINE | Controller command intended to report the current scan row. | TinyDraw’s QSPI read path returned zero for it and all controls; it supplied no evidence. | **Core reason for using the Fuji.** |

### Things not to accidentally say

- “Beam racing fixed tearing.” It was software-green and physically falsified; the ring survived, the beam race did not.
- “The Fuji directly proved the beam-race cell tore across the entire clip.” Cell 4 proved the instrument red-capable; cell 1 proved the simple boundary sweep clean. Cells 2/3/5 were not fully classified, and glass had already falsified beam race.
- “All 37,920 frames were classified.” The decisive automated analysis was 1,495 cell-1 frames, plus contact-sheet inspection and the positive control.
- “The camera measured product FPS.” Camera judged optical correctness; serial timing measured cadence. Inferring FPS from the earlier footage was part of the original error.
- “TE synchronization means no tearing.” Software observed an edge; it could not observe scan position or write-to-visible behavior.
- “The bus ran at 60 MHz and the writer easily outran the beam.” It ran at 40 MHz for every requested setting; writer and modeled beam were near parity.
- “GETSCANLINE returned row zero.” Every control read returned zero, showing the read path was unusable—not that the scan was at row zero.
- “The first camera-clean build was already 30 FPS in the product.” The minimal sequence was 29.4 FPS; the first clean product composition was ~19.9 FPS.
- “Burst pacing caused the fixed tear.” Reverting it moved the tear; expensive per-strip chrome/minimap staging was the deeper discriminator.
- “Final release panning is exactly 33.94 ms.” That is the accepted historical clean gate; a same-tree final-release distribution was not retained.
- “The transcript proves the device was against the laptop and the laptop brightness was lowered.” Those are Alice’s current physical memories; the surviving transcript confirms handheld/no tripod and a dim room, not those exact details.

**PLAIN ENGLISH:** Most risks here come from assigning the right observation to the wrong mechanism. The ring sped up panning; beam racing tore; the camera validated the ordered sweep; strip deadlines fixed the later UI-related tear.  
**NARRATIVE IMPORTANCE:** Use this as the final causal check after writing. The article must never imply that beam racing shipped or that the whole video was automatically classified.

## Source hierarchy used here

Highest weight: same-revision hardware receipts, preregistered optical protocol, direct contemporary user messages, and physical glass verdicts. Next: checked git history and durable handoffs. Lower weight: later summaries where raw footage or a final reset distribution no longer survives. The main archaeology entry points are [README](README.md), [git history](git-history.md), [performance/document ledger](docs-performance.md), [session history](session-history.md), and [supplement](supplement.md).

**PLAIN ENGLISH:** Trust measurements made on the exact code and device first, then what Alice said and saw at the time. Treat later summaries as memory aids when the original artifact is gone.  
**WHY IT HELPED:** This ordering resolves conflicts between a persuasive old write-up and newer physical evidence, especially for beam racing and cold benchmarks.  
**NARRATIVE IMPORTANCE:** **Supporting fact-check method.** It need not appear in the blog.
