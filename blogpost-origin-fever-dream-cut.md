<!--
PROPOSED CUT ONLY. blogpost-edited.md remains untouched.
Sources: Sarah's existing drafts, today's dictation, and verbatim log quotes.
Operations: reorder, cut, punctuation, capitalization, transcription correction,
and tense adjustment. No agent-summary language is used as article prose.
-->

## Hook

I spent nine days building a vector graphics editor on a $40 microcontroller with a 1.8-inch touchscreen. I knew it was possible. I wanted to see how fast I could make it.

## Origin

I've been following [Steve Ruiz](https://twitter.com/steveruizok) on Twitter for a while now, and he kept posting all these cool projects built on microcontroller-powered tiny little devices. And then I saw there was going to be a whole meetup about them, and I wanted to go, and I'm like, well, if I'm going, I want to present something.

But I still didn't know what to build. A mini [tldraw](https://tldraw.com/) seemed like the obvious answer, but surely someone has done it. Surely this was Steve's very first project. So I scrolled his entire timeline and I did not find a mini tldraw at all. So I was like, okay, I guess we're doing it.

I ordered the Waveshare from Amazon, and I asked my coding agent what we could build before the device arrived. It turns out quite a bit. I set up a development environment that first built a native macOS app, then one that targeted a QEMU-emulated version of the ESP32-S3. That wasn't cycle-accurate, but was still helpful, and helped me work out the initial performance kinks.

Even while building the first version, I was already dreaming of having a truly infinite canvas.

The device finally arrived, and I was able to test things Monday morning. I was immediately faced with the fact that drawing was extremely slow. My circles had way too many angles.

It used Steve's Perfect Freehand library, the first version of which powered tldraw, which simulates pressure with velocity and makes lines nice. There was never a question not to use Perfect Freehand.

> I draw the stroke and I can see the tile-updating-thingy under my finger.

> Angularity correlates with speed. The faster I draw it, the more angular it is.

> No change in angularity. Drawing feels maybe a bit slower now.

Over the day, we fixed performance. We made our curves a whole lot more curvy. By the evening, before the meetup, I had something pretty good, pretty fast.

> Quick question while I test it: should we have done a vector canvas in the first place?

And then when the presentations came up, there was no order yet, and Steve just pointed at me: You first. And I presented it, and people liked it, and afterwards people came to me to try it, and that was nice.

V1 was raster, and that's easy. It's just far easier to make a raster editor for a microcontroller than a vector one.

And in the end, I had an editor, a raster-based editor that had a 3×3-screen-size canvas. You could draw, you could erase, you had a couple of colors, ten levels of undo, and you could export things to PNG, and it would simulate USB mass storage, simulate being a flash drive, really.

> Yeah, it works. Update README, merge to main, commit, push, because we are starting something a lot more exciting next.

The next evening: alright. Check `V2_INITIAL_SPEC.md`. We're making this a real infinite canvas. By god, we are doing this.

A mini "real" tldraw: vector, arbitrary zoom levels, really fast. The agents tempered my expectations, and they were right. I really wanted 25% to 800% zoom, but I settled at 400%. It was a big compromise.

## The Fever Dream

I spent the next 26 hours in a fever dream chipping away at the problem. Nominally I was trying to answer the question if this was possible, but I think in reality I never doubted it was possible. I had to find an approach that worked.

I call it a fever dream because a lot of it was just: I barely know what the agents are doing, but I'm just gonna keep going.

> I tested visually at 50%, and there are many cache misses there too. Switching between the zoom levels takes five seconds. That's a lot. I'm happy to put more work into this if there's a good chance that this can be something.

> A brief pixelated zoom is fine, but brief is under half a second, not several seconds. That's not good user experience.

> Before we change anything else, I really feel like we're already getting lost in the woods. I asked for a second set of eyes.

After 26 hours, I had a prototype that I had to throw out. But on the other hand, I had the direction to go.

> Clean production island inside this repository. Yeah, let's do that.

And that's when the real building began.
