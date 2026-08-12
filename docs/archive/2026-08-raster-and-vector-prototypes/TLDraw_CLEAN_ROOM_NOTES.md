# tldraw research: clean-room constraints and transferable ideas

## Scope

This document records concept-level research only. TinyDraw does not incorporate
tldraw source code, translated source, assets, tests, documentation prose, UI
artwork, branding, or expressive module structure. Any implementation in this
repository must be independently designed for ESP32 constraints.

This is general engineering and licensing information, not legal advice.
Commercial release should receive fact-specific legal review, including patent,
trademark, contract, and non-US-law questions.

## License conclusion

The current `tldraw/tldraw` SDK is source-available under the custom tldraw
license, not conventional open source. Its default grant covers development,
testing, and private staging; production use requires an applicable tldraw
license/key or separate written agreement. Therefore TinyDraw must not reuse or
translate current tldraw implementation code without separate license review.

High-level ideas, processes, and methods can generally be independently
implemented under the US copyright idea/expression distinction, but this does
not create a blanket legal safe harbor. To reduce substantial-similarity risk,
TinyDraw should use official behavior/architecture documentation only for
concept extraction and preserve independent design notes and tests.

Branding is separate: do not use tldraw logos, product names, visual identity,
or imply affiliation or endorsement.

## Primary references

- Current license: https://github.com/tldraw/tldraw/blob/main/LICENSE.md
- Official license guide: https://tldraw.dev/community/license
- Current-model announcement: https://tldraw.dev/blog/tldraw-sdk-4-0
- Historical license change: https://tldraw.dev/blog/license-update-for-the-tldraw-sdk
- Archived v1 MIT license: https://github.com/tldraw/tldraw-v1/blob/main/LICENSE.md
- Trademark policy: https://github.com/tldraw/tldraw/blob/main/TRADEMARKS.md
- US copyright idea/expression rule: https://www.copyright.gov/title17/92chap1.html
- Copyright Office computer-program guidance:
  https://www.copyright.gov/register/tx-programs.html

## Useful concepts, independently adapted

These ideas reinforce decisions already reached from TinyDraw's own hardware
measurements and external review:

1. **Stable document records, separate session state.** Keep vector operations
   and stable IDs separate from camera, tool, selection, and transient stroke
   state. Publish one changed-ID/bounds set per committed gesture.
2. **Disposable derived geometry.** Cache bounds, simplified centerlines, and
   settled geometry by document revision and zoom bucket. Canonical vectors stay
   authoritative.
3. **Incremental spatial indexing and viewport culling.** TinyDraw already uses
   a conservative fixed-memory macrogrid. Production should update only changed
   entries and z-order only visible candidates.
4. **Camera-independent document geometry.** Camera changes select a zoom/LOD
   bucket; they do not rewrite vector geometry.
5. **Stable zoom LOD.** Keep canonical samples and use independently generated
   simplified centerlines at lower detail. Add hysteresis so zoom motion does not
   thrash geometry.
6. **Incremental dirty-region rendering.** Append operations update intersected
   resident overview/tile regions in document order; old-operation edits replay
   affected regions from checkpoints.
7. **Gesture-level reversible history.** Store compact inverse commands or
   diffs, not complete raster/document snapshots per pointer update.
8. **Snapshot plus journal persistence.** Use versioned binary snapshots and a
   checksummed append-only operation journal, with atomic rotation and explicit
   wear testing.

## Ideas rejected for this device

Do not transplant browser-specific implementation choices:

- React/DOM/SVG retained rendering;
- browser layout or CSS effects;
- generic reactive dependency graphs;
- JSON/base64-heavy in-memory persistence;
- IndexedDB, cross-tab state, WebSocket collaboration, or server history;
- unbounded multi-resolution caches;
- GPU assumptions, blur/shadow effects, or per-frame pressure-outline rebuilds.

## Relation to current TinyDraw work

No implementation in the current settled-renderer iteration was copied from
or based on tldraw source. The sample simplifier and capsule renderer were
independently designed before this research, based on TinyDraw's measured
ESP32 bottlenecks and the existing architecture review. The tldraw research
corroborates the broader direction—derived stroke LOD, stable camera state,
spatial culling, incremental records, and journaled history—but is not their
implementation source.
