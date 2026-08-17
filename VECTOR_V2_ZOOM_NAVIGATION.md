# Vector V2 zoom and navigation behavior

Status: zoom, pan, and required minimap interaction implemented
Last updated: 2026-08-17

## Scope

This document defines camera, zoom, pan, cache-protection, and navigation-overlay
behavior for Vector V2. It does not define settled anti-aliasing or persistence.

## Geometry

- World: 1472×1792 world units.
- Display: 368×448 pixels.
- Drawing area: display area above the bottom toolbar.
- Zoom levels: 25%, 50%, 100%, 200%, and 400%.
- 800% remains a stretch goal and is not part of this contract.
- Camera coordinates are level-space pixels at the active zoom.
- The camera is always clamped so the viewport cannot expose space outside the
  bounded world.

At 25%, the complete world fits exactly on the display. The camera origin is
therefore `(0,0)` and panning is disabled.

## Zoom controls

Vector V2 uses a vertical navigation rail on the right side of the drawing area:

```text
┌─────┐
│  +  │
├─────┤
│100% │
├─────┤
│  −  │
└─────┘
```

- Plus selects the next committed zoom.
- Minus selects the previous committed zoom.
- Minus is disabled at 25%.
- Plus is disabled at 400%.
- The percentage is always visible when the navigation rail is visible.
- Tapping the percentage may later toggle the minimap; that behavior is not
  required for the first implementation.
- Controls are display overlays and never become document pixels.

## Camera state

The navigation model stores:

1. the current zoom;
2. the current level-space origin;
3. the last clamped origin used at each tiled zoom;
4. the world-space focus associated with each remembered origin;
5. the current world-space focus point.

Per-zoom origins improve return navigation. World-space focus preserves spatial
continuity when entering a zoom that has no useful remembered view.

Camera state is application state, not document authority. It is reset by New
for the first implementation. Persistence across restart may be added later as
user-preference/session state.

## Zoom transition algorithm

Every zoom action has a focus point.

### Default focus

The default focus is the center of the visible drawing area, excluding the
bottom toolbar. This is the first implementation used by the plus/minus buttons.

A later gesture or minimap action may supply an explicit panel-space focus.

### Transition

When changing from zoom A to zoom B:

1. Convert the panel-space focus at zoom A to a world-space point.
2. Save zoom A's current clamped origin.
3. If zoom B has a remembered origin whose associated focus matches the
   retained focus within four quarter-world units and whose viewport still
   contains that focus, restore that origin. The tolerance covers the bounded
   integer-conversion drift of a complete button cycle without accepting a
   stale view after the user pans elsewhere.
4. Otherwise calculate zoom B's origin so the same world-space point remains at
   the same panel-space focus.
5. Clamp the result to zoom B's world extent.
6. Make zoom B and the new origin current.
7. Present valid cached materialization immediately and refine missing detail
   cooperatively.

This hybrid rule gives both desirable behaviors:

- zooming in/out stays centered on the same part of the drawing;
- returning to a recently explored zoom restores its useful local position when
  doing so does not cause a surprising jump away from the current focus.

Implemented 2026-08-17. The physical 400→25→50→100→200→400 classifier returns
exactly to the explored `(2300,3100)` origin; host coverage also rejects stale
remembered views. See
[`RECEIPT.md`](benchmark-results/zoom-cycle-return-2026-08-17/RECEIPT.md).

### 25% transitions

Entering 25% always shows the complete world at `(0,0)`, but the current
world-space focus is retained.

Leaving 25% uses that retained focus. If the user has not established a focus
through a prior tiled view, use the world center.

The current test-app behavior that always enters tiled zooms at `(0,0)` is not
product behavior and must be removed.

## Pan behavior

- Pan is active only when the Pan tool is selected.
- One finger drag moves the paper with the finger.
- Pan coordinates update from the latest touch sample; stale intermediate camera
  positions may be coalesced when display work is in flight.
- Touch sampling must not wait for cold tile production.
- Obsolete refinement work is canceled when the requested view changes.
- The camera clamps continuously at world edges.
- At 25%, pan input has no effect.

### Responsiveness contract

Correctness takes priority over showing newly refined detail. During motion:

1. accept and track the latest camera position;
2. present the best current source, including overview fallback where needed;
3. refine only the latest requested viewport when input permits.

Cold rendering must never make a pan gesture stop being recognized. The first
implementation must measure touch-poll gap, event-to-submit, event-to-first
physical completion, coalesced camera updates, and stale producer cancellation.

## Canvas-extent indicators

Four small directional markers communicate that more canvas exists:

- top when the camera can move upward;
- left when the camera can move left;
- right when the camera can move right;
- bottom when the camera can move downward.

Rules:

- no markers at 25%;
- a marker disappears exactly at its corresponding clamped edge;
- markers are display overlays, not document pixels;
- the right marker sits immediately left of the zoom rail;
- markers must remain legible over every palette color without becoming large
  interaction targets;
- indicators do not consume or block pan gestures.

## Cache-protection behavior

The raw materialized-tile pool started at 320 slots and currently uses 448.
The current pool and idle-repair policy pass mixed-drawing revisit gates; the
remaining overlap-50 cold-render red is tracked separately in
[`PROJECT_STATE.md`](PROJECT_STATE.md#finish-line-scorecard).

Each tiled zoom may protect the raw identities intersecting its most recently
committed viewport footprint. Protection means eviction preference, not an
absolute pin:

1. evict unprotected distant identities first;
2. preserve the active viewport above every inactive viewport;
3. preserve one recent footprint per inactive zoom when capacity permits;
4. never allow policy protection to prevent publication of the active view;
5. learned uniform identities remain in the compact catalog and do not consume
   raw slots;
6. a revision mutation invalidates affected protected identities normally;
7. cache policy never changes document or pixel correctness.

The implementation should use a small explicit protection generation/tier rather
than permanent display pins. Display pins retain their existing short lifetime
and correctness purpose.

### Capacity expectation

A worst-case arbitrary-alignment viewport intersects at most 56 tiles. Four
tiled zoom footprints require at most 224 raw identities before accounting for
uniform tiles, leaving 224 of the current 448 raw slots for the active route and
churn. Actual paper-aware demand is lower in sparse documents.

A mutation-free 16-stop 400% tour measured 63 return-trip refills at 320 slots
and none at 384. The later 448-slot default, committed-overlay split, and idle
repair close the mixed-drawing revisit gate while preserving the export reserve;
current measurements live in [`PROJECT_STATE.md`](PROJECT_STATE.md).

## Zoom and cache acceptance tests

### Host tests

- all adjacent zoom transitions preserve the chosen world focus before clamping;
- round trips 25→50→25, 25→100→25, 100→400→100, and 400→25→400;
- the complete 400→25→50→100→200→400 button cycle restores the exact explored
  400% origin;
- remembered origins restore only when compatible with current focus;
- every corner and edge clamps correctly at every zoom;
- extent indicators match pan availability;
- 25% always uses `(0,0)` and exposes no pan/extent affordance;
- protection order prefers unprotected identities and cannot refuse active-view
  publication;
- revision invalidation overrides protection safely.

### Hardware automation

- pan while a cold 400% fill is active without losing touch input;
- change direction repeatedly and prove obsolete refinement cancellation;
- warm 100% and 400% views, tour at 400%, then return with zero fallback in the
  protected footprints;
- verify the 1.5 MiB export reserve still allocates;
- record p50/p95/max touch-poll and presentation timings;
- classify the complete button cycle on device and require exact origin return.

### Glass test

1. Draw an unmistakable mark away from the world origin at 400%.
2. Zoom out to 25% and back through 50%, 100%, 200%, and 400%.
3. Confirm each transition stays spatially understandable.
4. Confirm returning to 400% finds the mark without a manual search.
5. Pan aggressively during visible refinement; motion must continue to track.
6. Visit every world edge and verify directional markers.
7. Confirm protected warm views do not visibly replay after the tour.

## Minimap navigation

The minimap sits in the bottom-right canvas corner, immediately above the
bottom toolbar and below the zoom rail. It reuses the complete overview and
shows the current viewport rectangle.

Implemented behavior:

- its visible frame owns touch for every selected tool, so an interactive
  overlay cannot leak pen/eraser authority beneath itself;
- a stationary tap centers the drawing-area focus on the selected overview
  point and performs one complete overlay-safe refresh;
- movement below 4 px is treated as touch jitter and remains a tap;
- at 4 px, the gesture becomes a continuous viewport drag through the ordinary
  boundary-drained pan path;
- drag projection is relative to the gesture's starting origin, remains
  captured outside the frame, and clamps at every world edge;
- popup, confirmation, and export-progress states hide and disable the minimap;
- at 25%, where the full world already fits, tap and drag are successful no-ops.

Tapping the zoom percentage to toggle visibility remains optional. The owner
promoted viewport dragging into feature-complete scope on 2026-08-17; host and
physical presenter gates landed the same day. The physical tap/drag sample
completed in 20.164/16.529 ms with ring reuse and exact target origins. See
[`RECEIPT.md`](benchmark-results/minimap-navigation-2026-08-17/RECEIPT.md).

## Non-goals

- arbitrary continuous zoom;
- rotation;
- infinite canvas;
- camera-aligned tile identities;
- zoom-specific document authority;
- permanent cache pinning that can refuse active publication;
- 800% in the first implementation.
