# PASS SHEET — Slot 6: inking, glass tests, evil hairlines

✎ Editor notes marked ✎ are never prose. Every quoted line is YOURS (sessions =
your typed/dictated words, fair game). Receipts cited per line. Write in ONE
section-sized pass. Ugly allowed. 20-minute first beat.

## SMALLEST LIVE BEAT (start here, voice note it)
✎ The loop in miniature: agent says fast → your finger says no. You did the
evil-hairline stress test AGAIN yesterday during the post-assembly glass test —
that memory is ~1 day old, the most alive inking material you have. Start by
narrating YOUR stress-test ritual (thinnest pen, ton of overlapping lines), then
let the history hang off it.

## YOUR LINES — the loop (from skeleton, unchanged)
- "I would draw lines really fast. And I would see the chords show up, which I should not be seeing... I just need this to feel fast." [draft]
- "One drawing should always have the lowest latency. That's by far number one." [draft] ✎ receipted verbatim-ish 8/14 19:00: "one drawing should always be the lowest latency. That's by far number one" [quote pack, msg 4b5b30a1]
- "Once again, the agent thought something was fast, but the glass test disagreed." [draft]
- Stress ritual: "my manual way of stress testing the whole thing is picking the thinnest pen and drawing a shit ton of lines, usually in one go, overlapping each other, because I knew that that's the nightmare scenario for rasterization and caching, because it's just so random" [today-8/22] ✎ decurse
- "it also exposed a bunch of inking issues, like when I drew for over a minute and it would stop" [today-8/22]
- "Evil hairlines absent from the supposedly good benchmark. Almost doubled cold time." [draft P.S.]

## YOUR LINES — newly mined, contemporaneous (quote pack, 8/22)
✎ These are your live messages from the actual days. Raw, typos preserved.
Sources: .codex-archaeology/origin-v1-v2-inking-quote-pack-2026-08-22.md

- 8/10 08:47, first hour on real hardware: "oh god we'll need to perf optimize this drawing is slaw as fuck im not even on the xl brush. drawing getss very angle-ey very very quick" [msg a2c8855b] ✎ decurse if used; "slaw as fuck" typo is charming but cursed
- 8/10 09:45: "i draw the stroke and i can see the tile-updating-thingy under-my-finger" [msg b313dbb2] ✎ ← "tile-updating-thingy" is peak your-voice
- 8/10 09:46: "there is definitely a way of fixing this, i believe in you. step back and think a bit" [msg 74e1de36] ✎ stop-and-regroup refrain, day 2
- 8/10 11:50: "angularity correlates with speed the fasteri draw it the more angular it is. but that should not happen" [msg 4e309b8d]
- 8/14 12:36: "i did a very very long thin hairline and it just disappeared… that's really bad" [msg c6e35430]
- 8/14 14:53: "our goal is basically smooth long strokes that does not have 70ms delay which is way too much… mechanical sympathy and elegance, demoscene mindset are the keywords here." [msg 500fd477] ✎ links this episode to slot 5's demoscene moment — the keywords came from Janka, you deployed them here
- 8/15 22:59: "Uniform with SVG expert? Absolutely no. No, that sounds horrible The whole point is to have perfect freehand" [msg 8082c577]
- 8/16 20:00: "the drawing lag was a visible lag. it's unacceptable." [msg 8c18ed8b]
- 8/16 21:29, the self-aware one: "It feels fine. I don't know how much I'm biased knowing that we have sub-millisecond numbers versus reality… I know it's faster… very happy for that" [msg f520a5fb] ✎ finger-vs-metrics from the other side; rare and honest
- 8/16 22:26, glass verdict: "Yes, it's much better. Honestly, I would like it to be even less angly… Definitely a big improvement." [msg 2f376d13] ✎ "less angly"
- 8/17 13:12: "the only one that lags a little bit is diagonal stroke in Excel" [msg 2115d64f] ✎ "Excel" = speech-to-text for XL; funny if you want it

## RECEIPTS (context only, not prose)
- Touch controller cadence: ~13–14 ms between fresh coordinates; fast gestures produced only 19–35 distinct points vs 134 slow. Polling faster manufactures nothing. [quote pack §1; session-history §2]
- Skeleton's iPhone comparison lives in draft: "samples touches about 38% less often than an iPhone X." [draft]
- Cold numbers if compressed here: 24 s naive → under 500 ms, 18 tricks, 22 rejected. [draft, receipted per 8/21 handover]
- Evil hairlines became a PERMANENT torture-test corpus on 8/18–19, days after you first noticed the battery lacked it. [session-history §9]

## ⚠ FACT-CHECK FLAGS (no trials — just "did this happen?")
- Skeleton slot 6 says the disappearing-hairline cause was "a 1,024-sample cap"
  [draft]. The quote pack says remembered caps are NOT supported by logs; the
  confirmed mechanism found was an every-64-samples defensive tile-copy pause of
  ~70 ms, and the color-switch detail isn't receipted in the pack either.
  [quote pack §6: "A hard '10–24 chord' cap is unsupported"]
  → On the dispatch list for GPT-5.6. Until resolved, tell the story at the
  depth you actually remember: line stopped, felt like a limit, agents found a
  cap-like mechanism. Options after check: exact mechanism, or honest vagueness.

## YOUR PASS ↓
(write here)
