# PASS SHEET — Slot 8: undo, the hourglass (the PM lesson)

✎ Editor notes marked ✎ are never prose. Quoted lines are YOURS.
✎ ALIVENESS: you hammered undo/redo a bit yesterday — mildly alive. If it needs
more, 90 seconds of undo-hammering on device before writing.

## THE SHAPE (per skeleton)
Cost of not thinking the broad picture through + a team that never says no →
ends on the UX save (hourglass), not on shame.

## YOUR LINES — from skeleton, unchanged
- "it hit me that, well, it sounds very trite, but an undo is a redraw, and redraws are very expensive." [draft]
- Sharper: "an undo and a redo is a redraw. You have to redraw the screen, and when it's all vector, this is an awful lot of computation." [talk]
- "it was kind of ill-advised to leave undo this late... would I have had probably less pain? Possible." [draft]
- Team angle: "i got myself in a situation where i didn't think thru the broad picture how undo would fit into the app... and then re-do a bunch of work bc my team 'didn't tell me about it'" [notes]
- "you were watching this line render in the middle of the screen. It just looked absolutely horrible." [draft]
- "you can hammer undo and redo and it will not mess it up." [draft] ✎ you re-verified this yesterday-ish ("I hammered it a little, I think it's fine") — can now be said in present tense with a clear conscience
- Hourglass: "I did not like the UX discrepancy. So I just said that we can just show the hourglass for even the short stuff. Just flash it and it's gone and it's fine. It's good to have consistent UX." [draft]
- "It was actually the hourglass that made it acceptable... It didn't quite fix the performance, but it fixed the UX." [draft]
- AA epilogue: "undo on steroids" [call 02:47] + "the other thing we put in way too late... the one area where I feel like performance isn't quite where I want it." [draft]
  ✎ NOTE: if the AA re-architecture run happens this weekend and lands, this
  epilogue line may need a present-tense update — but DO NOT hold the episode
  for it. Write it as true-as-of-now; adjust one clause later if needed.

## RECEIPT — the part of this story that's pure PM gold (session-history §9)
✎ Context only, but it's YOUR conceptual move, receipted: during the final
Fable round, undo exposed the jarring rerender. YOU reframed export RAM as a
modal, evictable resource — challenged the "sacred" allocation — and asked to
use all remaining RAM and eventually flash for undo preservation. That produced
copy-on-write preserve tiles + flash-backed takeover, instead of accepting
reraster on undo. "The conceptual change came directly from the user
challenging the 'sacred' allocation." [.codex-archaeology/session-history.md §9;
msgs 8aeabae0, 05b24f5e, ae3b4b56, session 2026-08-18T19-04-33]
✎ This is the episode's second PM beat if you want it: the fix wasn't code you
wrote, it was a resource-politics decision only the owner could make.
- Your 8/18 line, receipted: "Inking is in a pretty good place, although I think starting strokes sometimes feels a bit laggy." [msg 8aeabae0] — same session, if you want temporal texture.

## SMALLEST LIVE BEAT
✎ Start with the hourglass decision (it's the ending and it's UX-instinct
material, always alive for you), then backfill the pain.

## YOUR PASS ↓
(write here)
