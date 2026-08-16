# Mid-stroke pixelation diagnosis — Phase 1 receipt (2026-08-16)

Machine: ESP32-S3 `/dev/cu.usbmodem101`, gate harness at the hold-the-line
revision. Logs: `gate-fallback-probe-1.log` (probe only),
`gate-dropattr-1.log` (probe + drop attribution). Every run held the
standing guards: verdict vector unchanged (only the pre-existing
`overlap_cold` and `mixed_draw` reds), cache-tour ledger
`amplification=1.000 stale=0 unexplained=0`, five `TINYDRAW_INKTRACE
pass=1`.

## Question

Owner-queued diagnosis (Stage B handover §4.1): during benchmark stroke
replays, already-drawn content pixelates mid-stroke. Which mechanism drops
sharp viewport tiles to overview fallback — `materialize_uniform_as_raw`
returning nullopt, paint failure, or retention-budget exhaustion?

## Instrumentation landed

1. **Viewport fallback probe** (harness-only): counts viewport tile
   identities whose `lookup()` resolves to `SourceKind::kOverview` (or no
   source). Sampled per ink trace: before any ink (`fb_start`), after every
   chunk commit (`fb_mid_max`), at every lift (`fb_up_max`), after the
   trace (`fb_end`). Printed on the `TINYDRAW_INKTRACE` line.
2. **Drop attribution counters** (product path,
   `InPlaceRetainDrops` in `incremental_document.h`): the retain passes now
   classify every dropped identity — visible uniform with no raw slot,
   visible uniform paint-fail, visible raw edit-fail, visible raw
   paint-fail, and accepted offscreen skips (budget/lazy). Surfaced on
   `TINYDRAW_LIVE_STROKE` (product glass sessions), `TINYDRAW_INKTRACE`,
   and `TINYDRAW_GATE1_MIXED_DRAW`.

## Result: the drop hypothesis is falsified in every harness scenario

| Scenario | Evidence |
|---|---|
| Five recorded ink traces | `fb_mid_max == fb_up_max == fb_end == fb_start` on every trace; all drop counters zero. Ink commits never dropped a sharp viewport tile. |
| mixed_draw (prewarmed, all-zoom warm cache, pen+eraser at five zooms) | `visible_fallback=0` and all drop counters zero on all 10 strokes; `off_skip=0` — the retention budget never even engaged. |

The pixelation seen during benchmark replays is the **pre-existing overview
substrate at unwarmed gate views**: the replay gate presents at origin
views that idle repair never warmed, so the viewport starts 71% pixelated
before any ink lands — `fb_start=30` of 42 tiles at 400%, 24/42 at 100%
(25% has no tiles by design; `fb_start=0`). Committed ink composes from
the upscaled overview inside those tiles, which reads as "already-drawn
content pixelating" while the stroke replays.

Owner confirmation (2026-08-16): the pixelation was only ever observed
during benchmark replays and has never reproduced in a glass session —
consistent with this attribution, since glass drawing happens on settled,
materialized viewports.

## Consequences

- The committed-overlay / authority-revision-split design keeps exactly one
  driver: the mixed_draw append-latency overrun (worst 19.3 ms vs the
  15 ms budget in `gate-dropattr-1.log`, timing-only failures). Pixelation
  is no longer part of its case.
- The counters stay: any future nonzero `drop_*` on a
  `TINYDRAW_LIVE_STROKE` line during a glass session falsifies this
  receipt and reopens the diagnosis with the cause already attributed.
- Open follow-up (owner call): prewarm the replay-gate viewports so
  `fb_start=0` and the probe becomes a zero-expected guard. That changes
  the gate substrate (commits would edit resident raw tiles instead of
  riding fallback), so it reopens the frozen
  `ink-trace-replay-baseline/BASELINE.md` latency statistics per the
  one-variable rule.

## Reproduce

```sh
./scripts/esp32 vector-v2-gate-harness /dev/cu.usbmodem101
uv run --script tools/esp32-capture.py /dev/cu.usbmodem101 /tmp/gate.log 480 \
  --end-marker TINYDRAW_VECTOR_V2_GATE_HARNESS_DONE \
  --failure-regex 'task_wdt|Guru Meditation'
grep -E "TINYDRAW_INKTRACE |TINYDRAW_GATE1_MIXED_DRAW " /tmp/gate.log
```
