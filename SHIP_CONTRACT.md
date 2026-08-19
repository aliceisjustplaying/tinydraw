# TinyDraw ship contract

Updated: 2026-08-19. This is the product contract for the first Vector V2
release. Raster V1 and Vector V2 are both supported ESP32 product generations.
The complete pre-release contract and its dated author decisions are preserved
in
[`SHIP_CONTRACT_PRE_RELEASE_2026-08-19.md`](docs/archive/2026-08-vector-v2-performance/SHIP_CONTRACT_PRE_RELEASE_2026-08-19.md).

## Product tenets

1. The newest visible touch truth comes first. Durable authority and derived
   pixels may catch up through bounded background work.
2. Glass-visible claims need glass evidence. Software timings explain a result;
   they do not establish tearing, latency, or visual quality by themselves.
3. Vector operations are V2 document authority. Overviews, tiles, chrome,
   previews, settled pixels, and export buffers are derived or transient.
4. Pay work at the lifetime of the change. A touch updates the live tail, a
   camera move updates exposed canvas, a gesture updates authority and damage,
   and idle time funds settled quality.
5. Keep interaction work bounded and interruptible. Input and pending display
   work take priority over replay, repair, autosave, and export.
6. Preserve painter-order exactness across pen, eraser, Undo/Redo, recovery,
   cold replay, PNG, and SVG.
7. Keep the machine small. Use fixed-capacity state, measured caches, and narrow
   platform adapters. Keep V1 and V2 independently buildable.
8. Close a requirement with an oracle, a guard band where useful, and a known
   good revision. Shared-path changes reopen their dependent checks.

## Release requirements

### Drawing and navigation

- Finger drawing must not lose Down or Up events. The immediate stroke follows
  the raw clipped touch, and the committed stroke converges to shared ribbon
  authority after lift.
- Pen and eraser use whole-gesture authority. Intentional motionless taps are
  valid dots; contacts that begin or leave through the non-canvas top fringe
  must not create authority.
- Whole-gesture Undo and Redo retain at least ten levels and preserve exact
  painter order. A local history move invalidates only its damage region.
- Pan is tear-free on glass and sustains at least 24 FPS. The measured product
  cadence is about 29.4 FPS.
- Zoom supports 25, 50, 100, 200, and 400 percent. Transitions preserve one
  world-space focus. Minimap taps and drags navigate the complete world.
- Popups exclusively capture contacts while open. A popup tap cannot activate a
  toolbar control underneath it.
- The lower hardware button provides the existing four-second power-off flow.

### Rendering and quality

- The visible viewport must always have a complete source. Missing detail tiles
  fall back to the complete overview and refine without checkerboards.
- A cold viewport completes within 500 ms for the accepted release corpora.
  The final battery records every zoom and includes the captured drawing.
- A previously rendered view returns sharp without a visible cold-to-sharp
  cycle while its materialization remains within cache capacity.
- Live strokes may be hard-edged. Idle settlement applies the accepted analytic
  antialiasing and caches the settled result. Further AA speed and extreme
  pixel-edge polish are post-release work.

### Documents and recovery

- A V2 document is a blank baseline plus ordered vector operations, active and
  retained prefixes, generation, and epoch. Navigation, chrome, and derived
  pixels restart from defaults.
- Journal commits publish a CRC-checked final marker last. Recovery accepts only
  complete transactions and keeps the last valid commit after truncation or
  corruption.
- Autosave is asynchronous and exercised by the device battery. The current
  contract does not promise a time-based power-loss window; timed destructive
  power-loss characterization is post-release work.
- The 4 MiB journal reports capacity failure without overwriting the last valid
  recovery point. Two-arena compaction is post-release.
- New starts from the production blank-document path. Diagnostic firmware may
  load its embedded fixture; product firmware may not.
- Raster V1 files remain Raster V1 files and accessible through the V1 build.
  They are never silently reinterpreted as V2 vector documents.

### Export and platform behavior

- One authority snapshot produces both `DRAWING.PNG` and `DRAWING.SVG`.
- PNG uses the production settled renderer. SVG uses shared variable-width
  ribbon geometry, one painter-ordered path per physical gesture, exact span
  boundaries, no synthetic background, and transparent eraser masks.
- Export is read-only USB mass storage. **EJECT & EXIT** and host eject both
  tear down TinyUSB, return to drawing, and restore USB Serial/JTAG without a
  reset.
- Export progress says **EXPORTING**. Every export message is complete, centered,
  and contained by its visible target.
- Document → Clock performs the accepted one-shot NTP correction and writes the
  onboard RTC. Network failure returns a visible terminal result.
- Product firmware uses the 604-slot tile pool and the fixed 16 MiB partition
  map: 1.75 MiB app, 4 MiB journal, 10.125 MiB export, and 64 KiB coredump.

## Release evidence

The current scorecard and final-RC status are in
[`PROJECT_STATE.md`](PROJECT_STATE.md). Hardware limits live in
[`CO5300_PANEL_LIMITS_2026-08-15.md`](docs/receipts/hardware/CO5300_PANEL_LIMITS_2026-08-15.md).
The full optimization narrative, including rejected experiments, is in
[`PERFORMANCE_CHRONICLE.md`](docs/PERFORMANCE_CHRONICLE.md) and the dated
receipts. Deferred work is in [`POST_RELEASE.md`](docs/POST_RELEASE.md).

The release candidate must pass host Debug, host Release, ASan/UBSan, formatting,
the 604-slot physical battery, a normal product boot, and glass checks on
the same source revision. The exact product binary is flashed again after the
battery before tagging.
