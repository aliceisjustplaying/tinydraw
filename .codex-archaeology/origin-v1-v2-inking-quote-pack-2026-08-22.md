# Origin / V1 / V2 pivot / inking: raw-memory quote pack

Editor’s research notes, not article prose. The blockquotes are Sarah’s exact contemporaneous messages, including typos and dictation artifacts. The text beneath each quote summarizes what the agent did, measured, or concluded next.

All displayed times are local BST. The raw session files store UTC. Message IDs and line links are included so any quote can be reopened in context.

## Chronology anchors

- **August 9:** native Mac/QEMU raster V1 begins before the hardware arrives. Perfect Freehand is part of the plan in the first hour. The Mac version already reveals a large-brush performance failure.
- **August 10, morning:** first physical-board use. The hardware immediately reverses the Mac/QEMU impression: ink is slow, angular, and visibly updates by tile.
- **August 10, afternoon/evening:** the repeated finger-test/optimization loop produces a V1 Sarah loves; panning follows. At 17:11, before the meetup, Sarah asks whether the canvas should have been vector.
- **August 10, meetup:** the presentation build has the optimized raster ink, panning, Undo, and unreliable Wi-Fi PNG export. USB export, autosave, battery/RTC, and the completed 3×3 V1 arrive the following day.
- **August 11, 19:46:** explicit V2 commitment. This is the start of the 26-hour vector fever-dream section; it is the evening after the meetup, not immediately after getting home from it.
- **August 11–12:** synchronous vector rendering fails the feel test; vector strokes become authoritative and raster tiles become a disposable cache. Five-second zooms and a prototype/production muddle force a reset into a “clean production island.”
- **August 13–17:** V2 ink regains the Perfect Freehand character and V1 feel through authority-only commits, finer coordinates, streamline 0.4, and a provisional raw fingertip tail.

## 1. Origin and raster V1

### The opening instruction

**August 9, 18:08 — `43135de2` — [raw line 4](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:4)**

> “alright so we're building a micro-tldraw … i dnt have the hardware in my hand yet i will soon but we can get started … step 0 is setting up the dev env in a way that you have a tight OODA loop”

The agent created a native macOS loop, a shared C++ core, an SDL host window, sanitizers, and later a QEMU target. The founding premise was not merely “build before the board arrives”; it was “build a loop fast enough to learn.”

### Perfect Freehand was assumed, not discovered late

**August 9, 18:21 — `2915c008` — [raw line 98](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:98)**

> “we don't want to over-engineer it… you can just clone perfect freehand and just have it somewhere as reference… we wanna keep the code base small and nimble.”

The agent pinned Perfect Freehand as a reference and translated its point/outline behavior into a streaming C++ ink path. TinyDraw later changed the streaming, timestamp, state, and caching machinery, but the shaping inheritance was present from the first hour.

### The first performance humiliation happened on the Mac

**August 9, 21:09 — `e0e4e22d` — [raw line 1305](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:1305)**

> “oh things get VERY LAGGY if i choose the biggest pen an draw… and this is on my m1 pro mac”

The first implementation replayed the growing stroke and uploaded the whole framebuffer on every update.

**August 9, 21:11 — `ba8f2b05` — [raw line 1320](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:1320)**

> “will the dirty tile stroke raster help or is that orthogonal”

The agent confirmed that it directly attacked the growing-stroke cost. Streaming geometry plus a short changing tail and dirty 32×32 regions later cut the frozen 500-point host workload from 4,479 ms to 220 ms.

### Performance and infinite canvas were present before the hardware

**August 9, 22:32 — `16ea8ff8` — [raw line 1931](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:1931)**

> “i still don't have the hardware but i do care a ton about performance. especially since a stretch goal is to implement panning and infinite canvas. i guess it can't be quite infinite on an esp32 but it can be bigger than the screen. but that's all after this is buttery smooth on real hardware”

This supplies the early hierarchy: glass feel first, larger canvas second. The V2 desire did not appear after V1 was finished.

### Hardware arrives and reverses the result

**August 10, 08:22 — `8717f762` — [raw line 2590](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:2590)**

> “good morning it arrived display CO5300 touch CST820 is what i see”

This is the first surviving physical-board message. The agent switched from host/QEMU work to display and touch bring-up.

**August 10, 08:47 — `a2c8855b` — [raw line 2716](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:2716)**

> “oh god we'll need to perf optimize this drawing is slaw as fuck im not even on the xl brush. drawing getss very angle-ey very very quick”

The real board immediately invalidated the host/QEMU impression. Physical XL updates peaked at 72.9 ms and lift reached 105.1 ms; render stalls also delayed touch polling.

**August 10, 09:16 — `26687111` — [raw line 2876](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:2876)**

> “diagonal lines are still really rough in xl lagging my finger at one point one got drawn but disappeared”

Fast thick diagonals are the confirmed historical kryptonite.

### Sarah teaches the agents what the measurements are missing

**August 10, 09:45 — `b313dbb2` — [raw line 2947](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:2947)**

> “i draw the stroke and i can see the tile-updating-thingy under-my-finger”

This changed the problem from tile arithmetic alone to presentation semantics. The agent began composing coherent dirty regions before presentation and separating input sampling from rendering.

**August 10, 09:46 — `74e1de36` — [raw line 2950](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:2950)**

> “there is definitely a way of fixing this, i believe in you. step back and think a bit”

The next structural change removed the visible tile-by-tile effect and reduced long-stroke stalls.

**August 10, 11:33 — `5647b810` — [raw line 3096](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:3096)**

> “we've come a long way but i think we're still not quite there… i want to get every bit of juice out of this to make it smooth”

Ink feel was already the pass/fail criterion in V1.

**August 10, 11:50 — `4e309b8d` — [raw line 3145](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:3145)**

> “angularity correlates with speed the fasteri draw it the more angular it is. but that should not happen or it should be at least less this bad”

Telemetry showed only 19–35 distinct controller coordinates in fast gestures, versus 134 in a slow one. The controller produced new positions about every 13–14 ms; polling faster did not manufacture new points.

**August 10, 11:54 — `b02898a9` — [raw line 3167](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:3167)**

> “no change in angularity, drawing feels maybe a bit slower now… can we change the sampling rate or something or is this some hard hard hard limit of the hardware”

Stronger smoothing was reverted because Sarah felt the added lag. The work moved to reconstructing curves between sparse touch reports.

**August 10, 12:16 — `fd301ed5` — [raw line 3304](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:3304)**

> “this is a big step in the right direction… but. i see on curves some ‘whiteness’ some small white lines when they curve instead of solid color esp thicker ones”

Curve reconstruction improved the feel and exposed an antialiasing seam. The agent reproduced it at 207/255 coverage and fixed it to 255/255.

**August 10, 12:22 — `1220b465` — [raw line 3344](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:3344)**

> “i think this is the best we have so far. i still see drawing lag/drawing effect on fast diagonals idk if we can do anything about that or that is another hw limit?”

Curves averaged about 5.7 ms per update and fast diagonals mostly 5–9 ms, but Sarah still refused to treat improved telemetry as the product verdict.

### V1 crosses from acceptable to loved

**August 10, 14:28 — `dadfa65d` — [raw line 3826](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:3826)**

> “oh yeah i lov ethis update findings readme commit push… again we want to give a good demo”

This follows the sharp-turn round-join fix, tested with Sarah’s one-stroke XL “hey.” It is the clearest surviving V1 emotional payoff.

**August 10, 16:24 — `a8675bb2` — [raw line 4170](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:4170)**

> “seems good… id rather add more features save or infinite. honestly inifinite is nicer to show off no?”

The agent built a fixed raster world with a hand tool and panning while preserving the optimized draw path.

### The vector seed appears before the meetup

**August 10, 17:11 — `3f8dc44b` — [raw line 4481](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:4481)**

> “quick q while i test it. sohuld we have done a vector canvas the first place?”

The agent answered no at the time, but its answer described the architecture that eventually won: vector commands as authority, rasterized tiles as the interactive cache.

**August 10, 17:12 — `da58c522` — [raw line 4483](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:4483)**

> “oh hell yeah panning is night and day faster almost buttery smooth we are cooking”

Directly streaming the visible portion of the raster world removed most pan-frame work.

### The deadline arrives

**August 10, 17:17 — `ecea33a3` — [raw line 4505](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:4505)**

> “ok we have... not a lot of time. its 5:15pm i am arriving to the event at 6pm… no save but export over wifi…”

The presentation build ended with unreliable Wi-Fi PNG export. USB export and the completed 3×3 V1 belong to August 11.

**August 10, 18:40 — `a9c346b2` — [raw line 4893](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:4893)**

> “is this pressure sensitive? and how can i explain ppl how i made it fast?”

The agent prepared the demo explanation: simulated pressure from speed, streaming old geometry, dirty tiles, memory placement, second-core touch, DMA, direct panning, and dirty-tile Undo.

### Completed V1 hands off to V2 the next evening

**August 11, 19:45 — `6b510c49` — [raw line 8045](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:8045)**

> “yeah it works. update readme merge to main commit push bc we are starting something a lot more exciting next”

At this point Raster V1 had the 3×3 world, autosave, battery/RTC, and USB PNG export. The explicit V2 commitment came two minutes later.

## 2. The V2 pivot and fever dream

### Commitment

**August 11, 19:46 — `721b54f2` — [raw line 8063](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:8063)**

> “alright. check @V2_INITIAL_SPEC.md we're making this a real infinite canvas by god we are doing this.”

The agent began Phase 1 immediately. The initial-spec commit followed at 19:47:41; the first vector-code commit at 19:52:09.

### Sarah repeatedly arrests agent momentum to recover the actual question

**August 11, 20:37 — `ae121db9` — [raw line 8420](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:8420)**

> “please step back and think it feels like you're flailing. worth stepping back and thinking at these times. you got this.”

The agent acknowledged that it had changed too many variables, restored known-good firmware, and switched from speculative optimization to diagnosing the exact reset.

**August 11, 21:29 — `1f039f30` — [raw line 8682](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:8682)**

> “let's stop for a sec and step back. you've made a lot of optimizations so far which is really cool. but this is sort fo a pass/fail exercise in that it is either feasable to create the vector version or not.”

The agent returned a conditional pass: vector storage was viable; full rebuilding during interaction was dead; incremental raster caching was mandatory.

### Physical feel overturns the model

**August 11, 22:12 — `d7019ae2` — [raw line 8920](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:8920)**

> “can you double-check? if it does that's fine. but the existing raster fan sure feels fast to me”

This objection overturned the agent’s display-floor extrapolation. Measurement found raster pan at 25.45 ms and synchronous vector pan four to eight times slower, producing the vector-authority/disposable-raster-cache split.

### Context loss and conceptual cleanup

**August 11, 22:26 — `5d70ae75` — [raw line 4](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-11T21-25-39-963Z_019ff2b7-7ffb-7360-bc57-0e0b7b448998.jsonl:4)**

> “okay. christ. compaction failed bc i switched models lost context. uh. this was the last of it”

Sarah pasted the previous architectural result into a successor session. The next agent reconstructed state from repository evidence. This is confirmed context loss, not retrospective compression.

**August 11, 22:28 — `9b87fc15` — [raw line 56](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-11T21-25-39-963Z_019ff2b7-7ffb-7360-bc57-0e0b7b448998.jsonl:56)**

> “do we even need 3x3? like we are doing an infinite canvas after all. the 3x3 was for raster. or?”

The agent separated the infinite logical document from the finite movable raster cache. The cache dimensions became an implementation choice, not the world’s meaning.

### The glass falsifies another encouraging report

**August 11, 23:07 — `c0fa1ebc` — [raw line 186](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-11T21-25-39-963Z_019ff2b7-7ffb-7360-bc57-0e0b7b448998.jsonl:186)**

> “i tested visually 50% and there are many cache misses there too. … switching between the zoom levels takes five seconds. That's a lot. I'm happy to put more work into this if there's a good chance that this can be something.”

Cold-cache failure was general, not a pathological zoom corner. The agent stopped treating a five-second pause as something presentation tricks could hide.

**August 11, 23:12 — `ef35b951` — [raw line 199](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-11T21-25-39-963Z_019ff2b7-7ffb-7360-bc57-0e0b7b448998.jsonl:199)**

> “I put in quite a bit of time and money and tokens in this vector version already, and if [it] can somehow be pulled off, that would be incredible, but … I need to know if it's worth investing more time and money into it.”

The agent recommended one focused additional investment. Undo, persistence, and production integration were deferred until interaction had been proved.

**August 11, 23:16 — `ab791086` — [raw line 201](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-11T21-25-39-963Z_019ff2b7-7ffb-7360-bc57-0e0b7b448998.jsonl:201)**

> “A brief pixelated zoom is fine, but brief is under half a second, not several seconds. That's not good user experience.”

This is the user-authored performance boundary and acceptable compromise: temporary ugliness was allowed; several-second waiting was not.

### A bounded five-day bet

**August 12, 08:37 — `2786f6a8` — [raw line 314](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-11T21-25-39-963Z_019ff2b7-7ffb-7360-bc57-0e0b7b448998.jsonl:314)**

> “let's not get too bogged down on bit-exact. eventually it needs to be bit exact but we can take **slight** liberties there for sufficient performance gains … yes, spend the 5 days, i'm happy to spend those 5 days. create a new branch … and let's get started on this. work as autonomously as possible”

The agent created the decisive prototype branch with Undo/New, persistence, and export explicitly out of scope. This is the clearest contemporaneous persistence quote: a bounded bet after a night of doubt.

### Fast is not the same as correct on the glass

**August 12, 10:54 — `813bdd5c` — [raw line 888](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-11T21-25-39-963Z_019ff2b7-7ffb-7360-bc57-0e0b7b448998.jsonl:888)**

> “going from 50% to 200% also very fast i think but now i see a lot of very pixellated lines that should... not be the case... this is supposed to be vector?? am i missing sth OH OKAY ONCE I START PANNING IT SHOWS THE RIGHT IMAGE”

The prototype’s internal metrics and progressive-rendering story did not excuse visibly wrong publication. The agent classified these as real prototype bugs.

### The reset

**August 12, 21:56 — `35775812` — [raw line 114](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-12T20-46-27-703Z_019ff7b9-f777-7cda-b390-3e9e14444f44.jsonl:114)**

> “before we change anything else i really feel like we're already getting lost in the woods. i asked for a second set of eyes”

The rest of this raw message is pasted review text, not Sarah’s wording. The agent agreed that the supposedly production branch still contained the retired prototype and stopped before hardening the wrong architecture.

**August 12, 22:00 — `a33904fa` — [raw line 131](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-12T20-46-27-703Z_019ff7b9-f777-7cda-b390-3e9e14444f44.jsonl:131)**

> “clean production island inside this repository — yeah let's do that.”

The agent established independent production modules and tests with no dependency on the retired 3×3 coordinator. The goal survived; the prototype architecture did not.

## 3. Inking: from inheritance to product criterion

The first nine inking beats are already contained in the V1 section: Perfect Freehand as the obvious basis; largest-pen host lag; dirty tiles; catastrophic physical ink; fast XL diagonals; visible tile updates; “every bit of juice”; speed-correlated angularity; and the “I love this” V1 payoff. The following quotes cover the V2 regression and recovery.

### Early V2 does not yet have the finished ink character

**August 13, 08:15 — `dec643c8` — [raw line 3239](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-12T20-46-27-703Z_019ff7b9-f777-7cda-b390-3e9e14444f44.jsonl:3239)**

> “do we have the perfect-freehand style things in the drawing already?”

The early V2 capture build displayed fixed-width raw lines. The agent said production still needed smoothing, simulated pressure, thinning, and curved ribbon geometry. This supports “early V2 temporarily lacked the Perfect Freehand feel,” not a claim about the literal first commit.

**August 13, 08:34 — `2e815509` — [raw line 3329](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-12T20-46-27-703Z_019ff7b9-f777-7cda-b390-3e9e14444f44.jsonl:3329)**

> “for the next test we should have l and/or xl brushes because that shows the perfect freehand stuff way better”

The next capture used the real processed centerline and changing simulated-pressure radius. Sarah’s hostile tests were also demonstrations of what the technology was supposed to preserve.

### The long-stroke failure becomes concrete

**August 14, 12:36 — `c6e35430` — [raw line 291](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-14T10-07-47-932Z_019fffbd-f8dc-7972-aa29-d37429e79eb1.jsonl:291)**

> “i did a very very long thin hairline and it just disappeared… that's really bad”

The trace campaign separated finger-off-screen ambiguity, hard failures, and periodic commit stalls.

**August 14, 12:45 — `46f785b4` — [raw line 301](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-14T10-07-47-932Z_019fffbd-f8dc-7972-aa29-d37429e79eb1.jsonl:301)**

> “nothing disappeared, but drawing stopped and I'm unsure if my finger just ran off the screen and it stopped because of that or I hit some sort of limit.”

The symptom is contemporaneous; the later remembered numeric “10–24 chord cap” is not. Evidence instead found an every-64-samples defensive tile-copy pause of roughly 70 ms.

**August 14, 14:53 — `500fd477` — [raw line 6](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-14T13-50-13-181Z_01a00089-9abd-72a0-9faa-ee5a36f4e6c2.jsonl:6)**

> “our goal is basically smooth long strokes that does not have 70ms delay which is way too much… mechanical sympathy and elegance, demoscene mindset are the keywords here.”

The agent replaced defensive copy-out/copy-back chunks with validate-first, in-place commits. Worst long-stroke chunks fell below 15 ms.

### Live ink becomes the explicit top priority

**August 14, 19:00 — `4b5b30a1` — [raw line 114](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-14T17-48-49-844Z_01a00164-0f34-7c8e-8fbe-3c24b8767132.jsonl:114)**

> “one drawing should always be the lowest latency. That's by far number one”

Cold-render optimizations had begun to hurt live ink. This priority eventually produced authority-only commit and background cache absorption.

**August 15, 22:40 — `4a3e7c14` — [raw line 407](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-15T19-28-19-209Z_01a006e5-8109-7f08-85ca-ae3f19b8d435.jsonl:407)**

> “we haven't touched ink speed at all, and that was one of the biggest regressions. When does that happen?”

Panning was approaching closure, but Sarah refused to let that result bury the ink regression.

**August 15, 22:59 — `8082c577` — [raw line 421](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-15T19-28-19-209Z_01a006e5-8109-7f08-85ca-ae3f19b8d435.jsonl:421)**

> “Uniform with SVG expert? Absolutely no. No, that sounds horrible The whole point is to have perfect freehand”

The agent preserved exact variable-width geometry across display and SVG rather than exporting simplified centerlines.

### Metrics turn green before the glass does

**August 16, 20:00 — `8c18ed8b` — [raw line 25](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-16T18-55-33-408Z_01a00bed-de20-7636-ad9e-6b38abb9fe17.jsonl:25)**

> “the drawing lag was a visible lag. it's unacceptable.”

The agent stopped treating an internal green metric as closure. Authority-only commit reduced worst mixed-draw append from 19,324 µs to 173 µs; cache work moved into interruptible idle absorption.

**August 16, 21:29 — `f520a5fb` — [raw line 467](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-16T18-55-33-408Z_01a00bed-de20-7636-ad9e-6b38abb9fe17.jsonl:467)**

> “It feels fine. I don't know how much I'm biased knowing that we have sub-millisecond numbers versus reality… I know it's faster… very happy for that”

This is a self-aware positive result, not final closure. Subsequent traces still exposed 166–184 ms polling gaps.

### Antialiasing is not the angularity fix

**August 16, 21:47 — `b9f48aa3` — [raw line 563](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-16T18-55-33-408Z_01a00bed-de20-7636-ad9e-6b38abb9fe17.jsonl:563)**

> “AA helps, and looks nice and ideally we should have AA. but AA does *not* fix jaggedness optically. and what i do not understand is why the raster version doesn't have the jaggedness”

The agent compared V1 and V2 against a floating-point reference. V2’s quarter-world coordinate quantization, not absent AA, caused the careful-stroke zigzags.

**August 16, 21:55 — `47009da1` — [raw line 600](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-16T18-55-33-408Z_01a00bed-de20-7636-ad9e-6b38abb9fe17.jsonl:600)**

> “The fix is embarrassingly cheap: sixteenth-world units … let's try it and see if it causes regressions and if yes how much”

Sixteenth-world samples replaced quarter-world samples in the same storage. At 400% zoom, quantization improved from one screen pixel to one quarter pixel; joint p95 improved 30–40 percent.

**August 16, 22:26 — `2f376d13` — [raw line 735](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-16T18-55-33-408Z_01a00bed-de20-7636-ad9e-6b38abb9fe17.jsonl:735)**

> “Yes, it's much better. Honestly, I would like it to be even less angly… Definitely a big improvement.”

This is the finger-on-glass verdict after finer coordinates.

### Smooth saved geometry without a trailing fingertip

**August 16, 22:40 — `ad23bc4e` — [raw line 806](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-16T18-55-33-408Z_01a00bed-de20-7636-ad9e-6b38abb9fe17.jsonl:806)**

> “I do like that it's much smoother… As long as nothing lags and I do not see any lag, I think this is a keeper”

**August 16, 22:41 — `2e343cec` — [raw line 813](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-16T18-55-33-408Z_01a00bed-de20-7636-ad9e-6b38abb9fe17.jsonl:813)**

> “wait if what i'm notific is trailing from streamlining that is bad”

Streamline 0.4 improved curves but let the filtered endpoint trail the raw touch. The fix extended only the replaceable visual tail to the raw fingertip; committed and SVG geometry remained filtered.

**August 17, 11:55 — `2d6aa469` — [raw line 1564](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-16T22-55-23-236Z_01a00cc9-7064-7d5d-a31a-fe9aa2c61184.jsonl:1564)**

> “drawing at 400% feels laggy idk if its changing 0.35 to 0.4… i dont love this lag at all”

**August 17, 11:56 — `01604b6c` — [raw line 1582](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-16T22-55-23-236Z_01a00cc9-7064-7d5d-a31a-fe9aa2c61184.jsonl:1582)**

> “is it possible to keep it at 0.4 *and* not have lag?”

These two messages state the desired split in Sarah’s terms: smoother durable geometry without visible fingertip delay. The controlled capture later retained 0.4 plus the raw provisional endpoint.

### Practical closure remains qualified

**August 17, 13:12 — `2115d64f` — [raw line 2018](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-16T22-55-23-236Z_01a00cc9-7064-7d5d-a31a-fe9aa2c61184.jsonl:2018)**

> “I'm gonna draw a circle Visual, it's quite good… Do a long stroke that seems reasonably fast… the only one that lags a little bit is diagonal stroke in Excel, but honestly that's a lower priority thing”

“Excel” is speech recognition for XL. Ordinary circles, hairlines, long strokes, and thick strokes were accepted; mild diagonal XL lag remained.

**August 18, 20:13 — `8aeabae0` — [raw line 5](/Users/alice/.pi/agent/sessions/--Users-alice-src-tries-2026-08-09-espdraw--/2026-08-18T19-04-33-169Z_01a01642-d291-7ec1-aab8-148afee95b9e.jsonl:5)**

> “Inking is in a pretty good place, although I think starting strokes sometimes feels a bit laggy.”

The ending available in the logs is satisfaction with a reservation, not a claim of perfection.

## 4. Mechanism crib notes for retelling in Sarah’s own prose

These are explanatory notes, not proposed article sentences.

- **Perfect Freehand:** supplied the stroke-shaping lineage: smoothing, simulated pressure from motion, thinning, curves, joins, and caps. TinyDraw reworked it into streaming ribbon pieces that could be committed and cached incrementally.
- **V1 streaming:** stable ribbon pieces were committed while a short provisional tail could still change. Dirty tiles prevented the whole stroke from being replayed for every new touch sample.
- **Sparse touch:** the controller supplied fresh coordinates only around every 13–14 ms. Fast motion therefore exposed straight chords between sparse samples; a faster polling loop did not create missing coordinates.
- **Vector authority:** V2’s ordered stored strokes became the source of truth. Raster tiles were disposable pictures derived from those strokes.
- **Authority-only commit:** record each new stroke chunk quickly, show it through an overlay, and let the raster cache absorb it later in interruptible idle work.
- **Sixteenth-world coordinates:** fixed zoomed-in stair-stepping without increasing storage.
- **Streamline 0.4 plus raw fingertip:** the filtered path produced the smoother saved stroke; the transient display tail reached the actual fingertip, then converged to the filtered geometry on lift.
- **Settled antialiasing:** improved edges during idle time. It did not solve an angular centerline.

## 5. Emotional material already present in the raw record

Again, these are editorial labels, not drafted article language.

- **Desire precedes possession:** “we're building a micro-tldraw”; “i still don't have the hardware”; “infinite canvas” as a stretch goal.
- **The emulator-to-hardware reversal:** “slaw as fuck”; “very angle-ey”; a drawn stroke disappears.
- **Sarah as the sensory instrument:** “tile-updating-thingy under-my-finger”; “angularity correlates with speed”; “drawing feels maybe a bit slower”; “whiteness” in curves.
- **Belief under deadline:** “there is definitely a way”; “every bit of juice”; “i lov[e] this”; “we are cooking.”
- **The seed and the vow:** “should we have done a vector canvas”; then, the next evening, “by god we are doing this.”
- **Persistence with boundaries:** pass/fail feasibility; money and tokens; five more days; brief pixelation acceptable, five seconds unacceptable.
- **Refusal of false victories:** the raster version “sure feels fast”; metrics green while lag is visible; zoom fast while the picture is wrong; panning fixed while ink is still regressed.
- **Regaining control:** “lost in the woods”; a second set of eyes; “clean production island.”
- **What had to survive V2:** “The whole point is to have perfect freehand”; drawing is “by far number one.”
- **Qualified pride:** “very happy”; “much better”; “quite good”; the remaining diagonal XL and stroke-start weaknesses are acknowledged rather than erased.

## 6. Memories that are not contemporaneously preserved in these development logs

These can still be used as autobiographical memory; they should not be presented as chat-log quotations or mechanically established chronology.

- Following Steve’s tiny-device posts, seeing the meetup announcement, and deciding to present.
- “Surely someone has built this” / “surely it was Steve’s first project,” including scrolling Steve’s timeline.
- Ordering the board from Amazon. The logs establish only that the hardware was absent on August 9 and first identified on August 10 at 08:22 BST.
- Steve being astonished, pointing at Sarah, and saying “You first.”
- Audience reaction, people passing the device around, post-talk socializing, and arriving home.
- The exact live presentation content. A prepared guide survives, but the logs do not record which beats were delivered.
- “I knew deep in my heart it was possible,” the Don Quixote comparison, and “throw it all out/start from scratch” are retrospective emotional descriptions, not verbatim development-log lines.
- The explicit V2 vow is the next evening, August 11 at 19:46 BST. The raw reset language is “lost in the woods” followed by “clean production island.”
- A hard “10–24 chord” cap is unsupported. The confirmed nearby mechanism is an every-64-samples defensive tile-copy pause of roughly 70 ms.
- An early V2 stage lacked the polished Perfect Freehand path. The logs do not prove that the literal first-ever V2 commit lacked every Perfect-Freehand-derived component.

## Editorial reading

The raw material supports four large movements without inventing an emotional arc:

1. **Wanting the object before possessing it:** the project already has an identity, a feel standard, and an impossible stretch goal while it exists only on a Mac.
2. **Glass as the recurring adversary:** each plausible result is tested by a finger and often overturned. Sarah is not merely prompting agents; she supplies the sensory judgments that redirect the engineering.
3. **V2 as persistence with judgment:** “by god” is followed by repeated pauses, feasibility gates, financial/time boundaries, outside review, and a production reset. The commitment is to the outcome, not to preserving a failed implementation.
4. **Inking as the through-line:** Perfect Freehand connects the first-hour premise, the best of V1, the V2 regression, the architectural recovery, and the final qualified acceptance. It is both a technical subsystem and the clearest physical measure of whether the drawing program feels real.
