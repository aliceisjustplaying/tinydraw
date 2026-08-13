# Gate 1 execution refinements

Date: 2026-08-13  
Governing plan: [`PRODUCTION_GATE_PLAN_2026_08_13.md`](PRODUCTION_GATE_PLAN_2026_08_13.md)

These refinements are load-bearing for executing Gate 1. They clarify the final review of the governing plan; they do not expand Gate 1 into later product work.

## 1. Quality transitions are revision-scoped

The quality guard prevents a lower-quality publication from replacing a higher-quality tile **within the same document revision**.

A revision-advancing immediate mutation may and should downgrade an affected anti-aliased `kSettled` tile:

1. Revision N owns an anti-aliased `kSettled` tile.
2. A new stroke creates revision N+1.
3. The immediate append path applies that stroke with hard-edged rendering.
4. The resulting mixed-quality tile is labeled `kImmediate`.
5. Later settlement replaces it with revision N+1 `kSettled` output.

Required invariant: quality cannot decrease within one revision; revision-advancing immediate mutation may downgrade affected tiles because the new revision has not settled.

## 2. Cold refinement and warm cache behavior are separate measurements

Repeated zoom tests can become cache-hit tests. Receipts must distinguish:

- **cold refinement:** visible tiles absent before every trial;
- **warm zoom/pan:** valid resident tiles retained;
- **fallback presentation:** immediate overview-derived result;
- **settlement:** time until every visible tile reaches the requested quality.

A p95 claim requires a declared sample count. Target at least 20 measured trials per key case when hardware time permits.

## 3. The SSAA verdict uses a complete visible viewport

A single tile or supertask cannot establish the anti-aliasing gate. Ink density varies across the handwriting workload, and fixed costs do not extrapolate linearly.

The green/yellow verdict must use measured end-to-end 4-sample supersampled production of the **complete visible 100% viewport** using the seed-7 handwriting corpus. Per-tile measurements may diagnose cost but do not establish `<500 ms`.

## 4. Progressive presentation is bounded

`TileProducer` reports publications and remains unaware of display policy. The presenter owns a bounded operation that:

- accepts an upgraded level-space rectangle;
- intersects it with the current viewport;
- composes only that region into bounded scratch;
- preserves or clips around toolbar overlays;
- schedules only affected even-aligned panel strips.

Do not refresh the complete 368×448 display after every supertask. Quadrants inside a 128×128 supertask must be packed into contiguous tile scratch before `publish_tile()` because publication does not accept a source stride.

## 5. Worklist lookup distinguishes tiles from fallback

`MaterializedCanvas::lookup()` succeeds for both resident tiles and overview fallback. A key is resident only when:

```text
lookup(key).kind == SourceKind::kTileSlot
```

Gate 1's hard-edged worklist accepts `kImmediate` or better. A later settled worklist must require `kSettled` or better.

## Timebox interpretation

Gate 1 now includes a host-tested module, quality semantics, bounded presentation, a realistic-workload converter, independent pan diagnosis, cold/warm measurements, interaction instrumentation, and a complete-viewport SSAA probe.

The one-day limit is an elapsed-time kill condition, not a promise to finish every checkbox. If the timebox ends without a hardware hard-edged verdict, report **inconclusive/rescope** rather than silently extending Gate 1 through more small tasks.

## Execution order

1. Fix pan independently.
2. Produce missing high-resolution tiles with raw, painter-ordered source operations.
3. Measure genuinely cold refinement on deterministic and handwriting-like workloads.
4. Measure responsiveness while production work blocks the single loop.
5. Measure complete-visible-viewport SSAA rather than estimating a multiplier.
6. Decide green/yellow/red.
7. Do not begin persistence, Undo, or shipping integration without that verdict.

The next artifact after this note is one Gate 1 receipt containing either a result or an explicit timebox failure.
