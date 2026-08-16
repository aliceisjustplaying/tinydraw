# Ink angularity baseline — recorded owner corpus

Date: 2026-08-16
Tool: `tinydraw_vector_v2_ink_angularity` (host-release)
Corpus: the six recorded owner traces (`testdata/ink-traces/`, all
source=recorded, ~1 kHz sampler cadence, commits `9e67220` + `7513aa1`)
Pipeline: production `InkStream` (default config, size 5.0 / 20.0 for the XL
trace) → `operation_point` mapping (camera at origin) →
`ChainedOperationBuilder` with the product 32-sample chunk limit →
`prepare_incremental_curve_unit` chords (the exact level-space segments the
cold and append painters consume). The reference curve per unit is the
midpoint quadratic, its control point recovered exactly from the emitted
chords (B(0.5) is the shared joint).

Metrics: `dev` = max distance from the dense-sampled quadratic to the emitted
chord polyline (px, level space). `joint` = direction change between
consecutive chords (degrees). `dev/width` = dev_max / mean stroke diameter.

## Current authority (2 chords per unit)

```
fast-curve-dense-25.csv    zoom=100 size=5.0  chords=2 ops=14  units=398  chords_n=810   dev_max=0.06   dev_p95=0.01   joint_p50=1.0   joint_p95=19.2  joint_max=53.1   j>10=79   j>20=38   chord_len=0.9   mean_r=1.41  dev/width=0.022
fast-curve-400.csv         zoom=100 size=5.0  chords=2 ops=14  units=400  chords_n=814   dev_max=0.05   dev_p95=0.01   joint_p50=1.0   joint_p95=13.2  joint_max=35.9   j>10=51   j>20=16   chord_len=1.2   mean_r=1.34  dev/width=0.017
fast-curve-400-xl.csv      zoom=100 size=20.0 chords=2 ops=70  units=2095 chords_n=4260  dev_max=0.03   dev_p95=0.01   joint_p50=0.9   joint_p95=22.8  joint_max=135.0  j>10=599  j>20=292  chord_len=0.4   mean_r=10.39 dev/width=0.001
slow-precise-100.csv       zoom=100 size=5.0  chords=2 ops=22  units=632  chords_n=1286  dev_max=0.03   dev_p95=0.01   joint_p50=1.4   joint_p95=45.0  joint_max=90.0   j>10=240  j>20=129  chord_len=0.3   mean_r=1.80  dev/width=0.009
scribble-multistroke.csv   zoom=100 size=5.0  chords=2 ops=27  units=728  chords_n=1483  dev_max=0.03   dev_p95=0.01   joint_p50=0.6   joint_p95=14.0  joint_max=53.1   j>10=106  j>20=48   chord_len=1.8   mean_r=1.41  dev/width=0.011
under-overlay.csv          zoom=100 size=5.0  chords=2 ops=275 units=8237 chords_n=16749 dev_max=0.03   dev_p95=0.01   joint_p50=0.0   joint_p95=22.8  joint_max=90.0   j>10=2019 j>20=1061 chord_len=0.2   mean_r=2.22  dev/width=0.007

fast-curve-dense-25.csv    zoom=400 size=5.0  chords=2 ops=13  units=372  chords_n=757   dev_max=0.08   dev_p95=0.04   joint_p50=1.2   joint_p95=22.8  joint_max=90.0   j>10=132  j>20=63   chord_len=1.0   mean_r=1.41  dev/width=0.028
fast-curve-400.csv         zoom=400 size=5.0  chords=2 ops=13  units=381  chords_n=775   dev_max=0.10   dev_p95=0.04   joint_p50=2.1   joint_p95=22.8  joint_max=90.0   j>10=133  j>20=69   chord_len=1.2   mean_r=1.32  dev/width=0.037
fast-curve-400-xl.csv      zoom=400 size=20.0 chords=2 ops=57  units=1705 chords_n=3467  dev_max=0.11   dev_p95=0.04   joint_p50=0.0   joint_p95=46.8  joint_max=90.0   j>10=706  j>20=382  chord_len=0.5   mean_r=10.22 dev/width=0.005
slow-precise-100.csv       zoom=400 size=5.0  chords=2 ops=18  units=525  chords_n=1068  dev_max=0.10   dev_p95=0.04   joint_p50=0.0   joint_p95=53.1  joint_max=90.0   j>10=217  j>20=141  chord_len=0.4   mean_r=1.75  dev/width=0.029
scribble-multistroke.csv   zoom=400 size=5.0  chords=2 ops=26  units=703  chords_n=1432  dev_max=0.11   dev_p95=0.04   joint_p50=1.8   joint_p95=22.8  joint_max=90.0   j>10=213  j>20=116  chord_len=1.8   mean_r=1.40  dev/width=0.041
under-overlay.csv          zoom=400 size=5.0  chords=2 ops=186 units=5557 chords_n=11300 dev_max=0.09   dev_p95=0.04   joint_p50=0.0   joint_p95=53.1  joint_max=90.0   j>10=1412 j>20=836  chord_len=0.3   mean_r=2.08  dev/width=0.022
```

## Variants at 400% (selected traces)

```
1 chord:
fast-curve-400.csv         chords=1 chords_n=394   dev_max=0.35   dev_p95=0.18   joint_p95=36.9  j>10=96   dev/width=0.134
fast-curve-400-xl.csv      chords=1 chords_n=1790  dev_max=0.35   dev_p95=0.18   joint_p95=45.0  j>10=484  dev/width=0.104
slow-precise-100.csv       chords=1 chords_n=543   dev_max=0.31   dev_p95=0.18   joint_p95=63.4  j>10=168  dev/width=0.089

4 chords:
fast-curve-400.csv         chords=4 chords_n=1537  dev_max=0.03   dev_p95=0.01   joint_p95=14.0  j>10=154  dev/width=0.011
slow-precise-100.csv       chords=4 chords_n=2118  dev_max=0.03   dev_p95=0.01   joint_p95=28.1  j>10=295  dev/width=0.008
```

## Findings

1. **On real 1 kHz owner input, chord-vs-curve deviation is negligible:
   ≤0.11 px at 400% under the current 2-chord authority.** The prior
   14.65 px figure came from the synthetic/adversarial corpus with sparse,
   long segments; real curve units are 0.2–1.8 px long. The four-span
   rematch, framed as a smoothness fix, has nothing measurable to gain on
   real strokes.
2. **Even one chord per unit stays ≤0.35 px at 400%** (dev/width ≤0.134 on
   the thinnest brush). On real input, halving the chord count is nearly
   free geometrically. Flatness-adaptive subdivision therefore inverts its
   value proposition: on real documents its payoff is *cold-replay speed*
   (roughly half the chords for the dominant flat units), while 2–4 chords
   remain necessary for sparse, genuinely curved units like the frozen
   adversarial corpus.
3. **Subdivision does not touch the joint-angle tail.** Four chords improved
   p95 joints (22.8°→14.0°) but increased joint count (j>10: 133→154,
   shorter chords = more joints) and left max at 90°. The angular signal on
   real input is direction jitter on sub-pixel chords — input noise plus
   quarter-px quantization plus 32-sample chunk boundaries — which is
   exactly what arc-length resampling (external review §9.4) conditions,
   and what hard-edge aliasing amplifies visually.
4. The residual owner-visible angularity is therefore expected to live in
   input-jitter conditioning (resampling), rendering (settled AA), and the
   live-preview lane — not in committed curve subdivision.

## Caveats

- Geometry-only: this measures the committed centerline, not rasterized
  pixels; aliasing and the live preview are outside the metric.
- The joint metric weights every joint equally; sub-pixel chords make large
  angles cheap. Use it for A/B ranking, not absolute perception claims.
- Glass remains authoritative for feel; these numbers rank candidates and
  freeze a regression baseline.

## Reproduce

```sh
cmake --build --preset host-release --target tinydraw_vector_v2_ink_angularity
./out/build/host-release/vector_v2/tinydraw_vector_v2_ink_angularity            # full suite
./out/build/host-release/vector_v2/tinydraw_vector_v2_ink_angularity \
  --zoom 400 --chords 1 testdata/ink-traces/fast-curve-400.csv --size 5
```
