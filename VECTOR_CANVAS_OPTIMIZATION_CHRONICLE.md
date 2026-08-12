# Vector canvas optimization chronicle

This is the working source for a future engineering post. It records the order of events, including wrong assumptions and failed experiments. Raw measurements remain in `benchmark-results/`, `second_review_hardware_ab/`, and the linked findings documents.

## The constraint

TinyDraw runs on an ESP32-S3 with 8 MB of PSRAM and a 368×448 RGB565 display. The vector document must remain authoritative, but drawing and panning still need to feel immediate. Our working interaction gates are:

- first physically visible zoom feedback within 100 ms;
- complete physically visible fallback within 150–180 ms;
- settled visible rendering eventually within 500 ms;
- no stale, missing, or wrong-revision raster publication.

We currently test 50%, 100%, and 200%. Production should support at least 25% and 400%, with 800% as a desired upper level if hardware evidence supports it.

## 1. The first vector pan was much slower than the existing app

The initial vector prototype rendered during pan. Typical frames took 95–105 ms, and the handwriting workload took 197–208 ms. We first underestimated the existing raster path, then measured it directly:

```text
30 physical pan frames
minimum: 25.445 ms
median:  25.452 ms
average: 25.451 ms
maximum: 25.458 ms
```

The existing app was close to 39 FPS because it changed the origin into a 3×3 raster canvas and streamed a strided window to the display. It did not render vectors or shift a framebuffer. This result changed the architecture: vector rendering could not sit on the pan path.

## 2. The 3×3 canvas became a cache

We kept `VectorDocument` as the source of truth and redefined `WorldCanvas` as disposable raster storage. Panning inside materialized pixels remained a display copy. Background work filled neighboring regions and refined visible pixels.

The 3×3 cache is a prototype mechanism, not the intended production layout. The production direction is a complete low-resolution overview plus sparse world-aligned high-resolution tiles near the active viewport.

## 3. The first interactive cache test exposed misses and slow zoom changes

Aggressive panning at 50% exposed many checkerboard misses. Switching zoom levels could take several seconds. The prototype had proved that cached raster pan could remain fast, but it had not solved first feedback, cache provenance, or settled convergence.

We separated zoom into quality stages:

1. publish a valid resampled fallback;
2. render a faster noncanonical settled view;
3. finish canonical exact refinement in the background.

## 4. Removing the interaction-time atlas clear saved about 100 ms

The first hardware A/B run compared commit `61dd649` with the review patch. Removing the full-atlas clear and caching source-validity work cut 91–105 ms from every zoom transition.

| Transition | Baseline | Patched |
|---|---:|---:|
| 100→50 | 143 ms | 52 ms |
| 50→100 | 202 ms | 111 ms |
| 100→200 | 201 ms | 111 ms |
| 200→100 | 168–170 ms | 62–65 ms |

The panel transfer itself remained about 26 ms. This was the first strong hardware proof that sub-100-ms feedback was plausible.

## 5. Center-out strips made first feedback physically measurable

We changed fallback publication to 22-row center-out strips. One strip fits one panel transaction. Transfer completion callbacks, rather than queue submission time, became the endpoint.

On the realistic 1,000-stroke handwriting document:

- first strip physically completed in 6.6–16.2 ms;
- the complete visible fallback physically completed in 39–57 ms;
- all twelve automated zoom transitions succeeded.

This met both fallback gates. The screen could show a pixelated but valid preview quickly while vector refinement continued.

## 6. Cancellation corrupted the next zoom source

Adding visible settled rendering introduced a refusal loop. The first three zooms worked, then later zooms failed. Cancellation left the active atlas partial, and the next transition treated that partial atlas as its source.

A third full atlas would have cost another 2.97 MB and could not fit. We instead pinned the complete initial 100% atlas in the inactive arena. Later zooms rewrote only the active arena. Canceled runway or refinement work could no longer damage the source.

The next hardware run accepted all twelve transitions:

- first strip: 7.1–15.8 ms;
- complete fallback: 40.2–51.6 ms.

## 7. The first settled renderer was correct enough but slow

The settled renderer approximated each stroke with variable-radius capsules and analytic edge coverage. It preserved painter order and eraser behavior but allowed slight antialiasing and join differences.

Before LOD optimization, visible settled completion took roughly:

- 50%: 747–749 ms;
- 100%: 1.23–1.24 s;
- 200%: 902–905 ms.

## 8. The first LOD numbers were misleading

The initial LOD dropped nearby samples with fixed spacing. It looked fast, but independent review found that it could erase loops, hairpins, pressure peaks, and eraser dabs. The benchmark also reported cache-ready time before the final display transfer, so its claimed 200% result below 500 ms was false.

We replaced the spacing filter with an iterative centerline/radius error-bounded simplifier. It preserves endpoints, geometry that exceeds the centerline error, and all sampled radius extrema. We added loop, hairpin, pressure-pulse, painter-order, eraser, malformed-map, and raw-fallback tests.

We also moved the settled endpoint to the final physical transfer completion and guarded it with generation, document revision, and viewport checks.

This correction is important for the eventual post: the lower number was not a win. Better measurement and adversarial review invalidated it.

## 9. Grouping bands did not remove the main bottleneck

The renderer originally traversed the document once per 32-row band. We grouped visible bands from one cache cell into one rendering supertask and shared the canonical 164,864-byte coverage arena rather than allocating another full-cell scratch buffer.

The result was smaller than expected:

- 100% improved from about 708 ms to roughly 674–682 ms;
- 200% improved from about 656 ms to roughly 632–651 ms.

Document traversal was not the dominant cost. Current profiles put roughly 360–460 ms in capsule coverage rasterization and around 98–103 ms in compositing. The next large speedup needs a different pixel-coverage organization, likely ordered sparse microtiles or scanline spans, rather than more traversal cleanup.

## 10. Drawing forced explicit raster provenance repair

Once a user adds a stroke, the pinned overview belongs to the previous document revision. Zoom must not sample it until repair finishes.

The benchmark now keeps fallback storage ownership pinned, marks intersecting source bands pending, redraws those bands canonically, and advances the source revision only after all pending bands complete. Zoom refuses while the source is stale.

The automated hardware mutation test reported:

```text
TINYDRAW_FALLBACK_REPAIRED revision=2
TINYDRAW_AUTO_MUTATION started=1 committed=1 stale_zoom_accepted=0 repaired_zoom_accepted=1
```

Later review found another edge case: the live raster captures only the viewport. A cache band crossing the viewport edge cannot be marked current for its offscreen pixels. The fix marks a touched band current only when the captured viewport contains the whole band. Other touched bands remain invalid until vector redraw.

## 11. Current checkpoint

Commit `ba6c392` is the checkpoint prepared for Grok 4.6 review. Host debug, sanitizer, release, ESP build, flash, and automated hardware runs pass.

Latest physical results from `second_review_hardware_ab/grok-handoff-auto-hardware.log`:

- 12/12 unchanged-document zoom transitions accepted;
- first strip about 7 ms typical and 71 ms worst;
- complete fallback about 42–49 ms typical and 113 ms worst;
- settled 50%: 396–459 ms;
- settled 100%: 674–677 ms;
- settled 200%: about 632 ms;
- 125,208 bytes of PSRAM free after benchmark allocation;
- 7,537 settled samples, using 90,444 of 98,304 allocated LOD bytes.

Fast valid zoom fallback is established for 50/100/200%. Settled 100% and 200% still miss the 500-ms goal. Drawing and panning require their own continued latency and visual-regression coverage as zoom support expands.

## Open chapters

- Replace dense whole-viewport coverage work with ordered sparse microtiles or scanline spans.
- Extract the two-arena coordinator into a host-testable state machine and inject cancellation at publication boundaries.
- Measure complete pen-down-to-physical-feedback latency, not only individual raster updates.
- Add 25%, 400%, and 800% one at a time, with provenance and quality gates at each level.
- Replace the prototype 3×3 atlas with a complete overview and sparse world-aligned tiles.
- Add compact gesture history and snapshot-plus-journal persistence after rendering behavior is settled.

## 12. Grok 4.6 found a twelve-row publication hole

We gave commit `ba6c392` to Grok 4.6 at x-high reasoning in read-only mode. Its most important new finding concerned the bottom cache band after zoom. The visible drawing area is 372 rows high, which does not divide evenly into 32-row bands. We resampled 20 rows of the final band, left 12 rows from the previous camera, and marked the complete band derived. A downward pan of up to 12 pixels could expose those stale rows before settled rendering replaced them.

The fix starts every destination job invalid. After the visible center-out strips are submitted, it resamples the complete intersecting edge band before marking that band derived. The hardware driver now asks whether a 12-pixel downward view is valid immediately after every zoom. All twelve transitions report `down12_ready=1`.

## 13. Pan runway moved ahead of settled rendering

The next issue was scheduling. After zoom, adjacent cache cells remained invalid until the 400–680 ms settled pass completed. Even a one-pixel lateral pan could be refused.

We now resample a 32-pixel runway around the viewport before settled work. The driver polls a one-pixel lateral view. Hardware measurements show it becomes valid in 58–66 ms. The runway uses the pinned, revision-checked source and certifies only complete 368×32 jobs.

This changes how we interpret settled latency. The current settled endpoint includes useful pre-settled runway work. The visible image takes longer to sharpen, but the user can pan sooner.

## 14. Cancellation was hiding inside canonical compositing

The settled renderer already checked cancellation every eight raster and composite rows. The canonical renderer, used for exact repair and refinement, could still spend roughly 95 ms inside tile compositing without checking. This pushed three first-feedback samples just beyond 100 ms.

We added cancellation checks between canonical tiles and propagated incomplete compositing through `ViewportRenderStats`. On hardware, worst cancellation fell from about 95 ms to 14 ms. First physical feedback returned to 7–20 ms across the twelve-cycle run.

## 15. Two settled-rendering experiments did not meet the target

The first experiment disabled the fixed world-space LOD at 200%. This protected small high-zoom geometry but raised physical settled time from about 632 ms to about 840 ms.

The second experiment used conservative per-row capsule spans to skip empty corners of each segment’s axis-aligned box. It preserved the original per-pixel coverage test, but software square roots and interval setup cost more than the skipped pixels:

| Zoom | Raster before spans | Raster with spans |
|---:|---:|---:|
| 50% | ~313 ms with tight LOD | ~480 ms |
| 100% | ~576 ms with tight LOD | ~792 ms |
| 200% | ~439 ms with tight LOD | ~534 ms |

We reverted the span implementation. The failed run remains in `second_review_hardware_ab/grok-span-auto-hardware.log`.

The current compromise rebuilds one LOD buffer when crossing above or below 100%. The normal map uses 2.0 world-unit center error and 0.75 radius error. The 200% map uses 1.0 and 0.375, capping those errors at 2 and 0.75 screen pixels. One buffer avoids storing two maps under the prototype’s tight PSRAM budget.

The tighter map improves 200% over raw geometry but does not reach 500 ms. Latest physical settled results are about 489 ms at 50%, 800–851 ms at 100%, and 747–748 ms at 200%. The next optimization should change coverage representation without per-row square roots. Ordered sparse microtiles with incremental or fixed-point scan conversion remain the leading direction.

The clean-commit rerun is `second_review_hardware_ab/4fc345e-auto-hardware.log`.
Its firmware reports `App version: 4fc345e` and reproduces the dynamic-LOD
results, so the evidence now identifies the exact committed source.
