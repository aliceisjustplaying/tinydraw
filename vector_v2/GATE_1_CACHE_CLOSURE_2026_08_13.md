# Gate 1 cache closure

Date: 2026-08-13  
Branch: `feat/vector-canvas-production`  
Hardware: ESP32-S3, 8 MiB PSRAM, 240 MHz, CO5300 368×448 panel  
Evidence: [`hardware-receipts/gate1-paper-cache-scroller.log`](hardware-receipts/gate1-paper-cache-scroller.log)

## Result

**PASS for cache feasibility and cached pan.** The overall Gate 1 rendering
verdict remains **YELLOW** because anti-aliasing is still deferred.

This closes the uncertainty that a useful cache might require a larger raw tile
pool. The cache remains 320 raw RGB565 slots. A 54,768-byte dense catalog stores
uniform tile identities, and a 1,288-byte conservative paper-occupancy oracle
lets the producer publish known paper without replay or raw pixels.

The design follows the measured data rather than treating every tile alike:
for the complete seed-7 document at 100%, 378 of 644 identities are uniform
paper and only 266 require raw slots.

## Hardware facts

### Complete-world cache proof

The app cold-produced every 100% identity, then enumerated every source:

```text
identities=644 raw=266 uniform=378 fallback=0 slots=320 total_us=1716111 pass=1
```

Therefore the entire 100% world remains materialized simultaneously with zero
overview fallback. This is stronger than a single retained pan route.

### Cached pan proof

The test prewarmed the destination strips, established a fallback-free frame,
then moved 24 pixels diagonally. The presenter memmoved the overlapping
framebuffer and composed only two non-overlapping exposed strips.

| Zoom | Compose | Event to first physical completion | Frame reused | Result |
|---|---:|---:|---:|---|
| 100% | 29.118 ms | 30.600 ms | yes | PASS |
| 400% | 29.250 ms | 30.730 ms | yes | PASS |

Both remain below the 35 ms valid-cache pan gate. A pan beyond the bounded
32-pixel strip workspace safely falls back to full composition; ordinary touch
samples use the reuse path.

### Cache retention

After filling unaligned footprints at 50%, 100%, 200%, and 400%, then a disjoint
400% footprint, every original view returned with `remaining=0` and
`fallback_pixels=0`. The raw pool remained 320 slots.

### Export coexistence

With the live app, operation document, cache slabs, corpus, and workspaces all
allocated, the app successfully held a distinct contiguous 1,572,864-byte
export reserve:

```text
free_before=2744716 largest_before=2686976
free_held=1171848 largest_held=1146880 pass=1
```

This is a live coexistence proof, not an empty-heap estimate.

### Live PSRAM breakdown

| Category | Bytes |
|---|---:|
| Four overview-sized buffers | 1,318,912 |
| 320 raw RGB565 tiles | 2,621,440 |
| Raw metadata + uniform catalog + occupancy | 68,856 |
| Operation records and raw samples | 720,000 |
| Producer, append, display-region, and input scratch | 544,520 |
| Temporary seed-7 corpus source | 322,912 |
| **Explicit live storage** | **5,596,640** |

After all automated proofs: 2,744,716 bytes free PSRAM; largest contiguous block
2,686,976 bytes.

The 322,912-byte seed corpus is test-only. Production does not need to retain it
beside the compact operation authority.

## Responsiveness and second-core decision

The realistic 1,000-operation document measured:

- maximum producer compute slice: 13.389 ms during draw-while-fill;
- producer poll gap: 1.987 ms;
- live event to first completion: 4.234 ms;
- cached pan first completion: 30.600–30.730 ms.

These pass the current interaction limits on one application core. A second-core
producer would add synchronization and publication complexity without solving a
measured failure, so it is **not funded in this batch**.

## Cold work remains visible debt

Worst-case unaligned cold fills took 0.857–0.988 seconds in this instrumentation
run because each publication was also progressively transferred to the panel.
This does not affect the cache correctness result, and revisits do not replay.
The previously accepted 0.64–0.72 second cold-compute target remains optimization
debt; these display-inclusive probes are not relabeled as meeting it. The
`within_cold_gate` telemetry is intentionally diagnostic rather than part of
this cache-correctness verdict; cold-time regression gating remains future work.

An append crossing learned paper invalidates that uniform identity without
promoting it to a raw slot. Until idle refill relearns it, composition uses the
newly updated overview, not stale paper. This is a deliberate short-lived
quality tradeoff that avoids raw-slot churn and is host-tested for freshness.

## Validation

- Host test suite: 22/22 CTest entries passed.
- Added host coverage for in-place framebuffer movement and exposed-region
  partitioning.
- Added host coverage for grouped certain-paper publication.
- MaterializedCanvas mutation tests verify learned-paper invalidation,
  raw-to-uniform reclassification, and workspace non-aliasing.
- Hardware automated summary: every flag passed, including full-world cache and
  export reserve.

## Remaining manual test

One final glass test should verify actual touch behavior rather than cache
feasibility:

1. At 25%, confirm the seed-7 marks are present.
2. Cycle to 100%; select Pan and drag normally. Movement should track without
   repeated visible refinement after the destination has warmed.
3. Cycle to 400% and repeat.
4. Draw an XL stroke at 400%, pan away and back, then return to 25%; confirm the
   stroke remains and no stale strip or corruption appears.

Anti-aliasing remains deliberately outside this batch.
