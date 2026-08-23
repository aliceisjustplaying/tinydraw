This is a story of project management.

It did not start out as one. It started as a fun vibecoding project for [a meetup of small microcontroller-powered devices](https://luma.com/tldraw-vp8y) hosted by [Steve Ruiz](https://x.com/steveruizok) of [tldraw](https://tldraw.com) who's been shilling them on the timeline for months now. My RSVP being accepted on Luma was the push I needed to order one of these for myself; I wanted to demo something there. This was on a Friday; the device would not arrive until Sunday and I wouldn't be able to start testing on it until Monday morning.

I needed an idea and usually I have a hard time finding one. I did have one I thought was obvious, *too obvious*, surely someone has written a tiny tldraw clone for that [Waveshare ESP32-S3 touchscreen thing](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm), right? *Right?* After scrolling Steve's timeline the answer was, as far as I could tell, surprisingly a "no". So on a Sunday afternoon I got working. I asked GPT 5.6 Pro if it's possible and it helped me with the initial steps, then I started working with Sol first with an SDL native macOS target and then a QEMU one. 

Steve's thesis was: agents are good at writing code for these gadgets now so just get one, point your agent to it and tell them what you want. And by the end of Sunday, I had something: a raster drawing app that incorporated the ideas and code of the [Perfect Freehand](https://github.com/steveruizok/perfect-freehand) library to make the strokes pretty and simulate pressure with velocity, the same library that initially powered tldraw.

And I got it fast! Or so I thought until I plugged in the device Monday morning and watched as it was rendering things at glacial speed with inking being choppy and laggy on top of that. So I spent the rest of the day until the meetup pushing agents to make it fast, with the current state of the art prompting like "i can see the tile-updating-thingy" – and they delivered. tinydraw v1 was born.

I had an app with a 2x2 screen size canvas, pen and eraser, twelve colors and 4 line sizes, the inking was buttery smooth, panning was fast.

At the meetup I showed tinydraw to Steve and he liked it so much when it was time to demo he pointed at me saying "You first", and it was that point I felt like I made it: a big part of my motivation was to impress him, specifically, *and I did*. 

I met a lot of cool people at the meetup, saw many interesting projects, found future collaborators and tinydraw impressed many others as well, which made me happy and proud.

Even before my talk though, I wanted to aim higher. What if tinydraw would be a more real tldraw? Vector graphics, infinite canvas, arbitrary zoom levels, the works, with it being fast to boot? People have been writing drawing programs like this since at least the 80s for computers slower than the ESP32-S3 today.

[NOTE: in one of the documents i have some stuff how i did research on adobe illustrator 1.0 and what was the fastest mac at the time and how that compares to the esp32-s3; idk if to include that or not my inner nerd wants to but if it breaks the structure/whatever or weakens it then no]

So a day after the meetup, I started working on the tinydraw v2 feasibility prototype and spent the next 26 hours in a fever dream to get something that proves this was possible. At least that was my agents' perspective. They had far more doubt about it than I ever did, but even I started questioning things around hour 20 when I told Sol 

> "I put in quite a bit of time and money and tokens in this already... I need to know if it's worth investing more"

This was maybe the first big moment when I made the mistake of letting the agents work way too long without me understanding what they're doing and why and getting increasingly frustrated. Stopping and asking helped.

[NOTE: still not happy with the transition into this but I do think this needs to be said somehow]

And the research during the prototype did temper my ambitions a bit. I compromised on a not having a truly infinite canvas and only fixed power of two zoom levels. 

But by the end, I had something; a renderer system that was proven to be not right for this and yet more proof this was doable. So we scrapped most of the prototype and started working on the real version.

We as in: me and my agents, with GPT 5.6 Sol (high) as the workhorse and GPT 5.6 Pro and Fable 5 for code reviews, architecture and further optimizations. They wrote and reviewed all the code and came up with most of the architecture while I provided the persistence, the historical intuition, my nascent project management skills and perhaps most importantly my eyes and fingers.

[NOTE: am i giving conclusion-shaped stuff to readers too early or is this fine?]

By the end of the fever dream prototyping phase I had certain performance targets I wanted to hit: cold renders under 500ms at all zoom levels (25%-400%) tear free panning as fast as possible, later targeting 24 fps with the stretch goal of 30; undo and redo limited only by how much you've drawn, anti-aliasing and proper SVG export next to the PNG one we've already had in v1.

Nine days and change after starting we (me managing my agents) hit those targets and then some, with some bumps along the way and I tagged tinydraw v2's release version.

Not that I expected any users. I did this because I wanted to prove I can do this, to impress others and because I'm lonely and it numbs the pain. [NOTE: yeah unsure about that last one]

The bumps along the way were many. At one point I was pointing a Fuji X-T5 with the Sigma 56mm f/1.4 lens (which has terrible magnification) at the screen of the Waveshare device's 1.8" AMOLED screen recording a video of different flashing patterns at 1080p@240fps, set to 1/1024 (to reduce flicker) at f/4 handheld as I had no tripod for over 2 minutes in order to help my agent debug tearing; the video was going to tell it if the screen does something in a certain way or the opposite. And it worked! This was the turning point and the penultimate step in getting panning both fast and tear-free, almost 30 fps, at the physical limit of the panel/controller.

Glass testing, as my agents called it, was something only I can do, finger on screen. The agent would say "tearing is fixed and panning is fast now" and I'd try it and it's tearing and glitching like crazy and be like actually no, here's a picture, it looks bad, let's get back to work. Fingers and eyes, that only I could do.

Like after the Fuji episode and before getting it fully working; it seemed fine except for the fact it was tearing at one predictable spot, always the same spot, so I'd tell the agent "tearing back at one predictable spot, top grey edge of the minus button" and then they'd change something and I'd test again and this time it's tearing somewhere else ("moved to roughly two-thirds down the minimap") and only after that we got to the blessed "yes tearing seems to be gone." with me scrubbing the screen as fast as humanly possible to see if it really is fixed.

And it's something I have not done often enough: a recurring pattern was variations on the above. I ask for something, agents spend a lot of time writing it, report it working and/or fast but once my finger is on the glass this turns out to be not the case, or not as much the case. If I could do it again I'd do more glass tests more often.

The most extreme example were what I christened evil hairlines. I repeatedly ran into something passing our increasingly growing performance test suite but as soon as I busted out the smallest pen and started scribbling evil hairlines on the screen - dense, overlapping, long thin strokes, often a few of them stacked on top of each other, it was often time to go back and optimize cold rendering or caching or inking more.

> please step back and think it feels like you're flailing. worth stepping back and thinking at these times. you got this.

[✎ 8/11, 20:37. NOTE: i suppose this is a bit cheating bc this comes from the fever dream phase so idk if this should be elsewhere or idk]

If the issue seemed bad enough I'd have them pack up the source code and the logs for 5.6 Pro and wait ~40-100 minutes for the result then feed that to Fable, or get Fable working on it already and give it Pro's report inbetween and tell them this was done against a slightly older version of the code.

And Fable. Fable is brilliant. It's not particularly detail-oriented (I have Sol for that) but as of now they feel unparalleled when it comes to architecture and optimizing code for microcontrollers, especially when set to xhigh effort. This made an already scarce resource even more one; I did what I could to get the most out of a single Anthropic subscription and still blew thru it in like 3 days...

Keeping Fable around for the tough bits paid off though; discernment is another thing I brought to the table, sharing some but most definitely not all with the agents. Perhaps a gestalt of this is what people refer to as "taste" these days.

That, and persistence. I always knew this was possible, my confidence barely wavered and the agents were the ones always more skeptical. And yet, I kept pushing them for more again and again, and when you *do that the right way* their results went way beyond what they thought was possible.

Early in the project I felt stuck and asked a friend to help, a friend who is likely in the top 100 system engineers in the world and I'm not exaggerating here, and she just went: 

￼[screenshot of the demoscene convo; permission pending but 90% sure will be given]

And I do think telling the agents to think about mechanical sympathy, elegance and most importantly the demoscene mindset again and again helped.

(The demoscene is a subculture of programmers who compete to make the most impressive visuals and audio under real and/or artificial constraints. Old hardware, a certain file size, secret third thing)

There were other hard lessons learned during this project.

I'm a software engineer but not a systems engineer. The first time I wrote serious code in C was in the spring of 2023, and that was already heavily AI-assisted.

This project was in C++20 and the agents wrote and reviewed all the code. We stacked 18 different tricks and optimizations (as well as rejecting 20 others along the way) to get the performance I wanted. But I don't or only somewhat understand why we needed them and how they work.

I know the ESP32-S3 has two cores and pinning inking to the second core helped a ton with performance. I know that it has 512KB of fast SRAM shared between code and data (moving the rasterization core into the fast side gave us an immediate 7-12% compute boost) and 8MB PSRAM where the PS is short for "pretty slow" with the bandwidth and one of the main goals was it to be the bottleneck rather than the CPU. I know that we needed to cache tiles as much as possible and give all free PSRAM to that. I know that you can export PNGs and SVGs with surprisingly little PSRAM usage. I know that storing the ink coordinates on a 4× finer grid (a sixteenth of a world unit instead of a quarter) made the curves a whole lot smoother without any real penalty. And more.

AI can massively compress how a beginner can go from zero to one, but most of the time without the domain knowledge you do hit a wall and need to start thinking more while managing your agents, to dial in how much you need to know for them to work efficiently.

For a software development project it was normal and expected to be able to explain it and it still is at most places. Both socially and innately we reward effort and suffering, and there was a lot in this. I'm proud of what I've shipped but it *feels* weird to say "I built this" knowing that it does not conform to changing social expectations.

[NOTE: the above paragraph feels like the sentences don't quite flow together idk thoughts?]

Being a project manager does not often come naturally to a software engineer as it's a different and at best only somewhat overlapping set of skills. You need to delegate but you need to know what your people or indeed agents are capable of, how much they need managing, how they need to be managed and so on.

If I tell Claude to make atomic commits of a large number of changes it makes granular commits. If I tell GPT-5.6, it interprets it the opposite way and makes one giant commit. So I've finally learned to just say "granular".

The [best Vibecoder I know](https://x.com/seconds_0) is a 100x PM who still does not know how to code and yet he built and shipped very impressive projects like [ChinaRxiv](https://chinarxiv.org/) and [SovietRxiv](https://sovietrxiv.org/).

[NOTE: again, does this flow well as i go back and forth between... philsophizing vs more harder facts like below? v unsure]

I also know that adding Undo/Redo only towards the end bit me hard; the first version was very slow, indeed I was watching a line render block by block in the middle of the screen and it was around then it dawned on me that an undo is a re-render, and those are very expensive even after optimizations unless cached. I'm proud that we got it to a speed and UX I'm mostly happy about.

And if we're talking about taste, UX is also your responsibility. We've iterated through many versions of how Undo/Redo should feel, many suggestions from the agents just *feeling wrong* until we settled on the hourglass + not showing intermediate rendering setup.

And then there was Anti-Aliasing. 

Anti-Aliasing was Undo on steroids; that came last and the recurring pattern of optimizing one bottleneck hurting another showed hard. If we designed the cold rendering pipeline with AA in mind from the start it could have been a whole lot faster than it is now.

A lot of care went into making inking feel good, be fast and accurate. I know that switching to a Vector-first Authority was one of the biggest unlocks. I only somewhat grok how it works. Or changing perfect freehand's streamline constant (how much smoothing it applies) from 0.35 to 0.4 was the sweet spot. One of the last bugs we hunted down before releasing were phantom dots that would show up on the SVG export on the top of the document, render as tiny squares on the screen and not be present at all on the PNG export. These were related to how taps were interpreted at the edge. Hell, this was only one of two phantom dots bugs, the other being related to putting your finger down but then deciding not to draw a line. 

I'm proud our SVG exports have proper paths for all the lines.

I have these and so many more stories. I'm proud [NOTE: is it a refrain or is it one too many I'm proud bc it might be the latter] of the final product and mostly happy with performance with the exception of the aforementioned AA. tinydraw v1 was mostly the fun vibecoding version while v2 slowly but surely turned into a compo for a demoparty of all the people who scroll the timeline or read my substack/blog.

[NOTE: we have not introduced the demoscene at all so far we still need to do that ugh]

And the thing is, I kind of prefer tinydraw v1 for drawing. The absurdity of v2 is part of the charm. Who in their right mind would draw anything serious in a vector graphics editor running on a microcontroller and a tiny 1.8" screen? Definitely not me. If you want to though, it's here, it's shipped do DM me on twitter your creations I'd love to see them.

I did it, I persisted, I learned a lot about managing agents, I shipped and by the end started having follow-up ambitions that I'd love to do but am constrained on tokens.

I'm particularly proud of shipping something complex: like most vibecoders I know, I also have a large graveyard of unfinished and never-shipped projects. But not this one. This one's out there.

We're all project managers now whether we like it or not, and while understanding the engineering of what you work remains important, the degree you need to is starting to shift.

# Coda

I tagged the codebase as v2 on Wednesday, added the WASM version for [Puck](link goes here) on Thursday, and then it was just writing this blog post and shooting the video. That was supposed to be the end of it.

As I was writing this blogpost slowly and painfully, way too late on @literalbanana's curve [NOTE: image should go above maybe?] I spent a couple hours on Saturday talking with my friend [Janka](https://x.com/lubieowoce), helping me write this post and reframe it into what it is today. As I was looking at the time, impatiently waiting for my Anthropic reset at 8pm, I checked Codex and I had like 20 minutes until my Codex reset and 18% of my quota left, and I'm like, okay. I asked the agents many times now if it's worth doing assembly, and each time they were like, no, it's not appropriate at this time, or it's only worth doing assembly after settling many other things. And I was like, yeah, I'm just gonna tell this to GPT-5.6 Sol set to xhigh, turn on fast mode, yolo, let it rip:

> oh hey i have a lot of tokens to burn so let's go. do a disassembly on the esp32s3 binary (the native one, not the wasm one) and see if there are places where we can get performance wins in all areas (cold renders, panning speed, undo/redo, anti-aliasing, caching etc.) feel free to use subagents liberally be thorrough and be fully autonomous. i am very happy to hand-roll assembly or change our c++20 code to generate better assembly to squeeze out more performance, especially if the xtensa gcc is inefficient in some ways you can also research that i vaguely remember that we want to avoid memcpy for example but there's probably more!! anyways. fully autonomous! go as long as possible! good luck you go this

Two minutes later I added:

> (i have the device connected feel free to flash whatever) (don't stop) (research all possible ways we can make things faster with assembly)

Did I know there's probably more for a fact? No. But it's a reasonable guess, and it's a way to motivate the agent.

Sent at 6:48 p.m. As I'm dictating this, it's 9:22 p.m., and they're still going. So far there have been 43 subagents doing research and trying things. It just kicked off three more. It already used up 22% of my tokens since my reset, at least I have a banked reset. It's talking about the final three audits and I'm pretty sure this is not the first time it said final.

Then around 10:40 p.m. they did finally wrap up and they sure delivered:

- cold render compute 18.92–41.87% faster (all 15 corpora/zooms)
- direct pan composition 51.59–51.65% faster
- RGB565 ring staging 57.63–57.75% faster
- saturated 604-slot cache tour 39.15% faster
- undo/redo worst-case 21.78–24.24% faster
- settled anti-aliased rendering 17.23–26.45% faster
- nine hand-written Xtensa PIE (SIMD) kernels in the final ELF, output bit exact

35 experiments accepted, 23 rejected or superseded. 

And I do feel extremely vindicated. I let them go to town, they ran the benchmarks. I have not glass tested yet. It's quite possible that once I glass test it, I find some bugs, like many times before. I may be repeating the mistake of not glass testing early enough. I'm also a little mad at myself that I wasn't pushing them earlier, but here we are.

# Coda of the Coda

I just did a glass test, and as far as I can tell, the only thing broken is the SVG export in a way that seems not too hard to fix. They did not break anything — famous last words — but the PNG export is fine, the performance is as they said, cold rendering got faster, anti-aliasing got faster. Free wins in assembly. The SVG fix landed the next day.

[✎ SCREENSHOT: the DM exchange with the PM friend I got approval now] [NOTE: if the screenshot will go as is, it honestly holds up on its own with no explanation imo]

if it wasn't for the Janka call, I might not have run this assembly thing. I cannot know. But I would like to think that talking all this about project management contributed to me being like: yeah, you know what, I should actually just tell them to disassemble it. And lo and behold, they kept finding optimizations.

# Coda of the Coda of the Coda

Tibo announced another reset Monday 10pm my time so I just asked 5.6 Pro to write me a plan to rearchitecture cold rendering incorporating AA. And oh my god I need to start telling 5.6 more to cut all the "do not bits" because I know why it does it but it drives me up the wall and is almost always pointless information I did not need. I asked 5.6 Pro to rewrite the whole rearch document with the "do not"s limited to a bulleted list at the very end on the off chance they are possibly, maybe useful. 

Then I fed that to Fable for adversarial reviewing and more research (with 5.6 Sol subagents) and then fed that back to 5.6 Pro for an adversarial review of the... you know it goes now, don't you.

# About ambition; and a request

This project gave me confidence to aim much higher to the point that I'm now constrained by the money I can reasonably (ha) spend on tokens. If someone wants to fund at least 20 OAI and 10 Anthropic subscriptions or give me at least $30k in tokens do let me know; you could be credited for helping the embedded community by giving the Xtensa gcc built-in vectorization; while upstream GCC has generic vectorization the Xtensa can't do any ESP32-S3 specific PIE/SIMD.

# P.S.: mistakes made of which I sure did a lot

- Wi-Fi blamed for psychedelic vertical stripes. Wi-Fi removed. Stripes remained.
- Requested 40/50/60 MHz SPI. All actually 40 MHz.
- GETSCANLINE and every control-register read: all zero.
- Internal scratch predicted ≥40% savings. Measured −0.36%.
- Sacred 1.5 MiB export reserve. Actual peak 291,484 bytes.
- "512-slot" run. Actually 384.
- Word-mask window scans: 7-13% slower than byte-mask on ESP32. GCC-Xtensa emits callx8 memcpy libcalls.
- 4-sample SSAA: 808 ms. Dead.
- Color popup magenta. Black and white hiding the extra/missing byte swap.
- Pen-size selector also firing Redo. Both fires and the whole UI just gets messed up.
- There was a whole thing when I was testing exports, and there were two dots in the SVG on the top, and none in the PNG, and two squares on the actual app. Schrödinger's dot. Long story short, we did a render parity fix and a separate top-edge contact fix.
- Fuji classifier detour. "We've been at this for... over an hour?"
- Tear "fixed" near the zoom-minus button. After the next change, it moved two-thirds down the minimap. That was the last thing we fixed before tearing was gone.
- Flash-icache layout moves hot-loop timing ±2-3% per build.
- PSRAM placement matters: a 40 KB workspace mid-heap cost +9 ms; placed dead-last, 0 ms.
- Sub-500 ms benchmark. Stopped before pixels reached the glass.
- Mathematically exact AA optimization. Slower on almost every case.
- Evil hairlines absent from the supposedly good benchmark. Almost doubled cold time. [✎ optional soften: receipts frame this as two eras, 664→1,269 ms, not one clean A/B]
- 1×2 cold-render supertasks. Task watchdog before any timing result.
- Fast LOD. Deleted loops, hairpins, pressure peaks, and eraser dabs.

*The agents wrote all the code, I wrote all these words. Edited by GPT 5.6 Sol, Every's Spiral and Claude Fable 5.*
