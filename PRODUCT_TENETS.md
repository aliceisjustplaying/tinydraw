# TinyDraw product tenets

Restored: 2026-08-16. These are the stable design priorities behind the
numeric requirements in [`SHIP_CONTRACT.md`](SHIP_CONTRACT.md). The ship
contract wins if the two documents ever conflict.

1. **The newest visible touch truth comes first.** Drawing must follow the
   finger; durable authority and derived materialization may catch up in
   bounded work after the visual update is submitted.
2. **Glass-visible claims require glass evidence.** Software timings explain
   behavior but cannot declare tearing, latency, or visual quality closed.
3. **Vector operations are V2 document authority.** Overviews, tiles, chrome,
   previews, settled output, and export buffers are derived or transient.
4. **Pay work at the lifetime of what changed.** A touch sample updates the
   volatile tail, a camera move updates exposed canvas and viewport lines, a
   gesture updates authority and damage, and idle time funds settled quality.
5. **Keep interaction work bounded and interruptible.** Input and pending
   visual presentation take priority over cold replay, repair, autosave, and
   export work.
6. **Preserve exactness.** Pen/eraser painter order, Undo/Redo, persistence,
   SVG, PNG, and cold replay must agree with the same authoritative geometry.
7. **Keep the machine small.** Use explicit fixed-capacity state, measured
   caches, and narrow adapters; retain Raster V1 as the fallback until V2 earns
   promotion.
8. **A requirement closes with an oracle, guard band, and known-good revision.**
   A shared-path change mechanically reopens every dependent gate.
