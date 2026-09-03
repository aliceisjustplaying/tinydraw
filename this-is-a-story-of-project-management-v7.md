# How I Made a Fast Vector Drawing App For a Tiny Device Running on a Microcontroller

*that I still don't fully know how it works*

[VIDEO GOES HERE]

[Try it now in your browser!](https://tinydraw.ink/)

## This is a story of project management.

It did not start out as one. It started as a fun vibecoding project for [a meetup of small microcontroller-powered devices](https://luma.com/tldraw-vp8y) hosted by [Steve Ruiz](https://x.com/steveruizok) of [tldraw](https://tldraw.com/) who's been shilling them on the timeline for months now. My RSVP being accepted on Luma was the push I needed to order one of these for myself and I wanted to demo something there. This was on a Friday but the device would not arrive until Sunday and I wouldn't be able to start testing on it until Monday morning.

I needed an idea and usually I have a hard time finding one. I did have one I thought was obvious, *too obvious*, surely someone has written a tiny tldraw clone for that [Waveshare ESP32-S3 touchscreen thing](https://www.waveshare.com/esp32-s3-touch-amoled-1.8.htm), right? *Right?* After scrolling Steve's timeline the answer was, as far as I could tell, surprisingly a "no". So on a Sunday afternoon I got working. I asked GPT-5.6 Pro if it's possible and they helped me with the initial steps, then I started working with GPT-5.6 Sol first with an SDL native macOS target and then a QEMU one.

![](https://substackcdn.com/image/fetch/$s_!wZRK!,f_auto,q_auto:good,fl_progressive:steep/https%3A%2F%2Fsubstack-post-media.s3.amazonaws.com%2Fpublic%2Fimages%2F22b8063b-5fd3-4cc3-8b84-793564f291e4_478x600.png)

Steve's thesis was: agents are good at writing code for these gadgets now so just get one, point your agent to it and tell them what you want. And by the end of Sunday, I had something: a raster drawing app that incorporated the ideas and code of the [perfect-freehand](https://github.com/steveruizok/perfect-freehand) library to make the strokes pretty and simulate pressure with velocity, the same library that initially powered tldraw.

And I got it fast! Or so I thought until I plugged in the device Monday morning and watched as it was rendering things at glacial speed with inking being choppy and laggy on top of that. So I spent the rest of the day until the meetup pushing agents to make it fast, with the current state of the art prompting like "i can see the tile-updating-thingy" – and they delivered. [tinydraw](https://github.com/aliceisjustplaying/tinydraw) v1 was born.

I had an app with a 2×2 screen size canvas, pen and eraser, 12 colors and 4 line sizes, the inking was buttery smooth, panning was fast.

At the meetup I showed tinydraw to Steve and he liked it so much that when it was time to demo he pointed at me saying "You first", and it was at that point I felt like I made it: a big part of my motivation was to impress him, specifically, *and I did*.

I met a lot of cool people at the meetup, saw many interesting projects, found future collaborators and tinydraw impressed many others as well, which made me happy and proud.

## Even before my talk though, I wanted to aim higher.

What if tinydraw were a more real tldraw? Vector graphics, infinite canvas, arbitrary zoom levels, the works, with it being fast to boot? People have been writing drawing programs like this since at least the 80s for computers slower than the [ESP32-S3](https://www.espressif.com/en/products/socs/esp32-s3) today. [Adobe Illustrator 1.0](https://www.youtube.com/watch?v=fPD30I0g2D8) was released in March 1987 on the Macintosh, the fastest one at the time running an 8 MHz [Motorola 68000](https://en.wikipedia.org/wiki/Motorola_68000) CPU.

So a day after the meetup, I started working on the tinydraw v2 feasibility prototype and spent the next 26 hours in a fever dream to get something that proved this was possible. At least that was my agents' perspective. I was only an hour in when I told Sol:

> *please step back and think it feels like you're flailing. worth stepping back and thinking at these times. you got this.*

They had far more doubt about it than I ever did, but even I started questioning things a few hours in when I once again interjected:

> *"I put in quite a bit of time and money and tokens in this already… I need to know if it's worth investing more"*

This was maybe the first big moment when I made the mistake of letting the agents work way too long without me understanding what they were doing and why while getting increasingly frustrated. Stopping and asking helped.

My doubt came from not understanding, from not being able to judge whether continuing was worth it because I no longer knew what the agents were doing.

And the research during the prototype did temper my ambitions a bit. I compromised on not having a truly infinite canvas and only fixed power of two zoom levels.

But by the end, I had something: a renderer system that was proven to be not right for this and yet more proof this was doable. So we scrapped most of the prototype and started working on the real version.

We as in: me and my agents, with GPT-5.6 Sol (high) as the workhorse and GPT-5.6 Pro and Claude Fable 5 for code reviews, architecture and further optimizations. They wrote and reviewed all the code and came up with most of the architecture while I provided the persistence, the historical intuition, my nascent project management skills and perhaps most importantly my eyes and fingers.

By the end of the fever dream prototyping phase I had certain performance targets I wanted to hit: cold (uncached) renders under 500 ms at all zoom levels (1×-16×), tear-free panning as fast as possible, later targeting 24 fps with the stretch goal of 30; undo and redo limited only by how much you've drawn, anti-aliasing and proper SVG export next to the PNG one we already had in v1.

Nine days and change after starting we (me managing my agents) hit those targets and then some, with some bumps along the way and I tagged tinydraw v2's release version.

Not that I expected any users. I did this because I wanted to prove I could do this and to impress others.

## The bumps along the way were many.

At one point I was hand-holding a [Fuji X-T5](https://www.fujifilm-x.com/en-us/products/cameras/x-t5/) with the [Sigma 56mm f/1.4](https://sigmauk.com/56mm-f1-4-dc-dn-c) lens (which has terrible magnification) pointing at the Waveshare device's 1.8" AMOLED screen. I had no tripod. And I recorded a video of different flashing patterns at 1080p@240fps, set to 1/1024 (to reduce flicker) at f/4 for over 2 minutes in order to help my agent debug tearing. The video was going to tell them if the screen does something in a certain way or the opposite. And it worked! This was the turning point and the penultimate step in getting panning both fast and tear-free, almost 30 fps, at the physical limit of the panel/controller.

**Glass testing**, as my agents called it, was something only I can do, finger on screen. The agent would say "tearing is fixed and panning is fast now" and I'd try it and it's tearing and glitching like crazy and be like actually no, here's a picture, it looks bad, let's get back to work. Fingers and eyes—only I had those for this project.

After the Fuji episode and *before* getting it fully working, it seemed fine except for the fact it was tearing, so I'd narrate my glass test to the agent (voice to text, lightly edited):

> *Alright, it's 11:57 PM. Probably the last thing I try before I go to sleep. Hopefully this works and we can move on to other things. I'm gonna start with some normal \[drawing\] stuff at 1x as I always do. \[…\] some evil hairlines… okay, let's do 4x \[…\] alright, let's zoom in further. I'll start panning. That's faster. That's faster panning, that's for sure and tearing is back. Good news, if we can call it good news, that it seems to be tearing at the exact same spot… yeah, so tearing is back, but it's tearing at one exact spot, on the UI it's the top grey edge of the minus button roughly \[…\] So yeah, panning is fast now, but it's tearing, but at least it's tearing in a very predictable way. So what gives?*

and then they'd change something and I'd test again and this time it's tearing somewhere else:

> *tear is not gone instaed\[sic\] it moved. panning is slower. tearing is now way further down somewhere idk 2/3rdsish of the minimap*

and after that, finally:

> *yes tearing seems to be gone. what are the next steps? also please stop trying to put me to sleep*

with me scrubbing the screen as fast as humanly possible to see if it really is fixed.

And it's something I have not done often enough: a recurring pattern was variations on the above. I ask for something, agents spend a lot of time writing it, report it working and/or fast but once my finger is on the glass this turns out to be not the case, or not as much the case. If I could do it again I'd do more glass tests more often.

## The most extreme example was the aforementioned evil hairlines.

I repeatedly ran into something passing our ever-growing performance test suite but as soon as I busted out the smallest pen and started scribbling evil hairlines on the screen—dense, overlapping, long thin strokes, often a few of them stacked on top of each other—it was time to go back and optimize cold (uncached) rendering or caching or inking more.

If the issue seemed bad enough I'd have them pack up the source code and the logs for 5.6 Pro and wait ~40-100 minutes for the result then feed that to Fable, or get Fable working on it already and give them Pro's report in between and tell them this was done against a slightly older version of the code.

And Fable. Fable is brilliant. They're not particularly detail-oriented (I have Sol for that) but as of now they feel unparalleled when it comes to architecture and optimizing code for microcontrollers, especially when set to xhigh effort. This made an already scarce resource even more of one; I did what I could to get the most out of a single Anthropic subscription and still blew thru it in like 3 days…

And yet, and yet. Keeping Fable around for the tough bits paid off.

## Discernment is another thing I brought to the table.

That, and persistence. I always knew this was possible, my confidence barely wavered and the agents were always the more skeptical ones. And yet, I kept pushing them for more again and again, and when you *do that the right way* their results go way beyond what they thought was possible. Like, I got things a whole lot faster than my targets after I formally wrapped the project! More on that later.

Is this what people refer to as "taste" these days?

I sign off many of my messages to agents with some form of "thank you and good luck, you got this."

Early in the project I felt stuck and asked a friend to help, a friend who is likely in the top 100 systems engineers in the world and I'm not exaggerating here, and she just went:

![](https://substackcdn.com/image/fetch/$s_!OzF5!,f_auto,q_auto:good,fl_progressive:steep/https%3A%2F%2Fsubstack-post-media.s3.amazonaws.com%2Fpublic%2Fimages%2F0cd67f44-d4ee-406d-af09-4125eb14d195_1078x870.png)

And I do think telling the agents to think about mechanical sympathy, elegance and most importantly the demoscene mindset again and again helped.

In case you're not familiar: the [demoscene](https://en.wikipedia.org/wiki/Demoscene) is a subculture of programmers who compete to make the most impressive visuals and audio under real and/or artificial constraints. The point of a demo is to squeeze out every bit of performance, and then some, out of a computer, a device, a piece of hardware that's usually underpowered by today's standards to create something that… well, something that *looks cool*. Something that you wouldn't think was possible in the first place. You'd usually submit a demo like this for a [demoparty](https://en.wikipedia.org/wiki/Demoscene#Parties), where the coolest one is picked.

The first version of tinydraw was the fun thing I built over less than two days for Steve's meetup, where I met many other people showing off their fun projects. And sure, the meetup definitely had some spiritual elements of a demoparty, but ultimately it wasn't about making the fastest, coolest thing.

tinydraw v2, on the other hand, had the more or less explicit goal of making it more real, making it vector and most importantly, making it as fast as humanly possible on an ESP32-S3, which is about as fast as a Pentium II CPU from 1997, except most of the memory is about half a decade slower.

## There were other hard lessons learned during this project.

I'm a software engineer but not a systems engineer. The first time I wrote serious code in C was in the spring of 2024, and that was already heavily AI-assisted.

This project was in C++20 and the agents wrote and reviewed all the code. We stacked 18 different tricks and optimizations (as well as rejecting 20 others along the way) to get the performance I wanted. But I don't understand, or only somewhat understand, why we needed them and how they work.

I know the ESP32-S3 has two cores and pinning inking to the second core helped a ton with performance. I know that it has 512 KB of fast SRAM shared between code and data (moving the rasterization core into the fast side gave us an immediate 7-12% compute boost) and 8 MB [PSRAM](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/external-ram.html) where the PS is short for "pretty slow" with the bandwidth. One of the main goals was for it to be the bottleneck rather than the CPU. I know that we needed to cache tiles as much as possible and give all free PSRAM to that. I know that you can export PNGs and SVGs with surprisingly little PSRAM usage. I know that storing the ink coordinates on a 4× finer grid (a sixteenth of a world unit instead of a quarter) made the curves a whole lot smoother without any real penalty. I know that the unit of a line we care a lot about is called a "chord" though I still don't exactly know how it works. And more.

With AI you can just see this interesting thing on Twitter, oh, I want to make something like this, buy the \$30-40 device from Waveshare, plug it in, point Claude at it and just tell Claude what you want. For one-shot or smaller projects this works, but for anything more complex you will hit a wall where you start to need to think more about how you manage your agents, how much you need to know for them to work efficiently, all the new forms of engineering, I suppose. But again, you can do the whole thing without knowing embedded engineering, for example.

Five years ago, if you wrote a piece of software, it was normal and even expected that you knew what it does, how it works. You can explain it, you can argue it, etc. This is still the standard in many places, especially at workplaces, even though it's starting to shift. And while this is a pet project, I'm proud of what I've shipped. And I'm proud of what I built.

Being a project manager does not often come naturally to a software engineer as it's a different and at best only somewhat overlapping set of skills. You need to delegate but you need to know what your people or indeed agents are capable of, how much they need managing, how they need to be managed and so on.

If I tell Claude to make atomic commits of a large number of changes they make granular commits. If I tell GPT-5.6, they interpret it the opposite way and make one giant commit. So I've finally learned to just say "granular".

[Seconds_0](https://twitter.com/seconds_0), the best vibecoder I know, is a 100x PM who still does not know how to code and yet he built and shipped very impressive projects like [ChinaRxiv](https://chinarxiv.org/) and [SovietRxiv](https://sovietrxiv.org/).

I also know that adding Undo/Redo only toward the end bit me hard; the first version was very slow, indeed I was watching a line render block by block in the middle of the screen and it was around then it dawned on me that an undo is a re-render, and those are very expensive even after optimizations unless cached. We got it to a speed and UX I'm mostly happy with. Hell, we even fixed déjà vu, the phenomenon Fable coined for when panning around you'd see tiles cold render that you shouldn't because *you were just there*.

And if we're talking about taste, UX is also your responsibility. We iterated through many versions of how Undo/Redo should feel, many suggestions from the agents just *feeling wrong* until we settled on the hourglass + not showing intermediate rendering setup.

A lot of care went into making inking feel good, be fast and accurate. I know that switching to a "Vector-first Authority" was one of the biggest unlocks. I only somewhat grok how it works. Or that changing perfect-freehand's streamline constant (how much smoothing it applies) from 0.35 to 0.4 was the sweet spot. One of the last bugs we hunted down before releasing was phantom dots that would show up on the SVG export at the top of the document, render as tiny squares on the screen and not be present at all on the PNG export. These were related to how taps were interpreted at the edge. Hell, this was only one of two phantom dots bugs, the other being related to putting your finger down but then deciding not to draw a line. But hey, after fixing all these, we finally had a correct SVG export with proper paths for all lines.

*\[after finishing most of this section, I have, of course, stumbled upon another SVG bug we had to fix.\]*

## I have these and so many more stories.

I'm proud of the final product and mostly happy with performance. tinydraw v1 was mostly the fun vibecoding version while v2 slowly but surely turned into a demo for a demoparty of all the people who scroll the timeline or read my substack/blog.

The demoscene rewards craft, effort and, well, suffering. And I put a lot of effort and definitely suffering into making tinydraw v2. But does it count as craft, in the way I built it, with agents? A whole lot of people would say no. I'm leaning toward yes.

And the thing is, I kind of prefer tinydraw v1 for drawing. But the absurdity of v2 is part of the charm. Who in their right mind would draw anything serious in a vector graphics editor running on a microcontroller and a tiny 1.8" screen? **Definitely not me.** If you want to though, it's here, it's shipped, you can [try it](https://tinydraw.ink) and if you do draw something serious in it, I'd love to see that.

I did it, I persisted, I learned a lot about managing agents, I shipped and by the end started having follow-up ambitions that I'd love to pursue but am constrained by tokens. More on that below.

I'm particularly proud of shipping something complex: like most vibecoders I know, I also have a large graveyard of unfinished and never-shipped projects. But not this one. This one's out there.

We're all project managers now whether we like it or not, and while understanding the engineering of what you work on remains important, the degree to which you need to is starting to shift.

***

I tagged the codebase as v2 on Wednesday, added the [WASM](https://en.wikipedia.org/wiki/WebAssembly) version for [Puck](https://github.com/s0lness/puck) on Thursday, and then it was just writing this blogpost and shooting the video.

## That was supposed to be the end of it.

As I was writing this blogpost slowly and painfully, way too late on [@literalbanana](https://twitter.com/literalbanana)'s curve, I spent a couple hours on Saturday talking with my friend [Janka](https://x.com/lubieowoce), who helped me write this post and reframe it into what it is today.

![](https://substackcdn.com/image/fetch/$s_!88ou!,f_auto,q_auto:good,fl_progressive:steep/https%3A%2F%2Fsubstack-post-media.s3.amazonaws.com%2Fpublic%2Fimages%2Fc30d73ca-f792-4c9d-8e37-758da80bb8ee_1704x1398.jpeg)

That evening I kept checking the time, waiting for my Anthropic reset at 8pm because in 2026 I pay \$200 to use arguably the most powerful AI model in the world, and that money doesn't even go that far.

Around this time I checked Codex and saw I had 20 minutes until my Codex reset with 18% of my quota left, and I'm like, okay. I've asked the agents many times now if it's worth doing assembly, and each time they were like, no, it's not appropriate at this time, or it's only worth doing assembly after settling many other things. And I was like, yeah, I'm just gonna tell this to GPT-5.6 Sol set to xhigh, turn on fast mode, yolo, let it rip:

> *oh hey i have a lot of tokens to burn so let's go. do a disassembly on the esp32s3 binary (the native one, not the wasm one) and see if there are places where we can get performance wins in all areas (cold renders, panning speed, undo/redo, anti-aliasing, caching etc.) feel free to use subagents liberally be thorrough and be fully autonomous. i am very happy to hand-roll assembly or change our c++20 code to generate better assembly to squeeze out more performance, especially if the xtensa gcc is inefficient in some ways you can also research that i vaguely remember that we want to avoid memcpy for example but there's probably more!! anyways. fully autonomous! go as long as possible! good luck you go this*

Two minutes later I added:

> *(i have the device connected feel free to flash whatever) (don't stop) (research all possible ways we can make things faster with assembly)*

Did I know for a fact there's probably more? No. But it's a reasonable guess, and it's a way to motivate the agent.

Sent at 6:48 p.m. As I'm dictating this, it's 9:22 p.m., and they're still going. So far there have been 43 subagents doing research and trying things. They just kicked off three more. They already used up 22% of my tokens since my reset, at least I have a banked reset. They're talking about the final three audits and I'm pretty sure this is not the first time they said final.

Then around 10:40 p.m. they did finally wrap and my god they sure delivered:

![](https://substackcdn.com/image/fetch/$s_!F8LP!,f_auto,q_auto:good,fl_progressive:steep/https%3A%2F%2Fsubstack-post-media.s3.amazonaws.com%2Fpublic%2Fimages%2F56a50fa6-3e2e-4076-b008-2ef79f1a561e_1100x464.png)

> nine hand-written [Xtensa](https://en.wikipedia.org/wiki/Tensilica#Xtensa_configurable_cores) PIE ([SIMD](https://en.wikipedia.org/wiki/Single_instruction,_multiple_data)) kernels in the final ELF, output bit exact\
> 35 experiments accepted that night alone, 23 rejected or superseded.

(Note: none of the assembly wins are in the WASM build due to their nature of being written for the hardware specifically)

Almost everything got faster! Sometimes a lot faster!

And I do feel extremely vindicated. I let them go to town, they ran the benchmarks. I have not glass tested yet. It's quite possible that once I glass test it, I find some bugs, like many times before. I may be repeating the mistake of not glass testing early enough. I'm also a little mad at myself that I wasn't pushing them earlier, but here we are.

These four hours were the project again, just on a smaller scale.

## Coda

I just did a glass test, and as far as I can tell, they did not break anything, the performance is as they said, cold rendering got faster, anti-aliasing got faster. Free wins in assembly.

![](https://substackcdn.com/image/fetch/$s_!iec9!,f_auto,q_auto:good,fl_progressive:steep/https%3A%2F%2Fsubstack-post-media.s3.amazonaws.com%2Fpublic%2Fimages%2Fee62d671-6942-4c03-8f4e-9090e19518a3_1108x730.png)

(LobsterFalcon is [@seconds_0](https://twitter.com/seconds_0) on Twitter)

Would I have run this if it wasn't for the call with my friend Janka? I don't know. But I would like to think that talking about all this project management stuff contributed to me being like: yeah, you know what, I should actually just tell them to disassemble it. And it paid off.

## Coda of the Coda

[Tibo announced another reset](https://x.com/thsottiaux/status/2091407991736332689) so I just asked 5.6 Pro to write me a plan to rearchitect cold rendering incorporating AA. And oh my god I need to start telling 5.6 more to cut all the "do thing X instead of obviously wrong thing Y" bits because I know why they do it but it drives me up the wall and is almost always pointless information I did not need. I asked 5.6 Pro to rewrite the whole rearch document with the negative stuff limited to a bulleted list at the very end on the off chance they are possibly, maybe useful.

Then I fed that to Fable for adversarial reviewing and more research (with 5.6 Sol subagents) and then fed that back to 5.6 Pro for an adversarial review of the…

## About ambition; and a request

This project gave me confidence to aim much higher to the point that I'm now constrained by the money I can reasonably (ha) spend on tokens. What I want to do next is fix the Xtensa GCC fork so it supports vectorization for the ESP32-S3; while upstream GCC has generic vectorization the Xtensa one currently can't do any ESP32-S3 specific PIE/SIMD. I want other software engineers to get the benefits of SIMD vectorization without having to tell their agents to write a bunch of assembly. If someone wants to fund at least 20 OAI and 10 Anthropic subscriptions or give me at least \$30k in tokens do let me know; you could be credited for helping the embedded community.

## P.S.: a random assortment of mistakes made of which we sure did a lot

As I was writing this I asked my editor (Fable) to just dump as many interesting ones here as possible. I know what *some* of these were.

- Wi-Fi was blamed for psychedelic vertical stripes, it was actually 80 MHz SPI causing issues

- Requesting 40/50/60 MHz SPI all gives you 40 MHz

- GETSCANLINE and every control-register read returns zero which is… yeah.

- Internal scratch predicted ≥40% savings. Measured −0.36%.

- We had a whole "sacred" 1.5 MB reserved for the SVG/PNG exports but the actual peak memory usage was 291,484 bytes, that freed up a LOT of space for more caching and also decided cache can be evicted as needed during export because why not

- Had a "512-slot" run that was actually 384 I think this was also caching?

- Word-mask window scans were 7-13% slower than byte-mask on ESP32. As we've learned, GCC-Xtensa emits callx8 memcpy libcalls. This was I think the first disassembly rabbit hole.

- 4-sample [SSAA](https://en.wikipedia.org/wiki/Supersampling) cost us 808 ms so that was scrapped.

- There was a byte swap bug that made the color popup outline magenta instead of blue. "Black and white hiding" is what I'm being told was the problem

- During a late glass test I did one where the pen-size selector also fired Redo — the button below it on the screen — at the same time because getting tap targets right on a screen this small is hard. This glitched the UI up really bad. The tap targets are now 30% less bad.

- There was a whole thing when I was testing exports, and there were two dots in the SVG on the top, and none in the PNG, and two squares on the actual app. Schrödinger's dot. Long story short, we did a *checks notes* "render parity fix" and later a "top-edge contact fix" and that was that.

- After I gave the agent the slo-mo video from my Fuji to figure out whatever they needed they spent way too long trying and failing to build a classifier (*"We've been at this for… over an hour?"*) anyways after I stopped them in frustration they switched to making a [contact sheet](https://en.wikipedia.org/wiki/Contact_print) instead which solved the problem.

- Flash-icache layout moves hot-loop timing ±2-3% per build. This was very silly and I don't think we actually fixed it; we just made some benchmark numbers fail less hard or something? Especially when at 16× zoom we were juuuuuust above the 500 ms target.

- PSRAM placement matters: a 40 KB workspace mid-heap cost +9 ms but if placed dead-last it's 0 ms. Uh, what Fable said.

- Had a sub-500 ms cold rendering benchmark that stopped before the pixels actually reached the glass. I honestly forgot why or when it happened but it sounds bad and hey at least we caught it.

- Using mathematically exact AA optimization was slower in almost every case.

- 1×2 cold-render supertasks… Task watchdog before any timing result… Fast LOD… Deleted loops… hairpins… pressure peaks… eraser dabs… I don't know what most of these mean.

*The agents wrote all the code, I wrote all these words. Edited by GPT-5.6 Sol, Every's Spiral, Claude Fable 5 and most importantly my friend* [*Janka*](https://github.com/lubieowoce)*.*
