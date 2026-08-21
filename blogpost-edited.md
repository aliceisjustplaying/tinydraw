# TinyDraw V2

[VIDEO]

## Hook

I spent nine days building a vector graphics editor on a $40 microcontroller with a 1.8-inch touchscreen. I knew it was possible. I wanted to see how fast I could make it, and how far I could take the project.

[YOUR CHOICE: Add the "why would you even want this" here, OR save it for the ending. Current recommendation: save it for ending as callback.]

## Origin

I've been following Steve Ruiz on Twitter for a while now, and he kept posting these cool projects built on microcontroller-powered tiny little devices. And then I saw there's going to be a whole meetup about them, and I wanted to go, and I'm like, well, if I'm going, I want to present something.

But I still didn't know what to build. A mini tldraw seemed like the obvious answer, but surely someone has done it, or surely this was Steve's very first project. So I scrolled his entire timeline and I did not find a mini tldraw at all.

Okay, I guess we're doing it.

I ordered the Waveshare from Amazon, and I asked my coding agent what we could build before the device arrived. It turns out quite a bit. I set up a development environment that targeted a QEMU-emulated version of the ESP32-S3, which let me develop on my Mac without the real hardware.

And then when the presentations came up, there was no order yet, and Steve just pointed at me: You first. And I presented it, and people liked it, and afterwards people came to me to try it, and that was nice.

V1 was raster, and that's easy. It's just far easier to make a raster editor for a microcontroller than a vector one.

## The Commitment

The next evening: alright. We're making this a real infinite canvas. By god, we are doing this.

My first ambition was to actually see if I can make a mini tldraw — vector, arbitrary zoom levels, really fast. The agents tempered my expectations, and they were right. I really wanted 25% to 800% zoom, but I settled at 400%. It was a big compromise.

## The Fever Dream

I spent the next couple of days in a fever dream chipping away at the problem. Nominally I was trying to answer the question if this is possible, but I think in reality I never doubted it was possible. I had to find an approach that worked.

I call it a fever dream because a lot of it was just: I don't know what the agents are doing, but I'm just gonna keep going. In hindsight, I would have done this differently, but hindsight is 20/20.

[FACT: The fever dream was ~26 hours, Aug 11 evening → Aug 12 night]

After a couple days I had a prototype that I had to throw out — the camera-aligned 3×3 atlas was explicitly rejected after 103 rejected pan requests and up to 12 seconds of cumulative repair during a six-stroke burst. But on the other hand, I had the direction to go.

And that's when the real building began.

## The Graveyard

In August 2026, every software engineer has a giant graveyard of vibe-coded things they started and never shipped. I have it. Everyone I know has that. That's the reality of August 2026.

And I'm shipping it.

## The Demoscene Moment

[FACT: This happened after the fever dream, during production, Aug 12-14]

At one point I asked a friend who's a really good systems engineer — does low-level Nix and Rust work — and I said: Hey, I just feel kind of stuck. And she told me: just tell your agent this is a skill issue, and they should use mechanical sympathy, elegance, and most importantly demoscene mindset.

"Tell it that it has skill issue and should rethink the problem and think about mechanical sympathy and elegance, demoscene mindset."

"Whatever you currently have, I have probably seen a more impressive demo in kilobytes and a fraction of the cycles."

"Surprising fraction of how I use Claude well is conjuring up the right guy from latent space."

"Who would absolutely crush this."

"Ok Claude go be that guy."

The demoscene is a subculture of programmers who compete to make the most impressive visuals and audio under real and artificial constraints. A compo — short for competition — is a category at a demoparty, which is basically a gathering where people show off what they've built under severe constraints.

It was a slow realization: okay, yeah, this is a compo. In hindsight, the whole project was very demoscene-y from the start. I just wasn't conscious about it until she said it.

And this actually helped a lot, because from then on we incorporated a whole host of tricks.

## Cold Rendering

[DEFINITION: A "cold render" is when you have to render something from scratch because it's not cached. The vector strokes are the source of truth. The raster tiles are just cached pixels. The whole point of the optimization work was to avoid cold renders as much as possible — and when you couldn't avoid them, make them fast.]

[FACT-CHECK: Is "cold render" the standard term, or is this project-specific naming?]

The agent built me the first version where I could draw and it would render it. However, it was excruciatingly slow. Almost 24 seconds once you zoomed into 400% on our overlap torture testing.

So I was like, okay, we need to make this faster.

The first round of optimizations brought it from over 20 seconds to about a second.

*Mistake made: Host disagreed with device on 2 out of 5 experiments. Optimizations that looked good on my M1 MacBook would actually run slower on the ESP32 because the PSRAM is so slow.*

We're using the fast inverse square root, all the way from Quake III. The agent moved the hot raster loop into IRAM — part of the 512 KB of real RAM, as opposed to the 8 MB of "pretty slow" RAM — that saved us 6.93 to 11.68%.

*Mistake made: Internal scratch predicted ≥40% savings. Measured −0.36%.*

And this happened in stages. I would work on it for a while, then work on something else and it was slow again, work on it again, and so on.

*Mistake made: Flash-icache layout moves hot-loop timing ±2-3% per build. Placement in the heap matters: a 40 KB workspace mid-heap cost +9 ms; placed dead-last, 0 ms.*

Over time, the tile cache grew from 320 slots to 604, almost twice as many by the end.

The last one was what I called evil hairlines. Even though our automated testing — which was called Battery — showed good numbers, if I drew a lot of thin lines crossing each other and started zooming in, things started getting really slow again. That was the thing that gave us the final push and tweaks to our testing suite.

We stacked eighteen optimizations on cold rendering, and eventually hit our target: under 500 milliseconds. The final numbers were 390 ms at 50%, 383 ms at 100%, 457 ms at 200%, and 493 ms at 400%.

[FACT: 22 rejected experiments. The full list is in .pi/plans/2026-08-21-cold-optimization-inventory/scout-context.md if you want to cherry-pick more mistakes]

## Panning / Tearing

[FACT-CHECK NEEDED: Verify the 15 FPS starting point and ~30 FPS final — numbers are in the receipts somewhere]

Next thing I had to solve was panning. Initially it was just really slow, around 15 frames per second, and I'm like, no, we want this faster. And then seemingly the agent was like, oh, okay, I fixed it. It's 30 FPS now. And then I looked at it and tried it, and it's like, nope. It looked terrible, glitched, there was a lot of tearing.

That was the point I realized that the agent cannot really test this or see it. The only way to test panning feel, and especially the lack of tearing or glitches, is just with my fingers and my eyes.

And the agent kept hammering at it. It kept not working. I kept being frustrated, and at one point I was like, okay, stop. You seem like you're failing. Step back and think.

*Mistake made: Requested 40/50/60 MHz SPI. All actually 40 MHz.*

*Mistake made: GETSCANLINE and every control-register read: all zero. No trustworthy software scan-position oracle.*

And then the agent was like, okay, if you make a slow-motion video of a bunch of blinking patterns, then I could determine why it's tearing and what the fix is.

And I was like, okay, let's see. I do have a Fuji X-T5 that can record at 240 FPS. I found it in the menu, there's a flicker-free option that let me put 1/1024 for the shutter speed, which, if I remember right, was good for this one.

For Fuji, I have one lens currently, the 56 millimeter from Sigma. I just always think in 85 millimeter equivalent. It has horrible magnification, entirely not made for this. I propped up the device against my laptop screen. I turned down the laptop backlight, and then I set up the camera, and I handheld over two minutes of video just in case.

And I gave the video to the agent, and the agent spent way too much time trying to come up with a classifier to find the information it needed. That didn't really work. Eventually what worked is when I was like, okay, no, you've been at this for a while and it's not working. And the agent was like, okay, let me try a contact sheet, and it did a contact sheet, and that gave it the answer.

*Mistake made: Classifier detour. "We've been at this for... over an hour?"*

And then suddenly we did have fast panning, but we still had tearing. There was tearing at a certain spot, and I told the agent that. And then the agent looked at it, hammered things at it, and tried again. I'm like, well, okay, now I have tearing at a different spot.

*Mistake made: Tear "fixed" near the zoom-minus button. After the next change, it moved two-thirds down the minimap.*

And from these pieces of information, the agent could deduce the last missing step to make it tearing-free, and indeed it fixed it. Now we have almost 30 FPS tearing-free panning, which I'm really proud of.

## Undo

[FACT-CHECK: Timeline of undo work, the hammering fix, the low-res preview attempts]

As I remember it, undo was the most painful one because I felt like I got to a good point where cold render was pretty fast, we'd fixed panning, we'd fixed tearing. I think by then we'd fixed even déjà vu. And then I added undo to V2.

[DEFINITION: Déjà vu is when you pan around — go right, down, left, up — and when you come back to where you started, it's already re-rendering again. An area that was rendered not too long ago, and you go back and see it cold render again. The name was coined by one of the agents, and it's a good name.]

I distinctly remember the very first version the agent built already had some smart stuff where it only re-rendered the blocks on the screen that needed it, which were pretty small. But what it meant in practice is that you were watching this line render in the middle of the screen. It just looked absolutely horrible.

And that's when it hit me that, well, it sounds very trite, but an undo is a redraw, and redraws are very expensive. And that's when it hit me that it was kind of ill-advised to leave undo this late. If I would have had that feature very early on, could I have made it faster? I'm not sure. But would I have had probably less pain? Possible.

The next bit I remember is the hammering thing. It took quite a few tries to get it right, that when you hammer undo or hammer redo, then the previous rendering gets cancelled because, well, that's how you do it. So it took quite a bit of iteration to get to where it is now, where you can hammer undo and redo and it will not mess it up.

And then I was still thinking about the UX because I still thought it was kind of bad. One thing we tried was to just show the absolute low-res cold render for the entire line or section, and then don't show the intermediate render, but then show the finalized cold render. The crude one was just so ugly.

And that's when I was like, having an hourglass icon while the undo settles would be better. We could actually try just drawing an hourglass and rendering the final one.

Initially the agent was like, if it takes less than 120 milliseconds, we don't show anything, we just show the final thing, so there's just a small lag, and if it's longer, we put up the hourglass. But I did not like the UX discrepancy. So I just said that we can just show the hourglass for even the short stuff. Just flash it and it's gone and it's fine. It's good to have consistent UX.

It was actually the hourglass that made it acceptable. We went from either not showing anything intermediate — which meant you were waiting 500 milliseconds with no visual feedback — or various bad versions of intermediate feedback, to the hourglass icon, which people know and recognize. That's what fixed the UX. It didn't quite fix the performance, but it fixed the UX.

Anything still unsatisfying? Honestly, no. I think we optimized the actual undo rendering pretty well. I think the hourglass UX is the least bad UX.

[FOOTNOTE: Undo/Redo keeps raster tile versions for history states you've already visited. Going back to a recently visited state is fast because we're just swapping the cached pixels back in.]

## Anti-Aliasing

And finally, there was anti-aliasing; the other thing we put in way too late. What I learned from that is if we'd started the optimization work with AA already being part of the renderer, things could have been faster. It's the one area where I feel like performance isn't quite where I want it.

It's a somewhat subtle thing on a screen this small, but it bothers me when it's not there.

## What the Agents Did (and Didn't Do)

They wrote all the code. I never touched a single line. They reviewed all the code. They did all the architecture review, all the code cleanup.

Ultimately this is vibe coded. It's just vibe coded with an engineering mindset.

GPT-5.6 Sol wrote most of the code, and overall it did a really good job. Sol is very detail-oriented, not the best at architecture, but pretty okay. Sol makes far fewer bugs.

Once I felt like Sol was stuck, I would ask it to pack up all the source code and give it to GPT-5.6 Pro to really review the whole thing — for bugs, performance, or architecture. That takes 60 to 90 minutes. And then I would feed that to Claude Fable.

Fable is both brilliant and deeply flawed, but when it's brilliant, it's brilliant. It's insane at architecture. And when I set thinking to xhigh and threw performance optimization at it, that was something else. Sol is good at catching Fable's bugs, and vice versa. The right shape was usually: write the code with Sol, have Fable review it.

I squeezed out all Fable usage I could of a single Anthropic subscription over three days.

Building this app became a game of: I want to make this as fast as possible, I want to get every ounce of performance out of this hardware, I'm gonna stack eighteen different tricks to make cold rendering faster, and I'm gonna keep throwing more agents and more prompts at it until it's not painfully slow.

The persistence is mine. The stubborn persistence is mine. Because at any time I could have just been like, yeah, forget this, I'm not gonna finish this.

They don't have fingers. The agent would be like, "this is running at 30 FPS." And then I do a glass test, and I'm like, no, it's not, and it's glitching.

My finger actually was a fairly important thing.

And there was another thing. If I let agents run too long by themselves and it felt like I understood less and less what they were doing, I just felt bad about it. And often it helped to stop and be like, okay, let's step back, explain this to me, or let's measure things again. When I felt lost, that was the signal to stop and regroup.

## Landing

So what did I build?

A vector graphics editor on a 1.8-inch screen. You can zoom in, zoom out — 25% to 400%. It has a minimap. You can draw, you can erase, you can choose from 32 colors. You can undo and redo, and you can hammer it. I'm proud that after many iterations, as far as I can tell, we got the SVG export right. That was a whole ordeal. You can export to PNG. Cold rendering is under 500 milliseconds. Panning is almost 30 FPS and tearing-free.

Now, you may ask, why would you use this for actually drawing things? And you really wouldn't. The entire existence of TinyDraw V2 is completely absurd, but that's kind of part of the charm. And it's nice to know that I built this.

I built something that's kind of weirdly fast and kind of silly, but it is, at the end of the day, a real vector graphics program. It's really fast for running on an ESP32-S3 with 8 megabytes of PSRAM where, as far as I can tell, PS is short for Pretty Slow.

I like the fact that I was persistent. I spent an ungodly number of hours over nine days to get it where it is now. And each time I got stuck, I was like, okay, let's give it to a more powerful agent, or try harder, have a demoscene mindset, or let's slow down, think it over, step back.

I'm proud that I kept pushing things. The agent is like, oh yeah, this is pretty fast. I'm like, okay, so I did my evil hairlines and it's not. And then, okay, back to another round of optimizations.

I like the fact that I semi-accidentally created a compo for a nonexistent demoparty.

I liked that it helped me rediscover my skill of picking up and becoming proficient in a domain really fast.

LLMs make getting started way, way, way, way easier. And that's important. But there's also the part where, at some point after you made the fun part, you're gonna run into slow graphics, you're gonna run into tearing, you're gonna run into this and that. And yeah, you can just tell the agent to make it faster, but I did a lot more than that.

TinyDraw V2 runs on Puck as well, where it's compiled to WASM and Puck runs it. It's not clock-accurate and has other limitations, but it reproduces the important bits really, really well.

[NOTE: This will be near a "Try it yourself in your browser" link to Puck]

## P.S. — Mistakes Made

[FACT-CHECK ALL OF THESE]

- [FACT-CHECK: Was this RP2350 or ESP32?] Wi-Fi blamed for psychedelic vertical stripes. Wi-Fi removed. Stripes remained.
- Requested 40/50/60 MHz SPI. All actually 40 MHz.
- GETSCANLINE and every control-register read: all zero.
- [NEEDS EXPANSION] Internal scratch predicted ≥40% savings. Measured −0.36%.
- Sacred 1.5 MiB export reserve. Actual peak 291,484 bytes.
- [NEEDS CONTEXT: What was this?] "512-slot" run. Actually 384.
- Word-mask window scans: 7-13% slower than byte-mask on ESP32. GCC-Xtensa emits callx8 memcpy libcalls.
- 4-sample SSAA: 808 ms. Dead.
- Color popup magenta. Black and white hiding the extra/missing byte swap.
- Pen-size selector also firing Redo. Both fires and the whole UI just gets messed up.
- There was a whole thing when I was testing exports, and there were two dots in the SVG on the top, and none in the PNG, and two squares on the actual app. Schrödinger's dot. Long story short, we did a render parity fix and a separate top-edge contact fix.
- Fuji classifier detour. "We've been at this for... over an hour?"
- Tear "fixed" near the zoom-minus button. After the next change, it moved two-thirds down the minimap. That was the last thing we fixed before tearing was gone.
- Flash-icache layout moves hot-loop timing ±2-3% per build.
- PSRAM placement matters: a 40 KB workspace mid-heap cost +9 ms; placed dead-last, 0 ms.
- [NEEDS CLARIFICATION: What does this mean?] Sub-500 ms benchmark. Stopped before pixels reached the glass.
- [NEEDS CLARIFICATION: What does this mean?] Mathematically exact AA optimization. Slower on almost every case.
- Evil hairlines absent from the supposedly good benchmark. Almost doubled cold time.
- [NEEDS CLARIFICATION: What was this?] 1×2 cold-render supertasks. Task watchdog before any timing result.
- Fast LOD. Deleted loops, hairpins, pressure peaks, and eraser dabs.
