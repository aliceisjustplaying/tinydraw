# Chrome prestaging receipt

Recorded: 2026-08-16
Hardware: ESP32-S3 + CO5300 on `/dev/cu.usbmodem101`
Before: `live-ink-visual-first-author.log`
After: `gate-chrome-prestage.log`

The author reported two rare micro-flickers during the visual-first ink check.
They could not be reproduced on demand, and no live-ink failure appeared in the
trace. One full-frame chrome refresh spent 82.192 ms inside staged chrome paint
and 96.577 ms between transport start and drain, matching the visible symptom.

## Cause and change

`ChromeStagingCache::paint()` could regenerate invalid sprites from inside the
DMA staging callback. A document revision could therefore redraw the minimap
base while the panel stream was already in progress.

The presenter now prepares only cache sprites intersecting the upcoming panel
bounds before transport starts. DMA staging uses `paint_prepared()`, which
cannot regenerate cache state and fails if an intersecting sprite is stale.
Small live-ink updates do not pay for an unrelated minimap revision.

A host regression proves that a revision outside a small submitted region does
not redraw the minimap, a stale minimap cannot be painted, and explicit bounded
preparation makes it paintable without staging-time cache mutation.

## Device result

| Measurement | Before | After |
|---|---:|---:|
| Worst chrome work inside staging | 82.192 ms | 5.128 ms |
| Worst explicit pre-transport preparation | not separated | 11.936 ms |
| Worst total chrome work | 82.192 ms | 17.064 ms |
| Live overlay chrome maximum | not separated | 0.255 ms |

The automated gate completed without a crash or watchdog. Pan, live overlay,
mixed draw, long gesture, exactness, cache, memory, and export gates remained
green. The aggregate verdict remains red only for the pre-existing adversarial
cold-render target.

The normal interactive firmware was restored and reached
`TINYDRAW_VECTOR_V2_READY`. Glass confirmation that the rare flicker is gone is
still pending because the original symptom was intermittent.
