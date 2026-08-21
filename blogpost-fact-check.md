# Fact-check of `blogpost-edited.md`

Checked 21 August 2026 against the repository, `.codex-archaeology`, surviving Codex/Pi transcripts, benchmark receipts, current source/tests, and the external primary sources linked below. The draft itself was not edited.

## Verdict key and limits

- **VERIFIED** — the claim is supported by the cited evidence.
- **NOT VERIFIED** — the evidence contradicts the claim as phrased.
- **INCONCLUSIVE** — the surviving record cannot establish it. The entry says what the record does establish.
- **SUBJECTIVE / RHETORICAL** — a judgment, memory, joke, aspiration, or transition rather than a checkable fact.
- **EDITORIAL** — a drafting instruction or placeholder, not publication prose.

“Verified” is scoped to the evidence. Host tests establish software behavior, not physical timing or tear-freedom. Historical device measurements establish the tested build and setup; they are not fresh measurements of the current rewritten Git tree. Personal recollections absent from the transcripts are **INCONCLUSIVE**, not presumed false.

## Material corrections before publication

1. **Nine days** is correct only for Vector V2, from 11–19 August inclusive. The overall TinyDraw project ran 9–19 August (eleven calendar dates). Say “I spent nine days building TinyDraw V2.”
2. **$40** is not established by the record. The board model and 1.8-inch display are verified; retain $40 only from a purchase receipt or explicit personal memory.
3. Arbitrary zoom was an ambition, not the shipped behavior. V2 has five discrete levels: 25%, 50%, 100%, 200%, and 400%.
4. “Cold render” is project shorthand built on ordinary cold/warm-cache language, not a clearly standardized graphics term.
5. The four recorded cold-render gate values cover **50–400%**, not every supported zoom level. At 25%, the complete overview is already the authority/derived overview and has no equivalent tiled cold-fill path.
6. IRAM is internal SRAM mapped for instructions. The 6.93–11.68% experiment moved hot raster code from flash execution/cache into IRAM; it was not a PSRAM-to-IRAM data move.
7. The Quake-style inverse-square-root hack is used only to create a conservative seed bound. Exact per-pixel coverage remains authoritative.
8. Software timestamps measured panning speed. Hands, eyes, and high-speed video were needed to assess feel and optical tearing.
9. The panning story conflates two stages: the clean simple sweep was about 19.9 FPS; compositor work later recovered the historically accepted ~29.5 FPS, after which a fixed tear still had to be eliminated.
10. Undo/Redo preserves raster versions for history states already visited. It does not simply save every tile’s “before stroke” pixels at stroke creation.
11. Settled AA is an idle analytic rerender from vector authority, not a filter applied after cold rendering. Its late arrival did not make that architecture the only possible one.
12. “They wrote/reviewed all the code” is too absolute to prove. The transcripts establish agent-authored substantive implementation and extensive review; Git identity and transcripts cannot prove no private human edit or exhaustive review of every line.
13. GPT-5.6 Pro’s complete-source upload and 60–90 minute review duration are not in the accessible local record. A handoff/review workflow is documented.
14. Puck is a browser/WASM port and deterministic simulation harness, not a clock-accurate hardware emulator. It shares real application code but does not reproduce hardware timing, touch faults, battery/RTC, USB, or physical panel behavior.
15. In the mistakes list, “+7–13% slower on ESP32 than on my M1 Pro” is wrong. Word-mask scanning regressed 7–13% **against the byte-mask implementation on device**; the host and device chose opposite winners.
16. The retrospective optimization inventory is internally inconsistent: its table shows 18 successful and 20 rejected rows, while its summary says 16 successful and 22 rejected. Do not publish either count until the counting rule or inventory is corrected.

## Sentence-by-sentence audit

Line references point to `blogpost-edited.md`.

### Title, hook, and origin (lines 1–25)

| Ref | Claim | Verdict | Evidence / publication-safe version |
|---|---|---|---|
| L1 | “TinyDraw V2” | **VERIFIED** | Repository, firmware targets, design documents, and release materials consistently use this name. |
| L3 | `[VIDEO]` | **EDITORIAL** | Asset placeholder. |
| L7a | “I spent nine days building a vector graphics editor…” | **VERIFIED WITH SCOPE** | Vector V2 begins 11 Aug and releases 19 Aug: nine calendar dates inclusive. The whole project begins 9 Aug. Use “I spent nine days building TinyDraw V2.” Evidence: `.codex-archaeology/README.md`, `.codex-archaeology/git-history.md`. |
| L7b | “…on a $40 microcontroller with a 1.8-inch touchscreen.” | **INCONCLUSIVE / PARTLY VERIFIED** | The Waveshare ESP32-S3 Touch AMOLED 1.8 model and screen size are verified. The accessible record has no purchase price. Waveshare currently lists the product below $40, which does not establish the Amazon price paid. [Official board documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8), [current product page](https://www.waveshare.com/product/esp32-s3-touch-amoled-1.8.htm). |
| L7c | “I knew it was possible.” | **SUBJECTIVE / AUTOBIOGRAPHICAL** | The record supports sustained confidence and persistence, but an internal belief is the author’s recollection. |
| L7d | “I wanted to see how fast I could make it…” | **VERIFIED IN SUBSTANCE** | Repeated optimization prompts, performance contracts, experiment matrices, and measurements establish this aim. |
| L7e | “…and how far I could take the project.” | **SUBJECTIVE / AUTOBIOGRAPHICAL** | Consistent with the development record. |
| L9 | Bracketed placement recommendation | **EDITORIAL** | Remove or resolve before publication. |
| L13a | Following Steve Ruiz on Twitter “for a while” | **INCONCLUSIVE** | Personal social-media history is not in the surviving project record. |
| L13b | Steve “kept posting” microcontroller tiny-device projects | **INCONCLUSIVE** | The transcript does not preserve the posts or timeline review. What is established: the meetup and Steve’s connection to it appear in the author’s contemporaneous messages. |
| L13c | Seeing a meetup, wanting to attend and present | **VERIFIED AS RECOLLECTION** | The contemporaneous timeline records presentation preparation and a user-confirmed meetup/presentation around 10 Aug 19:15–19:20. The exact moment of discovery is not logged. Evidence: `.codex-archaeology/meetup-day-timeline.md`. |
| L15a | “I still didn’t know what to build.” | **INCONCLUSIVE / AUTOBIOGRAPHICAL** | The transcript establishes exploration before settling on TinyDraw, not the author’s exact state of mind. |
| L15b | A mini tldraw seemed obvious; surely it already existed or was Steve’s first project | **SUBJECTIVE / RHETORICAL** | This is the author’s reasoning, not an external fact claim. |
| L15c | “I scrolled his entire timeline and… did not find a mini tldraw” | **INCONCLUSIVE** | No retained browser history or transcript proves an exhaustive timeline search. Safe version: “I searched his timeline and didn’t find one.” |
| L17 | “Okay, I guess we’re doing it.” | **RHETORICAL** | Transition. |
| L19a | Ordered the Waveshare from Amazon | **INCONCLUSIVE / PERSONAL RECOLLECTION** | The board arrived 10 Aug at 08:23 and is identified as the Waveshare ESP32-S3 Touch AMOLED 1.8. Retailer/order details are not retained. Evidence: `.codex-archaeology/meetup-day-timeline.md`. |
| L19b | Asked the coding agent what could be built before arrival | **VERIFIED IN SUBSTANCE** | Pre-hardware work and agent sessions precede the board’s arrival. |
| L19c | “It turns out quite a bit.” | **SUBJECTIVE, SUPPORTED** | V1 implementation, tests, and presentation preparation existed before physical-device work. |
| L19d | Set up a QEMU-emulated ESP32-S3 development environment | **VERIFIED** | Repository history and build material document the QEMU target and pre-hardware development path. |
| L19e | It enabled development on a Mac without hardware | **VERIFIED** | The timing and artifacts establish host/QEMU development before the 10 Aug board arrival. |
| L21 | V1/meetup gap note | **EDITORIAL** | The proposed facts are mostly supportable: V1 was raster and working; the exact audience reaction is personal recollection. |
| L23a | There was no presentation order and Steve pointed at the author first | **INCONCLUSIVE / PERSONAL RECOLLECTION** | The surviving session is quiet during the presentation window. It proves the prepared guide and author-confirmed presentation, not Steve’s exact gesture or words. Evidence: `.codex-archaeology/meetup-day-timeline.md`. |
| L23b | “I presented it” | **VERIFIED AS CONTEMPORANEOUS USER REPORT** | The author later confirms the presentation; timing is reconstructed to roughly 19:15–19:20. |
| L23c | “people liked it” and came to try it afterward | **INCONCLUSIVE / PERSONAL RECOLLECTION** | No audience recording or third-party account survives. What is proved: a working raster V1 and a presentation guide existed before the event. |
| L23d | “that was nice” | **SUBJECTIVE** | Personal reaction. |
| L25a | “V1 was raster” | **VERIFIED** | V1 history and source use raster bitmap authority. Evidence: `.codex-archaeology/git-history.md`. |
| L25b | Raster is easy / far easier on a microcontroller than vector | **SUBJECTIVE GENERALIZATION** | True for this project’s architecture and feature set, but not a universal technical law. Safer: “V1’s raster architecture was much easier for this project.” |

### Commitment, fever dream, and graveyard (lines 27–51)

| Ref | Claim | Verdict | Evidence / publication-safe version |
|---|---|---|---|
| L29a | “The next evening” | **VERIFIED** | The meetup was 10 Aug; the explicit infinite-canvas commitment appears 11 Aug at 19:46:58. Evidence: `.codex-archaeology/meetup-day-timeline.md`. |
| L29b | “We’re making this a real infinite canvas” | **VERIFIED AS QUOTED INTENT** | The commitment is present in the transcript; first vector code follows at 19:52. |
| L31a | First ambition: mini tldraw, vector, arbitrary zoom, fast | **VERIFIED AS AMBITION, NOT OUTCOME** | The initial spec supports the ambition. The shipped design uses discrete power-of-two levels, so avoid implying arbitrary zoom was delivered. Evidence: `docs/archive/2026-08-raster-and-vector-prototypes/V2_INITIAL_SPEC.md`, current zoom controls/tests. |
| L31b | Agents tempered expectations and were right | **SUBJECTIVE, SUPPORTED** | The design record shows scope/performance constraints and discrete zoom choices. |
| L31c | Wanted 25–800%, settled at 400% | **VERIFIED AS AMBITION/OUTCOME** | The transcript records the 25–800% ambition; released UI supports 25–400%. |
| L31d | “It was a big compromise.” | **SUBJECTIVE** | Personal assessment. |
| L33 | V2 gap note | **EDITORIAL** | Supported summary: V2 replaced raster authority with vector strokes and added pan, discrete zoom, tile caching, settled AA, and SVG export. |
| L37a | “next couple of days” in a fever dream | **NOT VERIFIED AS DURATION; SUBJECTIVE DESCRIPTION** | The concentrated post-commit prototype episode ran about 25 h 57 m from 11 Aug evening into 12 Aug night. Use “the next day” or “about 26 hours.” |
| L37b | Nominally testing possibility, never doubting it | **AUTOBIOGRAPHICAL** | The work supports the stated objective; inner certainty is personal recollection. |
| L37c | Needed to find an approach that worked | **VERIFIED IN SUBSTANCE** | Multiple architectures and experiments were tried and rejected before the production design. |
| L39a | “I don’t know what the agents are doing… keep going” | **AUTOBIOGRAPHICAL, TRANSCRIPT-SUPPORTED** | Sessions show the author reporting loss of understanding and later regrouping. |
| L39b | Would do it differently; hindsight is 20/20 | **SUBJECTIVE / IDIOM** | Not falsifiable. |
| L41 | Fever dream was ~26 hours, 11 Aug evening → 12 Aug night | **VERIFIED** | Supported by session timestamps. |
| L43a | After a couple days, a prototype had to be thrown out | **PARTLY VERIFIED** | The camera-aligned atlas was explicitly rejected, but the measured interval was about 26 hours, not two full days. |
| L43b | 3×3 atlas rejected after 103 pan requests and up to 12 s cumulative repair in six-stroke burst | **VERIFIED** | Exact recorded diagnostic figures. Evidence: `.codex-archaeology/git-history.md`, relevant design/benchmark receipts. |
| L43c | It nevertheless supplied the direction | **SUBJECTIVE, PLAUSIBLE** | Later work retained lessons but used a different architecture. |
| L45 | “real building began” | **RHETORICAL** | Transition. |
| L49a | “In August 2026, every software engineer has…” | **NOT VERIFIED / HYPERBOLE** | No evidence could establish a universal claim. Keep as an obvious joke or change to “a lot of software engineers.” |
| L49b | “I have it.” | **INCONCLUSIVE / PERSONAL** | The project record does not inventory the author’s abandoned projects. |
| L49c | “Everyone I know has that.” | **INCONCLUSIVE / HYPERBOLE** | Social-circle-wide claim cannot be established here. |
| L49d | “That’s the reality of August 2026.” | **RHETORICAL** | Cultural observation, not a checkable project fact. |
| L51 | “And I’m shipping it.” | **VERIFIED** | V2 release commit/tag and release documents exist. Evidence: `.codex-archaeology/git-history.md`. |

### Demoscene moment (lines 53–65)

| Ref | Claim | Verdict | Evidence / publication-safe version |
|---|---|---|---|
| L55 | Happened after fever dream, during production, 12–14 Aug | **VERIFIED IN BROAD ORDER** | The mechanical-sympathy/demoscene exchange appears during the production optimization period. |
| L57a | Friend is a very good systems engineer doing low-level Nix/Rust work | **INCONCLUSIVE / PERSONAL DESCRIPTION** | The transcripts preserve the author’s description, not independent credentials. Attribute it: “a friend I know through low-level Nix and Rust work.” |
| L57b | Author said they felt stuck | **VERIFIED IN SUBSTANCE** | The exchange and surrounding frustration are in the session record. |
| L57c | Friend advised “skill issue,” mechanical sympathy, elegance, demoscene mindset | **VERIFIED AS TRANSCRIPT-RECORDED ADVICE** | The wording/substance is preserved in the sessions. Evidence: `.codex-archaeology/session-history.md`. |
| L59 | Proposed “conjuring the right guy from latent space” quote | **EDITORIAL / PERMISSION DEPENDENT** | Include only with the friend’s approval as the note says. |
| L61a | Demoscene is a programming subculture competing on visuals/audio under real/artificial constraints | **VERIFIED IN BROAD TERMS** | Scene sources describe realtime graphics/music programs and competitions; “under constraints” fits many compos, though not every production or party is severely constrained. [Scene.org FAQ](https://files.scene.org/view/resources/in4k/pcdemoscene_faq.txt), [Scene.org Awards overview](https://awards.scene.org/info.php). |
| L61b | “Compo” is short for competition and is a demoparty category | **VERIFIED** | The Scene.org FAQ defines compos as competitions at demoparties. |
| L61c | Demoparty is a gathering to show work “under severe constraints” | **PARTLY VERIFIED / OVERBROAD** | It is a scene meeting/festival with competitions. Specific compos impose size, platform, or time constraints; “severe” does not describe every demoparty entry. |
| L63a | Realization that the project was a compo | **SUBJECTIVE / METAPHORICAL** | The project was not an official competition entry. “My own compo” makes the metaphor explicit. |
| L63b | Project was demoscene-like from the start | **SUBJECTIVE, SUPPORTED** | Small hardware, realtime graphics, and performance constraints support the analogy. |
| L63c | Was not conscious of it until friend said it | **AUTOBIOGRAPHICAL** | The exchange is documented; awareness is personal recollection. |
| L65 | Advice helped because many tricks followed | **CAUSAL CLAIM, INCONCLUSIVE** | The sessions show many optimizations after the exchange, but cannot isolate causation. Safe: “After that, we started explicitly framing the work that way and used a whole host of tricks.” |

### Cold rendering (lines 67–93)

| Ref | Claim | Verdict | Evidence / publication-safe version |
|---|---|---|---|
| L69a | Cold render means rendering from scratch because pixels are not cached | **VERIFIED AS PROJECT DEFINITION** | Matches design and benchmark usage. |
| L69b | Vector strokes are source of truth; raster tiles are cached pixels | **VERIFIED** | This is the core Vector V2 authority/materialization design. Evidence: design docs and `vector_v2` source. |
| L69c | Goal was to avoid cold renders and speed unavoidable ones | **VERIFIED IN SUBSTANCE** | Cache-retention work and the cold optimization campaign directly support it. |
| L71 | Is “cold render” standard? | **ANSWERED: PROJECT-SPECIFIC SHORTHAND** | “Cold/warm cache” is standard language; no authoritative graphics definition matching this exact project usage was found. Define it on first use as the draft does. |
| L73a | First drawable/rendering version was built by an agent | **VERIFIED IN SUBSTANCE** | Agent transcripts and history show substantive implementation. They cannot prove the absence of any unlogged human edit. |
| L73b | It was excruciatingly slow | **SUBJECTIVE, MEASUREMENT-SUPPORTED** | Early adversarial cold render was 23.6–23.8 s at 400%. |
| L73c | “Almost 24 seconds” at 400% on overlap torture test | **VERIFIED** | Recorded baseline was approximately 23.6–23.8 s. Evidence: cold benchmark receipts and `.codex-archaeology/docs-performance.md`. |
| L75 | Need to make it faster | **SUBJECTIVE / QUOTED INTENT** | Performance targets and optimization work support the intent. |
| L77 | First round moved >20 s to ~1 s | **VERIFIED AS ROUNDING** | The historical sequence records the drop from roughly 23.8 s to around one second before later sub-500 work. |
| L79a | Host disagreed with device on 2/5 experiments | **VERIFIED** | The experiment matrix records opposite winners in two of five cases. Evidence: `.codex-archaeology/docs-performance.md`. |
| L79b | M1-good optimizations could be slower on ESP32 “because PSRAM is so slow” | **PARTLY VERIFIED / CAUSE OVERSTATED** | Host/device reversals are real. PSRAM behavior, Xtensa code generation, caches, flash layout, and memory placement all contributed in different experiments. Do not assign every reversal to PSRAM. |
| L81a | Uses fast inverse square root “from Quake III” | **VERIFIED WITH ATTRIBUTION CAVEAT** | Current code contains the `0x5F3759DF` Quake III-style routine. The technique is best described as “popularized by Quake III,” not originating there. It supplies a conservative seed bound; exact pixel tests decide coverage. [id Software source](https://github.com/id-Software/Quake-III-Arena/blob/dbe4ddb10315479fc00086f08e25d968b4b43c49/code/game/q_math.c#L546-L581), `vector_v2/src/incremental_rasterizer.cpp`. |
| L81b | Put “a bunch of stuff in IRAM” | **IMPRECISE** | The measured experiment placed the hot stroke raster function in IRAM. Say “moved the hot raster loop into IRAM.” |
| L81c | IRAM is part of 512 KB “real RAM,” opposed to 8 MB “pretty slow” RAM | **PARTLY VERIFIED / MISLEADING** | ESP32-S3 has 512 KB internal SRAM and this board has 8 MB external PSRAM. IRAM is an executable mapping of internal SRAM; PSRAM means pseudo-static RAM. [Espressif internal memory](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/memory-types.html), [external RAM](https://docs.espressif.com/projects/esp-idf/en/v5.2.5/esp32s3/api-guides/external-ram.html), [board docs](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8). |
| L81d | IRAM saved 6.93–11.68% | **VERIFIED** | Same-tree 11-case physical A/B; median 8.70%, range 6.93–11.68%, about 10,836 B IRAM cost. Evidence: `.codex-archaeology/docs-performance.md`. |
| L83 | Internal scratch predicted ≥40%; measured −0.36% | **VERIFIED WITH METRIC CLARITY** | Scratch-raster stage changed −0.36%; total wall changed −1.69%. The ≥40% forecast was rejected by device measurement. |
| L85 | Optimization happened in stages with other work between | **VERIFIED** | Git/session chronology interleaves cold, pan, cache, export, history, and AA work. |
| L87a | Flash/i-cache layout moves timing ±2–3% per build | **VERIFIED AS OBSERVED RANGE** | Receipts document 2–3% movement from code-placement/layout effects. |
| L87b | 40 KB workspace mid-heap cost +9 ms; dead-last cost 0 ms | **VERIFIED FOR THAT EXPERIMENT** | Memory placement produced those measured outcomes. Avoid generalizing to every allocation. |
| L89a | Last issue was “evil hairlines” | **NOT VERIFIED AS CHRONOLOGY** | Evil hairlines were in the combined corpus by 16 Aug, and later campaigns—including IRAM work—followed. They were a late, important torture workload, not the literal last optimization issue. |
| L89b | Battery looked good but dense crossing lines slowed zoom | **VERIFIED** | The earlier battery omitted this workload; the added corpus nearly doubled the apparent cold time. |
| L89c | It drove final push and test-suite tweaks | **VERIFIED IN SUBSTANCE** | The adversarial corpus and gates were added/refined in response. |
| L91a | “We stacked sixteen optimizations on cold rendering” | **NOT VERIFIED** | The cited inventory table shows 18 successful rows while its summary says 16, and some rows concern settled AA rather than general cold. Reconcile the counting rule before using a number. |
| L91b | Hit under 500 ms “at all zoom levels” | **NOT VERIFIED AS WRITTEN** | The cited release-gate values cover the four tiled levels: 50%, 100%, 200%, 400%. At 25%, the complete overview has no equivalent tiled cold-fill path. Use “under 500 ms at all four tiled zooms, 50–400%.” |
| L91c | Final 389/383/456/492 ms at 50/100/200/400 | **NOT VERIFIED AT STATED PRECISION** | Exact recorded values are 389.942, 383.159, 456.961, 492.793 ms; nearest-ms values are **390, 383, 457, 493 ms**. The draft truncates three values. The old receipt SHA was lost in an identity rewrite, so call these “recorded release-gate values.” |
| L93 | 22 rejected experiments; list location | **NOT VERIFIED** | The cited inventory table visibly contains 20 rejected rows while its summary says 22. Reconcile the missing/grouped entries before publishing the count. Evidence: `.pi/plans/2026-08-21-cold-optimization-inventory/scout-context.md`. |

### Panning and tearing (lines 95–123)

| Ref | Claim | Verdict | Evidence / publication-safe version |
|---|---|---|---|
| L97 | 15 FPS start and ~30 FPS finish | **VERIFIED WITH HISTORICAL CAVEAT** | Round start: 67.3 ms/frame = 14.86 FPS. Accepted historical result: p95 33.940/33.939 ms at 100%/400% = ~29.5 FPS, glass-clean at 50–400%. No same-tree final-release distribution survives. Evidence: `docs/receipts/vector-v2/PAN_FLOOR_CLOSURE_2026_08_15.md`, `FINAL_PERFORMANCE_BASELINE_2026_08_18.md`. |
| L99a | Panning was the next problem | **VERIFIED AS NARRATIVE ORDER** | Pan work began 14–15 Aug during the broader rendering campaign; cold work later resumed. |
| L99b | Initially around 15 FPS | **VERIFIED** | 67.3 ms/frame is about 14.9 FPS. |
| L99c | Wanted it faster | **SUBJECTIVE, SUPPORTED** | The frozen requirement was at least 24 FPS. |
| L99d | Agent appeared to fix it to 30 FPS | **VERIFIED AS CONTEMPORANEOUS APPARENT SUCCESS** | Beam-race build reported 28.1 ms average and p95 32.95 ms (~30.3 FPS at p95), but glass later invalidated the design. Do not present this as a successful product result. |
| L99e | Glass looked terrible, glitched, and tore | **VERIFIED** | Manual testing reported severe tearing, fixed-row overlay corruption/stripes, and visible glitches despite zero software synchronization failures. |
| L101a | Author then realized agent “cannot really test this or see it” | **VERIFIED AS REALIZATION; TECHNICALLY TOO ABSOLUTE** | Agents could measure cadence and later analyze footage; software self-reports could not observe the physical glass. |
| L101b | Only fingers/eyes can test panning speed and lack of tearing | **NOT VERIFIED / OVERSTATED** | Software timestamps measured speed; hands/eyes measured feel; high-speed video plus a positive control measured optical tear-freedom. Suggested sentence: “Software timestamps could measure speed, but feel and tear-freedom needed the actual glass—my hands and eyes, and eventually high-speed video.” |
| L103a | Agent kept trying; failures and frustration continued | **VERIFIED IN SUBSTANCE** | Sessions and pan design receipts record repeated failed presenters and explicit frustration. |
| L103b | “stop… failing… step back and think” | **VERIFIED IN SUBSTANCE, NOT VERBATIM** | The surviving wording demands a reset and a scientific finish; the exact polished quotation is not retained. |
| L105 | Requested 40/50/60 MHz SPI; all actually 40 MHz | **VERIFIED** | All requests measured 17.998–17.999 ms per full frame and mapped to 40 MHz because of the ESP32-S3 divider. Evidence: `docs/receipts/hardware/CO5300_PANEL_LIMITS_2026-08-15.md`. |
| L107a | GETSCANLINE and every control-register read returned zero | **VERIFIED** | GETSCANLINE plus RDDID, RDDST, RDDPM, RDDMADCTL, RDDCOLMOD, and brightness readback returned zero. |
| L107b | No trustworthy software scan-position oracle | **VERIFIED FOR THIS CONFIGURED QSPI PATH** | This does not prove the controller can never report scan position; unsupported/broken reads or dummy-cycle handling remained possible explanations. |
| L109 | Agent proposed slow-motion blinking-pattern video to diagnose tearing | **VERIFIED IN SUBSTANCE** | The proposed full-field red/green optical protocol distinguished persistent tearing from ordinary camera/frame straddle. |
| L111a | Fuji X-T5 records 240 FPS | **VERIFIED** | Session clip metadata and Fujifilm’s specification support Full HD high-speed 240p. [Fujifilm X-T5 specifications](https://www.fujifilm-x.com/en-gb/products/cameras/x-t5/specifications/). |
| L111b | Flicker-free option allowed 1/1024 shutter | **VERIFIED** | The author found the setting; the protocol records 1/1024 s. |
| L111c | 1/1024 was good for the test | **VERIFIED** | Protocol estimated about 26 rows of exposure smear versus roughly 111 rows of inter-frame boundary motion. |
| L113a | “I have one [Fuji] lens currently” | **INCONCLUSIVE** | The transcript proves a Sigma 56 mm was mounted, not the author’s entire lens inventory. |
| L113b | Sigma 56 mm, thought of as 85 mm equivalent | **VERIFIED** | The author corrected the lens wording in-session. On APS-C, 56 mm × 1.5 is 84 mm, conventionally rounded to 85 mm equivalent. |
| L113c | Horrible magnification / not made for this | **SUBJECTIVE, FACTUALLY SUPPORTED** | Sigma specifies 50 cm minimum focus and 1:7.4 maximum magnification. [Sigma 56mm F1.4 specifications](https://www.sigma-global.com/en/lenses/c018_56_14/). |
| L113d | Device propped against laptop; backlight turned down | **INCONCLUSIVE / PERSONAL RECOLLECTION** | These details do not occur in the surviving session. It does establish no tripod/handheld shooting in a dim room. |
| L113e | Handheld over two minutes of video | **VERIFIED** | Supplied clip: 37,920 frames at 240 FPS, about 158 seconds (2:38); session says handheld/no tripod. |
| L115a | Gave footage to agent; classifier detour took too long | **VERIFIED IN SUBSTANCE** | About 40 minutes went into registration, rotation, segmentation, and a counter bug. “Way too much” is the author’s judgment. |
| L115b | “That didn’t really work” | **MISLEADING WITHOUT CONTEXT** | Full-video automation bogged down, but a later focused automated pass succeeded: 1,495 frames, zero tears and zero anomalies. |
| L115c | Author stopped the failing approach | **VERIFIED IN SUBSTANCE** | The raw session records the intervention after roughly an hour to an hour and a half. |
| L115d | Contact sheet gave the answer | **VERIFIED** | Two 24-frame sheets showed the positive control tearing in every frame and the candidate boundary moving normally. |
| L117 | Classifier detour; “over an hour?” | **VERIFIED IN SUBSTANCE** | Raw author message: “over an hour? hour and a half maybe?” |
| L119a | “suddenly… fast panning, but… tearing” | **NOT VERIFIED AS WRITTEN / TIMELINE CONFLATION** | Correct order: Fuji validated the simple sweep → product was optically improved but ~19.9 FPS → compositor work recovered ~29.5 FPS → a fixed tear appeared near minus → per-strip staging fix removed it. |
| L119b | Tear appeared at one spot, then another after a change | **VERIFIED** | First near the top gray edge of zoom-minus; reverting burst pacing moved it roughly two-thirds down the minimap. |
| L121a | Tear “fixed” near zoom-minus | **VERIFIED IF “FIXED” MEANS STATIONARY** | Author located it within about ±5–10 pixels of that edge. Keep the quotation marks. |
| L121b | Next change moved it two-thirds down minimap | **VERIFIED** | The specific change was reverting burst pacing. |
| L123a | Agent deduced last missing step from those observations | **VERIFIED IN SUBSTANCE** | The moving stationary tear exposed a local writer/beam crossover; every strip had to finish staging before its own wire deadline. |
| L123b | The step fixed tearing | **VERIFIED AS HISTORICAL GLASS ACCEPTANCE** | Author reported tearing gone, followed by all-zoom confirmation on 16 Aug. |
| L123c | “Now” almost 30 FPS, tear-free | **VERIFIED ONLY AS HISTORICAL ACCEPTANCE** | ~29.5 FPS p95 was accepted glass-clean at 50/100/200/400%. Current source was not freshly hardware-profiled. Say “The accepted build reached…” |
| L123d | “which I’m really proud of” | **SUBJECTIVE** | Personal reaction. |

### Undo (lines 125–149)

| Ref | Claim | Verdict | Evidence / publication-safe version |
|---|---|---|---|
| L127 | Timeline of undo/hammering/preview attempts | **ANSWERED** | Whole-stroke V2 history landed 17 Aug, after pan/tear closure (15–16 Aug) and all-zoom déjà-vu work (16 Aug). Low-resolution fallback, holdback, hourglass, and copy-on-write work followed 18 Aug. |
| L129a | Undo was the most painful feature | **SUBJECTIVE / AUTOBIOGRAPHICAL** | Strongly consistent with the author calling early results “brutal” and worse than cold render, but no objective ranking exists. |
| L129b | Cold was fast; panning/tearing fixed before V2 Undo | **VERIFIED WITH NUANCE** | Pan/tearing was accepted before whole-stroke history. Most cold levels were under 500 ms then; 400% was still around 507 ms before later work. |
| L129c | Déjà vu had probably been fixed by then | **VERIFIED BECAUSE HEDGED** | All-zoom idle retention landed 16 Aug; revisit refill fell from 188–326 ms to ~0.37–0.38 ms, with eviction/budget residuals. |
| L129d | “Then I added undo to V2” | **VERIFIED** | Whole-stroke V2 history landed 17 Aug. Raster V1 already had ten-entry Undo, so keep “to V2.” |
| L131a | Déjà vu definition via right/down/left/up return | **VERIFIED AS AN EXAMPLE, SLIGHTLY NARROW** | Project term covers rerendering already-materialized content on zoom or tile-identity revisits, not only that pan loop. |
| L131b | Name coined by an agent | **PROBABLY VERIFIED** | Earliest durable tracked use is in an agent-produced ship-contract/review context; no earlier author use was found. The surviving record cannot establish coinage beyond that. |
| L131c | “it’s a good name” | **SUBJECTIVE** | Personal judgment. |
| L133a | First V2 Undo rerendered only affected blocks | **VERIFIED** | It rebuilt the affected overview rectangle and invalidated intersecting detail tiles while retaining unaffected ones. |
| L133b | Blocks were “pretty small” | **WORKLOAD-DEPENDENT** | Damage was bounded to the stroke region, but the evil-hairline corpus could cover most of the viewport. |
| L133c | Watched “this line render” in the middle | **NOT VERIFIED LITERALLY** | The affected region fell back to the overview and sharpened block by block over roughly 11–16 presentations. Use “you watched the affected patch sharpen tile by tile.” |
| L133d | Looked absolutely horrible | **SUBJECTIVE, DIRECTLY SUPPORTED** | The author called the result “more jarring,” “worse,” and “really ugly.” |
| L135a | Undo is a redraw and redraws are expensive | **VERIFIED HISTORICALLY, NOT UNIVERSALLY FINAL** | Initial broad Undo reached 433.851 ms, ~89% of a full adversarial cold render. Final preserved-state hits can swap cached pixels without reconstruction. |
| L135b | Leaving Undo late was ill-advised | **SUBJECTIVE RETROSPECTIVE** | The transcript explicitly records this hindsight. |
| L135c | Earlier Undo might have been faster/less painful | **UNTESTABLE COUNTERFACTUAL** | No evidence can establish the alternate development history. |
| L137a | Hammering behavior took many tries | **VERIFIED** | Work passed through per-publication repair, holdback, 25% conversion, merged chains, hourglass variants, copy-on-write swapping, and a coordinate-space fix. |
| L137b | New Undo/Redo cancels previous rendering | **PARTLY VERIFIED / IMPRECISE** | New taps supersede unfinished producer/settle work, merge damage, and produce one final exact update. Work already submitted to the panel cannot be canceled, and each authority move still occurs. |
| L137c | Hammering Undo/Redo no longer breaks it | **VERIFIED FOR FINITE RELEASE TESTS** | Host exactness, ASan, device battery, and rapid interaction capture passed; one capture logged 47 hold conversions with zero failures/overflows/resyncs. |
| L139a | Tried low-res render, no intermediates, then final render | **VERIFIED IN MECHANISM; TERMINOLOGY WRONG** | It showed the exact low-resolution overview fallback for the damaged region, suppressed detail publications, then made one exact union presentation. It was not a low-res cold render of the entire line. |
| L139b | Crude version was ugly | **SUBJECTIVE, DIRECTLY SUPPORTED** | Matches the author’s contemporaneous verdict. |
| L141 | Author proposed an hourglass until final render | **VERIFIED** | The raw message proposes an hourglass while Undo settles and rendering only the final result. |
| L143a | Agent proposed no cue below 120 ms, hourglass above | **VERIFIED** | 120 ms was a UI threshold, not a measured Undo duration. Later device capture found 25% p50 34.3 ms and overall p95 387.4 ms. |
| L143b | Author disliked inconsistent feedback and required hourglass even for short actions | **VERIFIED** | The transcript explicitly prefers a brief, consistent flash; the corresponding commit always shows the history hourglass. |
| L143c | Consistent UX is good | **SUBJECTIVE DESIGN JUDGMENT** | Rationale is transcript-supported. |
| L145a | Hourglass made Undo acceptable | **SUBJECTIVE, SUPPORTED** | Author accepted the immediate hourglass and called it the “least of evils.” |
| L145b | Without feedback, user waited 500 ms | **APPROXIMATE; TIGHTEN** | Worst deterministic single-move baseline was about 434–441 ms. “About half a second” is fair; “500 ms” is not the exact measurement. Merged spam chains could exceed one second. |
| L145c | Various intermediate-feedback versions were bad | **VERIFIED AS SUBJECTIVE SUMMARY** | Per-tile popping and one-soft-flash/two-transition holdback were tested and rejected/revised. |
| L145d | People know and recognize an hourglass | **INCONCLUSIVE GENERALIZATION** | No user-recognition study was performed. Say “a familiar hourglass cue.” |
| L145e | Hourglass fixed UX, not performance | **VERIFIED FOR THE HOURGLASS ITSELF** | Holdback changed wall time by about 1%; later copy-on-write preservation materially improved revisited-state performance. |
| L147a | Nothing still unsatisfying | **SUBJECTIVE** | Release docs still name measurement gaps, but no known correctness blocker. |
| L147b | Actual Undo rendering was optimized well | **VERIFIED AS MODEST QUALITATIVE CLAIM** | Final experiments reduced device authority/overview max to 27.3 ms, 400% holdback max to 112.3 ms, and preserved revisit repair from 338,998 to 229 µs. |
| L147c | Hourglass is least-bad UX | **SUBJECTIVE, TRANSCRIPT-SUPPORTED** | Personal design conclusion. |
| L149a | Spare cache slots preserve pixels from before a stroke | **NOT VERIFIED AS WRITTEN** | There is one 604-slot pool. History moves retag affected current tiles as the departing state; previously materialized arriving versions can then be swapped in. Ordinary stroke creation does not simply pre-save all “before” pixels. |
| L149b | Undo/Redo swaps preserved pixels when present; rebuilds after eviction/unvisited state | **VERIFIED WITH REWRITE** | Suggested footnote: “Undo/Redo keeps raster tile versions for history states you’ve already visited. A reverse move can swap those pixels back immediately while they remain in the 604-slot cache; the first visit, eviction, or a replaced Redo branch falls back to exact reconstruction.” |

### Anti-aliasing (lines 151–161)

| Ref | Claim | Verdict | Evidence / publication-safe version |
|---|---|---|---|
| L153a | “Finally, there was anti-aliasing” | **NOT VERIFIED AS CHRONOLOGY** | Settled analytic AA landed 16 Aug, before V2 Undo (17 Aug), later cold work, and release closure. |
| L153b | “last big thing I did last” | **NOT VERIFIED / REDUNDANT** | AA was not the final major feature campaign. If this means the last topic in the essay, it is only a transition. |
| L155a | AA is “undo on steroids” | **METAPHOR, TECHNICALLY MISLEADING** | Both can trigger derived-pixel work, but history restoration and settled analytic rerendering have different authority, cache, and scheduling paths. |
| L155b | AA was added “way too late” and needed optimizations | **PARTLY VERIFIED / SUBJECTIVE** | AA did receive several performance experiments. “Way too late” is retrospective judgment, and it arrived before Undo and several later campaigns. |
| L155c | “should have definitely had [it] when I started” | **UNTESTABLE COUNTERFACTUAL** | Earlier AA might have changed architecture or workload, but the record cannot prove a better outcome. |
| L157a | Because AA arrived late, the only option was waiting for cold rendering then applying AA | **NOT VERIFIED** | Settled AA is a newest-first **analytic rerender from vector authority** scheduled only after drain/fill/repair become quiet. It publishes a settled-quality tile; it is not a postprocessing filter applied to cold pixels. The record does not say lateness made this the only architecture possible. |
| L157b | “We just made the applying somewhat faster” | **PARTLY VERIFIED / UNDERSTATED** | Multiple exact AA optimizations did make the settled tier faster, though “applying” obscures that this was analytic rerendering plus scheduling, not a filter pass. Final campaign totals moved from 75.217/87.869/177.282/386.169/959.910 to 75.102/87.647/176.885/383.594/946.849 ms. |
| L159 | Did AA have to bolt onto existing architecture? | **ANSWERED: NO EVIDENCE OF NECESSITY** | It was integrated as an idle settled-quality tier within the vector-authoritative tile architecture. The documents support the implemented design, not the claim that late timing forced it. Evidence: `docs/design/VECTOR_V2_SETTLED_EDGE_SPANS.md`, `benchmark-results/settled-aa-prototype/RECEIPT.md`, `vector_v2/src/settled_tile.cpp`. |
| L161a | AA is subtle on a small screen | **SUBJECTIVE, PLAUSIBLE** | No controlled visibility/user study was run. |
| L161b | Its absence bothers the author | **SUBJECTIVE / AUTOBIOGRAPHICAL** | Personal preference. |

### What the agents did and did not do (lines 163–185)

| Ref | Claim | Verdict | Evidence / publication-safe version |
|---|---|---|---|
| L165a | “They wrote all the code. I never touched a single line.” | **INCONCLUSIVE / TOO ABSOLUTE** | Transcripts show agents producing the substantive implementation. Git attribution uses the author’s identity and cannot distinguish human from agent; inaccessible/private edits cannot be excluded. Safe: “The agents produced the substantive implementation; I directed, tested, and evaluated it.” |
| L165b | “They reviewed all the code.” | **INCONCLUSIVE / TOO ABSOLUTE** | Extensive review is documented, but exhaustive line-by-line review is not. A later adversarial audit still found gaps, including absent CI and incomplete static-analysis coverage. |
| L165c | They did all architecture review and cleanup | **INCONCLUSIVE / TOO ABSOLUTE** | Agents performed substantial architecture review and cleanup. “All” cannot be proved, and the author made architectural/product decisions through prompts, tests, and acceptance. |
| L167a | “Ultimately this is vibe coded.” | **SUBJECTIVE / TERM-DEPENDENT** | “Vibe coded” has no fixed technical test. The agent-led implementation workflow supports the intended meaning. |
| L167b | “…with an engineering mindset.” | **SUBJECTIVE, EVIDENCE-SUPPORTED** | Measurement gates, rejected experiments, physical A/Bs, receipts, and adversarial reviews support the characterization. |
| L169a | GPT-5.6 Sol wrote most code | **VERIFIED IN OBSERVED SESSION HISTORY, NOT BY GIT AUTHORSHIP** | Local sessions assign Sol the primary implementation/hardware/integration role. Exact percentage is unavailable. |
| L169b | Sol did a very good job | **SUBJECTIVE** | Product shipped and tests pass, but quality judgment belongs to the author. |
| L169c | Sol is detail-oriented, not best at architecture, but okay | **SUBJECTIVE MODEL CHARACTERIZATION** | Consistent with the author’s observed division of labor; no controlled comparison establishes it. |
| L169d | “Sol makes far fewer bugs” | **INCONCLUSIVE COMPARATIVE CLAIM** | No normalized bug-rate dataset by model exists. Safe: “In these sessions, I found Sol more reliable on implementation details.” |
| L171a | When Sol was stuck, author packed all source and sent it to GPT-5.6 Pro for whole-project review | **INCONCLUSIVE / PARTLY SUPPORTED** | Local material includes review handoff packets and copied review output. It cannot inspect the Pro web session or prove that every source file was uploaded. |
| L171b | Reviews covered bugs, performance, or architecture | **VERIFIED IN SUBSTANCE** | Surviving handoffs/reviews cover these areas. |
| L171c | “That takes 60 to 90 minutes.” | **INCONCLUSIVE** | The accessible local record does not retain reliable Pro web-session durations. Present as personal recollection if kept. |
| L171d | Author fed the result to Claude Fable | **VERIFIED IN WORKFLOW, NOT EVERY INSTANCE** | Sessions show external-review material being handed into Fable/Codex work. Avoid implying every review followed an identical path. |
| L173a | Fable is brilliant and deeply flawed | **SUBJECTIVE** | Personal model assessment. |
| L173b | Fable is exceptionally good at architecture | **SUBJECTIVE, ROLE-SUPPORTED** | Sessions assign Fable architecture/performance/review work; no controlled benchmark proves superiority. |
| L173c | xhigh Fable performance optimization “was something else” | **SUBJECTIVE** | No falsifiable claim as written. |
| L173d | Sol catches Fable bugs and vice versa | **VERIFIED IN EXAMPLES, NOT AS A GENERAL RATE** | Cross-review and defect discovery in both directions appear in the transcripts. |
| L173e | Usual best workflow: Sol writes, Fable reviews | **VERIFIED AS AUTHOR’S OBSERVED WORKFLOW / SUBJECTIVE AS “RIGHT”** | Session roles broadly follow this pattern, with exceptions. |
| L175 | Used all Fable quota from £200 plan in ~3 days | **INCONCLUSIVE / EDITORIAL** | No billing or usage record is locally accessible. Verify from Anthropic account data if published. |
| L177a | Building became a game of maximizing performance | **SUBJECTIVE, SUPPORTED** | Repeated performance campaigns establish the focus. |
| L177b | “stack sixteen different tricks” | **NOT VERIFIED** | The retrospective inventory’s table/summary conflict (18 displayed successful rows versus summary count 16), and some concern settled AA. |
| L177c | Kept adding agents/prompts until not painfully slow | **SUBJECTIVE SUMMARY, SUPPORTED** | Multiple model handoffs and optimization rounds are documented. |
| L179a | Persistence/stubborn persistence belongs to author | **SUBJECTIVE ATTRIBUTION** | The author repeatedly chose to continue, supplied physical tests, redirected agents, and set acceptance criteria. |
| L179b | Could have abandoned at any time | **RHETORICAL COUNTERFACTUAL** | Not meaningfully falsifiable. |
| L181a | Agents do not have fingers | **LITERALLY TRUE IN THIS WORKFLOW / RHETORICAL** | Physical interaction was performed by the author. |
| L181b | Agent said 30 FPS; glass test showed otherwise and glitched | **VERIFIED IN SUBSTANCE** | Software reported ~30 FPS on the beam-race design; physical glass exposed severe tearing/glitches. The issue was optical correctness, not that the timestamp itself was necessarily false. |
| L183 | Author’s finger was important | **SUBJECTIVE, STRONGLY SUPPORTED** | Physical gesture feel, tearing observations, dense-hairline torture drawings, and touch-fault reports changed the product and tests. |
| L185a | Long autonomous runs made author understand less and feel bad | **AUTOBIOGRAPHICAL, TRANSCRIPT-SUPPORTED** | Sessions contain explicit loss-of-understanding/frustration messages. |
| L185b | Stopping for explanation/measurement often helped | **CAUSAL PERSONAL ASSESSMENT, SUPPORTED** | Several resets led to clarified mechanisms and new measurements, though causality is not experimentally isolated. |
| L185c | Feeling lost became signal to regroup | **AUTOBIOGRAPHICAL WORKING RULE** | Consistent with recorded interventions. |

### Landing (lines 187–211)

| Ref | Claim | Verdict | Evidence / publication-safe version |
|---|---|---|---|
| L189 | “So what did I build?” | **RHETORICAL** | Section transition. |
| L191a | Vector graphics editor on 1.8-inch screen | **VERIFIED** | Vector strokes are authoritative; board has a 1.8-inch 368×448 capacitive AMOLED. Evidence: `README.md`; [Waveshare documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8). |
| L191b | Zoom 25–400% | **VERIFIED WITH DISCRETE-LEVEL CLARITY** | Five supported levels: 25%, 50%, 100%, 200%, 400%; not continuous zoom. |
| L191c | Has a minimap | **VERIFIED** | UI/source/tests and README document it. |
| L191d | Draw and erase | **VERIFIED** | Current source and host tests cover stroke/eraser behavior. |
| L191e | Choose from 32 colors | **VERIFIED** | Chrome defines two 16-color PICO-8 palettes. Evidence: `vector_v2/include/tinydraw/vector_v2/chrome.h`. |
| L191f | Undo/Redo and “hammer it” | **VERIFIED FOR TESTED RAPID INTERACTIONS** | Rapid-history device capture and exactness tests passed; this is finite evidence, not a proof for every possible input schedule. |
| L191g | SVG export is right “as far as I can tell” | **VERIFIED BECAUSE QUALIFIED** | Export parity work/tests support confidence; the hedge avoids claiming mathematical proof over every document. |
| L191h | SVG was a whole ordeal | **SUBJECTIVE, SUPPORTED** | Multiple parity bugs and fixes are documented. |
| L191i | PNG export exists | **VERIFIED** | Export implementation/tests and README document PNG. |
| L191j | Cold rendering under 500 ms at all zoom levels | **NOT VERIFIED AS WRITTEN** | The release battery covers the tiled 50/100/200/400 paths only. At 25%, the overview has no equivalent tiled cold path. Settled AA is a separate, slower tier. Use: “In the release battery, cold fill was under 500 ms at every tiled zoom from 50% to 400%.” |
| L191k | Panning almost 30 FPS and tear-free | **VERIFIED AS HISTORICAL ACCEPTANCE** | Accepted p95 ~33.94 ms (~29.5 FPS), glass-clean at 50–400%. No fresh same-tree device distribution was produced in this audit. |
| L193 | Cache progression 320 → 384 → 448 → 604, nearly doubled | **VERIFIED; EDITORIAL PLACEHOLDER** | Source history records that progression; 604/320 = 1.8875, so “almost twice” is accurate. Do not insert the separate failed/mislabeled 512 experiment into it. Evidence: `vector_v2/include/tinydraw/vector_v2/memory_layout.h`. |
| L195a | People would not really use it for drawing | **SUBJECTIVE / RHETORICAL** | Product-value judgment; contradicted only if framed as universal. “I don’t expect many people to…” is safer. |
| L195b | Its existence is absurd | **SUBJECTIVE** | Comic framing. |
| L195c | Absurdity is part of charm | **SUBJECTIVE** | Personal aesthetic judgment. |
| L195d | Nice to know author built it | **SUBJECTIVE** | Personal reaction. |
| L197a | Weirdly fast and silly | **SUBJECTIVE, PERFORMANCE-SUPPORTED** | Recorded timings justify the comparison within this hardware/project, not an industry-wide ranking. |
| L197b | It is a real vector graphics program | **VERIFIED** | Vector authority, drawing/erasing, pan/zoom, history, and SVG export support this description. |
| L197c | Fast for ESP32-S3 with 8 MB PSRAM | **SUBJECTIVE, HARDWARE VERIFIED** | Hardware and recorded timings are established. “Fast” needs the project’s workload/targets for objective comparison. |
| L197d | “PS is short for Pretty Slow” | **JOKE / FACTUALLY FALSE IF LITERAL** | PSRAM means **pseudo-static random-access memory**. Keep only as an explicit joke. [Espressif external RAM guide](https://docs.espressif.com/projects/esp-idf/en/v5.2.5/esp32s3/api-guides/external-ram.html). |
| L199a | Author likes being persistent | **SUBJECTIVE** | Personal reflection. |
| L199b | “Ungodly number of hours over nine days” | **PARTLY VERIFIED** | Intensive sessions are clear; total hours were not reliably measured. The entire repository span from first commit to release was about 9 days 18 hours; V2 occupied nine calendar dates, 11–19 Aug inclusive. |
| L199c | At each blockage: stronger agent, more effort, demoscene framing, or regroup | **SUBJECTIVE SUMMARY, SUPPORTED** | All named tactics occur, but “each time” is universal rhetoric. |
| L201a | Proud of continuing to push | **SUBJECTIVE** | Personal reflection. |
| L201b | Agent called it fast; evil hairlines showed it was not | **VERIFIED IN SUBSTANCE** | An apparently good battery omitted the adversarial dense-hairline workload, which exposed a large regression. Do not imply this was the literal final campaign. |
| L201c | Another optimization round followed | **VERIFIED** | Test corpus and implementation were revised after the finding. |
| L203 | Created a compo for a nonexistent demoparty | **METAPHOR / SUBJECTIVE** | No official compo or demoparty existed; the phrasing intentionally describes a self-imposed challenge. |
| L205 | Rediscovered skill at rapid domain proficiency | **SUBJECTIVE SELF-ASSESSMENT** | Not externally measurable from the project record. |
| L207a | LLMs make starting “way… easier” | **GENERAL OPINION** | Supported by this author’s experience, not a universal measured claim. |
| L207b | After fun part, “you’re gonna” encounter graphics/tearing/etc. | **RHETORICAL GENERALIZATION** | These problems occurred here; they are not inevitable in every LLM-built project. |
| L207c | Can ask agent to optimize, but author did much more | **VERIFIED IN SUBSTANCE / SUBJECTIVE EMPHASIS** | Author supplied product direction, physical testing, measurements, adversarial inputs, acceptance decisions, and repeated resets. |
| L209a | TinyDraw V2 runs on Puck | **VERIFIED** | Puck builds the real ESP32 V2 application/authority code to WASM with browser replacements for physical facilities. Evidence: `puck/README.md`. |
| L209b | Puck is “not quite an emulator” | **NOT VERIFIED AS THE PROJECT’S OWN DESCRIPTION** | Puck calls itself an emulator and the architecture review calls its CO5300 implementation an intentional emulator. More precise: “Puck is an emulator/browser hardware shim, though not a cycle- or hardware-timing-accurate one.” |
| L209c | TinyDraw compiles to WASM and Puck runs it | **VERIFIED** | This is the documented build path. |
| L209d | It is not clock-accurate | **VERIFIED** | Puck uses virtual deterministic time and does not reproduce hardware timing. |
| L209e | It reproduces the important bits “really, really well” | **SUBJECTIVE / NEEDS SCOPE** | It shares application semantics and supports exact trace/frame verification. It does not reproduce panel scan timing, physical touch faults, battery/RTC, USB, autosave, or hardware performance. Say which behavior matters. |
| L211 | Browser-link placement note | **EDITORIAL** | Remove or resolve before publication. |

### P.S. — mistakes made (lines 213–236)

| Ref | Claim | Verdict | Evidence / publication-safe version |
|---|---|---|---|
| L215 | “Fact-check all of these” | **EDITORIAL** | Addressed below. |
| L217a | Wi-Fi blamed for psychedelic vertical stripes | **VERIFIED WITH PLATFORM** | This was **ESP32 Raster V1**, not RP2350. Wi-Fi was suspected during the requested 80 MHz panel-bus experiment. |
| L217b | Wi-Fi removed; stripes remained | **VERIFIED** | Removing Wi-Fi did not eliminate the colored vertical lines. Evidence supports unstable display-bus timing as the likely cause, not a proved single root cause. |
| L218 | Requested 40/50/60 MHz; all actually 40 MHz | **VERIFIED** | Duplicate of L105; exact measured frame times were 17.998–17.999 ms. |
| L219 | GETSCANLINE and every control-register read zero | **NOT VERIFIED LITERALLY; VERIFIED FOR TESTED PROBES** | GETSCANLINE and six tested nonzero control-register probes returned zero. Use: “GETSCANLINE and all six control-register probes we tried returned zero.” |
| L220 | Internal scratch predicted ≥40%; measured −0.36% | **VERIFIED, SIGN AMBIGUOUS** | Raster time improved only 0.36%; wall improved 1.69%. Use “only 0.36% faster” to avoid reading the minus sign as a slowdown. |
| L221 | Sacred 1.5 MiB export reserve; actual peak 291,484 bytes | **VERIFIED WITH ORIGIN CAVEAT** | 291,484 B is the measured export peak. The 1.5 MiB reserve was a synthetic/planning allowance, not measured need; “sacred” is rhetoric. |
| L222 | “512-slot” run was actually 384 | **VERIFIED** | The run/file name said 512, but the harness silently used its 384-slot default. Explain this in prose; it was a benchmark-configuration error. |
| L223a | Word-mask scans +7–13% slower on ESP32 than M1 Pro | **NOT VERIFIED / WRONG COMPARISON** | On-device word-mask variants were 7–13% slower than the on-device byte-mask baseline. The M1 host chose the opposite winner; the percentage is not ESP32-versus-M1 absolute speed. |
| L223b | GCC-Xtensa emitted `callx8 memcpy` libcalls | **VERIFIED** | Disassembly identified these calls in the word-scan path. Safe combined wording: “Word-mask scans won on my M1 Pro but regressed 7–13% against byte masks on the ESP32; Xtensa disassembly showed `callx8 memcpy` libcalls.” |
| L224 | Four-sample SSAA took 808 ms and was killed | **VERIFIED** | Rejected experiment measured roughly 808 ms. “Dead” is rhetorical. |
| L225a | Color popup became magenta | **NOT VERIFIED LITERALLY** | All colored regions were hue-rotated; specifically, dark-gray outlines became magenta. Rewrite with that detail. |
| L225b | Black/white hid extra/missing byte swap | **VERIFIED IN SUBSTANCE** | One RGB565 byte-swap mismatch produced the color error; black and white are invariant under swapping and masked it. Use “a byte-swap mismatch” unless the direction is important. |
| L226a | Pen-size selector also fired Redo | **PARTLY VERIFIED** | First occurrence fired Undo and the second fired Redo after a physical gesture split into multiple contacts. Use “also firing Undo/Redo.” |
| L226b | Both fired and whole UI got messed up | **VERIFIED AS OBSERVED, RHETORICALLY WORDED** | Receipt records transient chrome corruption. The fix required six no-contact polls before accepting a new gesture. |
| L227a | Export test showed two dots in SVG, none in PNG, two squares on app | **PARTLY VERIFIED / SHAPE WORDING WRONG** | Receipt records two blue dots at y=4 in SVG, absent in PNG, with raw/hard-path/glass inconsistency. “Two squares on the actual app” is not established as phrased. |
| L227b | “Schrödinger’s dot” | **JOKE / RHETORICAL** | No factual claim. |
| L227c | Render parity fix plus separate top-edge contact fix | **VERIFIED** | The SVG/PNG/render mismatch and phantom top-edge contact were distinct bugs with separate fixes. |
| L228 | Fuji classifier detour; over an hour | **VERIFIED WITH DETAIL** | Author reported roughly 60–90 minutes; about 40 minutes of logged flailing is directly reconstructable. Focused automation later succeeded, so do not imply all classification failed. |
| L229a | Tear fixed near zoom-minus then moved two-thirds down minimap | **VERIFIED** | Reverting burst pacing caused the location change. “Fixed” means stationary. |
| L229b | Last thing fixed before tearing was gone | **VERIFIED IN SUBSTANCE** | The per-strip staging/wire-deadline fix followed this diagnostic and produced glass acceptance. |
| L230 | Flash/i-cache layout moved hot-loop timing ±2–3% per build | **VERIFIED AS OBSERVED RANGE** | Scope to the measured builds/layouts. |
| L231 | 40 KB PSRAM workspace mid-heap +9 ms; dead-last 0 ms | **VERIFIED FOR THAT TEST** | “0 ms” means no measurable regression at tested precision, not a universal zero-cost allocation rule. |
| L232 | Sub-500 benchmark stopped before pixels reached glass | **PARTLY VERIFIED / OVERSTATED** | The early timer stopped at cache-ready before final display-transfer/DMA completion. Some pixels may already have changed; the result did not measure complete visible output. Its shortcut also deleted geometry. Suggested: “Our first sub-500 result stopped before the final display transfer completed—and its shortcut deleted geometry.” |
| L233 | Mathematically exact AA optimization slower on almost every case | **VERIFIED FOR THE EXPERIMENT, NEEDS NAME** | An exact analytic AA candidate was benchmarked and regressed nearly all measured cases; it was rejected. Identify the optimization or remove “mathematically exact,” which does not explain why it was slower. |
| L234a | Evil hairlines absent from supposedly good benchmark | **VERIFIED** | Earlier battery omitted the pathological dense-overlap case. |
| L234b | Adding them almost doubled cold time | **NOT VERIFIED AS A CONTROLLED CAUSAL A/B** | Earlier tapered-only 400% p95 was 0.675 s; a later combined tapered+hairline maximum opened at 1.269 s (~1.88×), but revision, statistic, and corpus all changed. Say: “A later combined evil-hairline corpus opened at 1.269 s, far worse than the earlier tapered-only result.” |
| L235 | 1×2 cold-render supertasks hit watchdog before timing result | **VERIFIED** | The combined two-tile work unit ran for about five seconds and triggered the task watchdog before producing a valid benchmark result. |
| L236 | Fast LOD deleted loops, hairpins, pressure peaks, eraser dabs | **VERIFIED AS REJECTED FAILURE** | The early simplification achieved attractive timing by discarding significant geometry/detail. It was invalidated and removed. |

## Fresh validation and evidence integrity

I configured a new Debug build with `TINYDRAW_BUILD_HOST=OFF` and `TINYDRAW_BUILD_TESTS=ON`, built it, and ran CTest. **13/13 current host tests passed** on 21 August 2026. This corroborates the current software’s tested semantics; it does not reproduce ESP32-S3 performance, physical touch behavior, panel scanout, or tear-freedom.

The recorded release values remain useful historical evidence, but their original `a5db58d` commit identity no longer resolves after a Git identity/history rewrite. The receipts survive, and current source/tests corroborate many mechanisms, but the old receipt-to-current-tree chain cannot be independently reconstructed. Evidence: `.codex-archaeology/adversarial-build-test-docs-review-2026-08-21.md`.

Primary local evidence used most heavily:

- `.codex-archaeology/README.md`, `git-history.md`, `session-history.md`, `meetup-day-timeline.md`, `docs-performance.md`, `supplement.md`, and `two-episodes-writing-memory.md`
- `docs/receipts/vector-v2/`, `docs/receipts/hardware/`, and `benchmark-results/`
- `docs/design/`, current `vector_v2/` and `esp32/` source/tests, `README.md`, `PROJECT_STATE.md`, and `puck/README.md`
- surviving raw Pi sessions under `/Users/sarah/.pi/agent/sessions/`

External primary sources were used only for hardware, camera/lens, demoscene terminology, ESP-IDF memory terminology, and the Quake III source attribution; all are linked beside the relevant claims.
