# Absolute minimap pointer + dock arbitration receipt

Date: 2026-08-17  
Branch: `feat/v2-performance-followup`

## Verdict

**Green for the release scope; author accepted.** The author’s final glass
verdict on `dbc4f67` was:

> this will do let's consider this done. maybe put a pin in it for further
> refinements but this is good enough now

Optional cosmetic or interaction refinement is deferred to the later general
touch-target review; it is not a blocker for authority/storage and Undo/Redo.

## Problem

Glass testing rejected the relative high-zoom mapping: the 0.25 drag
scale exhausted physical finger travel, and touches near a bottom-corner
viewport frequently became size/document dock taps. Moving the whole minimap
up 16 px improved bottom acquisition but was aesthetically rejected and still
left bottom-left presses colliding with the size picker.

The structural conflict is that a physical finger centered on the minimap’s
bottom edge can report inside the dock. Rectangle expansion alone cannot make
the same stationary Down/Up coordinates both an immediate minimap jump and a
truthful dock-button tap. Movement is the available intent signal.

## Final contract

1. The visible minimap remains at its original, preferred position.
2. Every direct minimap Down immediately maps the pointer’s absolute minimap
   position to a centered viewport origin. Dragging continues that absolute
   mapping; there is no viewport-box grab requirement or zoom-dependent delta
   scale.
3. A Down in the right-side dock below the minimap is an ambiguous candidate:
   - stationary release remains the existing size/document action;
   - 8 px horizontal/downward movement promotes to captured minimap control;
   - 2 px upward movement toward the visible map promotes early.
4. The candidate spans the complete dock height `(250,372)..(368,448)`, based
   on captured captured touches at `y=435..445` rather than an assumed finger
   bound.
5. Once promoted, the gesture stays captured outside the minimap and dock.

Relevant commits:

- `c0d3ec1` — absolute pointer mapping and immediate direct acquisition.
- `278f1cb` — post-coordinator capture without touch-path serial output.
- `01ae7a0` — compact duplicate Move samples.
- `62d8c8e` — dock/minimap intent arbitration; original map position restored.
- `3980b90` — 2 px upward promotion based on measured directional stickiness.
- `428b14a` — rejected coordinated vertical-stack layout experiment.
- `4c8d144` — restores the uncluttered, preferred layout.
- `dbc4f67` — extends drag-only candidate arbitration through the full dock.

## Deterministic gates

The full-dock regression was written red first. At `y=442`, the document
button remained the truthful stationary action, but candidate and upward-drag
promotion assertions failed. After `dbc4f67`, the focused result was:

```text
test cases:  1 |  1 passed | 0 failed | 243 skipped
assertions: 14 | 14 passed | 0 failed
```

The full host battery then passed:

```text
test cases:   244 |   244 passed | 0 failed
assertions: 92444 | 92444 passed | 0 failed
```

The physical classifier for absolute endpoints remains green:

```text
TINYDRAW_GATE1_MINIMAP_NAV hit=1 mode=absolute ...
bottom_right_to_center_x=2760 bottom_right_to_center_y=3398 ...
edge_x=0 edge_y=0 ... pass=1
```

Source: [`gate.log`](gate.log). Its only verdict red was the separately known,
requested overlap-50 cold workload (`604187 us > 500000 us`); every other
automated verdict bit was 1.

The repository-wide formatting gate is not claimed green: it still reports
pre-existing formatting violations in capture-related files, including
`esp32/main/vector_v2/vector_v2_app.cpp` and
`esp32/main/vector_v2/vector_v2_minimap_trace_capture.cpp`.

## Capture design

Capture mechanics:

- one compact record per Down/Up or coordinate-changing product-consumed Move;
- camera and routing flags recorded before and after the product coordinator;
- no serial output until eight hands-off seconds;
- PSRAM record storage allocated after every product workspace;
- no touch-path filesystem or serial I/O.

The capture flag bits are pressed `0x01`, minimap `0x02`, pan `0x04`, toolbar
`0x08`, ink `0x10`, and direct minimap hit `0x20`.

## First author capture and directional treatment

Artifact: [`author-capture-arbitrated.log`](author-capture-arbitrated.log)

```text
events=866 offers=4916 duplicate_moves=4050 overflow=0
append_total_us=50248 append_max_us=78
```

The capture contained 22 complete gestures: one ink Stroke and 21 minimap
gestures. All 14 gestures beginning in the dock-overlap candidate zone
promoted to minimap control. Candidate Down coordinates reached `y=418`,
proving that the conflict extends well into the dock.

A stationary direct minimap tap at `(332,342)` changed the origin on Down from
`(2098,3618)` to `(4232,5958)` and retained it on Up (artifact lines 857–858).
The author called this version “way better than anything we had before.”

Across those 14 candidates, the original 8 px threshold delayed contact capture by
118.4815 ms median / 225.993 ms max. Replaying the same events with a 2 px
upward-only threshold predicted 78.9945 ms median while preserving 8 px for
horizontal/downward dock ambiguity. That directional rule landed in `3980b90`.

Artifact [`author-capture-directional.log`](author-capture-directional.log)
recorded 726 events from 5,352 offers, 4,626 duplicate Moves, zero overflow,
71,852 us total append bookkeeping, and 90 us maximum. It also exposed four
stationary lower touches at `y=435..443`, below the then-current candidate end
at `y=424` (artifact lines 1055–1061). Those coordinates drove the full-dock
regression rather than another visual-layout guess.

## Final author capture

Artifact: [`author-capture-full-dock.log`](author-capture-full-dock.log)

```text
events=778 offers=4074 duplicate_moves=3296 overflow=0
append_total_us=63596 append_max_us=87
```

The driver reached `TINYDRAW_MINIMAP_CAPTURE_END`, reported
`failure_marker=False`, and the artifact contains no watchdog, Guru Meditation,
system-WDT reset, stack-overflow, or capture-overflow marker.

The 778 records form 32 complete gestures. Six deliberate gestures beginning in
the dock-overlap zone promoted and changed the camera. Examples:

- Down `(288,407)` promoted at `(295,404)` and moved the origin to
  `(5262,6720)` (artifact lines 540–547).
- Down `(359,393)` promoted after 3 px upward movement and reached the top
  world edge (artifact lines 701–707).
- Down `(359,412)` promoted after 2 px upward movement (artifact lines
  861–864).
- Down `(304,408)` promoted and traveled through the minimap to the top edge
  (artifact lines 1032–1047).

Captured minimap drags remained owned while the finger crossed the complete
lower dock, including `y=440` (artifact lines 839–860) and `y=441` (artifact
lines 995–1001). Four stationary taps at `y=443..445` did not promote and did
not change the origin (artifact lines 1002–1009), preserving dock intent.

## Deferred refinement pin

The accepted design has an unavoidable semantic boundary: a stationary miss
whose reported coordinate lands on a dock button remains a dock tap. A future
touch-target review may revisit visual placement, pressed feedback, or a
separate explicit minimap affordance. The release implementation should not be
reopened with unmeasured rectangle or geometry changes.
