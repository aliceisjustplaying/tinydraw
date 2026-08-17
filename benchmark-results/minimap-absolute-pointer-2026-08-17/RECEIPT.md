# Absolute minimap pointer + dock arbitration receipt

Date: 2026-08-17  
Branch: `feat/v2-performance-followup`

## Problem

Owner glass testing rejected the relative high-zoom mapping: the 0.25 drag
scale exhausted physical finger travel, and touches near a bottom-corner
viewport frequently became size/document dock taps. Moving the whole minimap
up 16 px improved bottom acquisition but was aesthetically rejected and still
left bottom-left presses colliding with the size picker.

The structural conflict is that a physical finger centered on the minimap's
bottom edge can report inside the dock. Rectangle expansion alone cannot make
the same Down coordinate an immediate minimap press and a truthful stationary
button tap.

## Implemented contract

1. The visible minimap is restored to its original position.
2. Every direct minimap Down immediately maps the pointer's absolute minimap
   position to a centered viewport origin. Dragging continues that absolute
   mapping; there is no viewport-box grab requirement and no zoom-dependent
   delta scale.
3. A Down in `(250,372)..(368,424)` is an ambiguous dock candidate:
   - stationary release remains the existing size/document action;
   - 8 px horizontal/downward movement promotes to captured minimap control;
   - after the first owner capture, 2 px upward movement toward the visible map
     promotes early to remove measured stickiness.
4. Once promoted, the gesture stays captured outside the minimap and dock.

Commits:

- `c0d3ec1` — absolute pointer mapping and immediate direct acquisition.
- `278f1cb` — post-coordinator capture without touch-path serial output.
- `01ae7a0` — compact duplicate Move samples.
- `62d8c8e` — dock/minimap intent arbitration; original map position restored.
- directional upward-threshold follow-up: pending commit at receipt write time.

## Deterministic gates

Focused tests:

```text
ambiguous minimap dock presses preserve taps and promote deliberate drags
10 assertions passed

minimap pointer absolutely centers the viewport at every zoom
3 assertions passed
```

Full host battery:

```text
test cases:   244 |   244 passed | 0 failed
assertions: 92440 | 92440 passed | 0 failed
```

The pre-arbitration physical gate proved the absolute endpoints:

```text
TINYDRAW_GATE1_MINIMAP_NAV hit=1 mode=absolute ...
bottom_right_to_center_x=2760 bottom_right_to_center_y=3398 ...
edge_x=0 edge_y=0 ... pass=1
```

Source: [`gate.log`](gate.log). Its only verdict red was the separately known
and owner-scheduled overlap-50 cold workload (`604187 us > 500000 us`); every
other automated verdict bit was 1.

## Exact owner capture

Artifact: [`owner-capture-arbitrated.log`](owner-capture-arbitrated.log)

Capture mechanics:

- one compact record per Down/Up or coordinate-changing product-consumed Move;
- camera and routing flags recorded before and after the coordinator;
- no serial output until eight hands-off seconds;
- PSRAM record storage allocated after every product workspace.

Capture result:

```text
events=866 offers=4916 duplicate_moves=4050 overflow=0
append_total_us=50248 append_max_us=78
```

This is 10.22 us average capture bookkeeping per offered event including timer
measurement. There was no capture overflow, watchdog, crash, or serial output
during interaction.

The capture contains 22 complete gestures: one ink Stroke and 21 minimap
gestures, including one stationary direct tap. All 14 gestures beginning in
the dock-overlap candidate zone promoted to minimap control. Candidate Down
coordinates reached `y=418`, validating that the conflict extends well into
the dock rather than ending at the rendered frame.

A stationary direct minimap tap at `(332,342)` changed the origin on Down from
`(2098,3618)` to `(4232,5958)` and retained it on Up (log lines 857–858).

## Owner verdict and sticky-up treatment

Owner verdict on `62d8c8e`:

> This is way better than anything we had before.

The remaining upward stickiness had a measured cause. Across the 14 candidate
gestures, the 8 px promotion threshold delayed ownership by:

```text
median=118.4815 ms
max=225.993 ms
```

Replaying those exact events with a directional rule—2 px upward toward the
visible map, 8 px otherwise—predicts:

```text
median=78.9945 ms
max=225.993 ms
```

The unchanged maximum is a gesture that initially moved right/down, so the
rule correctly preserves the dock ambiguity rather than classifying it as an
upward return. Physical owner acceptance of the directional follow-up remains
pending.

## Verdict

**Provisional yellow.** Absolute mapping and dock arbitration are validated by
host, hardware classifier, exact capture, and a strong owner improvement
verdict. Final green waits for one short glass check of upward acquisition at
100% and 400% with ordinary size/document taps preserved.
