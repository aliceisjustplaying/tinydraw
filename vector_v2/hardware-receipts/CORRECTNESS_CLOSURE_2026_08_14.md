# Vector V2 interaction-correctness closure — 2026-08-14

## Verdict

**GREEN for operation preservation and input-edge correctness.** The physical run preserved every reported stroke, chained both long contacts without losing authority, balanced every consumed Down/Up edge, overflowed no touch queue, and showed no presentation failure or visible tearing.

**Performance work remains funded.** Committing an intermediate long-stroke chunk blocked the main coordinator for roughly 0.61–0.73 seconds. No input edge or operation was lost because core 1 continued sampling into the transition-preserving FIFO, but this pause plausibly explains the tester's impression that the long stroke briefly stopped. It is not acceptable as final interaction latency.

## Code anchors

- `26a05f5` — correctness implementation plus restored main-task stack margin.
- `f722a48` — acceptance-only touch edge/age counters used by the physical run.

## Automated gate

Source: [`26a05f5-correctness-closure-gate.log`](26a05f5-correctness-closure-gate.log)

- Complete hardware harness: `pass=1`.
- 4× adversarial 400% cold replay: 1,451,904 us.
- Maximum producer tick: 9,785 us across the paced cold cases, below the 15 ms alarm.
- Touch errors/queue overflows: zero.
- Frame-reuse pan gates: pass at 100% and 400%.
- Cache retention: pass with 320 raw slots.
- Paper sweep: 266 raw + 378 uniform identities, zero fallback.
- Live 1.5 MiB export reserve allocation: pass.
- Main-task minimum free stack: 2,472 bytes in the larger gate harness.
- Live storage: 5,300,672 bytes, including the 4,096-sample input buffer.

## Clean 20-reset cold distribution

Source: [`26a05f5-cold-p95-20-runs.log`](26a05f5-cold-p95-20-runs.log)

| Corpus | Zoom | p95 wall time |
|---|---:|---:|
| 4× adversarial tapered | 50% | 164,971 us |
| 4× adversarial tapered | 100% | 243,956 us |
| 4× adversarial tapered | 200% | 602,969 us |
| 4× adversarial tapered | 400% | 1,451,905 us |
| Overlapping XL | 50% | 541,331 us |
| Overlapping XL | 100% | 405,954 us |
| Overlapping XL | 200% | 415,975 us |
| Overlapping XL | 400% | 415,966 us |
| Seed 7 realistic | 400% | 342,970 us |

The adversarial 400% maximum was 1,451,912 us. The distribution is extremely stable and remains below the existing two-second alarm. This receipt does not justify tightening the alarm around a single run without first deciding the intended product workload margin.

## Physical glass acceptance

Source: [`f722a48-correctness-closure-glass.log`](f722a48-correctness-closure-glass.log)

The tester performed more than the requested 100 rapid strokes/taps/pans, two long physical contacts totaling nearly three minutes after contact was interrupted, aggressive 400% pan, zoom changes, and additional 400% drawing.

### Input and authority

- 126 stroke reports; all 126 committed and refreshed successfully.
- 128 operation chunks; no rejection or commit failure.
- 323 Down events and 323 Up events consumed: exactly balanced.
- 74,624 semantic events consumed.
- 331 events were at least 8 ms old (0.44%), so more than 99.5% were under 8 ms and the p95-under-8-ms acceptance bound passed.
- Touch errors: zero.
- Touch queue overflows: zero.
- Document/materialization revision authority matched after every stroke report.

Endpoint distance was assessed on glass rather than reconstructed from telemetry. The tester observed no missing or shortened gesture in this run.

### Long-stroke chaining

Before the long test, the operation log held 100 operations and 697 samples. The two long contacts produced:

1. revision 102: 3,626 live samples stored as two chunks and 3,627 log samples;
2. revision 104: 4,377 live samples stored as two chunks and 4,378 log samples.

The operation log ended those contacts at 104 operations and 8,702 samples. All 8,003 live samples were retained as 8,005 log samples; the two additional samples are the intentional one-sample overlap at each chunk boundary, so no source segment is lost. Both contacts crossed the compact elapsed-time boundary, proving that chaining is needed even with the 4,096-sample input capacity.

The remaining latency defect is explicit in the same records:

- first long contact: 727,517 us total append work; 612,493 us worst live submit delay;
- second long contact: 830,027 us total append work; 727,648 us worst live submit delay.

The sampler preserved input during these stalls, but the coordinator must make intermediate chunk publication incremental or deferred before long-stroke interaction is considered polished.

### Pan and presentation

- 168 pan sessions, 700 presented pan frames, zero failures.
- 84 frames used framebuffer overlap; tile materialization remained available for the rest.
- Largest per-session compose maximum: 31,810 us.
- Largest per-session physical presentation maximum: 20,436 us.
- The tester reported no visible tearing under aggressive 400% pan.
- All 38 full presentation records passed with tear synchronization.
- All observed background-fill completions reported zero producer and presentation failures.

## Retained flake

One attempted repeated full-harness reset lost TE synchronization before the pan gate: startup/fallback records reported `tear_sync=0`, and the 100% pan completed in 68,473 us with `pass=0`. The next clean full harness passed, and the physical run showed no tearing. The raw evidence is retained as [`26a05f5-te-sync-flake.log`](26a05f5-te-sync-flake.log); it is not being mislabeled as a renderer regression or silently discarded. Panel/TE startup robustness remains a release-hardening item.

## Next measured work

1. Remove the 0.61–0.73 second intermediate long-stroke commit stall without weakening FIFO/input correctness.
2. Continue measured cold-refinement optimization, especially the 1.452-second 4× adversarial 400% case.
3. Improve warm-pan throughput while preserving the now-clean seam/tear behavior.
4. Keep the 1.5 MiB export reserve and exact raster equivalence as hard constraints.
