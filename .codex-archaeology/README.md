# TinyDraw project archaeology

Research date: 2026-08-19. This is an evidence index for later writing, not an article draft. Development runs from 2026-08-09 through the same-revision `v2` release marker on 2026-08-19.

## Bottom line

The remembered causal pattern is strongly supported: a working path exposed a bound, measurement redirected the implementation, and the improvement exposed the next bound. It was not monotonic. Several attractive closures were later revoked by real-hardware correctness, changed benchmark endpoints, or unlike-revision comparisons. The strongest recurring pattern is therefore **build → measure → falsify → change the model**, not a simple optimization ladder.

The largest model changes were: whole-stroke replay to dirty tiles; raster authority to vector authority plus disposable raster materializations; camera-aligned atlas to complete overview plus sparse retained tiles; software TE confidence to optical positive-control evidence; beam racing to a row-zero boundary sweep with per-strip staging invariants; synchronous multi-zoom commits to authority-only commit plus a coherent pending overlay; and fixed “reserved” memory to measured modal/evictable memory.

## Deliverable map

| Requested deliverable | Authoritative section |
|---|---|
| A. Master causal timeline | [`git-history.md`](git-history.md), 29 milestones; each records motive, changed assumption, next problem, survival, evidence, and confidence |
| B. Detailed chronology | [`supplement.md`](supplement.md), day-by-day Aug 9–19 |
| C. Performance ledger | [`docs-performance.md`](docs-performance.md), 36 strict rows plus safe/quarantined figures |
| D. First-appearance index | [`git-history.md`](git-history.md#first-appearances-of-major-ideas) |
| E. Dead ends and reversals | [`docs-performance.md`](docs-performance.md#dead-ends-reversals-and-measurement-traps) and [`git-history.md`](git-history.md#reversals-rejected-paths-and-corrected-claims) |
| F. “You probably forgot this” | [`supplement.md`](supplement.md#you-probably-forgot-this-list), 18 concrete incidents |
| G. Historical builds | [`supplement.md`](supplement.md#historical-builds-worth-demonstrating-maximum-five), five ranked comparisons |
| H. Open questions | [`supplement.md`](supplement.md#open-questions) |
| Reasoning chronology / AI roles | [`session-history.md`](session-history.md) |
| Fuji X-T5 tearing reconstruction | [`session-history.md`](session-history.md#7-aug-15-fuji-x-t5-tearing-experiment--exact-reconstruction) |
| Two-episode writing memory pack | [`two-episodes-writing-memory.md`](two-episodes-writing-memory.md), compact cold-render and Fuji/panning timelines, memory joggers, private glossaries, and wording traps |
| Usage and cost accounting | [`session-history.md`](session-history.md#usage-accounting) |

## Publication guardrails

- **Do not use** the 28.1 ms beam-race pan as a success: glass falsified it.
- Requested 50/60 MHz modes were both 40 MHz actual; the measured full-frame wall was 17.998 ms.
- The early “under 500 ms” LOD result used the wrong completion endpoint and invalid simplification.
- The 5.69× PNG-to-SVG number compares different products and operations, not an A/B optimization.
- The Aug-18 “final” baseline predates later same-day changes and is chronologically stale.
- The 1.5 MiB export reserve was synthetic; measured peak was 291,484 bytes.
- Content-attributed TinyDraw usage through the core development cutoff is 4,590,968,837 processed tokens: OpenAI 3,221,725,938; Anthropic 1,218,849,486; xAI 150,393,413. The $4,160.59 `ccusage` API-equivalent estimate does not establish actual subscription spend; web-only GPT-5.6 Pro reviews are missing.

## Fuji finding in one paragraph

The X-T5 footage was necessary because GETSCANLINE and every control-read probe returned zero, while software synchronization could report success even when the panel visibly tore. In the Aug-15 Pi session, the camera moved to 1080/240 with a flicker-free 1/1024 shutter and recorded `DSCF0665.MOV`. A deliberately unsynchronized control tore; the row-zero rising-edge cell was clean across a 1,495-frame analyzed slice. Later, the product tear stayed fixed near the minus-button edge and moved when burst behavior changed, exposing a deterministic writer/beam crossover in expensive strips. Per-strip staging-before-wire work and cached chrome produced the later glass-clean result.

## Validation and preservation

No historical firmware was flashed and the released board image was left alone. Current source built in a clean temporary directory and all 31 Debug tests passed in the final audit. The only working-tree additions from this research are the files in `.codex-archaeology/`.
