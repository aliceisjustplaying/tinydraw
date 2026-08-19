# Phantom top-edge contact closure — 2026-08-19

## Verdict

The remaining phantom dots were real one-sample operations admitted from the
touch controller's top sensor fringe. Commit `602660f` discards a contact that
both starts in the top four screen pixels and never produces a drawable
segment. Intentional taps elsewhere remain dots, and a stroke that begins at
the top edge remains valid once it produces a segment.

This is separate from the earlier cross-renderer one-sample fix documented in
[`ONE_SAMPLE_STROKE_DOT_BUG_2026_08_19.md`](ONE_SAMPLE_STROKE_DOT_BUG_2026_08_19.md).
That fix made every authority dot agree across screen, PNG, and SVG. This fix
prevents the unintended top-edge contacts from becoming authority at all.

## Physical reproduction and authority evidence

The author reported an unintended top-left dot visible at 50%, PNG, and SVG but
almost hidden at 25%, plus a later top-right dot visible on glass. The first
export pair contained eight SVG paths. Its final path is a circular XL-brush
operation centered at world `(64,4)` with radius `18.8516`; the PNG contains
the same clipped dot at approximately x=45–82, y=0–22.

The world coordinate and radius map exactly back through 25% zoom to a contact
at screen y=1: `1 / 0.25 = 4`, and the XL screen radius maps to world radius
`18.8516`. Two previously captured mystery dots have the same signature at
world `(772,4)` and `(996,4)`. This also explains their weak 25% visibility:
most of the circle lies outside the canvas until a higher zoom magnifies its
in-bounds portion.

A one-megabyte read of the physical drawing partition recovered 248 journal
transactions, sequence 310, 222 active operations, and 4,923 active samples
with no discarded tail. The recovered authority contains the earlier
single-sample world `(772,4)` and `(996,4)` contacts at radius `18.8516`, plus
other top/side fringe contacts. The initial four-megabyte read disconnected at
79%; the bounded one-megabyte retry recovered the complete active journal
prefix and produced a compact 40 KiB TDOC corpus.

## Intentional-dot control

The author then created multiple intentional interior dots and exported again.
The fresh SVG grew from eight to fifteen paths and both formats contain the
new dots, including single-sample taps around `(1177,184)`, `(1249,188)`,
`(1180,60)`, `(1125,276)`, and `(1353,117)`. The later top-right glass mark
also appears in this fresh pair, proving it was newer authority than the first
export rather than stale framebuffer state.

Artifact hashes retained in `/tmp/tinydraw-phantom-dot-2026-08-19` during the
investigation:

- first PNG: `19367ee82feca7b6b0c48cada03f325713f53c16de5baf65d35eb44d2ad9dea6`
- first SVG: `8f9552e784713fc4114d815b04a488fddcb8a9d4e2b7f27d55ddd6d4799029e6`
- fresh PNG: `d6d3c6dbc069f9349d1024d50d606f517ff23a8a9898de21eaa71b507106a922`
- fresh SVG: `d35dfc2fc6c6b9ee6f4c74a86b602a6cac22f0e0b7a4885f5dff0d8c05c316a6`
- flash prefix: `48a25b01c5a15212db5833003e68e03818d7c3a4d058c4cce2da0ecb0053e20b`
- compact TDOC: `c2063155dcceab2f981c9c5dafd8b2b5cf1e96eec8783ef8c6d3eac35d5b484e`

## Root cause and fix contract

The input path called `begin()` immediately for every down event outside
chrome. If the finger then exited upward without moving far enough to produce
a drawable segment, finish deduplicated the final coordinate and committed a
valid one-sample operation. Autosave and every renderer correctly retained it.

The admission check uses persistent drawing evidence, not raw controller
movement: a top-fringe contact is rejected when no chunk was published and the
builder still has one sample. Rejection cancels the builder, clears transient
ink, refreshes the canvas, and publishes no operation or autosave transaction.
This matches the physical failure mode where the finger slid off the top but
did not move enough to draw.

The regression covers y=0, y=1, and y=3 rejection, the y=4 boundary, and an
edge-started contact with a drawn segment. The complete host Debug 31/31,
ASan/UBSan 13/13, Release 31/31, format check, and ESP32 product build pass.

## Device closure

The current-head 604-slot hardware battery passed every verdict, including the
captured-drawing corpus, history, settled rendering, PNG/SVG export, and export
reserve. The capture ended at `TINYDRAW_GATE1_AUTOMATED_DONE` with
`failure_marker=False`; the retained SSAA progression receipt remains yellow
as documented performance debt, not a failure.

The final product image is `0x106e10` bytes with 41% of the smallest app
partition free. Its SHA-256 is
`8feb9bee2fcfd407c11569e3c129d7790e27ed7608e32c518dfa1d1e428a890f`;
esptool verified the write on `/dev/cu.usbmodem1101`. A 12-second boot capture
reported no watchdog, crash, or stack failure.

The battery uses the physical drawing partition and replaced the live journal
with its test state. After the product flash, the verified pre-battery capture
was restored to the exact drawing partition range. Product recovery reported
`status=2 generation=575 active=222 retained=232`, returning the author's
drawing to glass for the final manual top-edge check.
