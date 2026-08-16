# TinyDraw Vector V2 — correctness review

**Date:** 2026-08-16  
**Reviewer:** Grok 4.6 (read-only; six parallel source traces plus an independent pass)  
**Repo:** `$HOME/src/tries/2026-08-09-espdraw`  
**Branch:** `feat/v2-performance-followup`  
**Snapshot SHA:** `e76b98eb3084533912b18e9307a4f375a76a77e4`  
**Snapshot subject:** `docs: hand over the oracle session and point the queue at Cold Stage B`

This is a correctness review of the whole first-party tree (`core/`, `vector_v2/`, `esp32/main/`, `host/`, `tests/`) with extra weight on the last ~24 hours (panel compositor, visual-first ink, curved committed path, cold masked painters, ink-trace capture/replay, déjà-vu ledger, SVG, release-safety fixes).

It is **not** a performance review. Cold Stage B is in flight in parallel. While this review was running the working tree picked up uncommitted strided-publish edits (`tile_producer`, `materialized_canvas`, `AppStorage` dropping `producer_packed`). Line numbers below are from `e76b98e` unless a finding is about that in-flight work.

No source was modified by this review. Host tests were not re-run here; claims about test coverage come from reading the tests.

---

## Verdict

The architecture is still the project's strongest asset: explicit authority vs derived caches, fail-closed capacity on the 02790e5 holes, newest-first masked replay with `covers_pixel` as geometry authority, canvas-pure ring + compositor chrome, and serialized incremental commits. The last day did not blow that up.

The live correctness debt is concentrated in **seams between two geometries, two consumers of the same touch stream, and fail paths that do not roll back presentation state**. Several of those seams are new in the last 24 hours (visual-first ink, ink-trace replay, chrome cache split, déjà-vu ledger). A few are older product-contract gaps (SVG USB, undo) that the last day made more visible by landing the host SVG core without the device path.

Highest-priority product bugs: hardware zoom during an open stroke, zero-length pressure-change capsules painting the wrong radius, capture dump overflowing the live touch queue, and SVG/glass using different geometry.

---

## How to read this

Severity:

- **High** — wrong pixels, lost input, hang, or a ship-contract miss that a user can hit
- **Medium** — real defect with a narrower trigger, an oracle that will lie, or a fail path that leaves presentation/authority split
- **Low** — latent API, defensive hole, or missing feature that does not currently corrupt the happy path

Each item has a proposed fix. Fixes are surgical unless the cause is a representation split.

---

## High

### H1 — Hardware zoom during an open stroke or pan remaps world mid-gesture

- **File:** `esp32/main/vector_v2/vector_v2_app.cpp:974-983`
- **Also:** `esp32/main/vector_v2/vector_v2_presenter.cpp:144-154`, `400-408`, `424-454`
- **Status:** open at `e76b98e`

GPIO0 runs `presenter.set_zoom` every loop with no `pressed` / `ink.active()` / `panning` guard. `set_zoom` rematerializes the frame and clears the live overlay. The in-flight gesture keeps going.

On draw, `InkStream` / `CurvedRibbonStream` stay in screen space. Later `operation_point()` multiplies those screen points by the **new** `1/scale()` and origin. Chunks already in the log stay in the old world. The rest of the stroke jumps.

On pan, `pan_start_x/y` are the pre-zoom origin. The next `pan_from(pan_start_x, pan_start_y, …)` writes that old origin plus the screen delta and teleports the camera.

The mode button is the documented cycle-all-zooms control. A press during ink or pan is a normal hardware event.

**Proposed fix:**

```cpp
if (next_button_down && !button_down) {
  button_down = true;
} else if (!next_button_down && button_down) {
  button_down = false;
  if (pressed || panning || ink.active()) {
    // ignore, or finish/cancel first
  } else {
    // existing set_zoom
  }
}
```

If you want zoom to win, cancel the gesture first (`builder.cancel()`, `ink.end()`, `ribbon.reset()`, `panning = false`) then zoom. Do not mix the two.

---

### H2 — Zero-length tapered capsules paint only the first radius

- **File:** `vector_v2/src/incremental_rasterizer.cpp:93-104`, `149-160`
- **Also:** `vector_v2/src/operation_builder.cpp:128-132`
- **Contrast:** `core/src/settled_renderer.cpp:77-83` already does this correctly
- **Status:** open

`make_segment` sets `inverse_length_squared = 0` when endpoints coincide. `covers_pixel` then forces `amount = 0` and uses only `first.radius`.

`OperationBuilder::append_point` coalesces only when x, y, **and** radius match. A hold-in-place pressure change is two samples at the same quarter-pixel with different `radius_256`. One- and two-sample ops go straight to `paint_bounded_segment` / `paint_masked_segment`. Curved tails whose last two samples sit on one point emit a zero-length last chord (`curved_unit` at `:771-773`); the final cap is the midpoint radius, not `current.radius`.

The V1 settled renderer already documents the failure: "an increasing pressure sample at the end of a stationary stroke disappears." V2 reintroduced it in the product rasterizer.

**Proposed fix:** in `covers_pixel` (or `make_segment`), if `inverse_length_squared == 0`:

```cpp
if (segment.inverse_length_squared == 0.0F) {
  const float dx = pixel_x - segment.first.x;
  const float dy = pixel_y - segment.first.y;
  const float radius = std::max(segment.first.radius, segment.second.radius);
  return dx * dx + dy * dy <= radius * radius;
}
```

Add an oracle: same `x_quarter`/`y_quarter`, `radius_256` 256 vs 4096, masked and unmasked, all five zooms. Compare to a max-radius disk.

---

### H3 — Capture dump starves the production touch consumer

- **File:** `esp32/main/vector_v2/vector_v2_app.cpp:1218-1223`
- **Also:** `esp32/main/vector_v2/vector_v2_ink_trace_capture.cpp:92-126`
- **Status:** open

`dump_and_reset()` runs on core 0 in the product loop and prints the whole ring as CSV. The 1 kHz core-1 sampler keeps `offer()`ing into the 16-slot production buffer. Nothing calls `read_next()` during the dump. The 256-line yield fixed WDT interleave into the CSV. It did not drain or pause sampling.

16 slots fill in ~16 ms at 1 kHz. A 9k-event dump is far longer than that. A new Down during the dump overflows. `TouchEventBuffer` only evicts Moves, so a Down can be refused and `touching_` stays false (`touch_event_buffer.cpp:31-33,91-92`). Capture is also `enabled_ == false` for the whole dump so the next recording misses that Down.

**Proposed fix:** pause/stop the sampler for the dump, or `pop()` and discard production events on every yield. Re-enable capture only after the consumer is running again. Prefer a host-side pull of a binary ring over an in-loop printf of 9k lines.

---

### H4 — Ink-trace replay is not the product consumer

- **File:** `esp32/main/vector_v2/vector_v2_gate_harness.cpp:2913-2960`
- **Vs:** `esp32/main/vector_v2/vector_v2_app.cpp:998-1048`
- **Status:** open

Offer/coalesce is the production buffer. The consumer is a second ink-only state machine: every Down starts a stroke. Product routes chrome first (`chrome_contains` → toolbar / zoom rail / popup dismiss / pan).

`scribble-multistroke.csv` has a Down at `(355, 71)` on the zoom rail (`kZoomRailRect{304,72,360,226}` plus 8 px slop). Capture is upstream of chrome so the CSV keeps that Down. Live product treats it as a rail tap. Replay draws ink. `down_up_conserved` still passes. The gate can go green on a path the owner never inked.

**Proposed fix:** drive replay through the same chrome/toolbar/ink/pan branch as `vector_v2_app.cpp` (extract a shared `handle_touch_event`). Or strip chrome-hit events at record time and document that traces are pre-routing. Do not claim "production `offer()` path" for a different consumer.

---

### H5 — Replay gate hangs if the last Up is lost

- **File:** `esp32/main/vector_v2/vector_v2_gate_harness.cpp:2886-2893`
- **Status:** open

The loop only exits when `!pressed && replayer.exhausted()`. If a Down was consumed and the matching Up overflows or is never popped, `pressed` stays true after `done_ == true` and the task `vTaskDelay`s forever. No receipt line, no `pass=`.

**Proposed fix:**

```cpp
if (!sampled.has_value()) {
  if (replayer.exhausted()) {
    if (pressed) {
      // fail the trace: unclosed stroke
    }
    break;
  }
  vTaskDelay(1);
  continue;
}
```

---

### H6 — SVG is a different geometry than glass, and USB export is still PNG

- **File:** `vector_v2/src/svg_export.cpp:59-68,106-123`
- **Also:** `vector_v2/src/incremental_rasterizer.cpp:31,106-113,1019-1038`
- **Device path:** `esp32/main/vector_v2/vector_v2_export.cpp:70-104`
- **Contract:** `SHIP_CONTRACT.md` §6
- **Status:** open (new host SVG in the last 24h)

`export_svg` replays `CurvedRibbonStream` with raw `radius_256 / 256` and emits those circles/quads. Committed glass (and USB PNG via `WorldBandRenderer`) paints midpoint-quadratic **capsules** with `kMinimumScreenRadius = 0.75` screen pixels.

A legal authority sample `radius_256 = 1` exports as `r ≈ 0.0039` world units and paints on glass as `r = 0.75`. The SVG tests compare export to `RibbonRenderer` of the same primitives, never to `apply_incremental_operation`. Hairlines are in the frozen cold corpus.

Separately, chrome Export encodes a full-world PNG. `export_svg` is compiled and unit-tested. Nothing in the ESP32 export path calls it. Contract: "Delivered over the existing USB export flow" and "visually identical to glass." Neither clause is closed.

**Proposed fix:**

- Drive SVG from the same scaled samples / curve units the rasterizer uses, including the 0.75 floor at 100%, **or** export the rasterizer's capsule outlines.
- Add an oracle: rasterize `export_svg` coverage vs `apply_incremental_operation` at 100% on the hairline and tapered fixtures.
- Stream SVG through the USB store (keep PNG if you want a second file). Do not treat PNG as closing §6.

---

### H7 — Déjà-vu ledger marks a half-visible 2×2 group as fully rendered

- **File:** `vector_v2/src/tile_producer.cpp:657-681`, `591-603`
- **Also:** `vector_v2/src/rerender_ledger.cpp:111-127`
- **Status:** open (new in the last 24h)

`publish_group` only publishes tiles in `rendered ∩ visible`. Then `record_group_render` sets one `kRenderedFlag` for the whole 128² group. A viewport that intersects 1–3 tiles of a group still marks that group rendered.

A later view that first needs the hidden sibling (same revision, tile still resident, no eviction) is classified `kUnexplained`. Idle repair's job is exactly the cardinal neighbor (`idle_repair.cpp:48-51`). Unaligned home views do this: fill at `(63,63)` publishes group-6 tile 6 only; neighbor `x=431` needs tile 7.

Cache-tour receipts can stay at amplification 1.000 because they reuse the same views. Draw-and-return, pan, and repair will not.

Pixels are fine. The oracle the last day just wired will lie.

**Proposed fix:** keep four per-tile rendered bits, or do not set rendered until every in-grid group tile has been published. Classify "group rendered but this tile never published" as cold / continuation. Publishing off-screen siblings also works but burns slots.

---

## Medium

### M1 — Replay ignores add/commit reject; product cancels the tail

- **File:** `esp32/main/vector_v2/vector_v2_gate_harness.cpp:2967-2986`
- **Vs:** `esp32/main/vector_v2/vector_v2_app.cpp:1087-1104`

Product on non-`kAccepted` cancels builder, resets ribbon, `ink.end()`, full refresh. Replay only increments `commit_failures` and keeps feeding a `kRejected` builder. Ribbon still grows. `builder.finish` on Up then fails. Two authority policies for the same `process_live_ink_move` result.

**Fix:** copy the product reject path (or a shared helper).

---

### M2 — Capture `offer()` and production `offer()` diverge after overflow

- **File:** `esp32/main/vector_v2/vector_v2_touch_sampler.cpp:120-132`
- **Also:** `esp32/main/vector_v2/vector_v2_ink_trace_capture.cpp:43-55`, `vector_v2/src/touch_event_buffer.cpp:31-33`

Capture feeds a private 8-slot buffer and pops immediately (no coalesce, almost no overflow). Production is 16 slots and can refuse a Down. After a refused production Down, production `touching_` stays false so later points are Downs; capture already took the Down so later points are Moves.

A recorded session that overflowed is not a recording of what the app consumed.

**Fix:** record the production `TouchOfferResult` / accepted event, or share one derivation FSM and fork only accepted events.

---

### M3 — Visual-first ribbon is not the committed curve

- **File:** live `core/src/ribbon_geometry.cpp:122-141,247-319` baked at `vector_v2_presenter.cpp:367-371`
- **Authority:** `vector_v2/src/incremental_rasterizer.cpp:746-775`
- **Chunks:** `vector_v2/src/chained_operation_builder.cpp:89-109` with `kInteractiveChunkSampleLimit = 32`

Live commits outline quads from `emit_quadratic` (0.75 overlap, split spans). Document paints 2–3 capsules via `curved_unit`. Each 32-sample chunk restarts (`samples.size() <= 2` is a straight segment). Lift `refresh_region` replaces the baked ribbon with that authority.

`19ebbe3` put authority on *a* curve, not *this* ribbon. After lift the stroke can pop. Chunk seams are extra straight joins the live stream never showed. This is the mismatch visual-first is supposed to close, and it is still open.

**Fix:** rasterize the same units the live ribbon commits (or emit live from `curved_unit`), and treat a gesture as one curve across the 1-sample overlap instead of a new 2-sample stroke.

---

### M4 — `refresh_pan` does not recover after `ring_scroll`

- **File:** `esp32/main/vector_v2/vector_v2_presenter.cpp:525-554`

`refresh_pan` advances `frame_ring_` and sets `frame_ring_bottom_` before exposed compose, chrome prepare, or TE wait. Those later failures just `return timing` with `passed=false`. Scroll-null is the only path that falls back to `refresh()`.

Navigation is already at the new origin. The ring is shifted. The panel still shows the previous GRAM. Pan lift in the app (`1123-1126`) does not present again. The display stays wrong until some later `refresh()` (`show_start`, next pan reuse-miss, idle `refresh_region`).

**Fix:** on any post-scroll failure, call `refresh(chrome, event_us)`. Or do not commit the ring until compose+TE succeed.

---

### M5 — Only a full 368×448 present waits for TE

- **File:** `esp32/main/vector_v2/vector_v2_presenter.cpp:995-1006`, `239-267`

`present_pixels` waits for TE iff the window is the entire panel. Lift `refresh_region`, idle tile fill, and minimap-expanded bounds (minimap `y1` aligns to 370) stream without a TE edge. `refresh_region` can also split into several sequential windows.

A large lift or fill can start mid-scanout. Top-to-bottom RAMWR then races the beam: stale band or a horizontal tear. Pan is the path that actually waits then sweeps 0–371. This is the leftover tear hole on ink/fill after the pan work.

**Fix:** wait TE for any window taller than one strip (or any window that includes row 0 and more than ~N rows), not only exact full-frame.

---

### M6 — 931c7cf hole: modal/popup chrome still rasterizes after stream start

- **File:** `vector_v2/src/chrome.cpp:905-907,996-1002`
- **Also:** `esp32/main/vector_v2/vector_v2_presenter.cpp:986-1024`

`prepare_for` returns immediately when `!canvas_overlays_visible` (popup, confirm, export). `paint_prepared` then calls `draw_fixed_chrome` / `draw_export_toast` inside each DMA strip after CASET/RASET. Color popup is a lot of filled circles.

Cached dock/overlays are prepared before the stream. Modal chrome is not. A strip that overruns the ~20 B/µs wire budget lets scanout pass a half-drawn band.

**Fix:** pre-rasterize popup/dialog/toast into the cache (or a scratch) in `prepare_for` before `wait_for_tear_edge` / `stream_rect`, and only blit in `paint_prepared`.

---

### M7 — `RibbonPrimitiveBatch::overflowed()` is never honored

- **File:** `core/include/tinydraw/ink/ribbon_geometry.h:34-37`
- **Callers that ignore it:** `vector_v2_presenter.cpp:353-397`, `live_ink_coordinator.h:27`, `vector_v2/src/svg_export.cpp:97-121`

Release drops extra primitives and sets the flag. The header says callers must full-refresh the stroke. Presenter, coordinator, and SVG never read it. `finish()` copies provisional into committed then `update.provisional = {}`, so a provisional overflow flag is thrown away.

Documented worst case is 9 vs capacity 10. I could not construct a legal emit that exceeds 10. The fail-open is still real: if the bound is ever wrong, live ink and SVG silently lose geometry.

**Fix:** if `committed.overflowed() || provisional.overflowed()`, `presenter.refresh(...)` and fail `export_svg`. Keep the assert in debug.

---

### M8 — Failed present leaves the frame ahead of the panel

- **File:** `esp32/main/vector_v2/vector_v2_presenter.cpp:357-397`
- **Also:** `live_ink_coordinator.h:19-36`, `vector_v2_app.cpp:1087-1088`

`show_update` writes `update.committed` into `frame_` and replaces `live_provisional_*` **before** `present_unobscured`. Coordinator still `builder.add` / commit if present fails. App ignores `visual_passed`.

Panel can sit on the previous damage rect while the frame and overlay already moved. Next move usually unions bounds and heals. A failed present then a pause leaves a torn/stale tail. Authority may already include that sample.

**Fix:** present first, or keep a copy and roll back frame/overlay on `!passed`. On `!visual_passed`, refresh the damage rect even if add was accepted.

---

### M9 — Idle repair neighbors are not saturation-guarded

- **File:** `esp32/main/vector_v2/vector_v2_app.cpp:1330-1352`
- **Plan:** `vector_v2/src/idle_repair.cpp:42-57`

After a successful fill, repair first produces the four neighbor views. The saturation stop (`resident_raw_tiles() + kRepairSaturationHeadroomTiles >= slot_capacity()`) only applies once `repair_cursor >= repair_plan.grid_start`. Neighbor work is unguarded.

At 100% a dense/hairline document already fills the 448-slot pool. The next neighbor `produce_next` evicts resident tiles of the view the user is looking at. The next `refresh` (power, chrome, lift) can show fallback holes in the current camera.

**Fix:** apply the saturation guard to every repair step, or pin the active view's tiles until repair finishes.

---

### M10 — Undo / Redo are visible chrome actions with no authority

- **File:** `esp32/main/vector_v2/vector_v2_app.cpp:743-746`
- **Also:** `vector_v2/src/chrome.cpp:762-767`
- **Contract:** `SHIP_CONTRACT.md` §5

Dock slots 0/1 dispatch `kUndo` / `kRedo`. `apply_chrome_action` hits `break` and only refreshes. `can_undo` / `can_redo` stay false (arrows muted) but the hit targets still fire.

This is not a corrupting half-implementation. `restore_document_snapshot` wipes the operation sequence; wiring Undo to it would drop vector authority. The dangerous seam is someone using that function as undo.

**Fix:** active prefix over `gesture_id` groups. Do not implement undo via overview restore. Keep `can_undo` false until the prefix exists. Optionally stop emitting the actions.

---

### M11 — `discard_tiles` / `publish_overview` drop the cache without `mark_evicted`

- **File:** `vector_v2/src/materialized_canvas.cpp:1198+` (`discard_tiles`), `391-422` (`publish_overview`)

Eviction is only reported from `publish_tile` / `write_tile` / `materialize_uniform_as_raw`. A same-revision discard+refill (gate harness, census, cold-cache) is unexplained. A `publish_overview` revision bump with no damage mark is `kStaleRevision` even though every tile was thrown away.

**Fix:** on discard / overview replace, `mark_evicted` every occupied raw key (or `reset()` the ledger if it is a new session).

---

### M12 — `InkStream::end()` still returns the last live point

- **File:** `core/src/ink_stream.cpp:45-51,99`
- **Test hole:** `tests/ink_stream_test.cpp:144-157`

`end()` only clears `active_`. `update`/`finish` without an active stroke return `previous_`. After a real stroke that is the last ink point, not the zero "safe inactive point" the test describes for a never-started stream.

Product currently checks `ink.active()` before finish. The reject path calls `ink.end()` then later must not ingest. Any missed check after `end()` re-ingests the old tip. 02790e5 closed the never-started case and left this one.

**Fix:** on `end()` / failed `begin`, zero `previous_`. Test post-`end()`, not just never-started.

---

### M13 — Mixed-draw 50% appends still miss the 15 ms budget

- **File:** in-place path `vector_v2/src/incremental_document.cpp:310-397`
- **Evidence:** `PROJECT_STATE.md`, wave-3 receipt

18.8 ms max vs 15 ms. Dating evidence points at `19ebbe3` (curved committed ink). Not a pixel bug. It is a ship-gate miss that the last day attributed but did not close. Owner still needs to move the budget or the overview-replay-per-chunk design.

Phase timers (`prepare/overview/enumerate/uniform/raw/commit`) are in place. Use them before changing the painter again.

---

### M14 — Default V1 firmware: blocking export vs a 32-deep drop-oldest touch queue

- **File:** `esp32/main/hardware_app.cpp:104-111,600-627,1065-1214`

Raster V1 is still the default `app_main`. `enqueue_latest` drops the oldest `AppEvent` when the queue is full. `export_image()` blocks the consumer for seconds while `touch_task` still enqueues at ~1 kHz. After export, leftover `touching=true` events are processed as a new down/move without a matching hardware down, and a dropped Up leaves `pressed` stuck.

**Fix:** pause the producer during export, or drain/reset touch state on export entry and exit.

---

### M15 — Gate-harness firmware falls into the product loop with leftover authority

- **File:** `esp32/main/vector_v2/vector_v2_app.cpp:860-870`

On pass the harness only `set_view(25%, 0, 0)`. Last document writer is `run_long_gesture_commit_gate` (blank snapshot + a 1600-sample stroke). Then `run_vector_v2_app` continues into the interactive loop. A gate-harness flash is not a blank device. Export encode has already written the export partition.

Default product builds do not define `TINYDRAW_VECTOR_V2_GATE_HARNESS`.

**Fix:** after a pass, `restore_document_snapshot` + `producer.reset_uniform_baseline` + `set_view(25%)` + `refresh`, or `return` before the product loop.

---

### M16 — Replay timestamps are offer wall-clock, not trace `t_us`

- **File:** `esp32/main/vector_v2/vector_v2_gate_harness.cpp:2700-2728`
- **Also:** `core/src/ink_stream.cpp:53-61`

The task waits until `base+t_us`, then `offer(..., esp_timer_get_time())`. If the loop is late, several events are offered in a burst with ~0 µs gaps. `InkStream` then uses `nominal_dt_ms` (8 ms) for equal/backward stamps, not the recorded 1 ms. Latency numbers stay honest. The path does not.

**Fix:** stamp offers with `base+t_us` (or the trace delta). Keep wall-clock only in the latency struct.

---

### M17 — Replay parser cap is 4096; capture ring is 12288

- **File:** `esp32/main/vector_v2/vector_v2_gate_harness.cpp:2804-2822`
- **Vs:** `esp32/main/vector_v2/vector_v2_ink_trace_capture.h:24-26`

A legal recorded CSV longer than 4096 events (`under-overlay` is already 9284) parses as `kOutputTooSmall` and the gate fails that spec. Current five embedded traces fit.

**Fix:** size storage to `kInkTraceCaptureCapacity` or stream the parse.

---

### M18 — One-event-per-loop + 16-slot buffer can still drop Down

- **File:** `esp32/main/vector_v2/vector_v2_app.cpp:988`
- **Also:** `vector_v2/src/touch_event_buffer.cpp:26-50,83-96`

Moves coalesce; edges do not. Lift does finish-preview + every remaining chunk + region refresh on the Up iteration. Rapid tap-tap while that runs can fill 16 edge events. Next Down returns `kOverflow` and is not retried as Down.

**Fix:** drain more than one event per tick when idle/after lift, or keep a reserved slot for Down/Up.

---

### M19 — `present()` does not drain DMA if the stream dies mid-window

- **File:** `esp32/main/vector_v2/vector_v2_presenter.cpp:1022-1028`
- **Also:** `esp32/main/co5300_panel_transport.cpp:610-636`

`stream_rect` can return false after some strips have already been submitted. `present_pixels` aborts the scheduler and returns without `wait_for_all`. The next present programs a new CASET/RASET while the old RAMWRC queue may still be draining.

Product `paint_prepared` should not fail after a matching `prepare_for` on one thread. `tx_color` failure is still a real driver path. `refresh_pan` does drain after `present_ring`. The linear `present()` path does not.

**Fix:** always `wait_for_all` after a started stream, including abort.

---

### M20 — Cross-core I2C: PMIC on core 0, touch on core 1, same bus, no app lock

- **File:** `esp32/main/vector_v2/vector_v2_app.cpp:779-780,962-971`
- **Also:** `esp32/main/vector_v2/vector_v2_touch_sampler.cpp:104-134`

V1 reads power inside the touch task. V2 samples CST816S on core 1 every 1 ms and calls `power.read()` on the main task every 30 s (`!pressed`). Both use `touch.bus()`. There is no app mutex. If `i2c_master` does not serialize two devices, this is bus corruption / `kError` storms.

I did not verify the IDF 5 bus lock inside this repo.

**Fix:** one I2C lock around `PhysicalTouch::read` and `PowerManager::{read,write}_register`, or move `power.read()` onto the sampler task like V1.

---

### M21 — Beam-race experiment issues a second window while the first stream is in flight

- **File:** `esp32/main/vector_v2/vector_v2_presenter.cpp:612-649`
- **ifdef:** `TINYDRAW_VECTOR_V2_PRESENTATION_BEAM_RACE_CONTROL` (off by default)

One pan does `present_ring(start_row…bottom)` then `present_ring(0…start_row)` and only then `wait_for_all`. Each `stream_rect_ring` programs CASET/RASET and starts RAMWR/RAMWRC. Up to 3 color DMA slots can still be filling window 1 when window 2 is programmed. Default build is `BOUNDARY_TOP_SWEEP` (single sweep).

**Fix:** `wait_for_all` after the first band, or one window for the full canvas. Or delete the experiment now that the default sweep is accepted.

---

### M22 — `kMinimumScreenRadius` silently underwrites warm-start correctness

- **File:** `vector_v2/src/incremental_rasterizer.cpp:31`, `569-575`

The 0.75 clamp exists so "stroke presence survives every committed zoom." The warm-start painter is only correct because adjacent-row chords overlap in x, which holds iff screen radius is ≥ ~0.5 px. Comments were added at the painter in the cold campaign. The constant's own comment should also name the dependent invariant. Lowering the clamp would silently miss pixels.

Not a current miss. Load-bearing coupling across two files.

**Fix:** name the painter invariant on the constant. Add a host test that paints a 0.5 px stadium with the warm path vs a brute-force oracle.

---

### M23 — `commit_incremental_revision` can mutate then return false

- **File:** `vector_v2/src/materialized_canvas.cpp:567-614`
- **Caller:** `vector_v2/src/incremental_document.cpp:154-157`

Validation runs first, then `apply_overview_publication`, then slot writes. `choose_slot()` returning nullopt returns `false` with overview already replaced and `current_revision_` still old. Incremental then `cancel()`s the log.

After current checks (`any_tile_pinned`, `raw_publications <= slots_.size()`) `choose_slot` should not fail. Product interactive path uses in-place commit. Still a contract hole on the transactional path.

**Fix:** choose every dest slot before `apply_overview_publication`, or roll back the overview on failure.

---

### M24 — Lift diagnostics hold off cold fill across the next gesture

- **File:** `esp32/main/vector_v2/vector_v2_app.cpp:1231,1382-1391`

`fill_allowed` requires `!lift_timing.pending`. Pending is cleared only when `idle_before_poll && !pressed`. A new down in the next poll keeps `pending` true so fill/repair stay off for the whole next stroke. Rapid sketching never settles tiles until there is a fully idle poll.

**Fix:** clear `pending` when a new down starts (print later or drop the report).

---

## Low

### L1 — `prepare()` `std::copy` does not reject overlapping source/destination

- **File:** `vector_v2/src/operation_log.cpp:116-125`

02790e5 closed record/sample aliasing. `prepare` still copies `append_request.samples` into `samples_.subspan(sample_count_)` with no overlap check. Overlapping `std::copy` is UB. Product uses a separate `input_samples` arena so the device path is fine.

**Fix:** reject `storage_overlaps` unless the ranges are identical. Test a tail-aliasing append.

---

### L2 — Capture dump reset does not reset the drain FSM

- **File:** `esp32/main/vector_v2/vector_v2_ink_trace_capture.cpp:128-131`

Only `count_`, `overflowed_`, `strokes_`, `enabled_` are cleared. `drain_` keeps `touching_` / `no_touch_reads_` / `last_point_`. Today dump requires `!touching()`, so this is usually idle.

**Fix:** reconstruct `drain_` (or add `reset()`) and zero `touching_` / `last_activity_us_`.

---

### L3 — Lift does not pass the raw Up coordinate into `InkStream::finish`

- **File:** `esp32/main/vector_v2/vector_v2_app.cpp:1134-1135`
- **Test that expects the other behavior:** `tests/ink_stream_test.cpp:120-128`

Finish is `{last_ink.position, finished_us}`. Product first applies the Up as a Move only if `point != last_touch`. Production Up is enqueued as `last_point_`, so it almost always matches the last Move and never snaps. Last ink point is the last streamlined sample, not the last contact.

**Fix:** `ink.finish` the raw Up (or last contact), matching the unit test.

---

### L4 — Shared staging assumes even width; stream paths do not enforce it

- **File:** `vector_v2/include/tinydraw/vector_v2/panel_staging.h:35-42`
- **Vs:** `esp32/main/co5300_panel_transport.cpp:546-555,643-650`

`stage_pixels_swapped` steps by 2 and reads `source[column+1]`. Odd `width` is OOB. `push_rect` fail-closes on odd windows. `stream_rect` / `stream_rect_ring` do not. Product `align_bounds` keeps windows even.

**Fix:** apply the same even-window reject in both stream functions.

---

### L5 — Exposed-ring patch writes host pixels onto a swapped strip

- **File:** `esp32/main/vector_v2/vector_v2_presenter.cpp:849-860,931-935`

`present_ring` sets `accepts_byte_swapped=true`, so the bounce buffer is already panel-endian when `paint_stage_surface` runs. `copy_ring_to_stage` copies host-order ring pixels. Chrome then blends with swap; the exposed canvas hole stays wrong-endian.

Product `refresh_pan` pre-composes exposed rows and passes an empty `exposed` span. The patch still accepts `exposed`.

**Fix:** swap in `copy_ring_to_stage` when `surface.byte_swapped`, or drop in-patch compose.

---

### L6 — Leftover provisional ink fail-closes the fused pan stream

- **File:** `esp32/main/vector_v2/vector_v2_presenter.cpp:863-866`

If `live_provisional_count_ != 0` and the surface is byte-swapped, the patch returns false and the ring stream aborts. App ink vs pan are exclusive, and `finish()` clears provisional. `refresh_pan` does not `clear_live_overlay()`.

**Fix:** clear provisional on pan, or byte-swap the live color.

---

### L7 — Uncached chrome on a swapped surface would be wrong-endian

- **File:** `vector_v2/src/chrome.cpp:996-1000`

Modal path writes host-order colors. Fused pan marks the surface swapped. App cannot pan with a popup. Latent if `present_ring` is reused for modal chrome.

**Fix:** same as M6: blit prepared sprites.

---

### L8 — TE ISR classifies the edge by sampling the pin after ANYEDGE

- **File:** `esp32/main/co5300_panel_transport.cpp:799-815`

ANYEDGE fires, then `gpio_get_level`. A bounce or a pulse that already flipped again increments the other counter. Can miss the selected edge (feeds M4) or start the sweep at the wrong phase.

**Fix:** separate POS/NEG ISRs, or record the transition without a post-event level sample.

---

### L9 — Lost color completion can hang the next present

- **File:** `esp32/main/co5300_panel_transport.cpp:583-584,678-679`

Slot acquire is `portMAX_DELAY`. `wait_for_all` can time out at 2 s while slots are still held. The next stream then blocks forever if all 3 completions were lost.

**Fix:** timed acquire plus a hard transport reset.

---

### L10 — `publish_group` / paper publish can fail after some tiles are already live

- **File:** `vector_v2/src/tile_producer.cpp:668-677`, `195-197`

Each published tile is a complete newest-first surface copy. The *group* can be 1–3 tiles live with no ledger completion. Next `produce_next` retries leftovers. Pins are pre-checked so this should be rare.

**Fix:** two-phase publish (validate all four, then commit) or undo already-published keys on failure.

---

### L11 — `PixelPainter` does not validate `pixels.size()` and ignores stride

- **File:** `core/include/tinydraw/ui/pixel_painter.h:31-47`

Writes `y * width + x`. Current stage surfaces are packed (`stride == width`). An undersized span is OOB. Not a live write bug today.

**Fix:** take a stride and a size; reject short spans.

---

### L12 — `AppStorage` never frees; fail paths leak the arena

- **File:** `esp32/main/vector_v2/vector_v2_app.cpp:188-309`

`heap_caps_malloc` with no destructor. Partial `allocate()` failure leaks whatever already succeeded. Happy path never returns. Not a double-free.

**Fix:** a destructor that frees every non-null pointer, and free-on-failure inside `allocate()`.

---

### L13 — `restore_document_snapshot` is canvas-then-log

- **File:** `vector_v2/src/incremental_document.cpp:399-415`

`canvas.restore_snapshot` runs first and can mutate. `log.reset` is second and only fails if `append_pending_`. `can_reset()` was already checked. Under the serialized contract this cannot fail. If it ever did, canvas would be at the new revision with the log still holding old ops.

**Fix:** treat `log.reset` after `can_reset()` as infallible, or snapshot/rollback canvas on reset failure.

---

### L14 — CoverageTile / StrokeRaster still use `assert` as the only local guard

- **File:** `core/src/coverage_tile.cpp:43-44,67-72,315`
- **Also:** `core/src/stroke_raster.cpp:78,86,98,146`

Default V1 firmware only reaches them after `canvas.ready()`. Negative or oversized `CoverageTile::reset` in Release writes with a stale/zero size. `RibbonRenderer::render_surface` now validates before calling `reset` (02790e5). Direct callers of `CoverageTile` do not.

**Fix:** fail-closed `reset` like `render_surface`, or keep the assert and make every caller validate.

---

### L15 — OperationLodStore is fully unwired

- **File:** `vector_v2/include/tinydraw/vector_v2/operation_lod_store.h` and `vector_v2/src/operation_lod_store.cpp`

~330 lines plus tests. The roadmap forbids the four-LOD design. `memory_layout.h` still budgets `kLodStorageBytes` in the external plan.

**Fix:** delete it or mark it as a measured-and-rejected reference. Do not leave the bytes in the live plan if they are not allocated.

---

### L16 — `apply_masked_incremental_curve_step` lost its product caller

- **File:** `vector_v2/src/incremental_rasterizer.cpp:1160-1187`

Public API now one-line-delegates into the prepared-unit path for the one-sample case and still uses the warm per-chord painter for multi-step. Product cold path uses `apply_masked_prepared_curve_unit`. Fine as a seam. Say so in the header.

---

### L17 — `incremental_segment_step_count` always returns 1

- **File:** `vector_v2/src/incremental_rasterizer.cpp:965-968`

`incremental_segment_step_work` ignores everything but `step == 0`. The step abstraction outlived its use.

---

### L18 — `vector_v2_app.cpp` is still 1400+ lines

Already tracked in `PROJECT_STATE.md`. Do not mix a split into the performance campaign. `AppStorage::allocate()` is the natural first extraction.

---

## In-flight concurrent work (do not treat as reviewed)

While this review ran, uncommitted edits appeared on:

- `vector_v2/src/tile_producer.cpp`
- `vector_v2/src/materialized_canvas.cpp` + header (new strided `publish_tile`)
- `vector_v2/src/tile_payload_analysis.cpp` + header
- `esp32/main/vector_v2/vector_v2_app.cpp` (drops `producer_packed`; harness-only `harness_tile_scratch`)
- `esp32/main/vector_v2/vector_v2_gate_harness.cpp` / `.h`
- related tests and `raster_census.cpp`

That is Cold Stage B item 1 from the oracle handover (strided publish, skip packed SRAM copy). Re-review that delta before treating packed-copy comments in older reviews as current. The findings above that live in those files (H7, M11, L10, M23) should be re-checked against the landed strided path.

---

## 02790e5 — what actually closed

| Hole | State |
|---|---|
| `RibbonRenderer::render_surface` NDEBUG underflow / OOB | Closed. Runtime check before `(height-1)*stride+width`. |
| `RibbonPrimitiveBatch` OOB write | Closed (fail-closed + capacity 10). Callers still ignore `overflowed()` (M7). |
| `InkStream` ingest without an active stroke | Closed for never-started. Post-`end()` leftover (M12). |
| `OperationLog::ready` overlap + uint32 index | Closed. `prepare()` source/dest overlap still open (L1). |

---

## Checked and OK (so this is not a 40-item fishing trip)

**Raster / mask**

- Mask bit layout (`LSB = pixel & 7`) is consistent across finalize, span paint, `mask_unset_window`, `mask_range_all_set`.
- Linear index is `(y-y0)*stride+(x-x0)`, matching `valid_surface`.
- Warm-start is only used on constant-radius chords. Tapered rows brute-force a conservative interval then `covers_pixel`.
- Prepared units pack the same floats `curved_unit` just built. Producer walks endpoints newest-first; same color so the mask union matches oldest-first forward replay.
- `operation_world_bounds` includes a 6-quarter-unit tiled halo. Replay-index cells plus the inverse group query are conservative. Intersecting paint intersects the query. On-device workspace currently leaves `replay_index_words` empty so firmware replay is linear fallback.

**Authority / incremental**

- `prepare` / `publish` / `cancel` keep counts and revision unchanged until publish. One pending prepare. Token skips 0.
- In-place: fallible prep before mutation. Failed commit invalidates painted keys and cancels the log. Visible tiles are budget-exempt. Same-color uniforms retain at every zoom. Other-zoom raw drop is the stated bargain.
- `OperationBuilder` rejects non-finite / out-of-world / tiny radius / time regression. Product `ChainedOperationBuilder` splits on capacity and elapsed overflow with a 1-sample overlap.
- No silent drop inside `OperationLog`. Log full → tail discarded, committed chunks remain.

**Panel / chrome (happy path)**

- Product pan sweep is rows 0–371 (`chrome_canvas_bottom = 372`). Dock starts at 372.
- Frame stays canvas-pure. Chrome is only applied on the bounce buffer.
- `stage_ring_row` wrap model matches `panel_staging_test`.
- DMA: 3×16384 internal buffers, counting semaphore, fill then submit. Patch `StageContext` lives on the `present_*` stack and is only used before each submit. DMA never reads `frame_` or the chrome cache.
- Chrome cache lifetime split: zoom / battery / bottom / minimap base invalidate independently. Viewport rectangle is drawn live. `blend_cached_sprite` honors `byte_swapped`.
- Reverted tear-wait overlap is gone. Default pan is wait → one 0…371 stream → one `wait_for_all`.

**Touch (happy path)**

- Down/Up never coalesce. Newest move overwrites newest move. Failed Down does not set `touching_`. Failed Up retries. Lift needs two `kNoTouch` reads.
- Capture-upstream-of-coalesce is spec. It is only wrong when the two `offer()` FSMs desync (H3, M2).
- Successful lift: `finish()` moves provisional into committed and empties provisional. Not a stale-overlay-after-commit bug on the happy path.

**Navigation math**

- `1472*400` and `1792*400` fit in `int`. Focus uses `int64`. Origins clamp. 25% origin is always `{0,0}`. Hardware wrap 400% → 25% is explicit. `operation_point` clamp matches `valid_point`.

**V1 default firmware (scanned, not "V1 is old")**

- `world_canvas.cpp`, `vector_document.cpp`, `viewport_renderer.cpp` (including `alignof` before `reinterpret_cast`), `stroke_lod.cpp` have runtime size/NaN guards. No additional V1 correctness bug stood up besides M14 and L14.

---

## Residual test / coverage risk

- No host run in this review. Prior session claimed host debug 29/29, release 29/29, asan 11/11 at `e76b98e`.
- No oracle for `covers_pixel` on zero-length unequal-radius segments (H2).
- No oracle for SVG vs `apply_incremental_operation` (H6).
- No test that GPIO zoom is ignored while `pressed` (H1).
- No test that replay follows chrome routing (H4).
- Ledger tests do not cover a half-visible 2×2 then sibling visit (H7).
- `live_ink_coordinator_test` covers visual-before-authority ordering only. No reject, overflow, or present-fail cases.
- Replay index is untested for bit-exact produce vs direct paint on a host with the index enabled.
- In-flight strided publish is unreviewed.

---

## Suggested fix order

Do these before more cold-compute surgery. Speed must not buy amplification or lost Downs.

- **H1** zoom-during-gesture (one `if`, no architecture)
- **H2** zero-length radius (one predicate, matches settled_renderer)
- **H5** replay hang (one loop condition)
- **H3 / M2 / M18** touch overflow family (sampler pause + reserved Down/Up slot)
- **H4 / M1 / M16 / M17** make replay actually be the product consumer
- **H7 / M11** ledger truth (or the oracle is worse than no oracle)
- **M4 / M5 / M8 / M19** presentation fail paths
- **H6** SVG = glass, then USB
- **M3** one geometry for live and committed (this is also the lift pop and part of mixed_draw)
- **M9** repair saturation on neighbors
- **M10** undo prefix, or stop pretending the dock does it

Then Cold Stage B. Re-review the strided-publish delta as its own correctness pass.

---

## Counts

| Severity | Count |
|---|---:|
| High | 7 |
| Medium | 24 |
| Low | 18 |
| **Total** | **49** |

Plus the in-flight strided-publish delta, which is explicitly out of scope of the snapshot.

No finding above is a style nit. Several low items are latent APIs that will become product bugs the first time a caller uses the documented hook (exposed ring patch, odd `stream_rect` width, modal chrome on a swapped surface).
