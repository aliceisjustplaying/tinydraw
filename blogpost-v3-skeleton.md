# V3 SKELETON — assembly instructions
#
# RULES OF THIS FILE
# - Every quoted line below is YOURS (draft, your notes, the Janka call, today's
#   voice message, Thursday talk). Sources tagged: [draft] [notes] [call HH:MM]
#   [today] [talk].
# - Lines marked ✎ are editor notes — never prose, never for publication.
# - Your job per slot: keep / rewrite / kill the quarried lines, and write the
#   connective tissue in your own voice. Write in SECTION-SIZED passes, not
#   sentence surgery. Then read the whole thing top to bottom (glass test the
#   draft — early and often, unlike last time).
# - Standing constraints (from your own 8/21 handover — confirm they still hold):
#   no cursing in final prose, no token/money bragging, insider baseball is fine,
#   bragging tuned to American levels.
# - No trials rule: if a sentence feels wobbly, we ask only "did this happen?"
#   and "do you actually want to say this?" — then keep, mark, or cut. No audits.
#
# TARGET: ~2,500–3,500 words. Twitter article. Audience: vibe coders, ESP32
# hackers, AI power users, and (quietly) people who might hire you.

---

## 0 · TITLE (workshop last, decide family now)

✎ Chosen family: "don't entirely know how it works" — utilitarian pick, it IS the center.

- "How I made a vector drawing app [that's very impressive] and i still don't know how it works entirely" [notes]
- "I made a fast vector drawing app, but I don't know how it works" [call 03:03]
- "Not entirely. Like I, I know some parts, just not—" [call 03:04] ✎ ← the honest qualifier; "don't *entirely* know" is the accurate and stronger version
- Rejected but maybe subtitle material: "How to make a vector drawing app that's fast enough" [call 01:02]

---

## 1 · VIDEO + TRY IT

[VIDEO — demo mode split-screen, you know the plan]
[Try it in your browser on Puck →]

✎ No prose needed here beyond a caption. Coda setup opportunity: the Puck build
  exists because you "formally wrapped" on Thursday — see slot 12.

---

## 2 · HOOK

✎ JOB: One short paragraph. The absurdity + the Doom-drive. Do NOT explain the
  whole post here. Runway rule: no definitions, no throat-clearing.

- "I spent nine days building a vector graphics editor on a $40 microcontroller with a 1.8-inch touchscreen. I knew it was possible. I wanted to see how I could make it." [draft] ✎ you flagged: content fine, sentence "eh". Also the call revealed the real frame is below ↓
- "most of the work was optimizing to make it work well in the first place" [call 01:10]
- "modern hardware lets you get away with a lot, this hardware does not" [notes, via Janka — you adopted it]
- "this is actually a version of making Doom or Quake run... except instead of Quake, I built a vector graphics editor" [call 01:09]
- "doom-like drive" [notes]
- "This project turned into how far I can take this, how much I can get out of this microcontroller." [talk]

✎ DECIDED: cold open right after the hook = "This is a story of project
  management." — verbatim, its own paragraph or first line of slot 5. [today]

OPTION — Graveyard as hook closer (or move to slot 4):
- "In August 2026, every software engineer has a giant graveyard of vibe-coded things they started and never shipped. I have it. Most people I know have that. And I'm shipping it." [draft]

---

## 3 · ORIGIN — V1, the meetup

✎ JOB: Why this project captured you. Keep it moving; this is still intro. The
  motivation stays light and confident — community/demo energy, no
  psychoanalysis on the page.

- "I've been following Steve Ruiz on Twitter... he kept posting all these cool projects built on microcontroller-powered tiny little devices. And then I saw there was going to be a whole meetup about them, and I wanted to go, and I'm like, well, if I'm going, I want to present something." [draft]
- "A mini tldraw seemed like the obvious answer, but surely someone has done it. Surely this was Steve's very first project. So I scrolled his entire timeline and I did not find a mini tldraw at all. So I was like, okay, I guess we're doing it." [draft]
- RECEIPT — earliest prompt found so far, Sunday 8/9 5:31 PM
  [.pro/ChatGPT-Tiny tldraw on ESP32-S3]: "can we make a tiny tldraw where you
  draw w your finger a few colors and also it somehow adopts steve's algo for
  hand drawing as best as possible" ✎ blockquote candidate — the project's
  birth certificate, typos and all. ⚠ CAVEAT per Sarah: verify against Codex/Pi
  logs whether a local-agent session predates this Pro conversation before
  calling it "the first prompt."
- Device late, dev env before hardware (macOS native + QEMU), Monday: "drawing was extremely slow. My circles had way too many angles." [draft]
- "Over the day, we fixed performance. We made our curves a whole lot more curvy." [draft]
- "there was no order yet, and Steve just pointed at me: You first." [draft]
- "people came to me to try it, and that was nice." [draft]
- V1 inventory sentence: "a raster-based editor that had a 3×3-screen-size canvas... ten levels of undo... simulate being a flash drive, really." [draft]
- Device flavor: "it has everything and the kitchen sink, which makes it very versatile" [talk]
- Confident close, American-calibrated: "I demoed it, and it was a big success, and I loved it." [talk]

---

## 4 · THE QUEST — V2 + fever dream

✎ JOB: The turn. "Actually, I can make this better." Ambition stated plainly
  (American calibration). Fever dream compressed to a beat, not a section.

- "The next evening: alright. Check V2_INITIAL_SPEC.md. We're making this a real infinite canvas. By god, we are doing this." [draft]
- "almost immediately the day after, I was like: okay, so how far can I take this?" [talk]
- "A mini 'real' tldraw: vector, arbitrary zoom levels, really fast. The agents tempered my expectations, and they were right." [draft]
- "I spent the next 26 hours in a fever dream chipping away at the problem... I barely know what the agents are doing, but I'm just gonna keep going." [draft]
- "After 26 hours, I had a prototype that I had to throw out. But on the other hand, I had the direction to go." [draft]
- "It was funny that I was always more sure that this project was viable than the agents... This will work. I know this will work." [call 00:34]
- Historical intuition: "vector drawing apps are not a new thing... people even in the '80s made vector drawing apps... this thing is at least as fast or faster than old computers." [call 00:33]
- Graveyard fragment lands well here too (see slot 2 option).

---

## 5 · THE CENTER, STATED EARLY — what I actually did

✎ JOB: One tight section that gives the post its spine BEFORE the episodes, so
  every episode reads as evidence. This is the project-management thesis. Keep
  it confident — this is the "what do programmers do now" answer, not an apology.
✎ REFRAIN: you want one (right instinct — your raw posts run on refrains).
  Receipts are already mined in .codex-archaeology/origin-v1-v2-inking-quote-pack
  ("Sarah repeatedly arrests agent momentum...", "The glass falsifies another
  encouraging report", "Metrics turn green before the glass does"). Candidates:
  · stop-and-regroup family — receipted 8/11 20:37 ("please step back and think
    it feels like you're flailing... you got this."), 8/11 21:29 ("let's stop
    for a sec and step back"), 8/12 21:56 ("we're already getting lost in the
    woods. i asked for a second set of eyes"). Your lean.
  · glass test as recurring noun — organic, already in every episode.
  · ★ "YOU GOT THIS" — discovered in the receipts: it's BOTH your launch
    sign-off (8/12, 8/14 prompts) AND your rescue line (8/11 20:37). One phrase,
    two jobs: pushing them and catching them. And today's assembly prompt ends
    "good luck you go this" — the refrain's final occurrence, typo'd, already
    sitting in the coda. If planted deliberately at episode ends, the coda
    closes it for free. Option, not verdict — feel it before choosing.
  One refrain done cleanly beats two done halfway.

- "this is a story of project management" [notes]
- "I'm the finger, I'm the eyes... I'm the persistence." [call 00:33]
- "me: finger, eyes, certain level of understanding, persistence, historical intuition, setting a direction, discernment (which agent, when to stop and think), looking at it from a meta level, conjuring up the right kind of guy, UX, taste, worked hard, asking how to make this better, effort, passion" [notes]
- "agents: write all the code, find optimizations, review each other's code, help with architecture" [notes]
- "They wrote all the code. I never touched a single line." [draft]
- "i'm also the QA / i'm the evaluator/judge" [notes]
- "managing agents with tunnel vision, very bad at telling me no -> more responsibility" [notes]
- "if this was a team of programmers it'd be very dysfunctional" [notes]
- "just because you're a software engineer it doesn't mean you're a good project manager" [notes]
- "everyone who vibe codes... everyone is forced to be a project manager" [call 02:50]
- "Ultimately this is vibe coded. It's just vibe coded with an engineering mindset." [draft]
- Glass test intro: "They don't have fingers. The agent would be like, 'this is running at 30 FPS.' And then I do a glass test, and I'm like, no, it's not, and it's glitching." [draft]
- ✎ OPTIONAL, one sentence max: vibe coding before the term existed — "I was
  getting models to write me working C in spring 2023, a language I can read
  but don't really know" [today, paraphrase — yours to rewrite]. State it, move
  on; no insistence, no credit-claiming.
- "When I felt lost, that was the signal to stop and regroup." [draft]
- The beginner thesis, cleanest version yet: "if you don't know much about coding for embedded — which I didn't until a little over a week ago — LLMs get you very quickly to a point where it used to take a lot more. But eventually you get to a point where you start optimizing, and then it starts taking time." [talk]

✎ The Sol/Fable/Pro casting paragraph from [draft] ("What the Agents Did") can
  live here or stay its own short section — your call when you read the flow.
  Includes the demoscene-moment scene:
- "she told me: just tell your agent this is a skill issue... mechanical sympathy, elegance, and most importantly demoscene mindset" [draft]
- "'Surprising fraction of how I use Claude well is conjuring up the right guy from latent space.' 'Who would absolutely crush this.' 'Ok Claude go be that guy.'" [draft]
- "In hindsight, the whole project was very demoscene-y from the start. I just wasn't conscious about it until she said it." [draft]

---

## 6 · EPISODE 1 — inking, glass tests, evil hairlines

✎ JOB: The feel of the loop: agent claims fast → your finger says no → repeat.
  Evil hairlines full; the rest of inking compressed hard. The 111×/vector
  authority stuff appears ONLY at the depth you actually hold it (see slot 10 —
  that's allowed now, it's the thesis).

- "I would draw lines really fast. And I would see the chords show up, which I should not be seeing... I just need this to feel fast." [draft]
- "One drawing should always have the lowest latency. That's by far number one." [draft]
- Evil line story: "I did a very, very long thin hairline and it just disappeared... I switched colors, and suddenly the evil line that seemingly stopped disappeared. It was a 1,024-sample cap." [draft]
- "Once again, the agent thought something was fast, but the glass test disagreed." [draft]
- Evil hairlines, your words today: "my manual way of stress testing the whole thing is picking the thinnest pen and drawing a shit ton of lines, usually in one go, overlapping each other, because I knew that that's the nightmare scenario for rasterization and caching, because it's just so random" [today] ✎ decurse
- "it wasn't incorporated [into Battery] until the last day" + "it also exposed a bunch of inking issues, like when I drew for over a minute and it would stop" [today]
- "Evil hairlines absent from the supposedly good benchmark. Almost doubled cold time." [draft P.S.]
- Cold-render numbers compressed to one beat here or in Landing: 24s naive → under 500ms, 18 tricks stacked, 22 rejected experiments. [draft]
- Touch rate constraint if you want it: "This controller samples touches about 38% less often than an iPhone X." [draft]

---

## 7 · EPISODE 2 — the Fuji night (cinematic heart, full length)

✎ JOB: The best scene in the project, told as a scene. NEW: you can now narrate
  the "why it helped" honestly. Editor briefing (not prose — receipts in
  .codex-archaeology/session-history.md §7):
  · Software couldn't be trusted: GETSCANLINE + all control reads returned zero,
    so no software beam oracle existed; software "sync" could self-report success
    while the glass tore.
  · The 240fps camera was an instrument — the only eye faster than the display.
  · The protocol included a positive control: one deliberately unsynchronized
    pattern that MUST tear on camera, or no clean result counts. It tore.
  · Verdict: the SIMPLE strategy (wait for rising edge, start at row zero, sweep
    top to bottom) was optically clean at 29.4 FPS. The footage killed the
    complicated beam-racing machinery — that's what it "fixed."
  · Same night: the real app reintroduced a fixed tear (minus button → minimap
    when things changed). Those moving-tear clues you reported identified strips
    that stage slower than the wire; per-strip staging-before-wire fixed it.
    Glass-clean 23:41 the same evening. The timeline was genuinely one night.

- "the agent cannot really test this or see it. The only way to test panning feel... is just with my fingers and my eyes." [draft]
- "okay, stop. You seem like you're failing. Step back and think." [draft]
- "if you make a slow-motion video of a bunch of blinking patterns, then I could determine why it's tearing and what the fix is" [draft]
- Camera setup paragraph [draft] — keep nearly whole, it's great: Fuji X-T5, 240 FPS, flicker-free 1/1024, Sigma 56mm ("I just always think in 85 millimeter equivalent"), "horrible magnification, entirely not made for this", propped against the laptop, two minutes handheld "just in case".
- Classifier detour: "the agent spent way too much time trying to come up with a classifier... Eventually what worked is... okay, let me try a contact sheet, and it did a contact sheet, and that gave it the answer." [draft]
- ✎ Contact sheet needs a one-line definition in YOUR words (Janka flagged it; you explained it well on the call): "take every X frames of a video and put them on a single page — analog photography term" [call 00:27, paraphrase yours to rewrite]
- Moving tear: "There was tearing at a certain spot, and I told the agent that... well, okay, now I have tearing at a different spot. And from these pieces of information, the agent could deduce the last missing step... and indeed it fixed it." [draft]
- "Now we have almost 30 FPS tearing-free panning, which I'm really proud of." [draft]
- P.S. cross-references: "Requested 40/50/60 MHz SPI. All actually 40 MHz." / "GETSCANLINE and every control-register read: all zero." [draft]

---

## 8 · EPISODE 3 — undo, the hourglass (full length)

✎ JOB: The PM lesson episode. "Undo is a redraw" = the cost of not thinking the
  broad picture through + a team that never says no. Ends on the UX save.

- "it hit me that, well, it sounds very trite, but an undo is a redraw, and redraws are very expensive." [draft]
- Sharper version: "an undo and a redo is a redraw. You have to redraw the screen, and when it's all vector, this is an awful lot of computation." [talk]
- "it was kind of ill-advised to leave undo this late... would I have had probably less pain? Possible." [draft]
- Team angle: "i got myself in a situation where i didn't think thru the broad picture how undo would fit into the app... and then re-do a bunch of work bc my team 'didn't tell me about it'" [notes]
- "you were watching this line render in the middle of the screen. It just looked absolutely horrible." [draft]
- Hammering: "you can hammer undo and redo and it will not mess it up." [draft]
- Hourglass decision: "I did not like the UX discrepancy. So I just said that we can just show the hourglass for even the short stuff. Just flash it and it's gone and it's fine. It's good to have consistent UX." [draft]
- "It was actually the hourglass that made it acceptable... It didn't quite fix the performance, but it fixed the UX." [draft]
- AA as the one-paragraph epilogue: "undo on steroids" [call 02:47] + "the other thing we put in way too late... the one area where I feel like performance isn't quite where I want it." [draft]
- Déjà vu definition can live here or die — it's charming but optional now. [draft]

---

## 9 · THE HONEST PART — I don't entirely know how it works

✎ JOB: The center, paid off. This is the vulnerable section — calibrated:
  vulnerability about UNDERSTANDING, not insecurity about WORTH. State the fact,
  state the norm it violates, state your position. No apologies, no trials.
  The effort-trauma undercurrent from today stays OFF the page unless a distilled
  confident version appears while you write (optional lines below) — your call,
  decided by writing, not by deliberation.

- "meta: i don't understand it enough still / you can finish it with less knowledge but writing about it is a different story" [notes]
- "'how i got it fast' i still don't fully understand / but i still played an active role" [notes]
- "I had to spend a long time after finishing the app understanding a lot of the concepts even after that in order to explain them in the first place." [call 02:31]
- "you have to understand it somewhat [to ship it], but to be able to write a somewhat technical blog post about it, you have to understand it more." [call 02:32]
- "And I think [it's] interesting because that's not how usually it goes." [call 02:32]
- "I could spend time, and it would legitimately be a lot of time, to understand every single trick... I don't want to." [call 03:04]
- "I'm at a point where I just want to ship the thing and this post is part of it" [notes]
- The claim, held: "i think this is an impressive thing to build and i'm really proud of it. historically it's correlated with how it works. now it's not and it feels strange but i still feel like i did [build it]." [notes]
- "just because I don't understand it all does not mean that this is not an achievement. It's still an achievement." [call 03:06]
- OPTIONAL (distilled ideal, only if it comes out confident):
  "there's an ideal that a proper engineer fully understands everything they ship — you can use as much AI as you want, but you need to be able to explain it, argue it. I didn't meet that ideal here." [today, paraphrase — yours to accept/rewrite/kill]
- Vector authority as the worked example of partial understanding, at your true depth: "the point was to just record a stroke and everything else comes after" [call 02:05] + the 111× number [draft].
- ✎ THE MAMMAL MOVE: your raw posts land self-blame-dissolving reframes ("not a
  personal failing; I'm just a mammal"). This section wants its equivalent —
  a reframe that accepts the fact (partial understanding) and dissolves the
  verdict. "This is a story of project management" [notes] may literally be it.
- ✎ DISCLOSURE SYMMETRY: your posts end "I've written 95% of these words, and
  used Claude for editing." This post has an available mirror image about words
  vs. code. Construction is yours; the spot is probably the very end, after P.S.

---

## 10 · DEMOSCENE — the compo that wasn't

✎ JOB: The cultural landing. Craft-is-the-point people vs. "this IS the new
  process." This is where the effort question gets handled — via demoscene, not
  Ozempic (your call on the call, and the right one).

- Demoscene definition sentence [draft] — keep, audience needs it. Live version you already delivered: "a subculture of programmers who compete to make the most impressive visuals and audio under real and artificial constraints — old hardware, or a certain file size" [talk]
- "it's entirely absurd to build a vector drawing application on a 1.8 inch device, but that doesn't matter." [talk]
- ✎ Optional beat: at the Thursday demo, Manuel pushed back in real time — "I
  think it's totally not [absurd], because you want that for, say, good
  typography." [talk] External voice disagreeing with your self-deprecation;
  use only if it earns its space.
- "I semi-accidentally created a compo for a nonexistent demoparty." [draft]
- "a lot of the demoscene people will probably be like, no, because you didn't write it" [call 03:10]
- "the demoscene is a lot about the craft... the outcome is cool but the point is the craft, the process is the point" [call 03:11, Janka's words you agreed with — rewrite as yours or attribute]
- "But this is the new process. This is the new process." [call 03:11]
- "their reaction to ai tools breeds insecurity / i care more about the outcome" [notes]
- Soft allusion if wanted: "there are many things these days that used to take a lot of suffering that you don't need to [suffer for] anymore" [call 03:12]
- Since-2008 demoscene history beat: "I've known about the demoscene... I never ever made a demo... I got into the demoscene by writing tools to help people make demos, but I didn't really use the tools." [call 03:10]

---

## 11 · CODA — tokens to burn (✅ NUMBERS VERIFIED 8/22 late night — receipt:
## docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md)

I tagged the codebase as v2 on Wednesday, added the WASM version on Thursday, and then it was just writing this blog post and shooting the video. That was supposed to be the end of it.

✎ JOB: The vindication scene, still warm. It proves the whole thesis in
  miniature: your conviction > agent consensus, one more push, real gains.
  HARD GATE: every number here is currently agent-claimed. The post's own rule
  says those don't count until measured on device. Verify before publish, and
  consider making that verification part of the coda's text — it's the most
  on-brand ending imaginable.

✎ BEAT 1: DONE — written above, no further edits needed.

BEAT 2 — the record (✅ RECEIPTED, with one honest wrinkle):
- Your asks, on the record:
  · 8/12 [.pro/Code Review Request]: "if there are any places we can optimize
    things with assembly, i would be happy to experiment with that"
  · 8/14 [.pro/Code Review Performance Focus]: "performance is paramount; i am
    willing to start doing some things in assembly if that helps us."
- Pro's pushback, on the record:
  · 8/12: "I would not hand-optimize the current float-per-pixel capsule AABB
    loop. Ordered microtiles and span scan conversion should remove far more
    work than assembly can accelerate."
  · 8/12: "Perhaps 10-50% in selected kernels, much less end-to-end... Profile
    first."
  · 8/14: "Keep the architecture. Do not start writing assembly."
  · 8/14: "Hand assembly becomes rational only if, after the algorithmic
    change, hardware cycle counters still identify this tiny bit-iteration
    helper as the dominant cost."
- THE SEEED LINK (✅ receipted, stronger than memory):
  · 8/9 day one: Pro's own search surfaced ESP32-S3 SIMD (Hackaday link in its
    research trail) and deferred it: "Do not prematurely write highly
    specialized SIMD/fixed-point code." [.pro/Tiny tldraw, lines 988, 2400]
  · 8/16 09:29, your message [Pi session 2026-08-16T09-29-19]: "in
    https://wiki.seeedstudio.com/round_display_animation_workshop/ at phase 5
    they do some SIMD magic in the SRAM so they're not constrained by the
    PSRAM bandwidth"
  · Same day, agent wrap-up: "Your Seeed link's SRAM lesson checked out too
    (scratch → internal, +319 KiB still free)" — one of five accepted,
    device-measured steps in the 1,269→669 ms cold campaign.
  · 8/22: the SIMD half lands as nine PIE kernels.
  ✎ Sentence shape available: your find was right twice on two clocks — SRAM
    in six hours, SIMD in six days — and the second landed only when you
    stopped asking and started instructing. Cleanest evidence in the post for
    what the human is for. Your words, though.
- ✎ THE WRINKLE (makes the story stronger, and receipt-proof): Pro's refusals
  were conditional — "algorithms first, assembly only after the waste is gone."
  By 8/22 the algorithmic work WAS done, i.e., Pro's own precondition had been
  met. So the honest frame isn't "they were wrong and I was right" — it's
  "they were right then, I was right NOW, and knowing when the conditions have
  flipped is exactly the PM's job." This dovetails with the mutual's line about
  knowing where agents aren't ambitious enough. A reader who opens the receipts
  finds the story MORE true, not less.

I spent a couple hours on Saturday talking with my friend Janka, who's helping me write this post and helped me reframe it in good ways. And I was looking at the time — I was waiting for my 8 p.m. Fable reset, and I remembered that my Codex reset was around the same time, and I checked and I had like 20 minutes, give or take, and like 90% left [✎ CHECK: earlier you dictated 6:45 p.m. and 19% — reconcile against session timestamps once], and I'm like, okay. I asked the agents many times now if it's worth doing assembly, and each time they were like, no, it's not appropriate at this time, or it's only worth doing assembly after settling many other things. So I had 20 minutes until the reset, and I'm like, yeah, I'm just gonna tell GPT-5.6 Sol: I have tokens to burn, disassemble the ESP32-S3 binary, and just see if there are any areas where we can get performance wins. In any area, just go to town, use subagents as much as you want. You can hand-roll assembly happily, or change our C++20 code, check if the Xtensa GCC isn't efficient in some ways, because I do remember that memcpy was a particular thing that was often wise to avoid. And I sent them off, and I'm writing this almost, what? No, six, seven, eight, nine. I'm writing this over three and a half hours later. The agents are still going. They've already made a ton of improvements. As of this writing, there have been 51 subagents, God knows how many experiments. The last summary I got from Sol was something like, what, cold-render compute(?) is 17.5–42% faster, saturated cache tours(?) are 39%(?) faster, undo/redo maxima are 16.1–22.2% lower, and exact-cell(?) anti-aliasing is 11.1–27.2%(?) faster [✎ VERIFY every term and number against the final session report — voice-to-text mangled these differently each retelling]. That's, I mean, that's incredible. And, I mean, I'm reading it now. It caught another 1–8% in, I think, cold rendering again. They just keep finding more stuff. It's crazy. And I will definitely let this thread run as long as possible. It already used up 22% of my tokens since my reset. But at least I have a banked reset. 

And I have to say, I do feel extremely vindicated. And I let them go to town. They have not, like, I have not done any glass testing yet. It's entirely possible that one more time it's gonna bite me in the back. We'll see. Honestly, one, I trusted they'll figure out if there's something broken. They figured that before. Two, I'm busy with other stuff. And I am also a little mad at myself that I wasn't pushing them earlier, but here we are. We're doing it. 

Soon after kicking off the assembly optimization loop, I had this whole exchange with a friend about how I'm writing this blog post, and after talking to a friend for a few hours, it turns out that it's really about how we're all project managers now. And one of the best lessons learned is that the agents do often need more pushing the right way, the same way humans do. His answer was that so much domain knowledge is knowing where the agents aren't ambitious enough — which again, just like humans.

✎ DECIDED (option A): include the DM exchange AS A SCREENSHOT, overclaim and
  all, then the self-catch follows in prose — your sentence, shape available in
  beat 2's wrinkle note ("when I mined the logs for receipts for this post, it
  turned out the refusals were conditional — and by this week their own
  conditions had been met"). The two "a friend"s in the paragraph above (Janka
  vs. the PM mutual) need untangling in pass two so readers don't merge them. 

if it wasn't for the Janka call, I might not have run this assembly thing. I cannot know. But I would like to think that talking all this about project management contributed to me being like: yeah, you know what, I should actually just tell them to disassemble it. And lo and behold, they keep finding optimizations.

BEAT 3 — the override:
- "Today around 6:45 p.m. I saw my Codex reset was in twenty minutes and I still had 19% of the quota left. I was like: you know what." [today, tidy]
- The actual prompt, VERBATIM, typos preserved [today, exact]:
  > oh hey i have a lot of tokens to burn so let's go. do a disassembly on the
  > esp32s3 binary (the native one, not the wasm one) and see if there are
  > places where we can get performance wins in all areas (cold renders,
  > panning speed, undo/redo, anti-aliasing, caching etc.) feel free to use
  > subagents liberally be thorrough and be fully autonomous. i am very happy
  > to hand-roll assembly or change our c++20 code to generate better assembly
  > to squeeze out more performance, especially if the xtensa gcc is
  > inefficient in some ways you can also research that i vaguely remember
  > that we want to avoid memcpy for example but there's probably more!!
  > anyways. fully autonomous! go as long as possible! good luck you go this
- The steering, sent two minutes later, VERBATIM [today, exact]:
  > (i have the device connected feel free to flash whatever) (don't stop)
  > (research all possible ways we can make things faster with assembly)
- ✎ Styling decision, open: blockquotes vs. screenshots of prompts. Blockquotes
  are searchable/accessible; a screenshot or two adds authenticity texture and
  suits a Twitter article. Decide at layout time, not now.
- ✎ SIGNATURE DETAIL: "good luck, you got this" is your recurring prompt
  sign-off — 8/12 ("good luck, you got this"), 8/14 ("thank you and good luck,
  you got this."), and today ("good luck you go this"). Three receipts, one
  habit, typo in the finale. Worth one knowing line in the post if you want it.
- The prompting confession, keep it: "Do I know there's probably more for a fact? No. But it's a reasonable guess, and it's a way to motivate the agent." [today]

BEAT 4 — the vindication (✅ VERIFIED against the receipt):
- "Sent at 6:48 p.m. As I'm dictating this, it's 9:22 p.m., and they're still going." [today, tidy]
- "So far there have been 43 subagents doing research and trying things. It just kicked off three more." [today] → final count 51+.
- FINAL MEASURED NUMBERS [receipt, exact]:
  · cold render compute 18.92–41.87% faster (all 15 corpora/zooms)
  · direct pan composition 51.59–51.65% faster
  · RGB565 ring staging 57.63–57.75% faster
  · saturated 604-slot cache tour 39.15% faster
  · undo/redo worst-case 21.78–24.24% faster
  · settled anti-aliased rendering 17.23–26.45% faster
  · nine hand-written Xtensa PIE (SIMD) kernels in the final ELF; bit exact,
    export CRC unchanged (e40499d1)
- "It's talking about the final three audits — and I'm pretty sure this is not the first time it said final." [today, tidy] ✎ finished coda sentence; sits beautifully next to "(don't stop)"
- "They just keep finding things... I told you so." [today, decursed]
- Self-aware irony, DO NOT CUT: "I let them go to town. They ran the benchmarks. I have not glass tested yet. It's quite possible that once I glass test it, some bugs come out. I may be repeating the mistake of not glass testing early enough." [today, tidy]
- "I'm also a little mad at myself that I wasn't pushing them earlier, but here we are." [today]

BEAT 4.5 — the glass test (happened later that night):
- "I just did a glass test, and as far as I can tell, the only thing broken is the SVG export — in a way that I think will not be too hard to fix. There might be a bug with undo/redo hammering; I'm not sure I can reproduce it." [today, tidy]
- "They did not break anything — famous last words — but PNG export's fine, performance is fine, cold rendering got faster, anti-aliasing got faster. Free wins in assembly." [today, tidy] ✎ "I mean, goddamn" — decurse or keep? borderline under your rule; your call
- ✎ The glass test caught exactly one break the benchmarks missed. The thesis,
  performing itself, one last time. You don't need to say this explicitly —
  placing the SVG line right after the irony beat does it.
- ✎ RECEIPT GEM, sentence available: the agents' own method section imposes
  four filters ending in "keep only changes that improved the full device
  workloads" — your glass-test discipline, institutionalized into their
  process. Two weeks ago they claimed 30 FPS while the screen tore. Point at
  it in your words if you want it; it's the arc of the whole post in one
  before/after.

BEAT 5 — the loop closes:
- The PM-mutual exchange [today, ⚠ permission/anonymity — see questions]: your message ("it turns out it's really about how we're all project managers now... agents do often need more pushing the right way, the same way as humans do, emphasis on the right way") and his reply: "so much domain knowledge is knowing where the agents aren't ambitious enough — which is also true for humans."
- Closing-line candidate for the whole post: "if it wasn't for the Janka call, I might not have run this assembly thing. I cannot know. But I would like to think that talking all this about project management contributed to me being like: yeah, you know what, I should actually just tell them to disassemble it. And lo and behold, they keep finding optimizations." [today] ✎ the post about the process caused the final act of the process — keep the honest hedge, it's load-bearing.
- ✎ Optional footnote in your footnote style: the moral-patients aside ("smug at
  your agents' expense is fine") [today] — cute, skippable.
- ✎ NEW ANECDOTE, home TBD (agents section or P.S.): "If I tell Claude to make
  atomic commits, it makes granular commits. If I tell GPT-5.6, it interprets
  it the opposite way and makes one giant commit. So now I just say granular."
  [today, tidy] ✎ tiny, perfect illustration of model-casting knowledge; also
  the word "atomic" genuinely is ambiguous (indivisible-small vs all-in-one) —
  you're not misusing it, the models just resolve the ambiguity differently.
- ✎ Pending from the agent: the landed/rejected experiments list — feeds P.S.
  and/or one coda line; also the SVG export fix + one look at undo/redo
  hammering before ship.
- ✎ $440 disclosure line, if kept, could live here or in P.S. — one plain
  sentence, factual register, never in the hook.
- ✎ OPTIONAL BLOCK, ⚠ one paragraph MAX or save for a separate post — the
  Xtensa GCC vectorization tangent [today]. If included, must be labeled as
  Sol's unverified estimate + your napkin math. The three usable observations:
  · the frontier moved in a day from "make my app fast" to "we could teach the
    compiler itself" ("right now all the PIE SIMD stuff needs to be hand
    written assembly" — agent claim)
  · Sol's estimate, in HUMAN engineering-months: 2–4 for the first part,
    6–18 for the broader thing. YOUR conversion: the broader thing ≈ one
    agent-week. "None of the AI agents can estimate their own work. They keep
    estimating work in human engineering months, but that's not how things
    work in August 2026." [today, near-verbatim] ✎ finished sentence — and a
    standalone finding: converting human-time estimates to agent-time is a
    calibration skill only the human PM has. Could also live in slot 5.
  · "I do not have the money for that week." (~$4,200 subscription-juggling /
    ~$25K API — napkin math, label it; today's run was in fast mode, up to
    1.5× speed for 2.5× price, so a slower run costs less over more wall-time.
    Conclusion survives the correction: still token-poor.) — the boundary of
    hobbyist ambition in 2026 is a token budget
  · upstream GCC won't accept AI-written code — the work can't go home because
    of who wrote it. Rhymes with the demoscene purists (slot 10); if used,
    place where it can echo that section.
  ✎ HARD CONSTRAINT: cannot compete with the Janka closing line. Two endings
    is no ending. Default disposition if in doubt: separate post.

---

## 12 · LANDING

✎ JOB: Inventory, pride, the absurdity owned, persistence named. Mostly exists
  in [draft] already — trim to match the new center, keep the strongest lines.

- "So what did I build?" inventory paragraph [draft] — keep, tighten.
- "The entire existence of TinyDraw V2 is completely absurd, but that's kind of part of the charm." [draft]
- "8 megabytes of PSRAM where, as far as I can tell, PS is short for Pretty Slow." [draft] ✎ keeping the joke per handover
- "The persistence is mine. The stubborn persistence is mine." [draft]
- "I liked that it helped me rediscover my skill of picking up and becoming proficient in a domain really fast." [draft]
- "LLMs make getting started way, way, way, way easier... but I did a lot more than that." [draft]
- Puck/browser link callback. [draft]

---

## 13 · P.S. — MISTAKES MADE

✎ Keep as-is structurally; resolve the remaining [FACT-CHECK]/[NEEDS ...] tags
  against .codex-archaeology (most have receipts in docs-performance.md and
  git-history.md). Items whose parent sections shrank still work here — the
  list is receipts, not narrative. Cut any item that now needs a paragraph of
  context to land.

---

# CUT LIST (from blogpost-edited.md — dies with the old center)
# - Cold Rendering as a full explainer section (numbers survive in slot 6/12)
# - Inking's unexplained internals: split-and-chain, 64-sample commit stall,
#   validate-first details, streamline-tail mechanics (the HTML-comment debts)
# - "Cached chrome" terminology and anything you can't say at your held depth
# - The 18-trick visualization ambition (v1 of the post ships without it)
