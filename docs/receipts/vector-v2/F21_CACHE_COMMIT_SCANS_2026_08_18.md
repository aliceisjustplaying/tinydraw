# F21 cache commit scan receipt — 2026-08-18

## Verdict

GO for zero-allocation retained-key marking in revision commits. NO-GO for a
free stack or CLOCK replacement: full-pool eviction remains a small part of AA
publication, and changing replacement policy would give up exact LRU behavior.

The benchmark uses the production 448-slot pool and 13,692-identity catalog.
Each commit sample starts from a fresh materialization, stages overview pixels
outside the timed interval, and times every bounded metadata slice through
completion. Release command:

```sh
cmake --build out/build/host-release --target tinydraw_vector_v2_cache_benchmark -j 8
./out/build/host-release/vector_v2/tinydraw_vector_v2_cache_benchmark
```

## Release A/B

| Workload | Baseline median | Treatment median | Result |
|---|---:|---:|---:|
| warm settled-AA publication | 0.174 us/tile | 0.172 us/tile | unchanged |
| publication with 280 resident slots | 1.174 us/tile | 1.181 us/tile | unchanged |
| full-pool AA publication/eviction | 1.906 us/tile | 1.815 us/tile | unchanged |
| local absorption, 56 retained raw tiles | 3.792 us | 3.333 us | -12.1% |
| full-world history commit, 448 retained raw tiles | 114.000 us | 59.250 us | -48.0% |
| full-world history commit, full uniform catalog | 2,330.625 us | 131.750 us | -94.3% (17.7x) |

The local absorption case scans 448 slots and a 110-identity uniform window.
Its retained membership work falls from 1,596 comparisons to 56 direct marks.
The raw history case falls from 100,576 comparisons to 448 marks. The full
uniform case falls from 6,033,888 comparisons to 448 marks; its 13,692-entry
catalog traversal remains linear and contiguous.

## Treatment

Raw retained entries use the already-impossible next revision as their
transient mark. Uniform entries use `0xFFFE`, the unused penultimate raw-slot
directory sentinel. Both marks are established and cleared under the existing
serialized commit contract. Cooperative commits count marking and cleanup
against `max_work_items`; cancellation restores both representations.

No canvas field, continuation field, heap allocation, tile slot, catalog byte,
or export-reserve byte was added. Lookup, quality, revision publication, and
LRU protection/eviction order are unchanged.

## Exactness and boundedness

Debug, Release, and ASan rendering suites each pass 125/125 tests and 63,090
assertions. The focused coverage compares synchronous and cooperatively staged
mixed raw/uniform commits, includes a superfluous retained key outside damage,
and cancels after a one-item retained prepass to prove lookup state restoration.

The full-pool eviction scan remains 448 candidates. Its roughly 1.8–1.9 us host
cost does not justify replacement metadata or a policy change. F21 therefore
improves absorption/history commit latency; it is not the primary all-zoom AA
raster speed treatment.
