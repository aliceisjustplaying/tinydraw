# Grok 4.6 x-high review of `ba6c392`

Date: 2026-08-12. Reviewer: xAI Grok 4.6, `xhigh` reasoning, read-only plan mode, web search and subagents disabled.

The text below is the reviewer’s final report. Its claims were independently checked before implementation; it is evidence, not authority. The reviewer did not modify the repository.

(¬_¬) **Verdict: request changes.** Commit `ba6c392` (`prototype: harden settled zoom publication`, parent `caed9b5`) is a real correctness tightening, not a ship. The 50/100/200 fallback gates are physically real. Settled 100% and 200% still miss 500 ms. One remaining publication hole still certifies wrong-camera pixels as valid. I did not inspect tldraw. I did not edit files. I could not inflate the parent diff or re-run tests in this session; findings are against the tree at `ba6c392ae9f6959324eb008bb91efaf8976a3e05`.

The hardware log that RESULTS treats as this checkpoint is **not labeled `ba6c392`**. `second_review_hardware_ab/grok-handoff-auto-hardware.log:59` says `App version: caed9b5-dirty`, compile time `Aug 12 2026 16:26:46`. The commit is `17:13:54 +0100`. Treat that log as the dirty tree later committed, not as a hashed ELF identity.

---

## Blockers

None that hang the machine or paint a full viewport of the wrong document revision during the 12-cycle unchanged-document run.

There is still a path that publishes **wrong-camera pixels as valid**. That is a High, not a Blocker, because the default zoom view does not show the sliver. A one-pixel downward pan does.

---

## High

**1. Zoom marks a partially written band derived.** Mutation was fixed. Zoom was not.

After a successful `set_zoom`, every job that *intersects* the 368×372 presented rect is stored as `kDerivedReady`, but only `presented_rows` are resampled.

```1318:1349:esp32/main/interactive_pan_benchmark.cpp
    const bool visible_job = intersects(rect, {visible.x0, visible.y0, visible.x1, visible.y1});
    const bool valid = benchmark.has_complete_initial_atlas && visible_job && fallback_valid[job];
    ...
    benchmark.ready[job].store(valid ? kDerivedReady : kInvalidReady);
  ...
    const Rect region{visible.x0, visible.y0 + row0, visible.x1, visible.y0 + row1};
    resample_valid_raster_region(...);
```

Center cell, `kCenterOriginY = 448`, band 11 is atlas y `800–832`. Visible is `448–820`. Rows `800–819` are the new zoom. Rows `820–831` still hold the previous zoom.

`missing_pixels` treats any nonzero ready quality as present (`interactive_pan_benchmark.cpp:919-921`). `view_changed` therefore accepts a 1–12 px downward pan and `push_world` puts those 12 rows on screen just above the toolbar, labeled derived.

The window lasts until the grouped settled pass rewrites the whole band, about 400–680 ms. Mutation already requires the live viewport to contain the whole band (`1566:1576`). Zoom needs the same rule: mark derived only for bands whose resampled rows cover the whole job, or resample the full intersecting band before setting ready.

This is wrong-camera publication. It is not exercised by the auto driver, which never pans.

**2. Production LOD still drops visible hooks and plateau extrema.** The sign-change keep is real and the one-sample pulse test is real. The claim “every sampled radius extremum” and the header claim “direction reversals” are not.

```29:36:core/src/stroke_lod.cpp
  return (incoming > 0.0F && outgoing < 0.0F) || (incoming < 0.0F && outgoing > 0.0F);
```

A two-sample peak `2, 5, 5, 2` is not an extremum. It survives only if it exceeds `maximum_radius_error`. Firmware builds LOD at fixed **2.0 / 0.75 world units** (`build_settled_lod` at line 362, `commit_stroke` at 1507). Hairpin tests use **0.75 / 0.25**, not production tolerances.

A 1.5-world-unit hook is flattened. Screen error:

| Zoom | 2.0 world chord | 0.75 world radius |
|---:|---:|---:|
| 50% | 1.0 px | 0.38 px |
| 100% | 2.0 px | 0.75 px |
| 200% | 4.0 px | 1.5 px |
| 400% | 8.0 px | 3.0 px |
| 800% | 16.0 px | 6.0 px |

Slight AA/join differences are allowed. Stroke presence is not. 4 px at 200% is already past “join.” 8–16 px at 400/800 makes the current LOD unusable as a settled representation.

**3. After zoom, lateral pan is refused until runway, and runway waits for settled.** `set_zoom` only materializes the visible 368×372. Neighbor cells stay `kInvalidReady`. A 1 px pan right intersects the right cell and `view_changed` reverts the move (`hardware_app.cpp:1025-1029`, `interactive_pan_benchmark.cpp:1612-1618`).

Runway is scheduled after the visible settled pass (`818:847`). Settled is 400–680 ms. Adjacent nearest-resample is cheap and is being starved by the settled-first policy. Drawing stays responsive because `begin_stroke` pauses the render task. Panning does not. The auto driver never pans, so this is architecturally certain and empirically unverified.

**4. Settled 100% and 200% miss the 500 ms physical gate.** Honest ISR settled on the handoff log:

| Zoom | Physical settled |
|---:|---:|
| 50% | 396–459 ms |
| 100% | 674–677 ms |
| 200% | 632 ms |

Profile at 100%: clear 14 ms, raster **459 ms**, composite **103 ms**, publish 52 ms. Grouping bands already happened and only moved 100% from ~708 ms to ~676 ms. Traversal is not the leftover. Pixel coverage is.

---

## Medium

**Failed `set_zoom` zeros that zoom’s clocks** (`1200:1210`) then returns false on stale/unproven source (`1246:1284`). A refused 50% after a good 50% makes `last_zoom_timing(50)` report zeros except cancel. Benchmark honesty bug.

**`settled_us` can fall back to submit-time wall clock** if `complete_time_us` returns -1 after the 64-slot ring is evicted (`690:714`, `hardware_app.cpp:97,146-149`). Generation/view can still match if the user returned to the same origin. The 12-cycle run does not evict the ring. A human pan+toolbar+exact present during the wait can.

**Canonical composite is not cancelable.** `ViewportRenderer` checks cancel in geometry, not in `composite_batch` / `composite_lane`. Publication is still gated, so this is cancel latency (up to the 2 s `set_zoom` join) not wrong pixels.

**Long `cache_mutex` sections still cover DMA queueing.** `set_zoom` holds the mutex across all 17 strip `push_rect`s (`1212-1379`). Grouped settled holds it across the full 372-row push (`672-687`). `push_rect` then blocks on a depth-3 `transfer_semaphore`. No lock-order deadlock. It does invert priority and extends the completion-ring window.

**Live `StrokeRaster` presents without the cache lock.** Safe today only because `begin_stroke` joins the render task first. If that 2 s wait times out and the caller still draws, `PhysicalDisplay` staging races.

**Extreme finite geometry.** `VectorDocument` rejects nonfinite samples (`vector_document.cpp:85-88`). `camera_project` of huge finite world becomes float Inf (`camera.cpp:18-21`). Settled then collapses a long chord (`inverse_length_squared → 0`). Canonical coverage rejects `|coord| > 1e6` (`coverage_tile.cpp:12-16`). Settled does not.

**Candidate bitset shorter than the document silently drops tail strokes** (`settled_renderer.cpp:228-234`). Firmware query words are capacity-sized. The API is still a missing-ink footgun.

**`WorldCanvas::capture` copies 448 rows** (`world_canvas.cpp:46-48`) including the 76 toolbar rows. Those land in bands that mutation now leaves invalid, so they are not certified. They are still junk in the atlas until vector redraw.

**No host coordinator tests.** Pin, refuse, repair, last-band ready, generation-at-lock, ring eviction and pan-after-zoom exist only as firmware interleavings.

---

## Nits

- Squared-edge ramp: `inner < 0` still uses `inner²` (`settled_renderer.cpp:106-111`). Thin capsules get a slightly fat solid core. Settled AA liberty, not missing ink.
- `stroke_lod.h:11-12` promises direction reversals. The implementation has no heading test.
- Incremental `t += t_step` can drift across a 368-wide row.
- `enqueue_refinement_published` uses `xQueueSend(..., 0)` (`hardware_app.cpp:627-630`). A full queue drops a toolbar refresh.
- Source validation still uses a 1.0 px “bilinear” halo on a nearest resample (`403:413`). Conservative refusals, not wrong pixels.
- `center_ready()` still requires all 14 center-cell bands, including rows below the 372-row presented region (`437:448`).

---

## Confirmed-correct

These hold on the current tree. Prior reviews are not the proof; the cited lines are.

- **Pinned fallback, no third atlas.** First interactive zoom exchanges arenas and never promotes a partial active atlas (`1289:1311`). Later zooms resample pinned `materialization_storage` → active `world`. After pin, source and dest do not alias.
- **Stale-source refuse.** Zoom returns false while `fallback_source_document_revision != document_revision` (`1242:1254`) and then notifies repair. Hardware: `stale_zoom_accepted=0`.
- **Incremental exact repair does not certify a partial source.** Pending bits are cumulative. Source revision advances only when none remain, then every `fallback_source_job_revision` is filled (`730:810`, `1515:1540`). Repair writes the pinned arena, not the active one (`774:786`).
- **Live mutation bands become current only if the captured viewport contains them** (`1566:1576`). Auto mutation passes `visible_raster_current=false` (`hardware_app.cpp:1406`), so that hardware path never exercises the live-capture branch. The live pen path does, after a locked `world.capture` (`1785:1789`).
- **Malformed LOD maps fall back atomically** to raw document geometry (`settled_renderer.cpp:212-226`). `count==0` or an OOB range dumps the whole map. Host test: `tests/settled_renderer_test.cpp` “malformed settled LOD”.
- **LOD simplify is fail-closed** on empty output or bad tolerances (`stroke_lod.cpp:40-50`, `90-91`). Setup tears down the benchmark rather than running a partial map (`349:375`, `1124:1129`). Live append zeros the new count first; a failed simplify leaves `count==0` and forces raw fallback (`1500:1512`).
- **Strict one-sample radius extrema are kept**, including the sub-tolerance `2.0 → 2.5 → 2.0` pulse (`stroke_lod.cpp:67-73`, `tests/stroke_lod_test.cpp:55-65`).
- **Settled publication binds one snapshot.** Completeness captures `(view, revision)` (`585:590`). Submit and `settled_us` recheck generation, pause, revision and view after the lock (`673:709`). The Sol-2 “prove one view, push another” race is gone.
- **`settled_us` waits for the last submitted transfer sequence** and prefers the ISR timestamp (`690:705`). On the 12-cycle run this is a physical endpoint.
- **Cancellation inside capsule raster and composite**, every 8 rows (`settled_renderer.cpp:83-86`, `131-133`), plus between strokes (`236-238`). Incomplete settled is not copied (`632-634`). Firmware sets both hooks to `render_cancelled`.
- **Grouped visible cell, one geometry pass, shared scratch.** Visible bands of one cell are one `settled_render_region` (`557:631`). `settled_scratch` is the 164,864-byte `ViewportRenderer` arena (`1073:1114`). One render task. `begin_stroke` joins before mutating the document.
- **Macrogrid append failure disables candidate filtering** (`1492:1498`). Out-of-world appends instead set conservative all-stroke fallback (`stroke_macrogrid.cpp:66-68`, `78-80`). Both are fail-safe. Empty `candidate_strokes` means all strokes.
- **Benchmark toolbar, timing reset, timing snapshot, live capture and pan take `cache_mutex`** (`hardware_app.cpp:959-999`, `1018-1045`, `1785-1787`).
- **32-bit ISR completion timestamps** (`hardware_app.cpp:126-130`). No 64-bit atomic in the ISR.
- **Painter order is document order.** Eraser composites background. Host test covers the 3-stroke case.
- **No lock-order deadlock.** Mutex then transfer semaphore, never the reverse. ISR never takes `cache_mutex`. Hung DMA can still pin the mutex forever; that is a hang, not a cycle.

---

## Evidence table

| Metric | Claimed | Actual endpoint | Source | Verdict |
|---|---|---|---|---|
| A/B zoom-out 143→52 ms | Physical confirmation of the 3 MB clear | Driver wall through **queued** `push_world`, queue depth 3 | `RESULTS.md:25-43`, `patched-auto-zoom.log` | Verified as queue-end delta. Not ISR. |
| Strip first complete, old synthetic | 6.3–8.1 ms typ, 56.2 ms worst | ISR `first_complete_us` | `strips-auto-zoom.log` | Verified physical |
| Strip last visible, old synthetic | 38.9–49.5 typ, 97.4 worst | ISR `last_complete_us` | same | Verified physical |
| Realistic first / last, pre-settled | 6.6–16.2 / 39–57 ms | ISR | `realistic-auto-zoom.log` | Verified physical |
| “p95 ≤ 100 / 150–180 by an order of magnitude” | Distributional gate | 12 scripted transitions. Later cancel-inclusive first is 71–81 ms, last 113–123 ms | `RESULTS.md:85-87` vs later logs | Contradicted as p95. Verified only as those 12 cycles |
| LOD settled 200% 464 ms | <500 ms settled | Cache-ready, before full submit/complete | `settled-lod-auto-zoom.log`, `RESULTS.md:133-140` | Contradicted as physical. Document later recants this, correctly |
| Handoff first strip | ~7 ms typ, 71 ms worst | ISR `7017` typ, `71109` worst | `grok-handoff-auto-hardware.log:95,107,119` | Verified. “Typical 7 ms” censors all three 200% cycles (69–70 ms) |
| Handoff full fallback | 42–49 typ, 113 worst | ISR last complete `42626–48514`; worst `112598` | same `:95-128` | Verified. 200% last is always ~102–103 ms |
| Handoff settled 50/100/200 | 396–459 / 674–677 / 632 ms | ISR `AUTO_SETTLED` after last transfer | `:97-130` | Verified physical. 100/200 miss 500 ms |
| Handoff PSRAM | 125,208 free, 124,928 largest | `TINYDRAW_BENCH_MEMORY` after benchmark allocs | `:92` | Verified. `TINYDRAW_PSRAM free=563060` (`:88`) is pre-benchmark |
| LOD size | 19,844 → 7,537 samples, 90,444 / 98,304 B | `TINYDRAW_SETTLED_LOD` + `BENCH_MEMORY` | `:90,92` | Verified |
| Mutation refuse/repair | stale 0, repaired 1 | Booleans only. No pixel checksum, no repaired-zoom timings | `:131-133`, `hardware_app.cpp:1391-1421` | Verified as one accept/refuse pair. Unverified as pixels |
| Mutation table in RESULTS 167–173 | 200% first 65–79, fallback 98–113 | Mixes `review-fixes-mutation-auto.log` with `review-fixes-final-auto-zoom.log`; 50% “80.2” ignores `81168` | RESULTS vs those two logs | Mixed / partly contradicted |
| Drawing | Interaction-class | Auto driver never draws the live path. `record_draw_update` is per-`raster.update` CPU | `hardware_app.cpp:1637-1644` | Untested. Metric censored even if you draw |
| Pan / pan refusal | “no regressions”; miss_frames | Driver never pans. Invalid pans return before `record_frame` | `hardware_app.cpp:1025-1029` | Untested and censored |
| Visual identity | “spot-check by eye” | No panel checksum, no repaired-pixel compare | RESULTS 91–93 | Unverified |
| 25 / 400 / 800% | Desired production range | Not in `kZoomPercents` `{50,100,200}` | `interactive_pan_benchmark.cpp:43` | Untested. Not implied by 100% pin |
| Host tests this session | 136/136 in Sol 2; chronicle says debug/ASan/release pass | Not re-run here | Sol 2, chronicle §11 | Unverified this session |
| ELF == `ba6c392` | Checkpoint prepared for this review | Log is `caed9b5-dirty` | handoff `:59`, git reflog | Unverified binary identity |

Fallback interaction on 50/100/200 is supported by ISR first/last complete. Settled 100/200 miss 500 ms on that same honest endpoint. Drawing, pan, visual identity, post-mutation pixels and 25/400/800 are not measured.

---

## 25%, 400% and 800%

Do not treat the 100% pin as a proof of arbitrary zooms. The pin proves: a complete 1104×1344 raster at zoom 1 can nearest-fill a destination whose **ink** sits inside that world window.

Atlas size does not change with zoom. It is always 1104×1344×2 = 2,967,552 bytes. Two arenas are 5.94 MB. Free PSRAM after benchmark allocs is **125,208 bytes**. There is no third buffer and no overview beside those arenas.

**25%.** Visible world is 1472×1488. The 100% pin is 1104×1344. That is not an automatic refuse. `region_proven_by_source` asks whether every stroke that intersects the destination is inside the source, not whether dest world ⊆ source world (`172:200`). The seed-7 handwriting lives in 368–736 × 448–896, inside the pin, so a 25% fallback of *this document* would downsample the center and paint white margins. That is legal for blank world and **untested**. It fails as soon as any ink outside the pin still intersects the 25% view. A 25% 3×3 cannot be allocated. Production 25% needs a complete overview, not another camera-aligned atlas.

**400%.** Visible 92×93 ⊂ the pin if focus stays inside it. 4× nearest can meet the fallback clocks the same way 200% already does. That is blocky valid fallback, not settled. Current LOD is an 8 px chord / 3 px radius error. Capsule AABBs get fatter, not cheaper, on the ink that remains. No 400% cycle exists. Do not extrapolate 200%.

**800%.** Same coverage trick, 8× nearest, 16 px LOD error. One screen-filling capsule AABB is the whole 368×372. Treat 800% as a launch option. If settled 400% still misses 500 ms after span raster, cap there. That is not a reason to drop vector authority.

---

## Highest-leverage next implementation

Profile leftover at 100%: **459 ms raster + 103 ms composite**. Publish is a flat 52 ms. Grouping already removed repeated document walks.

`rasterize_capsule` walks the **axis-aligned pad box** of every chord (`settled_renderer.cpp:82-119`). A diagonal handwriting segment pays for empty corners twice, once in coverage and once in `composite_and_clear`. LOD made chords longer, so this waste grew.

Compare the realistic options against that profile:

| Approach | What it attacks | Fit |
|---|---|---|
| **Scanline spans** | 459 ms AABB walk. Per row, solid interior is a span write; 1 px edge is coverage | Highest leverage. Same painter order and eraser-as-background. The one change that can put 100% under 500 ms without touching draw/pan |
| **Sparse ordered 32×32 microtiles** | Long-stroke dirty-rect blowups; gives a later dual-core partition; matches production tile keys | Do with spans, not instead of them |
| **Dual-core** | Split already-sparse tiles | Second. Two cores walking empty AABBs waste PSRAM bandwidth together. `ViewportRenderer::execute` exists and the interactive path does not set it |
| **Fixed-point / SIMD / assembly** | `double` project per sample is milliseconds; packed RGB565 blend might cut ~100 ms composite | Third. Power-of-two resample is already off the critical path (full visible fallback 42–49 ms typical) |
| **Zoom-specific LOD** | 4–16 px error at 200–800% | Necessary for 400/800 quality. Four LOD copies of 90 KB cannot fit **now**. Fine after the atlases die |

Do not: grow the 3×3, put bilinear back on first paint, clear 3 MB, or chase canonical ribbon inside 500 ms (initial exact atlas is 5.36 s).

Drawing and pan stay fast only if settled never owns the pen path. Live stroke writes the visible tiles or a 368×372 overlay. Resident tiles get operation N+1 only. Background work must not be a cancel barrier on `begin_stroke` for longer than a bounded slice.

---

## Production architecture

Overview + sparse world-aligned tiles is still the right production shape. Nothing else fits 8 MB and these clocks.

The 3×3 cannot become production. It locks 5.94 MB of camera-aligned RGB565, leaves 125 KB, cannot source a general 25% and still rebases. Adding 25/400/800 onto this coordinator will produce a misleading “fallback works” headline.

If both arenas go, a plausible budget on the 4096 world implied by `StrokeMacrogrid` is:

- 512×512 RGB565 overview at 12.5% = 512 KB (always valid)
- visible 368×372 as ≤72 tiles of 64×32 = 295 KB
- a 128-slot ring = 512 KB
- live viewport 330 KB
- current vector 326 KB
- scratch + LOD + index ~300 KB
- ~1–1.5 MB reserve

That fits. A 25% complete overview (1024×1024 RGB565 = 2.0 MB) also fits if you spend the reserve. An 8192 world at 12.5% is already 2 MB; do not grow the world without changing overview format.

Rejected alternatives: keep the 3×3 and add zooms; vector replay as the interactive path; raster authority; a full pyramid (800% of 4096² is ~2 GB); one sliding hi-res window without tile keys (you will reinvent tiles).

---

## Prioritized autonomous implementation plan

Do not extend `kZoomPercents` on this coordinator.

- **Correctness first.** Mark a zoom fallback band derived only when every row of that band was resampled, or resample the full intersecting band. Add a host test: zoom, then a 1–12 px downward pan, checksum the bottom 12 presented rows against the new camera not the old one.
- **Host coordinator harness.** Extract pin / refuse / repair / generation-at-lock / last-band ready behind a fake display. Inject cancel before render, before lock, after copy, after submit and before completion. Cover repeated mutation, eraser, allocation failure and macrogrid append failure.
- **Production LOD contract.** Keep endpoints, discrete extrema **and** plateau extrema. Keep hooks above a **screen-space** bound, or generate per-zoom LOD. Add tests at the firmware 2.0 / 0.75 tolerances, not the tighter unit-test tolerances. Add a plateau `2, 5, 5, 2` case.
- **Scanline capsule spans** on 50/100/200 until physical settled is under 500 ms at 100% and 200%. Preserve painter order and eraser-as-background. Do not change the live draw path.
- **Cheap neighbor runway before settled**, or resample the full 3×3 from the pin in the background immediately after the visible strips. Pan must not wait 600 ms.
- **Then delete both 3×3 arenas.** Stand up the 512×512 overview and a 64×32 world-aligned ring. Incremental append to overview + visible tiles.
- **Then add 25%** (overview-backed). **Then 400%** with zoom-specific LOD. **Then measure 800%** and be willing to drop it.

---

## Next hardware / manual test script

Do this on the physical ESP32-S3 after the last-band fix, on the same seed-7 1,000-stroke document. Record ISR fields, not `fallback_us`. Print `TINYDRAW_BENCH_MEMORY` after every boot.

- Flash a binary whose `App version` line is the commit you mean. If it still says `caed9b5-dirty`, the log is not that commit.
- Confirm 12/12 `changed=1` on `50,100,200,100,50,100,200,100,50,100,200,100`.
- For each cycle record `first_complete_us`, `last_complete_us` and `AUTO_SETTLED settled_us`. Do not quote `fallback_us` as physical. Do not average away the 200% and cancel-inclusive 50% cycles.
- After a 100→50 zoom, **before** settled finishes, pan down about 12 px. The bottom of the panel must match 50%, not leftover 100%. Photograph it. Repeat 50→100 and 100→200.
- After a 100→50 zoom, immediately pan right 1 px. Today this should refuse. After the runway-order fix it should show valid 50% pixels, not a stall and not a checkerboard.
- Draw a pen stroke that crosses the bottom edge of the viewport. Pan to expose the offscreen continuation. That continuation must not show the pre-stroke raster as current.
- Draw an eraser dab that is a two-sample pressure plateau and a small hook inside 2 world units. Zoom 50/100/200. The dab and the hook must still be there.
- Repeat the automated mutation. Then add a second mutation during repair. Then an eraser mutation. Each stale zoom must refuse. Each repaired zoom must accept. After repair, the new ink must be visible at 50% and 100%. No checksum in the current driver means you have to look.
- Time one live stroke from pen-down through first physical pixels, including the `begin_stroke` join. The current `record_draw_update` number is not that.
- Time one warm pan that stays inside already-derived pixels and one pan that would have been refused. Log accepted vs refused counts. `miss_frames` cannot see refusals today.
- Do **not** add 25/400/800 to the auto sequence until the last-band ready bug is gone and you have an overview or a written provenance story for dest-world-outside-pin. If you still want a 25% mechanism experiment on this document only, treat white margins as expected and refuse as soon as any stroke sits outside the pin.

I did not run host tests, ASan or the device in this session. Shell was blocked. Comments, Sol reviews and RESULTS.md were treated as claims. The last-band sliver, the LOD production-tolerance gap, the pan-vs-runway ordering and the 100/200 settled miss are the load-bearing leftovers.
