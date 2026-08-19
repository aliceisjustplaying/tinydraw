# Settled-AA performance experiments — 2026-08-19

Budget: at most five distinct measured hypotheses. Five used. Baseline does not
consume an attempt. Host-release results are five-run means over the frozen
25-case corpus; lower is better and every checksum remained exact.

## Baseline

The hardest representative ranges were 7.933–3.703 ms for long-crossing,
12.031–5.843 ms for hairline+eraser, and 0.765–3.179 ms for dense across
25–400%. The full on-device battery baseline was green; settle totals were
75.217/87.869/177.282/386.169/959.910 ms at 25/50/100/200/400%.

## Attempts

1. **Opaque-first composite fast path — accepted.** When a
   coverage-255 pixel reaches an untouched destination, assign its RGB and
   saturation directly. This is algebraically identical and removes four
   rounded divisions plus accumulation. Host: long-crossing −0.3…−2.8%, dense
   −2.2…−5.0% at 50–400%, hairline −0.4…−1.8% except a flat +0.01% at 200%;
   dense 25% was +0.55% (noise class). The full device battery remained green;
   settle totals were 75.102/87.647/176.885/383.594/946.849 ms, improvements
   of 0.2/0.3/0.2/0.7/1.4% versus the pre-change battery.
2. **Blank-pixel final-fold branch — rejected and reverted.** Directly writing
   white when accumulated alpha was zero regressed every representative case
   by roughly 2–11% versus attempt 1; branch cost exceeded saved arithmetic.
3. **All-opaque contribution shortcut — rejected and reverted.** Replacing
   the contribution formula with `255 - accumulated` for every alpha-255
   pixel was checksum-exact but indistinguishable/mixed (about ±1%); the useful
   untouched case is already covered by attempt 1.
4. **Row-local touched-span merge — rejected and reverted.** Deferring
   operation span metadata updates until the end of each chord row modestly
   helped long chords but regressed dense 50–200% by about 10–20%.
5. **Shift/add division by 255 — rejected and reverted.** An exhaustive
   compile-time proof confirmed the shift/add identity for every composite
   numerator, but replacing the four constant divisions regressed most
   representative host cases. Long-crossing rose 0.5–2.2% at 25–200%,
   hairline 0.8–1.9% at 25–100%, and dense 0.3–2.1% across all zooms. The
   optimizing compilers already lower constant `/255` efficiently; the longer
   dependency chain lost. Exact checksums were unchanged and the original
   expressions are restored.

The five-experiment budget is complete.

The post-change full gate finished with `failure_marker=False` and all battery
sections green, including cold fill, history, export, and the owner document.
