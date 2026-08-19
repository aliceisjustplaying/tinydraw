# Visual-first ink receipt

Recorded: 2026-08-16  
Hardware: ESP32-S3 + CO5300 on `/dev/cu.usbmodem101`  
Gate receipt: `gate-visual-first-ink.log`  
Author trace: `live-ink-visual-first-author.log`

This is provisional visual-lane closure. Formal five-trace injection, optical
latency, resumable lift authority, and autosave-on closure remain open.

## Accepted change

- Carry the original sampler timestamp into presentation.
- Render the newest provisional ribbon tail in DMA staging without mutating the
  reusable canvas ring or document authority.
- Replace the old transient tail with the newest tail on every visible update.
- Submit visible geometry before synchronous chunk authority work.
- Preserve committed geometry and restore authority on capacity rejection.

The host coordinator regression proves visual-before-authority ordering and
timestamp identity. The surface-renderer regression proves global ribbon
geometry clips correctly into a bounded staging surface without touching row
padding.

## Device gate

The gate exercised 48 clear-tail and 48 provisional-overlay replacements:

| Path | Wall maximum | Event→submit maximum | Event→DMA maximum | Failures |
|---|---:|---:|---:|---:|
| Clear old tail | 3.940 ms | 2.129 ms | 3.060 ms | 0 |
| Paint new tail | 3.912 ms | 2.048 ms | 3.175 ms | 0 |

Mixed draw, long-gesture, authority, pan, and staging gates remained green. The
full device gate still fails only its pre-existing adversarial cold headline.

## Author trace and glass verdict

The author drew dense hairlines and figure strokes across 25%, 50%, 200%, and
400% zoom using several colors. Across four aggregate reports:

| Samples | Event→submit average | Event→submit maximum | Event→DMA average | Event→DMA maximum |
|---:|---:|---:|---:|---:|
| 767 | 1.89 ms | 4.364 ms | 3.22 ms | 12.400 ms |

- 0 samples exceeded 16 ms to submit or 33 ms to DMA completion.
- 0 presentation failures, touch errors, touch overflows, or events at least
  8 ms old.
- 16 Down and 16 Up transitions were retained.
- Every report ended with exact authority and a committed final state.
- Glass verdict: inking feels fixed; visual-first inking is accepted
  provisionally.

Two rare micro-flickers were reported but could not be reproduced. No live-ink
anomaly coincided with them. The trace contains one unrelated full-frame chrome
refresh with 82.192 ms spent in staged chrome and 96.577 ms total transfer wait;
that refresh is tracked separately.
See `CHROME_PRESTAGE_RECEIPT.md` for its fix and device receipt.

## Remaining closure

- Remove synchronous lift draining through bounded resumable authority slices.
- Capture and inject the five canonical traces through the production `offer()`
  path, including spatial/time-gap and thinning metrics.
- Archive optical p95/p99 and repeat with autosave and normal services enabled.
