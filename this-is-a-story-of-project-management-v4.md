This is a story of project management.

It did not start out as one. It started as a fun vibecoding project for a meetup of small microcontroller-powered devices hosted by steve ruiz of tldraw who's been shilling them on the timeline for months now. my rsvp being accepted on luma was push i needed to order one of these for myself; i wanted to demo something there. This was on a Friday; the device would not arrive until Sunday and I wouldn't be able to start testing on it until monday morning. I needed an idea, of course, and I usually have a hard time finding one but I did have one I thought was obvious. Surely someone has written a tiny tldraw clone for an esp32-s3 touchscreen device, right? As far as I could tell after scrolling Steve's timeline the answer was, surprisingly, no. So on a Sunday afternoon I got working. I asked GPT 5.6 Pro if it's possible and it helped me with the initial steps, then I started working with Sol first with an SDL native macOS target and then a QEMU one. 

Steve's thesis was: agents are good at writing code for these gadgets now so just get one, point your agent to it and tell them what you want. By the end of Sunday, I had something: a raster drawing app that incorporated the ideas and code the Perfect Freehand library to make the strokes pretty and simulate pressure with velocity, the same library that powers tldraw.

And I got it fast, or so I thought until I plugged in the device Monday morning and watched as it was rendering things at glacial speed with inking being choppy and laggy on top of that.

I spent the rest of the time until the meetup pushing agents to make it fast, and they delivered.

[✎ OPTION, approved as "can work" — your live 8/10 line as texture here or nearby, weave or skip: "i draw the stroke and i can see the tile-updating-thingy under-my-finger" (msg b313dbb2)] I had an app with a 2x2 screen size canvas, pen and eraser, twelve colors and 4 line sizes, the inking was buttery smooth, panning was fast.

I showed tinydraw to Steve at the meetup and he liked it so much when it was time to demo he pointed at me saying "You first".

I met a lot of cool people at the meetup, saw many interesting projects and tinydraw impressed many others as well.

Even before I talk though I wanted to aim higher. What if tinydraw would be a more real tldraw, vector graphics, infinite canvas, arbitrary zoom levels and so on, with it being fast to boot? After all people have been writing vector graphics apps since at least the 80s for devices slower than the ESP32-S3 today.

A day after the meetup I started working nominally on the tinydraw v2 feasibility prototype and spent the next 26 hours in a fever dream to get something that proves this was possible. The agents had far more doubt about it than I ever did; they did temper my expectations somewhat and I settled on a not quite infinite canvas and fixed power of two zoom levels.

And by the end I had something; a renderer system that was proven to be not right for this but at the same time more proof this was doable. So we scrapped most of the prototype and started working on the real version.

We as in: me and my agents, with GPT 5.6 Sol (high) as the workhorse and GPT 5.6 Pro and Fable 5 for code reviews, architecture and further optimizations. They wrote and reviewed all the code and came up with most of the architecture while I provided the persistence, the historical intuition, my nascent project management skills and perhaps most importantly my eyes and fingers.

By then I had certain performance targets I wanted to hit: cold renders under 500ms at all zoom levels (25%-400%) tear free panning as fast as possible, later targeting 24 fps with the stretch goal of 30; undo and redo limited only by how much you've drawn, anti-aliasing and proper SVG export next to the PNG one.

And nine days and change after starting we (me managing my agents) hit those targets and then some, with some bumps along the way and I tagged tinydraw v2's release version.

Not that I expected any users. I did this because I wanted to prove I can do this and impress others. 

The bumps along the way were many. At one point I was pointing a Fuji X-T5 with the Sigma 56mm f/1.4 lens (which has terrible magnification) at the screen of the Waveshare device's 1.8" AMOLED screen recording video of different flashing patterns at 1080p@240fps, set to 1/1024 (to reduce flicker) at f4, handheld, as I had no tripod for over 2 minutes in order to help my agent debug tearing; the video was going to tell it if the screen does something in a certain way or the opposite. And it worked! This was the turning point and the penultimate step in getting panning both fast and tear-free, almost 30 fps, at the physical limit of the panel/controller.

Glass testing, as my agents called it, was something only I can do, finger on screen. The agent would say "tearing is fixed and panning is fast now" and I'd try it and it's tear and glitch like crazy and be like actually no, here's a picture and let's get back to work. Fingers and eyes, that only I could do.

[✎ PLACEHOLDER — your sentences: the two moving-tear reports, agreed to live here. Raw receipts to compress: 22:59 "tearing back at one predictable spot, top grey edge of the minus button" → 23:06 after a change "moved to roughly two-thirds down the minimap" → from those two clues the agent deduced the per-strip staging fix → 23:41 "yes tearing seems to be gone."]

And it's something I have not done often enough: a recurring pattern was variations on the above. I ask for something, agents spend a lot of time writing it, report it working and/or fast and once my finger is on the glass this turns out to be not the case, or at times not as much the case. If I could do it again I'd do more glass tests more often.

Evil hairlines. I repeatedly ran into something passing our increasingly growing performance test suite but as soon as I busted out the smallest pen and started scribbling evil hairlines on the screen - dense, overlapping, long thin strokes, ideally at least a few of them stacked it was often time to go back and optimize cold rendering or caching or inking more.

The most important lesson perhaps came from understanding and the lack thereof; many times I'd watch my agents running for a long time increasingly not understanding what they're doing or why a certain thing is taking way too long and get more and more frustrated. What helped is stopping, recouping, asking the agent for a status, to explain what's not working, and telling them to step back and think because I think they were just flailing.

> please step back and think it feels like you're flailing. worth stepping back and thinking at these times. you got this.

[✎ receipt: your verbatim message, 8/11 20:37, msg ae121db9 — refrain plant #1]

If the issue seemed bad enough I'd have them pack up the source code and the logs for 5.6 Pro and wait 40-100 minutes for the result then feed that to Fable, or get Fable working on it already and give it Pro's report inbetween with the disclosure that this was done against a slightly older version of the code.

And Fable. Fable is a beast. It's not particularly detail-oriented (I have Sol for that) but as of now they are unparalleled at architecture and optimizing code for microcontrollers especially when set to xhigh effort. This made an already scarce resource even more one; I did what I could to get the most out of a single Anthropic subscription and still blew thru it in 3 days.

Keeping Fable around for the tough bits paid off though; discernment is another thing I brought to the table, sharing some but most definitely not all with the agents. Perhaps a gestalt of this is what I refer to as "taste" these days.

That, and persistence. I always knew this was possible and the agents were the ones always more skeptical. And yet, I kept pushing them for more again and again and when you do that the right way their results went way beyond from what they thought was possible.

At one point, early in the project I felt stuck and asked a friend to help, a friend who is likely in the top 100 system engineers in the world and I'm not exaggerating here. 

￼[screenshot of the demoscene convo; permission pending but 90% sure will be given]

And I do think telling the agents to think about mechanical sympathy, elegance and most importantly the demoscene mindset again and again helped.

[✎ KEEP per your call — friend's verbatim lines, needs attribution + cover in the same permission ask as the screenshot; likely home is right here: "Surprising fraction of how I use Claude well is conjuring up the right guy from latent space." / "Who would absolutely crush this." / "Ok Claude go be that guy."] 

There were other hard lessons learned during this project: 


I'm a software engineer but I'm not a systems engineer. The first time I wrote serious code in C was in the spring of 2023, and that was already AI-assisted.

This project was in C++20, the agents wrote all the code, they reviewed all the code. For example we stacked 18 different tricks and optimizations (as well as rejecting 20 others along the way) to get the performance i wanted. I don't or only somewhat understand why we needed them and how they work.

I know the ESP32-S3 has two cores and pinning inking to the second core helped a ton with performance. I know that it has 512KB of fast SRAM shared between code and data (moving the rasterization core into the fast side gave us an immediate 7-12% compute boost) and 8MB PSRAM where the PS is short for "pretty slow" with the bandwidth and one of the main goals was it to be the bottleneck rather than the CPU. I know that we needed to cache tiles as much as possible and give all free PSRAM to that. I know that you can export PNGs and SVGs with surprisingly little PSRAM usage. I know that changing inking ??? from 2x2 to 4x4 made the curves a whole lot smoother without any real penalty. And more.

AI can massively compress how a beginner can go from zero to one, but most of the time without the domain knowledge you do hit a wall and need to start thinking more while managing your agents, to dial in how much you need to know for them to work efficiently.

Being a project manager does not often come naturally to a software engineer as it's a different and at best only somewhat overlapping set of skills. You need to delegate but you need to know what your people or indeed agents are capable of, how much they need managing, how they need to be managed and so on.

[✎ APPROVED anecdote — re-say in your voice: "If I tell Claude to make atomic commits, it makes granular commits. If I tell GPT-5.6, it interprets it the opposite way and makes one giant commit. So now I just say granular." [today, tidy]]

The best vibecoder I know is a 100x PM who still does not know how to code and yet he built and shipped very impressive projects like X and Y.

I also know that adding Undo/Redo only towards the end bit me hard; the first version was very slow indeed I was watching a line render block by block in the middle of the screen and it was around then it dawned on me that an undo is a re-render, and those are very expensive even after optimizations unless cached. I am proud that we got it to a speed and UX i'm mostly happy about. 

Anti-Aliasing was undo on steroids; that came last and the recurring pattern of optimizing one bottleneck hurting another. If we designed the cold rendering pipeline with AA in mind from the start it could have been a whole lot faster than it is now.

A lot of care went into making inking feel good, be fast and accurate. I know that switching to a Vector-first Authority was one of the biggest unlocks. I only somewhat grok how it works. Or changing the constant of perfect free hand from 0.35 to 0.4 was the sweet spot. One of the last bugs we hunted down before releasing were phantom dots that would show up on the svg export on the top of the document, render and tiny squares on the screen and not be present at all on the PNG export. These were related to how taps were interpreted at the edge. Hell, this was only one of two phantom dots bugs, the other being related to putting your finger down but then deciding not to draw a line. 

And our SVG exports have proper paths for all shapes.

I have these and so many more stories. I'm proud of the final product and mostly happy with performance with the exception of the aforementioned AA. tinydraw v1 was mostly the fun vibecoding version while v2 slowly but surely turned into a compo for a demoparty of all the people who scroll the timeline or read my
substack.

And the thing is I kind of prefer tinydraw v1 for drawing. Indeed the absurdity of v2 is part
of the charm. After all who in their right mind would draw anything serious on a vector graphics editor running on a microcontroller and a tiny 1.8" screen? Definitely not me.

And yet, I did it, I persisted, I learned a lot about managing agents, I shipped and by the end started having follow-up ambitions that I'd love to do but am constrained on tokens.

We're all project managers now whether we like it or not, and while understanding the engineering of what you work remains important, the degree you need to is starting to shift.



 

I tagged the codebase as v2 on Wednesday, added the WASM version on Thursday, and then it was just writing this blog post and shooting the video. That was supposed to be the end of it.

I spent a couple hours on Saturday talking with my friend Janka, who's helping me write this post and helped me reframe it in good ways. And I was looking at the time, and I checked and I had like 20 minutes until my Codex reset, give or take, and 18% of my quota left, and I'm like, okay. [✎ swapped 19%→18% per receipt; removed "waiting for my 8 p.m. Fable reset" — unsupported in logs, restore if it's your true memory] I asked the agents many times now if it's worth doing assembly, and each time they were like, no, it's not appropriate at this time, or it's only worth doing assembly after settling many other things. So I had 20 minutes until the reset, and I'm like, yeah, I'm just gonna tell GPT-5.6 Sol:

> oh hey i have a lot of tokens to burn so let's go. do a disassembly on the esp32s3 binary (the native one, not the wasm one) and see if there are places where we can get performance wins in all areas (cold renders, panning speed, undo/redo, anti-aliasing, caching etc.) feel free to use subagents liberally be thorrough and be fully autonomous. i am very happy to hand-roll assembly or change our c++20 code to generate better assembly to squeeze out more performance, especially if the xtensa gcc is inefficient in some ways you can also research that i vaguely remember that we want to avoid memcpy for example but there's probably more!! anyways. fully autonomous! go as long as possible! good luck you go this

[✎ connective, mine — re-say: "Two minutes later I added:"]

> (i have the device connected feel free to flash whatever) (don't stop) (research all possible ways we can make things faster with assembly)

Do I know there's probably more for a fact? No. But it's a reasonable guess, and it's a way to motivate the agent.

Sent at 6:48 p.m. As I'm dictating this, it's 9:22 p.m., and they're still going. So far there have been 43 subagents doing research and trying things. It just kicked off three more. It already used up 22% of my tokens since my reset. But at least I have a banked reset. It's talking about the final three audits — and I'm pretty sure this is not the first time it said final.

[✎ your framing sentence here ("The last summary I got from Sol…" family), then the numbers — verbatim from the receipt, keep as list or fold into prose:]
- cold render compute 18.92–41.87% faster (all 15 corpora/zooms)
- direct pan composition 51.59–51.65% faster
- RGB565 ring staging 57.63–57.75% faster
- saturated 604-slot cache tour 39.15% faster
- undo/redo worst-case 21.78–24.24% faster
- settled anti-aliased rendering 17.23–26.45% faster
- nine hand-written Xtensa PIE (SIMD) kernels in the final ELF, output bit exact

And I have to say, I do feel extremely vindicated. I let them go to town. They ran the benchmarks. I have not glass tested yet. It's quite possible that once I glass test it, some bugs come out. I may be repeating the mistake of not glass testing early enough. I'm also a little mad at myself that I wasn't pushing them earlier, but here we are.

I just did a glass test, and as far as I can tell, the only thing broken is the SVG export — in a way that I think will not be too hard to fix. They did not break anything — famous last words — but PNG export's fine, performance is fine, cold rendering got faster, anti-aliasing got faster. Free wins in assembly. [✎ your sentence needed: the SVG fix landed the next day — fact backup: commit e3278646, host suites 95/95 green]

[✎ SCREENSHOT: the DM exchange with the PM friend — decided 8/22; he is NOT Janka, give him one distinguishing clause; permission needed. Your existing self-catch line follows it:] And one of the best lessons learned is that the agents do often need more pushing the right way, the same way humans do. His answer was that so much domain knowledge is knowing where the agents aren't ambitious enough — which again, just like humans.

if it wasn't for the Janka call, I might not have run this assembly thing. I cannot know. But I would like to think that talking all this about project management contributed to me being like: yeah, you know what, I should actually just tell them to disassemble it. And lo and behold, they keep finding optimizations.

P.S. — mistakes made

[✎ P.S. to assemble — all 20 candidate items verified with receipts in writing-kits/FACTCHECK-RESULTS-v4.md §D4]

[✎ moved from landing, re-shape freely; numbers are placeholders pending the second napkin round:] If someone wants to fund at least 20 OAI and 10 Anthropic subscriptions or give me at least $30k in tokens do let me know; you could be credited for helping the embedded community to have the xtensa gcc built-in vectorization; while upstream GCC has generic vectorization the Xtensa can't do any ESP32-S3 specific PIE/SIMD.

[✎ moved from body tail per arc — flag if you want it back:] And oh my god I need to start telling 5.6 more to cut all the "do not bits" because I know why it does it but it drives me up the wall and is almost always pointless information I did not need. Yes I did just get a long report back from 5.6 pro and asked it to rephrase the whole document with the "do not"s limited to a bulleted list at the very end on the off chance they are possibly, maybe useful.

[✎ footnote — final placement: very end, after the P.S.]
The agents wrote all the code, I wrote all these words.
