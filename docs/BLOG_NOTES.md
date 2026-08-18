# TinyDraw V2 blog notes

## Interactive cold-render optimization explainer

Build an interactive visualization that runs one fixed synthetic drawing or tile through each cold-rasterization optimization layer:

1. Full forward replay as the reference result.
2. Bounds and occupancy culling.
3. Spatial-index candidate reduction.
4. Newest-first replay with a finalized-pixel mask.
5. Saturated-row and whole-surface skipping.
6. Prepared chords.
7. Row sweeps and batching.

Keep the final pixels identical at every stage while showing how the work collapses. Useful counters include operations considered, pixel coverage tests, actual pixel writes, skipped rows, and elapsed time. Show SRAM, IRAM, cache, and memory-layout optimizations as timing annotations because their effects are not spatially visible.

Treat this as a later blog asset: first catalog the complete optimization stack so each stage matches the implementation.
