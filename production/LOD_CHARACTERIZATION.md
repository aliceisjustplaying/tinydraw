# Synthetic append-time LOD characterization

This is a policy-sizing experiment, not representative product evidence and not
a settled-renderer quality or latency receipt.

The host release executable `tinydraw_production_lod_characterization` runs the
existing deterministic handwriting generator with seed 7 over the production
1472×1792 world. This run produced 1,000 strokes, 19,844 input samples, and a
198-sample maximum stroke. For each committed tiled zoom it applies the existing
allocation-free, error-bounded centerline simplifier with world tolerances chosen
to keep a fixed screen-space bound.

| Policy | Screen center error | Screen radius error | 50% | 100% | 200% | 400% | Four-zoom total | 90k capacity |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| strict | 0.25 px | 0.125 px | 13,711 | 16,766 | 18,793 | 19,576 | 68,846 | 76.5% |
| balanced | 0.50 px | 0.250 px | 10,151 | 13,711 | 16,766 | 18,793 | 59,421 | 66.0% |
| loose | 0.75 px | 0.375 px | 8,533 | 11,609 | 15,081 | 17,712 | 52,935 | 58.8% |

All 12 policy/zoom runs completed without output-capacity failure. The balanced
policy retains 51.2%, 69.1%, 84.5%, and 94.7% of source samples as zoom rises;
this is expected because a fixed screen-space error permits less world-space
simplification at high zoom.

## Decision

Do not commit a production LOD generator from this corpus alone. The experiment
shows that the provisional 90,000-sample slab accommodates all three policies
for this synthetic 1,000-stroke document, but there is no representative
captured document in the repository. It also does not compare visual output,
painter-order edge quality, or ESP32 append cost.

A later private real-touch capture was run through these policies; its aggregate
results are in [`REAL_TOUCH_CHARACTERIZATION.md`](REAL_TOUCH_CHARACTERIZATION.md).
Unlike this synthetic corpus, it projects far beyond the 90,000-point slab when
all four zooms store independent copies. That result rejects the current
capacity/model combination, but it does not select a policy: shared/nested or
on-demand detail must be characterized before rendered output is compared.
`OperationLodStore` remains an ownership experiment, not an approved final
storage layout or simplifier.
