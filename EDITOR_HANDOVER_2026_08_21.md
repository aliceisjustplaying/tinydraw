# Editor Handover — TinyDraw V2 Blog Post

**Date:** 2026-08-21  
**Status:** First editing pass complete, awaiting fact-check round  
**Working file:** `/Users/alice/src/a/tinydraw/blogpost-edited.md`  
**Original draft:** `/Users/alice/src/a/tinydraw/blogpost-current-stitched.md`

---

## What We Accomplished

1. **Established structure** — Hook → Origin → Commitment → Fever Dream → Graveyard → Demoscene Moment → Cold Rendering → Panning/Tearing → Undo → Anti-Aliasing → What the Agents Did → Landing → P.S. Mistakes

2. **Did keep/cut/integrate breakdowns** for Cold Rendering, Panning/Tearing, Undo, What the Agents Did, Landing, and P.S.

3. **Verified key facts via subagents:**
   - 24 seconds cold render baseline ✅ (23.66s, has receipt)
   - First rescue: 21.8s → 1.09s (word-skip optimization) ✅
   - Final cold render: all zooms under 500ms (400% = 492.793ms) ✅
   - 16 optimizations stacked, 22 rejected experiments ✅
   - Fever dream: ~26 hours, Aug 11 19:47 → Aug 12 21:39 ✅
   - Demoscene prompt: AFTER fever dream, during production (Aug 12-14) ✅

4. **Created optimization inventory** at `.pi/plans/2026-08-21-cold-optimization-inventory/scout-context.md`

5. **Assembled draft** using her words from yapping + original draft, with `[FACT-CHECK]`, `[POTENTIAL CUT]`, and `[DEFINITION]` markers

---

## Critical Constraints (Do Not Violate)

1. **She writes all sentences.** Do not write new prose for her. You can organize, suggest structure, identify gaps, but she writes the words. Exception: she may occasionally approve a sentence you constructed if it's really good.

2. **No token/money bragging.** Cut references to "5 billion tokens" or costs. The £200 Anthropic plan line is marked `[POTENTIAL CUT]` — her decision.

3. **Remove cursing.** She wants a feminine-coded voice, not masculine. "Very expensive" not "fucking expensive."

4. **Reduce "likes"** from talking quantity to writing quantity. Some are fine for voice; too many is verbal tic.

5. **Insider baseball is GOOD** for her audience (vibe coders, ESP32 hackers, AI power users). Don't oversimplify.

6. **98% on Pangram** — the current draft scores well on AI detection. Keep using her actual words.

---

## Pending Decisions

- **£200 Anthropic plan line** — keep or cut?
- **Cache slot progression (320→384→448→604)** — keep and rewrite, or cut?
- **Callback options in Landing** — she leaned toward "completely absurd, part of the charm" (already in draft)

---

## Key Evidence Files

- `.codex-archaeology/git-history.md` — commit timeline
- `.codex-archaeology/session-history.md` — session narrative
- `.codex-archaeology/two-episodes-writing-memory.md` — detailed memory
- `docs/PERFORMANCE_CHRONICLE.md` — receipted optimization timeline
- `docs/receipts/vector-v2/` — benchmark receipts
- `.pi/plans/2026-08-21-cold-optimization-inventory/scout-context.md` — full optimization list

---

## The Story in Brief

Alice built TinyDraw V2, a vector graphics editor on an ESP32-S3 microcontroller with a 1.8-inch touchscreen, over 9 days. She used AI agents (GPT-5.6 Sol for implementation, Claude Fable for architecture/perf, GPT-5.6 Pro for deep reviews) but never wrote a line of code herself. 

The project became a one-person demoscene compo — stacking 16 optimizations to get cold rendering under 500ms, fixing tearing with a Fuji X-T5 at 240fps, solving undo with an hourglass UX. Key insight: when she felt lost about what the agents were doing, that was the signal to stop and regroup.

Her friend (systems engineer, Nix/Rust, works at Anthropic but don't mention that) gave her the "demoscene mindset" prompt that reframed the whole project.

---

## Tone Notes

- Authentic, conversational, not polished-corporate
- Confident but not braggy (she did more than "just vibe coding")
- The absurdity is the point — why build a vector editor for a 1.8" screen? Because she wanted to see how fast she could make it
- "PS is short for Pretty Slow" is a joke she's keeping
- The Fuji X-T5 panning/tearing saga is the cinematic heart — don't compress it

---

## Next Steps for Future Session

1. Receive fact-check results from user
2. Integrate verified facts, cut or rewrite anything that didn't check out
3. User rewrites `[DEFINITION]` blocks in her voice
4. User makes final `[POTENTIAL CUT]` decisions
5. Final polish pass — trim remaining "likes", smooth transitions
6. Check Pangram score again
7. Done

---

Good luck, future me. She's a great collaborator — direct, knows what she wants, pushes back when needed. Trust her instincts.
