# Performance slice glass verdict — 2026-08-14

This note closes the cold-render, long-stroke, cache, and export slice at
`00d054a`. The branch is viable and worth merging, but it is not a performance
closure for the complete product. The final manual session confirmed the cold
render gains and exposed drawing, pan, and export debt that the automated gates
did not cover.

Raw capture: [`00d054a-manual-glass.log`](00d054a-manual-glass.log)

- physical ESP32-S3 running the Vector V2 product build;
- session start: 18:38:01;
- export began: 18:45:45;
- SHA-256:
  `ba3f5fdce794626939e0594a13bf9cfedf6aacb28fbfbf5eadff4f78000593cc`.

## What improved

### Cold refinement

The final 20-reset distribution at `6abfa0f` remains the accepted cold-render
receipt:

| Corpus | Zoom | Before | Current p95 | Change |
|---|---:|---:|---:|---:|
| 4× adversarial tapered | 50% | 165 ms | 161 ms | -2.4% |
| 4× adversarial tapered | 100% | 244 ms | 230 ms | -5.7% |
| 4× adversarial tapered | 200% | 603 ms | 539 ms | -10.6% |
| 4× adversarial tapered | 400% | 1,452 ms | 646 ms | -55.5% |
| overlapping XL | 50% | 541 ms | 468 ms | -13.5% |
| overlapping XL | 100% | 406 ms | 316 ms | -22.2% |
| overlapping XL | 200% | 416 ms | 315 ms | -24.3% |
| overlapping XL | 400% | 416 ms | 300 ms | -27.9% |
| seed-7 realistic | 400% | 343 ms | 362 ms | +5.5% |

Every producer tick in that distribution stayed under 12.7 ms. Future work may
trade some of this margin for drawing latency, but the agreed cold-render
ceiling is now **p95 below 800 ms for every gated corpus and zoom**.

### The long 400% stroke

The manual session included one continuous XL 400% gesture with 3,751 ink
samples split across 80 committed chunks:

- worst append: 13.3 ms;
- zero presentation failures;
- zero touch errors or queue overflows;
- zero rejected strokes;
- document and canvas authority matched.

The tester saw continuous ink and no freeze. This confirms that the in-place
commit removed the old repeated 70 ms stalls for this important case. An earlier
shorter 400% stroke reached 17.6 ms, so the deterministic gate's 11.1 ms maximum
must not be quoted as the product-wide worst case.

### Cache retention

The permanent tour A/B remains valid for navigation without intervening document
mutation. A 16-viewport 400% return trip refilled 63 tiles in 409 ms at 320 slots
and no tiles in 40 ms at 384. During the manual zoom tour, 50%, 100%, and 200%
returned sharp. The first return to an older 400% location used full overview
fallback and refined in 677 ms; subsequent returns were cached. The 384-slot
pool improves retention, but the existing gate does not include drawing between
visits.

### Export output

The tester mounted the read-only TinyDraw Export drive and opened the correct
1472×1792 `DRAWING.PNG`. The hard-edged output matched the current documented
quality level. Settled anti-aliasing remains open.

## Regressions and uncovered debt

### Drawing latency at lower zooms

The manual session invalidated the claim that interactive commit work is bounded
across the product. The current deterministic long-gesture gate tests a
controlled 400% case only. With a warm multi-zoom cache, real 25% and 100%
gestures produced these worst chunk times:

| Zoom | Worst append | Main-loop poll gap | Chunks over 33 ms completion |
|---:|---:|---:|---:|
| 25% | 120.1 ms | 126 ms | 9 across the affected strokes |
| 100% | 131.8 ms | 137 ms | 15 across the affected strokes |
| 400% long gesture | 13.3 ms | 96 ms | 0 |

No operation was lost and the touch task kept event age at or below 1.3 ms when
the coordinator consumed events. The coordinator still blocked for too long.
Code inspection points to eager in-place painting of every intersecting resident
raw tile at every zoom. A larger warm cache increases that cross-zoom mutation
fanout. This explanation needs a deterministic mixed-zoom hardware gate and
phase measurements before the policy changes.

The next drawing acceptance gate must warm a realistic multi-zoom cache, then
draw and erase at every zoom. The target is 10–12 ms per deterministic chunk,
with a hard product alarm no higher than 15 ms. Drawing latency takes priority
over cache retention and cold-refinement margin.

### Pan throughput

The tester's impression that panning had not improved was accurate. This slice
optimized cold production, not pan frames.

| Zoom | Frames | Framebuffer-reused | Reuse rate | Average compose | Average panel completion |
|---:|---:|---:|---:|---:|---:|
| 400% | 814 | 149 | 18.3% | 27.6 ms | 19.7 ms |
| 200% | 244 | 7 | 2.9% | 29.0 ms | 19.7 ms |

The 400% movement superseded 109 refinement jobs. Large coalesced deltas, the
96-pixel reuse limit, and missing sharp pixels commonly forced full-frame
composition. All recorded presentations passed and synchronized to TE; the log
does not confirm the tester's tentative tearing concern. Physical observation
remains authoritative for tearing.

A separate `feat/v2-warm-pan` worktree contains one attribution-gate commit and
an uncommitted adjustment. It was deliberately left out of this merge because
it is based five commits behind this branch. Preserve and reconcile that work in
the later performance round.

### Export watchdog starvation

Export is functionally correct but not operationally clean. At 18:45:52 the
five-second ESP task watchdog reported that CPU 0's idle task had not run. The
symbolized stack was in `PNGFindFilter` through `PNG_addLine` and the V2 export
path. The same warning was already present in the automated receipts:

- `7302963-export-gate.log`;
- `6abfa0f-full-gate.log`;
- `cache-tour-384.log`.

Those harnesses passed because `CONFIG_ESP_TASK_WDT_PANIC` is disabled. Calling
them fully green without disclosing the warning was incorrect. The future fix
must yield at bounded row intervals, keep PNG exactness, and complete with no
watchdog report. Export reliability matters more than speed. A visible progress
indicator may be added during the UI round.

### Popup dismissal

Tapping the canvas while a tool, size, or document popup is open does not dismiss
the popup. If Pan is selected, the touch may move the canvas behind the popup.
The UI round must consume an outside tap while closing the active popup.

## Accepted priority and deferred work

Performance work resumes in this order:

1. drawing and erasing latency;
2. cold refinement, with p95 below 800 ms at every gated zoom and corpus;
3. pan responsiveness and framebuffer reuse;
4. PNG export speed.

PNG watchdog starvation is reliability debt rather than a speed project and may
be fixed earlier. The next performance round also needs a mixed-workload
320-versus-384 A/B. The larger cache stays for now because its navigation benefit
is measured, but it is no longer accepted on that evidence alone.

The immediate next milestone is UI work: popup behavior, color-picker changes,
zoom controls, battery status, export progress, and a bounded minimap skeleton.
