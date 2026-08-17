# TinyDraw project state

Last updated: 2026-08-17 (stroke-logical SVG export, settled-AA PNG export,
faster color dialog, zoom-overlay pan promotion, exact zoom-cycle return,
owner-accepted absolute minimap control, on-demand V2 NTP UI, and the first
single-journal V2 autosave/recovery path landed;
receipts:
[`SVG`](benchmark-results/svg-export-2026-08-17/RECEIPT.md),
[`color dialog`](benchmark-results/color-dialog-2026-08-17/RECEIPT.md),
[`zoom-overlay pan`](benchmark-results/zoom-overlay-pan-2026-08-17/RECEIPT.md),
[`zoom-cycle return`](benchmark-results/zoom-cycle-return-2026-08-17/RECEIPT.md),
[`minimap navigation`](benchmark-results/minimap-navigation-2026-08-17/RECEIPT.md),
[`absolute minimap control`](benchmark-results/minimap-absolute-pointer-2026-08-17/RECEIPT.md),
[`responsive 0.40 ink`](benchmark-results/live-ink-responsive-tail-2026-08-17/RECEIPT.md),
[`logical SVG gate`](benchmark-results/svg-logical-gate-2026-08-17/RECEIPT.md),
[`NTP`](benchmark-results/v2-ntp-sync-2026-08-17/RECEIPT.md),
[`owner glass capture`](benchmark-results/owner-glass-capture-2026-08-17/RECEIPT.md),
[`fine minimap control`](benchmark-results/minimap-fine-control-2026-08-17/RECEIPT.md),
[`NTP text size`](benchmark-results/ntp-text-size-2026-08-17/RECEIPT.md))

State audited through: [`COLD_COMPUTE_CAMPAIGN_RECEIPT.md`](benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md)

Raster V1 and Vector V2 are supported ESP32 product generations. V2 is feature
complete; known bugs, cleanup, and final performance closure remain.
[`SHIP_CONTRACT.md`](SHIP_CONTRACT.md) owns V2 acceptance thresholds;
[`V2_ROADMAP.md`](V2_ROADMAP.md) is its forward queue.

## Finish-line scorecard

| Area | State | Current evidence / gap |
|---|---|---|
| Pan correctness | **Green — owner accepted** | Product pan is tear-free on glass at 50%, 100%, 200%, and 400%, including dense hairline content. Formal positive-control evidence still needs archiving for the release packet. |
| Pan pacing | **Provisional green** | PANSEQ p95 is 33.939 ms at 100% and 33.934 ms at 400% (~29.5 FPS), below the 38 ms guard. Camera motion caused zero persistent chrome redraws. |
| Ink latency | **Accepted for current scope — future 400% optimization noted** | The five-trace canonical corpus is recorded owner finger input replayed through the production `offer()` path: zero lost Down/Up. The committed overlay closed the lift hitch on glass: 87–199 ms → 10–34 ms ([`RECEIPT.md`](benchmark-results/committed-overlay/RECEIPT.md)). The 0.40 filter no longer constrains the replaceable visual tail: transient geometry reaches the raw clipped touch while operation/SVG authority remains filtered, and lift converges through `InkStream::finish`. Three device batteries passed every ink trace; 400% display p95 is 4.360 ms, a measured +0.419 ms cost for the extra responsive pixels versus the prior 0.40 median ([`responsive-tail receipt`](benchmark-results/live-ink-responsive-tail-2026-08-17/RECEIPT.md)). A controlled physical stroke isolated 190 distinct positions at a 12–14 ms controller cadence (73.758 Hz); product submit averaged 1.527 ms and DMA completion averaged 2.353 ms / maxed at 3.810 ms ([`glass-capture receipt`](benchmark-results/owner-glass-capture-2026-08-17/RECEIPT.md)). On ordinary firmware the owner judged circles, hairlines, long strokes, and thick strokes reasonably fast and asked to leave ink unchanged. Mild diagonal XL-brush lag remains a lower-priority future optimization; formal optical latency also remains open. Sixteenth-world authority still cuts joint_p95 30–40% ([`UNITS16_EXPERIMENT.md`](benchmark-results/settled-aa-prototype/UNITS16_EXPERIMENT.md)). |
| Cold 400% | **Hold-the-line accepted (owner 2026-08-16)** | Stage B walls: 437.9/428.4/488.0 ms at 50/100/200% under the ≤500 line; 400% was 507.0 ms. Owner accepted the residual until autosave exists; the gate holds at 520 ms and the ≤500 requirement still governs the final autosave-enabled 20-run closure. On 2026-08-17, unrelated minimap code layout moved two runs to 524.243/526.063 ms red; the queued 6.3 KiB producer IRAM pin removed that cache-placement lottery and produced a 496.693 ms green wall ([`receipt`](benchmark-results/minimap-navigation-2026-08-17/RECEIPT.md)). **Separate standing red, owner-ruled 2026-08-16:** the `overlap` workload 50% cold gate (8 stacked fat strokes; 628 ms vs 500, red since wave-3, invisible in every prior scorecard) is **binding and will be fixed**, sequenced strictly after the ink lag fix, the AA prototype review, and the déjà-vu fix. Ship-contract process rule 8 now forbids undocumented verdict-vector reds. |
| Revisit retention | **Fixed in harness — glass acceptance pending** | The déjà-vu fix landed 2026-08-16: idle absorption retains resident raw tiles at every zoom and materializes revisit-bound uniforms inside remembered views under a 25 ms idle budget. The mixed_draw revisit gate went from missing 4/9/16 tiles (188–326 ms visible refill per zoom) to **zero missing, ~0.4 ms** ([`DEJAVU_FIX_RECEIPT.md`](benchmark-results/committed-overlay/DEJAVU_FIX_RECEIPT.md)). Live ledger cause deltas now print during glass sessions (`TINYDRAW_LIVE_LEDGER`). Residuals: slot eviction under pressure, XL-stroke off-view budget skips (idle repair covers), high-water fallback path. Pure-revisit tour amplification stays 1.000. Owner glass verdict: "extremely impressed — way better, basically no redraws"; residual stray re-renders tracked as todo #15 (attribute via `TINYDRAW_LIVE_LEDGER` deltas next session). |
| Exactness | **Undo/Redo green — owner glass accepted** | The active/retained operation prefixes are one generation-checked authority. Whole-Stroke Undo/Redo spans adjacent chunks, preserves ≥10 levels, replaces Redo only when new ink publishes, and replays bounded damage transactionally. Mixed pen/eraser tests restore a line cut by an eraser byte-for-byte; producer rebuild and unaffected-tile retention are covered. All 29 host targets, all 11 ASan/UBSan targets, and both credentials-enabled ESP builds pass. Normal firmware was flashed without entering mass storage, and the owner’s cursory glass test confirmed Undo/Redo works. High-zoom affected-region rebuilding after Undo/Redo is visibly brutal; reducing that redraw latency is a binding requirement in the final optimization round. |
| Autosave / recovery | **Green — owner glass accepted** | V2 now journals only vector authority and session state: retained Redo, active prefix, generation/epoch, complete remembered navigation, tool/size/color, and next Stroke identity. Whole-Stroke appends, history, New, and state changes queue immutable generation-checked transactions; a low-priority worker owns aligned 4 KiB erase/write/readback and publishes the final marker last. Host fixtures preserve the prior Recovery point after every possible later-transaction truncation and every single-byte corruption (3,003 journal assertions); the complete 29-target host suite and all 11 ASan/UBSan targets pass. Normal firmware `0x1037c0` was flashed without mass storage. It restored blank checkpoint sequence 1, then restored a real multi-Stroke document at generation 12 with 6 active/10 retained chunks; the owner confirmed drawing, camera, selection, and Redo behavior on glass. A later reboot restored generation 52 with all 14 retained chunks active. Autosave-enabled performance joins the owner-ordered final optimization round. Two-arena compaction/metadata remains deferred; full preserves existing commits and reports capacity. |
| Settled AA | **Green — owner accepted for release scope** | Idle settle republishes tiles at the settled quality tier (revisit-ledger-safe); 25% settles presentation pixels only because the overview remains hard-edged replay authority. Per-tile work is 1.7–5.4 ms mean / 9.3 ms max after the annulus + batching round (was 5–11/17), and the full battery moved zero gates. The owner accepted AA as done for the current release scope on 2026-08-17. Faster progression and cosmetic tuning may return later but are optional rather than blockers. SVG preserves exact continuous ribbon paths; the new PNG streams the same settled-AA derivative used on device. |
| SVG + PNG export | **Physical open/appearance green; instrumentation pending** | `DRAWING.SVG` retains one painter-ordered filled path per physical Stroke, no synthetic background, and byte-identical core generation ([original device receipt](benchmark-results/svg-export-2026-08-17/RECEIPT.md), [stroke grouping](benchmark-results/stroke-svg-minimap-acquire-2026-08-17/RECEIPT.md), [logical gate](benchmark-results/svg-logical-gate-2026-08-17/RECEIPT.md)). `DRAWING.PNG` stitches production settled-AA windows into bounded bands and streams them through PNGenc. A pinned epoch/revision/count and one metadata-last manifest expose only a same-snapshot pair. The owner mounted and opened the physical PNG and judged it good. Known deferred semantic issue: SVG eraser Strokes are white paths rather than transparent cutouts, so they are only correct over white. Exact physical readback, timestamp, memory, timing, and watchdog telemetry remain. |
| USB export exit | **Green — owner glass accepted** | Export has explicit inactive/presenting/host-ejected/stopping state. SCSI eject and TinyUSB unmount latch the medium absent, so later host probes cannot re-expose it. The modal read-only export screen offers **Return to Drawing**, which deinitializes TinyUSB on its owning task and releases the USB PHY without resetting; the owner confirmed the drive disconnects and drawing resumes. Missing glyphs in the new screen were fixed by complete font coverage in `d9b4bb8`; horizontal centering of `COPY YOUR FILES` is deferred cosmetic work. |
| Color dialog | **Green — device gate** | Exact span rasterization plus color-only frame re-presentation cut open time 132.466 → 27.568 ms (4.81×); chrome work fell 82.364 → 9.396 ms (8.77×). A ≤40 ms physical guard and bit-exact snapshot/reference tests are live ([`RECEIPT.md`](benchmark-results/color-dialog-2026-08-17/RECEIPT.md)). |
| Overlay gesture arbitration | **Green — host + device classifier gate** | Zoom In/Out still own taps. With the pan tool active, an 8 px drag starting anywhere on the zoom rail promotes to the existing boundary-drained canvas pan from the original Down point; other tools/popups do not promote ([`RECEIPT.md`](benchmark-results/zoom-overlay-pan-2026-08-17/RECEIPT.md)). |
| Zoom-cycle return | **Green — host + device classifier gate** | Each tiled zoom remembers its explored origin and associated focus. The complete 400→25→50→100→200→400 button route returns exactly to `(2300,3100)`; stale views still yield to a changed focus. The normal 16 KiB product image boots with 6,504 bytes of measured stack margin ([`RECEIPT.md`](benchmark-results/zoom-cycle-return-2026-08-17/RECEIPT.md)). |
| Minimap navigation | **Green — owner accepted** | Absolute-pointer mapping spans the full world at every zoom: direct Down centers immediately, and dragging follows the finger without grabbing the tiny viewport box or applying the rejected 0.25 scale. The owner-preferred original visual position is restored. Bottom-map and dock ambiguity is resolved by intent: stationary presses remain size/document taps, deliberate upward movement promotes at 2 px, horizontal/downward movement promotes at 8 px, and drag candidacy covers the complete right-side dock through `y=448`. The final compact capture recorded 778 events from 4,074 offers with zero overflow or failure marker; dock-started drags promoted and remained captured through `y=441`, while four stationary `y=443..445` taps left the origin unchanged. Owner verdict: “this will do … consider this done”; optional refinement is pinned for the later all-target review ([receipt](benchmark-results/minimap-absolute-pointer-2026-08-17/RECEIPT.md)). |
| RTC / one-shot NTP | **Green — owner accepted** | Document → Clock launches a low-priority asynchronous connect/NTP/RTC-write attempt with modal `CONNECTING`/`SYNCING` and terminal `TIME SET`/`TIME ERROR` feedback. The owner confirmed unavailable-network handling terminates, successful sync reaches `TIME SET`, labels are centered, and the scale-3 text size is good. The NTP-only toast widens symmetrically to fit `CONNECTING`; the focused pixel test passes 16 assertions and the combined product booted `pass=1` ([text-size receipt](benchmark-results/ntp-text-size-2026-08-17/RECEIPT.md), [NTP receipt](benchmark-results/v2-ntp-sync-2026-08-17/RECEIPT.md)). Credentials stay in the ignored local header; timezone is UTC. |
| Feature parity | **Major drawing features green** | Undo/Redo and autosave/recovery are owner-accepted. The existing AXP2101 four-second full shutdown is active and owner-confirmed; explicit hold-triggered autosave flush is only a durability refinement. Remaining work is physical SVG+PNG/eject-return validation, failure UI, later touch refinement, final performance, and release soak. |
| Mixed-draw appends | **Green — harness and glass** | The committed-overlay / authority-revision split landed 2026-08-16: chunk commits publish authority only (worst input-path append **173 µs** vs the 15 ms budget, was 19,324 µs), the canvas drains in receipted idle absorptions behind a pending-ink overlay proven bit-exact on host, and lift defers its refresh to one exact swap after drain. `mixed_draw=1` for the first time; `visible_fallback=0`, drop counters zero, INKTRACE at baseline latency, ledger clean ([`RECEIPT.md`](benchmark-results/committed-overlay/RECEIPT.md)). The product-loop drain paths (idle slices, lift swap, pan boundary drain) and 400% draw feel need an owner glass session. Prior diagnosis receipts: [`ink-fallback-observability/RECEIPT.md`](benchmark-results/ink-fallback-observability/RECEIPT.md). |

Session continuity: Cold Stage B is **closed** and glass-tested (receipt
above); the Stage B session handover is
[`review_findings_2026_08_16_stage_b/HANDOVER.md`](review_findings_2026_08_16_stage_b/HANDOVER.md).
The four post-Stage-B owner decisions are recorded in the ship contract.
Next per the owner-approved queue: the mid-stroke pixelation diagnosis
(fallback observability first), then the committed-overlay /
authority-revision-split design, then AA + resampling host prototypes, the
déjà-vu campaign, and a triage pass over the 2026-08-16 correctness review.
The prior handover context:
(ranked candidates with code receipts, new standing ledger/ink-trace guards,
the post-B queue — AA prototype + resampling, then the déjà-vu campaign — and
pending owner decisions) is
[`review_findings_2026_08_16_oracle_session/HANDOVER.md`](review_findings_2026_08_16_oracle_session/HANDOVER.md).
The wave-3 A/B recipe and device-physics cheat sheet remain authoritative in
[`review_findings_2026_08_16_cold_campaign/HANDOVER.md`](review_findings_2026_08_16_cold_campaign/HANDOVER.md).

Latest permanent receipts:

- [`RECEIPT.md` — Vector V2 autosave/recovery](benchmark-results/v2-autosave-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — NTP toast text size](benchmark-results/ntp-text-size-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — fine 400% minimap control](benchmark-results/minimap-fine-control-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — owner glass capture and capture-only stall diagnosis](benchmark-results/owner-glass-capture-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — logical SVG path gate](benchmark-results/svg-logical-gate-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — responsive 0.40 ink tail](benchmark-results/live-ink-responsive-tail-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — minimap missed-touch input leak](benchmark-results/minimap-input-leak-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — Vector V2 on-demand NTP](benchmark-results/v2-ntp-sync-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — stroke SVG + minimap acquisition](benchmark-results/stroke-svg-minimap-acquire-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — minimap high-zoom touch target](benchmark-results/minimap-touch-target-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — minimap navigation](benchmark-results/minimap-navigation-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — zoom-cycle return](benchmark-results/zoom-cycle-return-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — zoom-overlay pan](benchmark-results/zoom-overlay-pan-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — color dialog](benchmark-results/color-dialog-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — detailed SVG export](benchmark-results/svg-export-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — Cold Stage B](benchmark-results/cold-stage-b-2026-08-16/RECEIPT.md)
- [`COLD_COMPUTE_CAMPAIGN_RECEIPT.md`](benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md)
- [`HARDWARE_LIMITS.md`](HARDWARE_LIMITS.md)
- [`STAGING_INVARIANT_RECEIPT.md`](benchmark-results/wave2-compositor/STAGING_INVARIANT_RECEIPT.md)
- [`CHROME_LIFETIME_RECEIPT.md`](benchmark-results/wave2-compositor/CHROME_LIFETIME_RECEIPT.md)
- [`VISUAL_FIRST_INK_RECEIPT.md`](benchmark-results/wave2-compositor/VISUAL_FIRST_INK_RECEIPT.md)
- [`CHROME_PRESTAGE_RECEIPT.md`](benchmark-results/wave2-compositor/CHROME_PRESTAGE_RECEIPT.md)
- [`CURVED_AUTHORITY_GLASS_RECEIPT.md`](benchmark-results/wave2-compositor/CURVED_AUTHORITY_GLASS_RECEIPT.md)
- [`COLD_SEGMENT_CHUNK_RECEIPT.md`](benchmark-results/wave2-compositor/COLD_SEGMENT_CHUNK_RECEIPT.md)
- [`COLD_RASTER_RECURRENCE_RECEIPT.md`](benchmark-results/wave2-compositor/COLD_RASTER_RECURRENCE_RECEIPT.md)
- [`COLD_PUBLICATION_BATCH_RECEIPT.md`](benchmark-results/wave2-compositor/COLD_PUBLICATION_BATCH_RECEIPT.md)
- [`COLD_GENERAL_BASELINE_RECEIPT.md`](benchmark-results/wave2-compositor/COLD_GENERAL_BASELINE_RECEIPT.md)
- [`GLASS_OBSERVATIONS.md`](benchmark-results/wave2-compositor/GLASS_OBSERVATIONS.md)
- [`gate-invariant-final.log`](benchmark-results/wave2-compositor/gate-invariant-final.log)

## Measured machine

- ESP32-S3 at 240 MHz with 8 MiB PSRAM; CO5300 368×448 RGB565 panel.
- Requested 40/50/60 MHz panel clocks all produce **40 MHz effective** because
  of the GPSPI divider: 10 Mpixel/s, 20 MB/s, 27.2 full-width rows/ms.
- TE period is 16.773 ms; ISR-to-task resume is ~9 µs; register reads provide no
  usable scanline oracle.
- A 448-row edge-synchronized stream sustains 29.4 FPS. A ≤368-row stream
  sustains 58.8 FPS; the one-period boundary is roughly 390–400 rows.
- The product pan sweep covers rows 0–371. Its 13.69 ms payload fits the panel
  envelope; the cache split now catches the two-TE cadence in device PANSEQ.
- The final product allocation uses 448 raw 64×64 tile slots. With the 1.5 MiB
  export reserve held, the final receipt leaves ~306 KiB free and ~303 KiB as
  the largest block. Broad viewport checkpoint caches are therefore unfunded.
- Product keeps a 16 KiB main-task stack; the latest NTP-enabled normal boot
  measured 6,152 bytes free. The monolithic diagnostic battery alone uses a 20 KiB
  gate-only stack and retained 2,136 bytes after the latest full battery.
- The bounded 6.3 KiB tile-producer text object is IRAM-pinned. This costs
  5,632 bytes of free internal memory in the gate build (290,860 remain) and
  removed a repeatable 524–526 ms flash-layout cold regression; the treated
  400% wall is 496.693 ms
  ([receipt](benchmark-results/minimap-navigation-2026-08-17/RECEIPT.md)).

## Current architecture

```text
blank baseline + ordered vector operations (durable authority)
        │
        ├── complete 368×448 overview at 25%
        └── sparse world-aligned materialization at 50–400%
              ├── compact paper/uniform identities
              └── 448 raw 64×64 tiles
                    │
             canvas-only toroidal frame ring
                    │
             internal DMA staging + transient chrome/ink
                    │
                  CO5300 panel
```

Committed geometry is a 1472×1792 world at 25%, 50%, 100%, 200%, and
400%. Vector operations are the complete V2 drawing authority. Overviews,
tiles, chrome, previews, settled output, and export buffers are derived or
transient.

Raster V1 documents remain Raster V1. They are never silently restored as a
raster baseline with an empty V2 operation log. The policy and implications for
Undo, persistence, and SVG are frozen in
[`SHIP_CONTRACT.md`](SHIP_CONTRACT.md#document-authority-policy).

## Immediate work order

The owner locked the feature-finishing order on 2026-08-17. Do not pull the
remaining performance campaign ahead of unfinished product features:

1. **Done 2026-08-17:** build a non-perturbing exact minimap capture path,
   capture the rejected bottom-right→center interaction, and fix the minimap
   with absolute-pointer behavior. The final full-dock arbitration passed host,
   exact capture, and owner glass acceptance.
2. **Done 2026-08-17:** the authority spine and whole-Stroke Undo/Redo are
   host-, sanitizer-, compile-, and owner-glass-green. High-zoom cold rebuilding
   after Undo is deferred to the final optimization round.
3. **Done 2026-08-17:** versioned authority journal, background flash writes,
   exact recovery, state restoration, export-time flush, and real drawn-document
   reset/Redo recovery are host-, sanitizer-, firmware-, and owner-glass-green.
   Autosave-enabled performance joins the final optimization round. Two-arena
   compaction/metadata is explicitly deferred by owner direction.
4. Review and physically validate every touch target and overlap.
5. **Done 2026-08-17 (software):** add a settled anti-aliased PNG alongside
   the editable path-based SVG; physical pair validation remains in release
   integration.
6. **Implemented; physical gate pending:** fix the USB export-mode exit wedge
   with latched host eject and an on-device **Return to Drawing** action.
7. Run one final optimization round covering settled-AA progression and the
   binding overlap workload's 50% cold stroke (628 ms vs 500), then call the
   implementation complete and enter release closure.

NTP and functional settled AA are accepted. Capacity/failure polish is not in
this ordered blocker list, and destructive power-interruption testing is not an
owner priority; autosave's deterministic recovery fixtures remain part of the
autosave feature itself.

## Proven foundation worth preserving

- Exact pen/eraser painter order and transactional incremental publication.
- Complete overview fallback with no checkerboards.
- World-aligned cache identities, paper-aware materialization, and bounded tile
  production with stale-work cancellation.
- Canvas-only toroidal reuse and one ordered row-zero presentation sweep.
- Independent touch sampling with transition-preserving Down/Up behavior.
- Exact variable-width SVG core with renderer-raster fidelity tests.
- Direct transactional SVG-on-flash export: one filled path per physical Stroke,
  4 KiB workspace, complete device readback gate, and read-only `DRAWING.SVG`
  FAT wiring. Prior PNG/USB evidence and the 1.5 MiB reserve remain archived.
- Production toolbar, two PICO-8 palettes, zoom rail, battery, confirmation UI,
  on-demand RTC/NTP feedback, and an interactive minimap with tap-to-jump and
  captured viewport dragging.
- TinyDraw V1, TinyDraw V2, V2 gate, QEMU, and focused hardware-diagnostic variants.

Foundation receipts and architectural history live in
[`vector_v2/README.md`](vector_v2/README.md),
[`vector_v2/hardware-receipts/`](vector_v2/hardware-receipts/), and
[`docs/archive/`](docs/archive/). Superseded results remain valuable evidence,
but do not override this scorecard or the frozen contract.

## Guardrails and validation

- No rewrite, camera-aligned atlas, hidden V2 allocation, or speculative
  second-core concurrency.
- Keep vector product state out of legacy core types such as `WorldCanvas` and
  `FirmwareCanvas`. Share stable platform-neutral mechanisms through narrow dependencies.
- Every cache needs a byte budget, identity, invalidation owner, reuse receipt,
  exactness oracle, and removal condition.
- One measured hot-path hypothesis per change. A shared-path change reopens its
  dependent gates as defined in the roadmap.
- Glass is authoritative for visible correctness and feel; software receipts
  provide attribution.
- Deferred structural debt: `vector_v2_app.cpp` is over 1,300 lines. Split its
  interaction, authority, and lifecycle coordinators after hot-path closure;
  do not mix that refactor into the performance campaign.

Host validation:

```sh
./scripts/dev test
./scripts/dev release
./scripts/dev asan
./scripts/dev format-check
./scripts/dev tidy
./scripts/dev cppcheck
git diff --check
```

ESP integration must build V1, V2, and the V2 gate variant. V2 release closure also
requires the relevant focused diagnostics, physical optical/ink checks,
interrupted-write tests, and a long-session soak.
