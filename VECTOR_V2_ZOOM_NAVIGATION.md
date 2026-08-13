# Vector V2 zoom and navigation behavior

Status: accepted design for implementation  
Last updated: 2026-08-13

## Scope

This document defines camera, zoom, pan, cache-protection, and navigation-overlay
behavior for Vector V2. It does not define settled anti-aliasing, persistence, or
the final minimap interaction implementation.

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
4. the current world-space focus point.

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
3. If zoom B has a remembered origin whose viewport still contains the
   world-space focus, restore that origin.
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

The raw materialized-tile pool remains 320 slots initially.

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
uniform tiles, leaving 96 of the 320 raw slots for the active route and churn.
Actual paper-aware demand is lower in sparse documents.

After this policy is measured, a 384-slot experiment may be considered. A
448-slot default is not approved under the current export-reserve margin.

## Zoom and cache acceptance tests

### Host tests

- all adjacent zoom transitions preserve the chosen world focus before clamping;
- round trips 25→50→25, 25→100→25, 100→400→100, and 400→25→400;
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
- record p50/p95/max touch-poll and presentation timings.

### Glass test

1. Draw an unmistakable mark away from the world origin at 400%.
2. Zoom out to 25% and back through 50%, 100%, 200%, and 400%.
3. Confirm each transition stays spatially understandable.
4. Confirm returning to 400% finds the mark without a manual search.
5. Pan aggressively during visible refinement; motion must continue to track.
6. Visit every world edge and verify directional markers.
7. Confirm protected warm views do not visibly replay after the tour.

## Minimap stretch goal

The minimap belongs in the bottom-right canvas corner, immediately above the
bottom toolbar and below the zoom rail. It reuses the complete overview and
shows the current viewport rectangle.

Future interactions:

- tap to jump;
- drag the viewport rectangle to navigate continuously;
- tap the zoom percentage to toggle visibility.

The minimap is not required for the initial navigation batch or V2 feature
completeness unless promoted later. It must never write document pixels or
starve touch input.

## Non-goals

- arbitrary continuous zoom;
- rotation;
- infinite canvas;
- camera-aligned tile identities;
- zoom-specific document authority;
- permanent cache pinning that can refuse active publication;
- 800% in the first implementation.
