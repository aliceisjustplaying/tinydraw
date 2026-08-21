# [TITLE]

I guess we're doing it.

[VIDEO]

I've been following Steve Ruiz on Twitter for a while now, and he kept posting these cool projects built on microcontroller-powered tiny little devices. And then I saw there's going to be a whole meetup about them, and I wanted to go, and I'm like, well, if I'm going, I want to present something. But I still didn't know what to build. A mini tldraw seemed like the obvious answer, but surely someone has done it, or surely this was, like, Steve's very first project. So I scrolled his entire timeline and I did not find a mini tldraw at all. And I was like, okay, I guess we're doing it.

I ordered the Waveshare from Amazon, and I asked my coding agent what we could build before the device arrived. It turns out quite a bit.

So on a beautiful Sunday, a day before the meetup, I started working on this. I did not have the device yet, so I had the agent set up a development environment that targeted a QEMU-emulated version of the ESP32-S3, which let me develop on my Mac without the real hardware. I was only able to start to work with the device Monday morning.

Monday morning I was quickly faced with the issue that what was fast in the emulator was not fast at all on the device. So I spent a big chunk of Monday optimizing the raster version to be, well, fast. It used Steve's Perfect Freehand library, the first version of which powered tldraw, which simulates pressure with velocity and makes lines nice.

Quick question while I test it: should we have done a vector canvas in the first place?

[17:11 BST, about two hours before the talk; first explicit vector-canvas mention; agent answer: no, raster-first sensible; eventual hybrid vector authority + raster cache]

And then when the presentations came up, there was no order yet, and Steve just, like, pointed at me: You first. And I presented it, and people liked it, and afterwards people, like, came to me to try it, and yeah, that was nice.

And in the end, I had an editor, a raster-based editor that had a 3×3-screen-size canvas. You could draw, you could erase, you had a couple of colors, ten levels of undo, and you could export things to PNG, and it would simulate USB mass storage, simulate being a flash drive, really.

Everyone who gave a talk got the RP2350 version of this little thing. So that's when I did the RP2350.

The next evening: alright. Check V2_INITIAL_SPEC.md. We're making this a real infinite canvas. By god, we are doing this.

[19:46:58 BST, Aug. 11; first surviving commitment prompt; first spec commit 19:47:41; first vector-code commit 19:52:09; private agent-assisted spec-drafting time absent]

I mean, agents will be pretty good at optimizing things, and I already learned somewhat that they're good at optimizing things. So probably.

And I spent the next couple of days in a fever dream chipping away at the problem. Nominally I was trying to answer the question if this is possible, but I think in reality I never doubted it was possible. I had to find an approach that worked, and the initial architecture did not really work after a bunch of testing. So after a couple days I had a prototype that I had to throw out, but on the other hand, I had the direction to go.

And that's when the real building began, or so it feels like. I quickly duplicated the features for drawing, colors, pen sizes. I really wanted arbitrary zoom levels. This did not seem terribly feasible. So eventually I settled on power-of-two zoom levels between 25% and 400%. And because of that, the canvas is a 4×4-screen-size canvas, which works out with this math quite well.

## [INKING]

I remember in the beginning doing a lot of tests when I would, like, just draw. I think that was actually the fever dream prototype phase, where I would just draw lines really fast. And I would see the chords show up, which is, you know, I should not be seeing it. So I remember that. I was like, no, this is too slow. This is too slow. Like, I just need this to feel fast.

[CONTEMPORARY: “oh things get VERY LAGGY if i choose the biggest pen an draw”; “i draw the stroke and i can see the tile-updating-thingy under-my-finger”; “angularity correlates with speed”]

There was some test where I was drawing circles. It wasn't the angularness. It was, like, seeing if it would sort of overrun when I moved my finger. I vaguely remember tweaking that.

[TIMELINE: Aug. 10 slow/fast circles + diagonals exposed sparse samples; RP-only post-lift-tail experiment later that night, worsened angularity and reverted; Aug. 18 blind circle A/B exposed jaggedness versus inward-pulling tail / difficult closure]

I think something was a whole thing that this touchscreen samples touch way less than your average smartphone. So a lot of work went into that. A lot of architecture work.

[FACT INSERT: ~73.758 Hz touch sampling; about 62% of the iPhone X's 120 Hz]

We tweaked the Perfect Freehand constant from 0.35 to 0.4. There was one point where the agent made some, like, 111 times faster.

[FACT INSERT: input-path append, 19,324 µs → 173 µs]

And then I think towards the end there was another breakthrough. There was a tail thing. There was another one of those felt right or didn't feel right.

[TIMELINE: Aug. 16 provisional ribbon before authority work → curved committed path → committed overlay + idle absorption / 111× worst input-path append → quarter-world to sixteenth-world coordinates → 0.35 to 0.4 streamline; Aug. 17 raw replaceable fingertip tail + filtered committed/SVG geometry]

[CONTEMPORARY: “yes please dying to fix inking”; “why is our drawing so angle-ey?”; sixteenth units: “Yes, it’s much better… definitely a big improvement”; 0.4: “my slow circles look a hell of a lot less jagged” / “very hard to finish the circles”]

Inking was another thing that I needed to solve. The feel, the performance went from not too bad to okay, this actually feels good now.

## [COLD RENDERING]

So after I started building the actual version after the fever dream prototyping, the agent built me, you know, the first version where I could draw and it would render it. However, it was excruciatingly slow, truly. Almost 24 seconds once you zoomed into 400% on our overlap torture testing.

[OPTIONAL ORIENTATION: vector strokes as authority; raster tiles as disposable cached pixels; unseen zoom/view requiring replay]

So I was like, okay, we need to make this faster. And okay, this is where I need to look up the fucking timeline, but basically the agent found a bunch of optimizations that made this somewhat usable.

[OPTIONAL TECHNICAL EXAMPLE: Aug. 14 first rescue, 23.663 s → ~0.971 s p95 through distant-segment culling, scanline-interior fills, and removal of fixed delay; later demoscene-prompt campaign, newest-first saturation, 1.452 s → ~0.675 s]

And then at one point, and this is where I can probably weave in the demoscene thing, I asked a friend who's a really, really, really good software engineer: Hey, I just feel kind of stuck. And that's when she told me that, like, oh yeah, just tell your agent this is a skill issue, and they should use mechanical simplicity, elegance, and most importantly demoscene mindset.

The demoscene is a subculture of programmers who compete to make the most impressive visuals and audio under real and artificial constraints.

[TWO JARGON GLOSSES: demoparty = gathering; compo = competition/category]

It wasn't until, like, a day or two later when it really clicked that, yeah, what I'm doing is basically a compo and not so much a fun little hack for this thing.

And this actually helped a lot, because from then on we incorporated a whole host of tricks. We're using the fast inverse square root, all the way from Quake III. The agent found a way to put a bunch of stuff in the IRAM that saved us 6.93 to 11.68%. And we tried many other things that just didn't work.

And this happened in stages. I would work on it for a while, then work on something else and it was slow again, work on it again, and so on.

The last one was what I called evil hairlines, when even though our automated testing, which was called Battery, showed good numbers, if I, like, drew a lot of thin lines crossing each other and started zooming in, things started getting really slow again. And that was the thing that gave us the final push and tweaks to our testing suite, and eventually hit our target for the general cold battery at 50%, 100%, 200%, and 400% rendering under 500 milliseconds.

Again, I love the fact that we just stack like, you know, six, seven, eight, however many optimizations on cold rendering. I like that the architecture is pretty good and fully understood. I should understand it more, but I can tell that it is pretty good.

## [PANNING / TEARING]

Next thing I had to solve was panning. Initially it was just really slow, around 15 frames per second, and I'm like, no, we want this faster. And then seemingly the agent was like, oh, okay, I fixed it. It's, you know, it's 30 FPS now. And then I looked at it and tried it, and it's like, nope. It looked terrible, glitched, there was a lot of tearing.

That was the point I realized that the agent cannot really test this or see it. The only way to test, like, panning speed, and especially the lack of tearing or glitches, is just, you know, with my fingers and eyes.

And the agent kept hammering at it. It kept not working. I kept being frustrated, and at one point I was like, okay, like, okay, stop. You seem like you're failing. Step back and think.

[BRIDGE MATERIAL: TE 16.773 ms / 59.62 Hz; requested 40/50/60 MHz all actually 40 MHz; full-frame transfer 17.998 ms; GETSCANLINE and control reads all zero; no trustworthy software scan-position oracle]

[CONTEMPORARY: “start with all the hardware measurements … without ever knowing the real limits”]

And then the agent was like, okay, I need, if you make a slow-motion video of a bunch of blinking patterns, then I could determine why it's tearing and what the fix is.

And I was like, okay, let's see. I do have a Fuji X-T5 that can record at 240 FPS. I found it in the menu, there's a flicker-free option that let me put 1/1024 for the shutter speed, which, if I remember right, was good for this one.

For Fuji, I have one lens currently, the 85 millimeter from Sigma, or the 56 millimeter technically. I just always think in 85 millimeter equivalent. It has horrible magnification, entirely not made for this. I propped up the device against my laptop screen. I turned down the laptop backlight, and then I set up the camera, and I handheld over two minutes of video just in case.

And I gave the video to the agent, and the agent spent way too much time on trying to come up with a classifier to find the information it needed. That didn't really work. Eventually what worked is when I was like, okay, no, you've been at this for a while and it's not working. And the agent was like, okay, let me try a contact sheet, and it did a contact sheet, and that gave it the answer.

[BRIDGE MATERIAL: deliberately unsynchronized positive control tearing in every 24-frame burst; TE-rising, row-zero, top-to-bottom continuation sweep; accepted cell, 1,495 frames, 0 tears / 0 anomalies]

And then suddenly we did have fast panning, but we still had tearing. There was tearing at a certain spot, and I told the agent that, yeah, we have this tearing at this spot. And then the agent looked at it, hammered things at it, and tried again. I'm like, well, okay, now I have tearing at a different spot.

[BRIDGE ORDER: Fuji-validated synchronized product, clean but ~19.9 FPS → compositor pacing, ~29.5 FPS → fixed tear near zoom-minus top edge → tear moved about two-thirds down minimap after pacing change]

[CONTEMPORARY: “panning is fast now, but it's tearing… very predictable way”; “fixed spot”; “tear is not gone instead it moved… 2/3rdsish of the minimap”]

And from these pieces of information, as far as I remember, the agent could deduce the last missing step to make it tearing-free, and indeed it fixed it. Now we have almost 30 FPS tearing-free panning, which I'm really proud about.

[BRIDGE MATERIAL: panel consuming strip N while CPU still preparing it → stationary tear row; expensive composition before sweep; remaining per-strip staging inside that strip's wire-time budget]

[CONTEMPORARY: “yes tearing seems to be gone”]

## [LLMS]

With regards to LLMs, GPT-5.6 Sol had the largest implementation footprint. It did a really good job. And usually, when I ran into something where it just got stuck, I would do a combination of two things.

One, give the entire source code, documentation, and logs to GPT-5.6 Pro and wait 60 to 90 minutes to get a very detailed code review, performance review, tips and bugs, and whatever. Either feed that to Fable or already ask Fable to see where to take this further. And my god, Fable is both brilliant and deeply flawed in many ways, but my god, when it's brilliant, it's brilliant.

I found that when I set thinking to xhigh and I was throwing architecture, and then especially performance optimization, that was just something else. For this stuff, that model is absolutely fucking incredible. I used up all the Fable quota I could squeeze out of a £200 Anthropic plan in three days or something.

And overall I spent something just south of five billion processed tokens, plus the unrecoverable GPT-5.6 Pro web usage, to build something purely because I wanted to see if I can build it on a $40 device.

[GAP: final subscription-spend/API-equivalent wording, only if worth keeping]

Building this app really became a game of, okay, I wanna make this as fast as possible. I wanna get every ounce of performance out of this hardware. I'm gonna stack ten different tricks to make cold rendering faster, and I'm gonna keep throwing more agents and more prompts at it, and we're gonna work on it until it's not painfully slow.

And smaller stuff like smashing undo several times, which is normal, should work. Same with redo.

## [UNDO]

As I remember it with undo, undo was the most painful one because I felt like I got to a good point where cold render was pretty fast, we'd fixed panning, we'd fixed tearing. I think by then we'd fixed even déjà vu. And then I added undo to V2.

I distinctly remember the very first version the agent built, again, as far as I remember, already had some smart stuff where it only re-rendered the blocks on the screen that needed it, which were pretty small. But what it meant in practice is that you were, like, watching this line render in the middle of the screen. It just looked absolutely horrible.

And that's when it hit me that, like, well, fuck, it sounds very trite, but an undo is a redraw, and redraws are fucking expensive. And that's when it hit me that, oh, it was kind of ill-advised to leave undo this late. If I would have had that feature very early on, then could I have made it faster? I'm not sure. But would I have had probably less pain? Possible.

I first saw that version, it was horrible, it was slow, even with the sort of partial rendering on the screen. And then, I don't know, I think we tried a bunch of stuff with the agent, I don't remember.

The next bit I remember is the hammering thing. It took quite a few tries to get it right, that when you hammer undo or hammer redo, then the previous rendering gets cancelled because, well, you know, that's how you do it. So it took quite a bit of iteration where it is now, where undo and redo, you can hammer it now and it will not mess it up. It will do it properly.

And then I was still thinking about the UX because I still thought it was kind of bad. The first thing we tried was just show, like, the absolute low-res cold render for the entire line or section, and then don't show the intermediate render, but then, like a pseudo-double buffering, show the finalized cold render. The crude one was just so ugly, so fucking bad.

And that's when I was like, having an hourglass icon while the undo settles would be better. We could actually try just drawing an hourglass and just rendering the final one.

Initially the agent was like, you know, okay, if it takes less than 120 milliseconds, we don't show anything, we just show the final thing, so there's just a small lag, and if it's longer, we put up the hourglass. But I did not like the UX discrepancy. So I just said that, yeah, I mean, we can just show the hourglass for even the short stuff. Just flash it and it's gone and it's fine. It's good to have consistent UX.

It was actually the hourglass that made it acceptable because we are not showing the intermediate rendering at all. It's just, if you smash it, immediate hourglass. We had to work out the logic of how to time things for multiple undos and redos and the hourglass, but as far as I can tell, we did it. So you smash it and it shows the final thing, and I don't know, it just got fast enough that it was fine.

Anything still unsatisfying? Honestly, no. I think we optimized the actual undo rendering pretty well. I think the hourglass UX is the least bad UX.

[OPTIONAL FACTUAL NUANCE: spare cache slots preserving pixels from before a stroke; Undo/Redo swapping them back when present; rebuild after eviction/unvisited state; hourglass still shown every time]

## [ANTI-ALIASING]

[RETROSPECTIVE ORDER: settled AA landed before V2 Undo; AA optimization resumed after the Undo round]

Anti-aliasing is, like, in some ways undo on steroids. That's something I should have definitely had when I started.

As I understand it, if we would have had anti-aliasing from the beginning, we could have optimized all the cold rendering around it. But because we put it in so late, essentially the only, or if we don't want to re-architect the whole goddamn thing, the only thing we can do is wait for cold rendering to be done and then apply anti-aliasing.

And it's a somewhat subtle thing on a screen this small, but it bothers me, and I think our worst case in the battery test right now is still almost one second. In many other cases it's faster. So I think, actually, it feels like AA is the worst case of, yeah, we should have just started with this.

[FACT BOUNDARY: dense 400% settled-AA case, ~946.849 ms; separate from general cold-render timing; late-feature causation as personal interpretation]

## [MINIMAP / EXPORT / MEMORY]

I added a minimap. I'm really happy about that. It makes navigation easier. It makes overview easier.

I'm proud that as far as I can tell we finally got the SVG export right. That was a whole ordeal.

I'm proud that, at least at the end, we made good use of the memory. We have almost twice as many slots for cache now: 320, then 384, then 448, then 604.

And then I was like, okay, we have this sacred 1.5 MiB, but we already have the export feature, and the agent said it doesn't need it. So I'm like, okay, then we just use all the remaining RAM for more caching, and then if we need it for export, we can just evict the cache for that. It's a cache, we can evict it for export. I'm perfectly fine with that tradeoff.

[OPTIONAL FACT INSERT: measured export peak 291,484 bytes; structural worst ~320 KiB; 156 extra cache slots]

## [PHANTOM DOT]

I was testing the SVG and PNG exports when I saw two dots in the SVG on the top, two what looked like squares on the glass, which was weird because the way we draw it, you cannot really draw squares. So I was like, something's wrong. And there was nothing in the PNG. I was like, what?

It was kind of like Schrödinger's dot. And then I saw sometimes other dots. It would flicker across panning and zooming, and sometimes even undo. I assumed that to be an issue around different renderers interpreting one-sample strokes differently. So we added a fix.

But then the next morning we found another issue, essentially, which is that when you touch the top edge of the screen, it was recorded as tiny strokes. These were never deliberate, so we implemented a fix to just reject them.

[FACT NUANCE: first renderer/export disagreement fixed; later top-edge source bug fixed separately]

## [ENDING]

I like the absurdity of, yeah, why would you use this for actually drawing things? You really wouldn't. But it's just kind of nice to know that, yeah, I can build it.

And yeah, I built something that's just kind of weirdly fast and kind of silly, but it is, at the end of the day, a real vector graphics program that's pretty fucking fast for running on an ESP32-S3 with 8 megabytes of PSRAM where, as far as I can tell, PS is short for pretty slow.

I like the fact that I was persistent. I spent an ungodly amount of hours and tokens and whatever. And each time I got stuck, I was just like, okay, let's give it to a more powerful agent, or tell it to try harder, or tell it to have a demoscene mindset.

I'm proud that I kept pushing things. The agent is like, oh yeah, this is pretty fast. I'm like, okay, so I did my evil hairlines and it's not. And then, okay, back to another round of optimizations.

I like the fact that I semi-accidentally created a compo for a nonexistent demoparty.

I like that it taught me, well, I cannot quite say it taught me a lot about embedded programming, but it did. It somewhat did.

And also, I like that I managed to exercise a skill that I kind of forgot I have, which is, when motivated, I'm very good at becoming at least somewhat proficient in a domain or area I have no experience with really fast, and this is a version of that.

LLMs make getting started way, way, way, way easier, and I think that's important. But then there's also the part where, okay, at some point after you made the fun part, you're gonna run into slow graphics, you're gonna run into tearing, you're gonna run into this and that. And yeah, you can just tell the agent to make it faster, but I think I did at least a tiny bit more than that.

TinyDraw V2 runs on Puck now, which is not quite an emulator, but you can compile TinyDraw to WASM, and Puck runs it. It's not clock-accurate, but it reproduces the important bits really, really well.

[PUBLISHING GAP: registry bundle `aliceisjustplaying/tinydraw`; no public playable URL yet; local runner only]

## [P.S.]

[GIANT UNRANKED LIST OF MOSTLY FUN MISTAKES, IN YOUR WORDS]

[SOURCE MATERIAL]

- Fuji classifier detour → two contact sheets; “god i hope all this hassle is worth it”; “we've been at this for... over an hour?”
- Two SVG dots / two glass squares / zero PNG dots; “fascinating”; renderer parity fix, then separate top-edge-contact fix
- Pen-size selector also firing Redo; “both fires and the whole UI just gets fucked”
- Evil hairlines absent from the supposedly good benchmark; “why dont these benchmark have the new evil hairlines one”; almost doubled cold time
- Wi-Fi blamed for psychedelic vertical stripes; Wi-Fi removed; stripes remained; “if it's nto the wifi then what??”
- Software-green beam racing; severe physical tearing
- Tear “fixed” near minus button; moved two-thirds down minimap
- Requested 40/50/60 MHz; all actually 40 MHz
- GETSCANLINE and every control-register read; all zero
- Sacred 1.5 MiB export reserve; synthetic malloc; actual peak 291,484 bytes
- Internal scratch predicted ≥40%; measured 1.69%
- Mathematically exact AA optimization; slower on almost every case
- 1×2 cold-render supertasks; task watchdog before any timing result
- Host-neutral mask optimization; ESP32 7–13% slower
- Fast LOD; deleted loops, hairpins, pressure peaks, and eraser dabs
- Sub-500 ms benchmark; stopped before pixels reached the glass
- Color popup magenta; black and white hiding the extra/missing byte swap
- “512-slot” run; actually 384
