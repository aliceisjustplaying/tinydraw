# Live-ink overlay clipping hardware receipt — 2026-08-14

Device: physical ESP32-S3 product hardware
Branch base: `feat/v2-ui-refinement` at `7b6b869` plus the candidate changes committed with this receipt
Command: `./scripts/esp32 vector-v2-gate-harness /dev/cu.usbmodem1101`

## User-visible failure

Drawing circles beneath the minimap or zoom rail produced angular ink in both the live preview and exported PNG. The manual product capture had zero touch errors/overflows and exact authority match, but an overlay-crossing sequence reached 38.5 ms presentation latency and coalesced roughly ten touch moves per accepted ink sample. The sparse accepted path therefore persisted into vector authority and export.

## Root cause

Every partial presentation intersecting a fixed canvas overlay redrew all overlays, including the minimap's 80×98 overview resample, then recomposed every underlying overlay region. This blocked the input consumer long enough to coalesce path-defining touch moves.

## Deterministic fingerless gate

`TINYDRAW_GATE1_LIVE_OVERLAY` drives identical 48-point circles through the real ribbon renderer and presenter: one on clear canvas and one crossing the minimap. It requires no touch input and gates overlay-specific chrome work and presentation latency.

```
TINYDRAW_GATE1_LIVE_OVERLAY clear_updates=47 clear_wall_max_us=2940 clear_submit_max_us=2162 clear_complete_max_us=2210 overlay_updates=26 overlay_wall_max_us=2928 overlay_submit_max_us=2335 overlay_complete_max_us=2375 overlay_chrome_max_us=0 clear_failures=0 overlay_failures=0 pass=1
```

The overlay circle is no slower than the clear circle, performs zero overlay redraw work, submits within 2.335 ms, and has no presentation failures.

## Related partial-fill improvement

Clipping partial canvas updates around fixed overlays also removed the same UI tax from progressive cold fill. Before clipping, the four overlap cold gates spent 173–199 ms presenting and reached 22.8–24.9 ms worst producer ticks. After clipping:

```
overlap 50%:  present_us=83451 max_tick_us=9928 pass=1
overlap 100%: present_us=80161 max_tick_us=10016 pass=1
overlap 200%: present_us=78789 max_tick_us=9553 pass=1
overlap 400%: present_us=78554 max_tick_us=9601 pass=1
```

## Explicit residual

This is not claimed as a full green harness receipt. The existing 100% pan gate remains red after the new UI overlays because pan intentionally redraws the changing minimap viewport on each frame:

```
TINYDRAW_GATE1_PAN zoom=100 ... chrome_us=7829 event_submit_us=40091 event_complete_us=40895 ... pass=0
```

The live-ink gate is independently green. Pan-overlay optimization remains separate work and must not be hidden by weakening downstream gate dependencies.
