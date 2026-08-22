<!--
PROPOSED CUT ONLY. No main draft changed.

Article prose is cut from Sarah's drafts, current dictation, and verbatim log
messages. Edits are limited to ordering, cutting, punctuation, capitalization,
grammar, transcription correction, tense, and factual units/numbers.
-->

## Inking

One of my first thoughts was that, like, obviously we're using Perfect Freehand, because that's what tldraw uses. There was never a question not to use Perfect Freehand. The raster version had a pretty damn good implementation. We started working on V2 without the Perfect Freehand-style inking.

I remember in the beginning doing a lot of tests where I would just draw. I think that was actually the fever dream prototype phase, where I would draw lines really fast. And I would see the chords show up, which I should not be seeing. I was like, no, this is too slow. This is too slow. I just need this to feel fast.

One drawing should always have the lowest latency. That's by far number one.

I did a very, very long thin hairline and it just disappeared. That's really bad. Then I did an evil line slowly. I switched colors, and suddenly the evil line that seemingly stopped disappeared. It was a 1,024-sample cap.

<!--
CONFIRMED: the fixed input-builder buffer held 1,024 samples. On overflow,
the whole stroke was cancelled, while temporary preview pixels remained until
a refresh such as changing color. This is separate from the later 64-sample stall.
-->

Once we fixed disappearing, our goal was basically smooth long strokes that did not have a 70-millisecond delay, which was way too much. Mechanical sympathy and elegance, demoscene mindset were the keywords here.

<!--
CONFIRMED: split-and-chain removed the 1,024-sample cliff. The next version
committed every 64 samples, but each commit froze visible ink for about 70 ms.
Validate-first, in-place commits later removed the expensive defensive copying.
-->

The thing with inking is that it happened in several bursts. We hadn't touched ink speed at all, and that was one of the biggest regressions. When does that happen?

Once again, the agent thought something was fast, but the glass test disagreed. The drawing lag was visible. It was unacceptable.

This is when the 111× thing happened: the worst mixed-draw append went from 19.3 milliseconds to 173 microseconds. The authority-only commit was an important bit. I don't remember how it came together.

<!--
YOU NEED TO EXPLAIN THIS IN YOUR WORDS: authority-only commit recorded the
vector stroke immediately, then let the cached pixel version catch up during
idle time. The 111× number applies to that input-path append, not the whole app.
-->

This controller samples touches about 38% less often than an iPhone X. Faster polling didn't help, because that was just the hardware limit or the driver limit or whatever. So a lot of work went into that. A lot of architecture work.

AA helps and looks nice, and ideally we should have AA. But AA does not fix jaggedness optically. What I did not understand was why the raster version didn't have the jaggedness.

This is what I meant by 2×2 to 4×4. The fix was embarrassingly cheap: sixteenth-world units. Yes, it's much better. Honestly, I would like it to be even less angly. Definitely a big improvement.

<!--
TECHNICAL WORDING CHECK: “2×2 to 4×4” is not an accurate replacement for
quarter-world to sixteenth-world coordinates. The approachable fact is that
stored points snapped to a grid, and the new grid was four times finer along
each axis. At 400% zoom, the rounding step fell from one screen pixel to a
quarter pixel. Sarah needs to write the public explanation.
-->

We tweaked the Perfect Freehand constant from 0.35 to 0.4. I think 0.4 was the sweet spot. My slow circles looked a hell of a lot less jagged.

Wait, if what I'm noticing is trailing from streamlining, that is bad. Drawing at 400% feels laggy. I don't love this lag at all. Is it possible to keep it at 0.4 and not have lag?

There was a tail thing. There was another one of those felt-right-or-didn't-feel-right things.

<!--
ANSWER TO “IS THIS WHAT I CALL LAGGING?”: yes. The 0.4-smoothed saved line
naturally ended behind the current finger position. The final display drew a
temporary replaceable tip to the newest raw touch while the finger was down,
but kept the saved and exported line smooth. Sarah needs to write this in her
own words before the comment can be removed.
-->

I'm gonna draw a circle. Visually, it's quite good. Do a long stroke; that seems reasonably fast. The only one that lags a little bit is the diagonal stroke in XL, but honestly that's a lower-priority thing.

Inking was another thing that I needed to solve. The feel, the performance went from not too bad to, okay, this actually feels good now. Inking is in a pretty good place, although I think starting strokes sometimes feels a bit laggy.
