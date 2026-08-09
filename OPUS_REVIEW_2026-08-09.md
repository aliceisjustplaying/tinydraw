# Whole-codebase review — Claude Opus 5

Date: 2026-08-09
Scope: complete repository at `6cd7bd9`, after the tight-ribbon-join fix at `6c15de4`.
Mode: high-effort, read-only review.

The reviewer read the four primary documents, every project-owned source/build/test/script file,
diffed the PF port against the pinned upstream checkout, ran debug/release/ASan/UBSan/format checks,
and built standalone probes under `/tmp`. No repository files were edited by the reviewer.

## Overall recommendation

**GO for streaming geometry, with small correctness preconditions.**

The reviewer found no fundamental drawing-engine or memory-model mistake. The PF baseline is a
trustworthy oracle, timestamp handling is strong, the coverage/composite contract is sound, and the
documentation accurately names the main limitations. Streaming geometry remains the highest-value
next milestone.

Project constraint added after the review: favor aggressive code simplicity. Fix concrete bugs with
small local changes. Do not add abstraction or machinery unless required to preserve correctness or
to implement the bounded streaming path.

## Verified strengths

- All documented numeric claims were accurate at review time: 32 doctest cases, 9 CTest entries,
  debug/release/ASan/UBSan green, format clean.
- `RibbonRenderer` scratch is a member (~12 KiB), not a render-function stack frame.
- The batch PF port closely matches the pinned TypeScript implementation, including constants,
  point expansion, suppression, sharp-corner latch, initial-pressure averaging, cap counts, and
  outline concatenation order.
- The unusual upstream floating-loop cap counts are reproduced correctly.
- The reference generator verifies the exact pinned upstream commit before writing oracle data.
- Timestamp equality, regression, wrap-around, cadence adjustment, and exact lift behavior are
  covered and were judged the strongest area of the implementation.
- Coverage pieces are unioned before one RGB565 composite; idempotence is tested.
- Snapshot terminology and documentation are honest about oracle versus characterization data.

## Findings to fix before streaming work

### High: float-to-int undefined behavior in raster bounds

Files:

- `core/src/ribbon_renderer.cpp`
- `core/src/coverage_tile.cpp`

Finite but extremely large coordinates can pass validation and be cast to `int` before clamping.
NaN can reach public `CoverageTile::rasterize_*` methods directly. Floating-to-integer conversion
outside the representable range is undefined behavior.

Reviewer probes triggered UBSan with approximately `±1e30` and NaN coordinates.

Small fix:

- validate finite values at coverage entry points;
- clamp in floating-point space before converting to `int`;
- add extreme-coordinate and NaN regression tests.

### Medium: zero-area convex polygons rasterize as filled

File: `core/src/coverage_tile.cpp`

`point_in_convex` returns true when every cross product is zero because no orientation sign is ever
established. Repeated or collinear points can therefore produce coverage despite having no area.

Small fix: return false unless at least one non-zero orientation was observed. Add repeated-point and
collinear-polygon tests.

### Medium: non-finite `InkStream` input has no explicit policy

File: `core/src/ink_stream.cpp`

NaN input poisons stream state, downstream primitives become invalid, and the stroke silently
disappears. Decide a simple policy—reject/ignore invalid samples or return an explicit status—and
cover it. Do not build a general validation framework.

### Medium: committed/provisional invariant lacks direct test coverage

The host code structurally keeps provisional pixels out of `committed_pixels`, but replay tests only
render on lift and never execute the interactive provisional loop.

Land this test as part of the streaming milestone at the reusable canvas/tile seam. Avoid coupling a
large test harness to SDL solely to test it now.

## Findings to decide explicitly during streaming work

The visible PF-style path intentionally or accidentally omits three upstream behaviors:

1. first-ten-point initial-pressure warm-up;
2. smoothing-based outline-point suppression;
3. start-of-stroke `runningLength < size` noise gate.

The batch reference implementation has these behaviors; the visible stream path does not. Record
each as implemented, intentionally changed, or deferred. Suppression may materially reduce primitive
count and work.

## Performance evidence from the review

The existing documented ~0.08-second, 1000-point replay measures one final render, not the
interactive per-sample rebuild path.

The reviewer measured the current full-frame-copy + full-geometry-rebuild + full-rerender path on
ARM64 release:

```text
samples   total interactive work   average frame   final-only render
100       ~150 ms                  ~1.5 ms         ~1.9 ms
300       ~608 ms                  ~2.0 ms         ~3.0 ms
1000      ~4.0 s                   ~4.0 ms         ~7.3 ms
3000      ~29 s                    ~9.7 ms         ~19 ms
```

The trend is quadratic over the complete gesture because every sample rebuilds and rerenders all
prior samples. At 3000 points, temporary geometry vectors were estimated around 624 KiB per frame.
This confirms—not changes—the planned move to bounded streaming geometry.

Add structural per-update assertions after streaming exists: newly touched primitives, provisional
count, committed count, and tiles touched. Prefer operation counts over fragile host timing gates.

## Lower-priority findings

- Most bounds/precondition checks use `assert` and disappear in release. Embedded-facing methods
  need bounded behavior or always-on checks where invalid input could corrupt memory.
- Dirty rendering uses one global stroke bounding rectangle and scans every primitive for every
  visited tile; thin diagonal strokes visit many empty tiles.
- `InkConfig::antialias` and `end_taper` are currently unwired; `smoothing` only affects the batch
  baseline.
- `DisplayBackend` and `Rect` are not yet exercised by production paths.
- Host timestamps have millisecond resolution multiplied into microseconds; do not infer hardware
  sub-millisecond feel from host input.
- A forward timestamp gap larger than roughly half the uint32 range is treated as regression. This
  is irrelevant to a real active stroke but should be documented as an assumption.
- Add `*.ppm binary` to `.gitattributes` so snapshot safety does not rely on Git's content heuristic.
- There is no documented snapshot regeneration/approval command yet.
- No fixture yet represents every requested benchmark class or a truly long retained stroke.
- The visible path has snapshot coverage but no independent pixel oracle; the batch PF path is the
  independently verified implementation.

## Residual risks

- No proven append-stability/commit horizon.
- No bounded primitive/point arena.
- No direct reusable committed/provisional invariant test.
- No ESP-IDF/QEMU/cross-target harness.
- No real hardware timing, DMA, PSRAM, panel, or touch evidence.
- The active host path still depends on vector allocations and whole-stroke rebuilds.

## Post-review resolution

Immediately after this review, the parent agent applied the small local fixes recommended by the
correctness findings:

- raster entry points reject non-finite/extreme coordinates;
- bounds clamp in floating-point space before integer conversion;
- repeated/collinear zero-area convex polygons produce no coverage;
- non-finite `InkStream` samples no longer poison stream state;
- regression tests cover each case under ASan/UBSan;
- `*.ppm` is explicitly marked binary.

These landed in `d9a1960` and `3fe4ad1`. The committed/provisional invariant test and operation-count
checks remain intentionally attached to the upcoming streaming module, where the correct reusable
seam will exist.

## Recommended immediate order

1. Fix float-cast UB and zero-area coverage with small tests.
2. Choose the minimal invalid-input policy for `InkStream`.
3. Add `*.ppm binary` and correct the performance wording in `PROJECT_STATE.md`.
4. Begin bounded streaming geometry.
5. In that milestone, directly test the committed/provisional invariant and operation counts.

Do not introduce a broad validation hierarchy, generic allocator framework, or elaborate benchmark
system in response to this review. The concrete local fixes and the already-planned streaming module
are sufficient.
