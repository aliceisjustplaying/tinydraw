# F10 history affected-replay receipt — accepted

## Decision

**GO. Use the existing `OperationSpatialIndex` to bound Undo/Redo overview
reconstruction.** The treatment adds no lineage identity, generation cache,
heap allocation, or PSRAM. The earlier adjacent-generation design remains a
documented no-go in `HISTORY_GENERATION_PROTOTYPE_2026_08_18.md`.

`move_history_incrementally()` still clears exactly the damaged 25% overview
rectangle. A prepared history move now queries conservative candidates for its
target active prefix, exact-bounds checks them, and replays them oldest-first.
The candidate array occupies the unused tail of the existing 329,728-byte
overview scratch. When the damaged pixels plus target-prefix candidates do not
fit, or when the index predicts a dense result, the original authority scan is
used unchanged.

## Deterministic 4,000-operation A/B

Build and run:

```sh
cmake --preset host-release
cmake --build --preset host-release --target tinydraw_vector_v2_history_benchmark
out/build/host-release/vector_v2/tinydraw_vector_v2_history_benchmark
```

The benchmark alternates 2,000 Undo/Redo cycles for each treatment and checks
the overview after both directions against the original full-prefix algorithm.
Repeated release runs on the M-series host produced:

| Corpus | Move | Baseline scan | Spatial candidates | Fallback |
|---|---|---:|---:|---|
| Sparse; 3,999 distant operations | Undo | 3,999 | 0 | No |
| Sparse | Redo | 4,000 | 1 | No |
| Dense; all operations intersect | Undo | 3,999 | not enumerated | Yes |
| Dense | Redo | 4,000 | not enumerated | Yes |

| Corpus | Baseline mean/move | Treatment mean/move | Speedup range |
|---|---:|---:|---:|
| Sparse | 0.0248–0.0313 ms | 0.000279–0.000405 ms | 77.4–89.4x |
| Dense | 0.1644–0.1656 ms | 0.1557–0.1651 ms | 1.00–1.06x |

Every comparison was pixel exact. The full release authority suite passed 79/79
tests and 25,609 assertions. Extra persistent and scratch storage is **0 bytes**.

## Correctness and boundedness

- The query world rectangle expands to the complete world-pixel footprint of
  the damaged overview pixels, retaining downscale-edge contributors.
- The index remains conservative metadata; exact bounds and raster validation
  still gate every candidate.
- Candidates are emitted newest-first and consumed in reverse, preserving
  painter order for pen/eraser overlap.
- Prepared Redo may query the retained prefix without publishing it first.
- Dense-prefix prediction declines acceleration before candidate enumeration.
- Missing, overlapping, undersized, or unsuitable workspace returns to the
  original full scan. Failure still cancels the prepared move atomically.

## Scope

This closes the prefix-discovery portion of F10. Dense reconstruction remains
the real raster coverage floor and does not justify adjacent-generation cache
state without a measured glass regression. Device timing can be collected in a
later full gate; no device-only behavior is introduced by this treatment.
