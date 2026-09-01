# Substack apply checklist — v5.md → Substack draft

**How to use:** go top to bottom. Use the browser's Cmd+F to locate the FIND text in the editor. Small fixes are one word; block swaps have paste-ready text (italics/quote formatting noted). Check off as you go.

---

## Top of post

- [ ] **DELETE** the whole line: `(for those giving me feedback: do try it out in the browser otherwise most of this will not make sense bc no video yet)`
- [ ] `I asked GPT 5.6 Pro` → `I asked GPT-5.6 Pro`
- [ ] Link text `Perfect Freehand` → `perfect-freehand` (keep the link)
- [ ] `twelve colors` → `12 colors`
- [ ] `it was that point I felt` → `it was at that point I felt`
- [ ] `What if tinydraw would be a more real tldraw` → `What if tinydraw were a more real tldraw`
- [ ] `something that proves this was possible` → `something that proved this was possible`
- [ ] `a few hours in when once again interjected` → `a few hours in when I once again interjected`
- [ ] `I had something; a renderer system` → `I had something: a renderer system`
- [ ] `GPT 5.6 Sol (high)` → `GPT-5.6 Sol (high)` — same sentence: `GPT 5.6 Pro and Fable 5` → `GPT-5.6 Pro and Fable 5`
- [ ] `(1x-16x) tear free panning` → `(1×-16×), tear-free panning` (comma added, hyphen added, × not x)
- [ ] `the PNG one we've already had in v1` → `the PNG one we already had in v1`

## BLOCK SWAP 1 — tearing/glass-test section

Find the paragraph starting `Like after the Fuji episode` and replace it through `...to see if it really is fixed.` with (quotes are blockquotes, italic):

> Like after the Fuji episode and before getting it fully working, it seemed fine except for the fact it was tearing, so I'd tell the agent (this was voice to text):
>
> [blockquote, italic] *Alright, it's 11:57 PM. Probably the last thing I try before I go to sleep. Hopefully this works and we can move on to other things. I'm gonna Start with some normal stuff at 25% as I always do. [...] So some evil hairlines Okay, let's do 100% [...] Alright, let's zoom in. Start panning. That's faster. That's faster, that's panning, that's for sure and tearing his bag Good news if we can call it good news That it seems to be tearing at the exact same spot I'm Yeah, so tearing is back, but it's tearing at one exact spot, on the UI it's the top grey edge of the minus button roughly [...] So yeah, panning is fast now, but it's tearing, but at least it's tearing in a very predictable way. So what gives?*
>
> and then they'd change something and I'd test again and this time it's tearing somewhere else:
>
> [blockquote, italic] *tear is not gone instaed it moved. panning is slower. tearing is now way further down somewhere idk 2/3rdsish of the minimap*
>
> and only after that we got to the blessed:
>
> [blockquote, italic] *yes tearing seems to be gone. what are the next steps? also please stop trying to put me to sleep*
>
> with me scrubbing the screen as fast as humanly possible to see if it really is fixed.

## Middle section

- [ ] `The most extreme example were what I christened` → `The most extreme example was what I christened`
- [ ] `give it Pro's report inbetween` → `give it Pro's report in between`
- [ ] `an already scarce resource even more one` → `an already scarce resource even more of one`
- [ ] `their results went way beyond` → `their results go way beyond`
- [ ] `one of the main goals was it to be the bottleneck` → `was for it to be the bottleneck`

## BLOCK SWAP 2 — AI-compression paragraph

Replace the whole paragraph `AI can massively compress how a beginner...` with:

> With AI you can just see this interesting thing on Twitter, oh, I want to make something like this, buy the $30-40 device from Waveshare, plug it in, point Claude at it and just tell Claude what you want. For one-shot or smaller projects this works, but for anything more complex you will hit a wall where you start to need to think more about how you manage your agents, how much you need to know for them to work efficiently, all the new forms of engineering, I suppose. But again, you can do the whole thing without knowing embedded engineering, for example.

## BLOCK SWAP 3 — social expectations paragraph

Replace the whole paragraph `For a software development project it was normal...` with:

> Five years ago, if you wrote a piece of software, it was normal and even expected that you know what it does, how it works. You can explain it, you can argue it, etc. This is still the standard in many places, especially at workplaces, even though it's starting to shift. And while this is a pet project, I'm proud of what I've shipped. And I'm proud of what I built.

## Rest of middle

- [ ] `adding Undo/Redo only towards the end` → `only toward the end`
- [ ] (same paragraph) `UX I'm mostly happy about` → `UX I'm mostly happy with`
- [ ] (same paragraph) `the phenomena Fable coined` → `the phenomenon Fable coined`
- [ ] `If we designed the cold rendering pipeline` → `If we had designed the cold rendering pipeline`
- [ ] `changing perfect freehand's streamline constant` → `changing perfect-freehand's streamline constant`
- [ ] `before releasing were phantom dots` → `before releasing was phantom dots`
- [ ] `do DM me on twitter your creations` → `do DM me on Twitter your creations`
- [ ] `the engineering of what you work remains important` → `the engineering of what you work on remains important`

## Coda

- [ ] `my friend Janka, helping me write this post` → `my friend Janka, who helped me write this post` (keep Janka link)

## BLOCK SWAP 4 — results block (Pangram fix)

Find `they did finally wrap and my god they sure delivered:` — then:

1. **DELETE** these six quote lines: cold render compute / direct pan composition / RGB565 ring staging / saturated 604-slot cache tour / undo/redo worst-case / settled anti-aliased rendering
2. **INSERT** the screenshot image (`Screenshot 2026-08-26 at 1.51.07 PM.png` from Desktop) in their place
3. **KEEP** as a blockquote below the image (note the added "that night alone"):

> nine hand-written Xtensa PIE (SIMD) kernels in the final ELF, output bit exact
> 35 experiments accepted that night alone, 23 rejected or superseded.

(keep the Xtensa + SIMD Wikipedia links if easy; keep your WASM note line after it unchanged)

## Coda of the Coda + rest

- [ ] `The SVG fix would land the next day` → add the period: `the next day.`
- [ ] `Would I have ran this` → `Would I have run this`
- [ ] `a plan to rearchitecture cold rendering` → `a plan to rearchitect cold rendering`

## BLOCK SWAP 5 — CTA (About ambition section)

Replace everything after `...money I can reasonably (ha) spend on tokens.` to the end of that paragraph with:

> What I want to do next is fix the Xtensa GCC fork so it supports vectorization for the ESP32-S3; while upstream GCC has generic vectorization the Xtensa one currently can't do any ESP32-S3 specific PIE/SIMD. I want other software engineers to get the benefits of SIMD vectorization without having to tell their agents to write a bunch of assembly. If someone wants to fund at least 20 OAI and 10 Anthropic subscriptions or give me at least $30k in tokens do let me know; you could be credited for helping the embedded community.

## P.S. section

- [ ] `"render parity fix" and a later a "top-edge contact fix"` → `and later a`
- [ ] `at 16x zoom we were juuuuuust` → `at 16× zoom`

## Footer

- [ ] Replace footer sentence: `The agents wrote all the code, I wrote all these words except for some of the P.S. section which is why this is only 90% human on Pangram. Edited by GPT 5.6 Sol, ...` → `The agents wrote all the code, I wrote all these words. Edited by GPT-5.6 Sol, Every's Spiral and most importantly, Claude Fable 5.`

## Before hitting publish

- [ ] Hook video in (replaces TKTK placeholder)
- [ ] `Try it now in your browser!` link works: https://tinydraw2.aliceisplaying.workers.dev/
- [ ] One last Pangram run on the final Substack text
- [ ] Preview on mobile (blockquote-heavy sections)
