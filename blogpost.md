<!-- HISTORICAL DRAFT. Canonical working draft: blogpost-edited.md -->

# [TITLE]

I've made a vector drawing application for a Waveshare device that has a 1.8-inch AMOLED touchscreen and runs on the ESP32-S3 microcontroller is pretty fucking fast.

[VIDEO]

It all started with Steve Ruiz posting a lot about these microcontroller-driven things and the fact that agents are very good at live coding things for them now. So you don't need to know anything about embedded development. You can just point the agent at the serial port and tell them what you want and you will get it. At least up until a point I'm and then I saw that there was going to be a meetup he organized. It was only a few days before the meetup, so I ordered one from Amazon because that was the fastest way to get it. And I was thinking, okay, what to build. And I was okay, well, the obvious answer is tldraw for the thing, but surely someone has done it, or surely this was Steve's first project. And to my surprise, no, as far as I can tell, no one has done this before, not with this much detail, anyways.

So on a beautiful Sunday, a day before the meetup, I started working on this. I did not have the device yet, so I had the agent set up a development environment that targeted a QEMU emulated version of the ESP32-S3, which let me develop on my Mac without the real hardware. And I only got access to the real hardware Sunday night, just before I went to sleep.


Monday morning I was quickly faced with the issue that what was fast in the emulator was not fast at all on the device. So I spent a big chunk of Monday optimizing the raster version to be well, fast. It used Steve's Perfect Freehand library, the first version of which powered tldraw, which simulates pressure with velocity and makes lines nice.

And in the end, I had an editor, a raster-based editor that had a 3x3 screen size canvas, you could draw, you could erase, you had a couple of colors, ten levels of undo, and you could export things, export it to PNG, and it would simulate USB mass storage, simulate being a flash drive, really.


[TIMELINE — IMPORTANT: This paragraph currently conflates **meetup V1** with **later completed Raster V1**. Ten-level raster undo existed before the meetup. But the memory-saving work that made the 3×3 raster world fit and the streaming PNG/FAT16/USB export landed Aug. 11, **after** the Aug. 10 meetup. If this paragraph is describing what you brought to the meetup, you need to rewrite the feature list. If it describes where V1 eventually ended up, move/label it accordingly.  ]

[REPLY: the thing is i hacked on it AT the meetup before i demo'd it and to the best of my memory i did land these before demo time (about 7:15pm local time on august 10) but possible i committed later. or i really am fucking the timeline]

And I went to the meetup and I showed it off and it was a big success. People loved it. I saw a lot of other cool things at the meetup as well. And then I don't even remember how exactly, but I got the idea in my head that is it possible to build real TinyDraw? Infinite canvas, arbitrary zooms, vector graphics. So I'm well, I mean, agents will be pretty good at optimizing things, and I already learned somewhat that they're good at optimizing things. So probably. So the next day I started working on it, and I spent the next couple of days in a fever dream chipping away at the problem of, nominally I was trying to answer the question if this is possible, but I think in reality I never doubted it was possible, but I had to find an approach that worked, and the initial architecture did not really work after a bunch of testing. So after a couple days I had a prototype that I had to throw it out because it produced invalid/misleading evidence [ugh reword later], but on the other hand, I had the direction to go.

And that's when the real building began, or so it feels like. I've quickly duplicated the features for drawing, colors, pen sizes. I really wanted arbitrary zoom levels. This did not seem terribly feasible. So eventually I settled on power-of-two zoom levels between 25% to 400%. And because of that, the canvas is a 4x4 screen size canvas, which works out with this math quite well.
.]

## [COLD RENDERING]

So after I started building the actual version after the fever dream prototyping, the agent built me, you know, the first version where I could draw and, you know, it would render it. However, it was excruciatingly slow, truly. almost 24 seconds once you zoomed into 400% on our overlap torture testing.

And okay, this is where I need to look up the fucking timeline, but basically the agent found a bunch of optimizations that made this somewhat usable.

[PLACEHOLDER: newest-first / saturation idea]

[FACT CHECK FOR PLACEHOLDER: Newest-first replay; once newer paint/eraser has provably determined a pixel/row/tile, older hidden operations are skipped. Exact optimization, not lossy approximation. ]

And then at one point, and this is where I can probably weave in the thing, the demoscene thing, that at one point I asked a friend who's a really, really, really good software engineer, Hey, I just feel kind of stuck, and that's when she told me that, like, Oh yeah, just tell your agent this is a skill issue, and they should, you know, use mechanistic simplicity, elegance, and most importantly demoscene mindset.

[VERIFY: Your later prompts definitely contain “mechanical sympathy, elegance and demoscene mindset.” The exact wording of your friend’s original advice is not preserved in the tracked archaeology, so don’t format this as a verbatim quote unless you find the conversation. ]

[REPLY: friend told this to me aug 13, 5:45pm]

[EXPLAIN: Most readers will not know “demoscene.” One brief gloss here. Needed concepts only: demos / demoparties / compos / technical creativity under severe or self-imposed constraints / squeezing the machine because squeezing the machine is part of the point. No demoscene-history detour.]

And this actually helped a lot, because from then on we incorporated a whole host of tricks. We're using the fast inverse square root, all the way from Quake III. We, the agent found a way to put a bunch of stuff in the IRAM that saved us 6.93 to 11.68%. And we tried many other things that just didn't work.

[FACT CHECK: IRAM number now corrected. Median improvement was **8.70%** across 11 same-tree cold/settled compute cases. ]

[FACT CHECK: Fast-inverse-square-root-style trick is real. Important qualification when you explain it: it only provided a **conservative search seed**. Exact coverage still decided every pixel. Don’t say approximate rsqrt rendered approximate geometry, and don’t credit Carmack as the inventor. ]

And this happened in stages. I would work on it for a while, then work on something else and it was slow again, work on it again, and so on.

[FACT CHECK: Yes. Initial Aug. 14 campaign → panning/tearing detour → Aug. 16 cold campaign → Aug. 17 overlap fix → Aug. 18/19 IRAM/pathological closure.]

The last one was what I called evil hairlines, when even though our automated testing, which was called Battery, showed good numbers, if I, like, drew a lot of thin lines crossing each other and started zooming in, things started getting really slow again. And that was the thing that gave us the final push and tweaks to our testing suite, and eventually hit our target of each zoom level rendering under 500 milliseconds.

[FACT CHECK: Evil hairlines existed manually / in harder corpora before the final day; the late change was making your pathological drawing habit part of the **permanent torture battery**. ]

[FACT CHECK: “each zoom level under 500 ms” is too broad. The same-revision **general cold battery at 50/100/200/400%** finished at 389.942 / 383.159 / 456.961 / 492.793 ms. 25% is not part of that four-row general-cold claim. Also, 492.793 ms is not a 20-run p95; the stronger final-product reset distribution was never run.  ]

## [PANNING / TEARING]

Next thing I had to solve was panning. Initially it was just really slow around 15 frames per second, and I'm like, no, we want this faster. And then seemingly the agent was like, Oh, okay, I fixed it. It's, you know, it's 30 FPS now. And then I looked at it and tried it, and it's like, nope, looked terrible, glitched, there was a lot of tearing.

[FACT CHECK: ~15 FPS start is good: ~67.3 ms/frame. The apparent beam-race “success” was ~28.1 ms average / 32.95 ms p95, but **must never be presented as a successful product measurement** because glass falsified it. ]

That was the point I realized that the agent cannot really test this or see it. The only way to test, like panning speed, and especially the lack of tearing or glitches, is just, you know, with my fingers and eyes.

[FACT CHECK: Strongly supported. Physical feel/tearing/angularity/UI distortion repeatedly required your eyes/hands. ]

And the agent kept hammering at it. It kept not working. I kept being frustrated, and at one point I was like, Okay, like, okay, like stop. You seem like you're failing. Like step back and think.

And then the agent was like, Of course I know this was the agent, that, like, okay, I need— if you make, like, a slow-motion video of a bunch of blinking patterns, then I could determine why it's tearing and what the fix is.

[TIMELINE — IMPORTANT: There is a missing causal stage **before the Fuji request**. You stopped flailing and measured the actual panel: TE = 16.773 ms / 59.62 Hz; requested 40/50/60 MHz all turned out to be 40 MHz actual; full-frame best wall = 17.998 ms; safe full-screen cadence ceiling ≈29.4 FPS. Then GETSCANLINE + six control-register reads all returned zero, so software had no trustworthy way to know where the panel was scanning. **That** is why external optical measurement became necessary. ]

And I was like, Okay, let's see. I do have a Fuji X-T5 that can record on 240 FPS. It even has, like a— I found it in the menu, there's a flicker-free option that let me put 1/1024 for the shutter speed, which, if I remember right, that was good for this one.

[FACT CHECK: Correct. X-T5 1080/240; 1/1024 became the preferred flicker-free shutter. ]

And first the agent came up with, like, something over three minutes to record. Oh, and for Fuji, I have one lens currently, the 85 millimeter from the Sigma, or the 56 millimeter technically. I just always think in 85 millimeter equivalent. Has horrible magnification, entirely not made for this, but eventually we got to a point where the agent made a shorter loop that was only, like, 65 seconds, and I propped up the device against my laptop screen. I turned down the laptop backlight, and then I set up the camera, and I handheld over two minutes of video just in case.

[CORRECTED: 45mm equivalent → **85mm equivalent**.]

[VERIFY: I cannot find durable evidence for the “shorter loop was ~65 seconds” number. Actual supplied video was **37,920 frames ≈158 seconds (2:38) at 240 FPS**. ]

[VERIFY/MEMORY: Device propped against laptop + laptop backlight down are your current physical memories. Surviving transcript confirms handheld/no tripod + dim room, but not those exact two setup details. Fine as autobiography; don’t call them transcript-derived. ]

And I gave the video to the agent, and the agent spent way too much time on trying to come up with a classifier to find the information it needed. That didn't really work. Eventually what worked is when I was like, Okay, no, you've been at this for a while and it's not working. And the agent was like, Okay, let me try a contact sheet, and it did a contact sheet, and that gave it the answer.

[FACT CHECK: Full-video automated classifier path flailed for ~40 minutes; two 24-frame contact sheets answered the gross question. But automated analysis did not completely fail: a later focused **1,495-frame** analysis of the accepted cell found **0 tears / 0 anomalies**. ]

[FACT CHECK / EXPLAIN: Camera protocol had a deliberately unsynchronized **positive control that had to tear**. It did. The candidate rising-edge, row-zero sweep then showed the normal moving scan boundary rather than a persistent tear. This is what made the camera result meaningful. ]

And then suddenly we did have fast panning, but we still had tearing. There was a tearing at a certain spot, and I told the agent that yeah, we have this tearing at this spot. And then the agent, you know, looked at it, hammered things at it, and tried again. I'm like, Well, okay, now I have tearing at a different spot.

[TIMELINE — IMPORTANT: This needs rewriting. Actual order: **Fuji validated the simple ordered top-to-bottom sweep → first product implementation was optically much better but only ~19.9 FPS → staging compositor recovered ~29.5 FPS → then a fixed tear appeared near the top gray edge of the minus button → reverting pacing moved the tear about two-thirds down the minimap.** ]

And from these three informations, as far as I remember, the agent could deduce the last missing step to make it tearing-free, and indeed it fixed it, and now we have almost 30 FPS tearing-free panning, which I'm really proud about.

[FACT CHECK: The moving tear exposed the remaining rule: **each horizontal strip had to finish staging before its own wire deadline**. Combined with TE-rising, row-zero, top-to-bottom ordered presentation and the toroidal ring, that produced the accepted clean historical result. ]

[FACT CHECK: “almost 30 FPS tearing-free” is fair as a **historical accepted** statement. Preserved p95 ≈33.94 ms (~29.5 FPS), optically clean at 50/100/200/400%. A final same-tree release pan distribution was not retained, so avoid an exact “final release FPS” claim. ]

[PLACEHOLDER: panel facts / asking controller which row is scanning / failed GETSCANLINE / what Fuji established / final top-down + per-strip mechanism]

[NOTE: Most of this placeholder is now covered by the annotations immediately above; you still need to write it yourself.]

## [INKING]

The inking stuff was just a lot of small things. It was the decision to go from 2x2 to 4x4. I think something was a whole thing that this touchscreen samples touch way less than your average smartphone, like less than half as much as an iPhone X. So a lot of work went into that. A lot of architecture work.

[FACT CHECK — IMPORTANT: **2×2 → 4×4 is the wrong memory for inking.** The inking representation change was **quarter-world coordinate units → sixteenth-world coordinate units**, giving 4× finer coordinate resolution with the same packed storage. The 2×2 concept belongs to the cold-render producer/supertask architecture. ]

[FACT CHECK: “less than half an iPhone X” is wrong. CST820 new-coordinate rate measured **73.758 Hz**; the iPhone X comparison is 120 Hz. TinyDraw therefore gets about **62% as many samples**, or ~1.6× fewer—not less than half. ]

Inking was another thing that I needed to solve. The feel, the performance went from not too bad to okay, this actually feels good now.

[PLACEHOLDER: inking details / sparse touch samples / quantization / provisional versus committed geometry]

[FACT CHECK FOR PLACEHOLDER: Smoothing/streamline could make committed geometry visibly trail the finger. Final direction split **filtered committed/SVG geometry** from a **raw provisional fingertip visual tail**. ]

## [LLMS]

With regards to LLMs, most of the code was written by GPT-5.6. So set to usually high. It did a really good job. And I usually, when I ran into something where it just got stuck, then I would do a combination of two things.

[FACT CHECK: Safest formulation for you to write later: GPT-5.6 **Sol had the largest implementation footprint** and was the main implementation/device/integration agent. The archaeology did not perform a literal final LOC authorship audit proving a percentage. ]

One, give the entire source code, documentation, and logs to GPT-5.6 Pro and wait 60 to 90 minutes to get a very detailed code review, performance review, tips and bugs, and whatever. Either feed that to Fable or already ask Fable to see where it to take this further. And my god, Fable is both brilliant and deeply flawed in many ways, but my god when it's brilliant, it's brilliant, especially anecdotaly.

[VERIFY: 60–90-minute GPT-5.6 Pro timing is your memory. Original web sessions/tokens/settings are not recoverable from local evidence. ]

I found that when I set thinking to X high, and I was throwing architecture, and then especially performance optimization, that was just something else. For this stuff, that model is absolutely fucking incredible. I used up all the Fable quota I could squeeze out of a £200 Anthropic plan in three days or something.

[FACT CHECK: “Fable xhigh was incredible for me” = personal-experience claim, fine. “xhigh caused better code/performance” = not established by a controlled comparison. ]

[CORRECTED: Entropic → **Anthropic**; $200 → **£200** based on your later billing clarification.]

And overall I spent 4,590,968,837 processed tokens, and insert number here, but anyways £440 worth of AI subscriptions that equal to a little more than 10 times of API costs to build something purely because I wanted to see if I can build it on a $40 device.

[CORRECTED: “something just south of 5 billion tokens” → **4,590,968,837 processed tokens**. This includes cached context and excludes later archaeology/blog work plus unrecoverable GPT-5.6 Pro web usage. ]

[FACT CHECK — IMPORTANT: The “a little more than 10×” relationship is wrong. The content-attributed logged usage maps to **$4,160.59 API-equivalent**, not the older $7,337 figure. API-equivalent ≠ actual money paid. Your subscription figure is in pounds, so if you want a ratio you also need a currency conversion and must distinguish flat subscription spend from marginal/API-equivalent cost. ]

[VERIFY: $40 is fine if that is your actual Amazon purchase price; don’t present it as Waveshare’s universal/current MSRP.]

There was a point, I think a couple days in, where I just felt completely stuck. So I talked with a friend who's one of the best software engineers I know, who's probably on our own in the top 100 software engineers in the world. And I asked her for, and she knows systems programming well. And I asked her that, okay, maybe she can help because the agents just seem stuck.

[FACT CHECK: “top 100 software engineers in the world” is inherently unverifiable. Personal judgment “one of the best … I know” is not a factual problem.]

And then she was like, yeah, just tell the agents to think about mechanical simplicity and mechanical elegance and demoscene mindset. And I think it was some time after that that I realized that what I'm building is much closer to a compo for a demoparty than a fun little app for this gadget.

[VERIFY: Again, exact friend wording not preserved.]

[EXPLAIN: **compo** and **demoparty** need glossing for a non-demoscene audience before/at first use. “Compo” in particular will otherwise mean nothing to many readers.]

And building this app really became a game of okay. I wanna make this as fast as possible, I wanna get every ounce of performance out of this hardware. I'm gonna stack ten different tricks to make cold rendering faster, and I'm gonna keep throwing more agents and more prompts. Add it and we're gonna work on it until it's not painfully slow.

[CORRECTED: code rendering → **cold rendering**.]

And smaller stuff like smashing the undo several times, which is normal, should work. Same, with redo. yeah.

## [UNDO / ANTI-ALIASING]

One thing that bit me is that I made a mistake of adding undo and anti-aliasing until I optimized all the other things.

[FACT CHECK: Qualify this as **Vector V2 undo**. Raster V1 had ten-entry dirty-tile undo from Aug. 9. Device settled AA lands late Aug. 16; whole-stroke Vector V2 history lands Aug. 17. ]

And well, it turns out that if you undo something, then you have to re-render things, and you already had a whole architecture that wasn't exactly built around supporting repeated undoes or repeated undoes. So something goes here.

[FACT CHECK: Historically right as the problem you hit, but not the complete final behavior. Final architecture added copy-on-write preserved tiles, allowing many history moves to swap preserved raster state; first-descent/eviction cases can still require reconstruction. ]

The same goes with anti-aliasing. In hindsight, of course, it would have been much better to start out all the optimizations with anti-aliasing already built in some naive way. Because of that, I think it still can take up to a second to render. It's kind of subtle, but it's like the one thing I'm not quite happy with.

[FACT CHECK — IMPORTANT: Don’t causally connect these two claims without qualification. A late dense 400% **settled-AA** case measured about **946.849 ms**, but that is a different workload/stage from the general cold renderer. Evidence does not prove “it takes ~1 s because AA was added late.” ]

An interesting decision you made on Undo was to not do partial renders, but just show an hourglass waiting icon. And then again, a whole lot of optimizations to make Undo fast.

[FACT CHECK: Final behavior is more specific: hourglass for **genuine reconstruction**; preserved-tile history swaps can avoid the reconstruction entirely. ]

[VERIFY: Authorship of the hourglass idea is not settled by archaeology; find the transcript before claiming “I invented this.”]

[PLACEHOLDER: undo architecture / preserved tiles / why the hourglass]

[FACT CHECK FOR PLACEHOLDER: Safest strong device receipt on preserved path: repair **338,998 µs → 229 µs**; total **440,301 µs → 116,253 µs**. Conditional on preserved preimages surviving and matching history/timeline; not a universal 1,000× undo claim. ]

Again, the lesson that feels most alive is to serve implementing undo and being hit by the fact that oh fuck this is and the same with the entire thing.

[TRANSCRIPTION: this sentence needs reconstruction.]

[PLACEHOLDER: anti-aliasing details / late-feature consequences]

## [MINIMAP / EXPORT / MEMORY]

I added minimap. I'm really happy about that. It makes navigation easier. It makes overview easier.

[FACT CHECK: Minimap product integration is Aug. 17; several placements/input behaviors were tried/reverted. ]

Again, I love the fact that we just stack like, you know, six, seven, eight, however many optimizations on cold rendering. I like how the elegance that the file As far as I remember, it's a ledger, basically. Well, but not an app, and only a ledger. I like that the architecture is pretty good and fully understood. I should understand it more, but I can tell that it is pretty good.

[TRANSCRIPTION: “the elegance that the file … ledger” needs reconstruction.]

Again, I'm proud of, I'm proud that we hit the performance target Hit the performance targets with a lot of things. I'm proud of the minimap. I'm proud that as far as I can tell we finally got the SVG export right. That was a whole ordeal

[FACT CHECK: Yes, but SVG really did keep producing correctness bugs until Aug. 19 release closure—eraser masks/curves/seam issues among them—so “finally” is warranted. ]

I like the absurdity of yeah this is an entirely you know why would you use this for actually drawing things you you really wouldn't but it's just kind of nice to know that yeah I can build it

And again, the lesson that feels most alive is to serve implementing undo and being hit by the fact that oh fuck this is and the same with the entire thing.

[TRANSCRIPTION: same issue as above.]

The tearing thing is funny because I have a different thread with an agent Explaining me things so I understand more, and I think that agents actually misremembering how we fixed it, but never mind that. I can feed them the current documentation

I like the fact that I was persistent. I spent an ungodly amount of hours and tokens and whatever. And each time I got stuck, I just like, okay, let's just give it to a more powerful agent or tell it to try harder or tell it to have a demoscene mindset

I like the fact that I semi-accidentally created a compo for a non-existent demoparty. I kind of had a chip on my shoulder about the demoscene for a long time. I think this is insecurity, so it doesn't go in. But yeah, I like the sort of coincidental part

[EXPLAIN: By here the reader needs to already know **compo = competition/category at a demoparty** and why the analogy is about mindset rather than TinyDraw literally being a traditional audiovisual demo.]

Especially because initially I was kind of hung up that okay what I'm doing is goes against Steve's ethos but well he does not have a monopoly on ethos.

And yeah I built something that's just kind of weirdly fast and kind of silly but like It is at the end of the day a real vector graphics program that's pretty fucking fast for running on an ESP32-S3 with 8 megabytes of PSRAM where, as far as I can tell, PS is short for pretty slow.

[FACT CHECK: Joke obvious, fine. Actual acronym is pseudo-static RAM. Project hardware: 8 MiB octal PSRAM at 80 MHz; project sequential clear measurement ~36 MB/s. ]

I'm proud that we At least at the end, but we made good use of the memory. We expanded almost twice. We have almost twice as many slots for cache now. I think it was like 384. Maybe we went from 256 to 384, and then with some more memory we got to like What 448 or something?

[FACT CHECK — NUMBERS: Recorded progression is **320 → 384 → 448 → 604**. “Almost twice” works if comparing 320 → 604 (1.89×). The final 448 → 604 jump came from the export-memory correction. ]

And then I was like, okay, we have this sacred 1.5 MiB, but we already have the export feature, and the agent said it doesn't need it. So I'm like, okay, then we just use all the remaining RAM for more caching, and then if we need it for export, we can just evict the cache for that. It's a cache, we can evict it for export. I'm perfectly fine with that tradeoff.

[CORRECTED: MB → **MiB**.]

[FACT CHECK: Core memory story right. The 1.5 MiB “reserve” was synthetic. Actual measured concurrent export peak **291,484 bytes**, structural worst about **320 KiB**. Removing the fictional reserve funded **156 extra slots, 448 → 604**. ]

Again, I'm proud how well Undo works. I'm proud that we settled on a I serve no visible redraw undo and instead there is the hourglass.

[TRANSCRIPTION: “a I serve no visible redraw undo.”]

[FACT CHECK: Same hourglass nuance: genuine reconstruction only; preserved swap path avoids it when possible.]

I don't remember if an agent came up with it or I did. I think it was actually me. I think and I would need to look up the transcripts that I came up with the idea. No, yeah right, no, the agent had the idea that for shorter undos don't do anything intermediate and then for longer And then for longer undos we would show the rendering or something and I was like you might as well put an hourglass there.

I need to dig up that interaction.

[VERIFY: Yes—this is the one transcript worth digging up if you want to assign authorship.]

I've said a lot of this already But I think there are all these pieces that I'm just really proud of.

I'm proud that I kept pushing things, especially with you. Agent is like, oh yeah, this is pretty fast. I'm like, okay, so I did my evil hairlines and it's not. And then okay, back to another round of optimizations.

[CORRECTED: EVO → **evil**.]

I like that it taught me Well, I cannot quite say it taught me a lot about embedded programming, but it did. But I mean, it somewhat did.

And also, I like that I managed to exercise a skill that I kind of forgot I have, which is, when motivated, I'm very good at becoming at least somewhat proficient in a domain or area I have no experience with really fast and this is a version of that.

I think in an earlier conversation with another LM there was the angle of like, you know, LMs make getting started way way way way easier and I think that's important but then there's also the part where I like, okay but at some point after you made the fun part you're gonna run into like slow graphics, you're gonna run into tearing, you're gonna run into this and that

[CORRECTED: tiring → **tearing**.]

And yeah, you can just tell the agent to make it faster, but I think I did at least a tiny bit more than that.

## [PHANTOM DOT]

[PLACEHOLDER: hunting down where the phantom dots came from / one-sample stroke / last-day bug]

[FACT CHECK FOR PLACEHOLDER: Aug. 19 release closure. A one-sample stroke/tap was effectively a **“Schrödinger dot”**: different replay/export/render paths disagreed about whether it existed until the real journal-derived torture corpus made it reproducible. Exact dot rendering then fixed it. ]

## [ENDING]

[PLACEHOLDER: where TinyDraw ended up / final object / demo / what “I wanted to see if I could” means]

[FACT CHECK FOR END-STATE, if useful: release marker Aug. 19; general cold battery 50/100/200/400 below 500 ms; accepted historical pan ~29.5 FPS optically clean; 604 tile slots; vector SVG + settled PNG export; whole-stroke undo/redo; settled AA. Keep the caveats already noted around final pan distribution and AA being a separate ~1 s worst dense settled pass.]

## [P.S.]

[GIANT LIST OF MISTAKES — MOSTLY FUN ONES]

[FACT-CHECK CANDIDATES THAT ARE SAFE TO INCLUDE IN THE MISTAKES P.S., IN YOUR OWN WORDS:

* benchmark omitted the overlap-50 red case
* evil hairlines became formal too late
* software-green beam race physically tore
* requested 50/60 MHz were really 40 MHz
* GETSCANLINE/readback gave all zeroes
* moving scratch to SRAM predicted ≥40%, actual total-wall win 1.69%
* Wi-Fi PNG export built, then removed
* unstable 80-MHz V1 pan produced colored artifacts
* “1.5 MiB export reserve” was synthetic
* full-video Fuji classifier rabbit hole; contact sheets solved the gross question
* one-sample Schrödinger dot
* several AA “optimizations” made dense 400% substantially worse
* host wins repeatedly failed on device
* one benchmark stopped timing before final physical display transfer
* early LOD “win” altered geometry
  ]

---

The **highest-priority annotations to actually resolve in your next pass** are only these seven:

1. **Meetup V1 vs completed Raster V1** — currently the biggest chronology error.
2. **Cold-render opening number** — choose subjective “horribly slow” or the precise 23.663-second torture case; don’t blend them.
3. **Tearing middle** — panel characterization + failed GETSCANLINE must happen before Fuji.
4. **Tearing ending** — Fuji → clean-but-20-FPS → 29.5-FPS compositor → localized/moving tear → per-strip fix.
5. **Inking 2×2→4×4** — replace that idea completely with the actual quarter-world→sixteenth-world story.
6. **LLM money paragraph** — £ subscriptions vs $4,160.59 API-equivalent; kill the “10×” until you decide exactly how you want to present it.
7. **Cold <500 claim** — specify 50/100/200/400 general-cold battery, so it doesn’t conflict with the ~947 ms dense settled-AA case.

Everything else can survive into the second draft before you worry about it.

[1]: https://tldraw.dev/blog/tldraw-sdk-5.2?utm_source=chatgpt.com "tldraw SDK 5.2 – tldraw: Infinite Canvas SDK for React"
