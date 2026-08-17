# TinyDraw project state

Last updated: 2026-08-17 (stroke-logical SVG export, faster color dialog,
zoom-overlay pan promotion, exact zoom-cycle return, direct-acquisition
minimap navigation, and on-demand V2 NTP UI landed; receipts:
[`SVG`](benchmark-results/svg-export-2026-08-17/RECEIPT.md),
[`color dialog`](benchmark-results/color-dialog-2026-08-17/RECEIPT.md),
[`zoom-overlay pan`](benchmark-results/zoom-overlay-pan-2026-08-17/RECEIPT.md),
[`zoom-cycle return`](benchmark-results/zoom-cycle-return-2026-08-17/RECEIPT.md),
[`minimap navigation`](benchmark-results/minimap-navigation-2026-08-17/RECEIPT.md),
[`responsive 0.40 ink`](benchmark-results/live-ink-responsive-tail-2026-08-17/RECEIPT.md),
[`logical SVG gate`](benchmark-results/svg-logical-gate-2026-08-17/RECEIPT.md),
[`NTP`](benchmark-results/v2-ntp-sync-2026-08-17/RECEIPT.md))

Branch: `feat/v2-performance-followup`

State audited through: [`COLD_COMPUTE_CAMPAIGN_RECEIPT.md`](benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md)

Raster V1 remains the default firmware and operational fallback. Vector V2 is
the accepted product architecture, but it is not feature complete or ready for
promotion. [`SHIP_CONTRACT.md`](SHIP_CONTRACT.md) owns acceptance thresholds;
[`V2_ROADMAP.md`](V2_ROADMAP.md) is the only forward queue.

## Finish-line scorecard

| Area | State | Current evidence / gap |
|---|---|---|
| Pan correctness | **Green — owner accepted** | Product pan is tear-free on glass at 50%, 100%, 200%, and 400%, including dense hairline content. Formal positive-control evidence still needs archiving for the release packet. |
| Pan pacing | **Provisional green** | PANSEQ p95 is 33.939 ms at 100% and 33.934 ms at 400% (~29.5 FPS), below the 38 ms guard. Camera motion caused zero persistent chrome redraws. |
| Ink latency | **Provisional green — responsive 0.40 tail flashed; glass pending** | The five-trace canonical corpus is recorded owner finger input replayed through the production `offer()` path: zero lost Down/Up. The committed overlay closed the lift hitch on glass: 87–199 ms → 10–34 ms ([`RECEIPT.md`](benchmark-results/committed-overlay/RECEIPT.md)). The 0.40 filter no longer constrains the replaceable visual tail: transient geometry reaches the raw clipped touch while operation/SVG authority remains filtered, and lift converges through `InkStream::finish`. Three device batteries passed every ink trace; 400% display p95 is 4.360 ms, a measured +0.419 ms cost for the extra responsive pixels versus the prior 0.40 median ([`responsive-tail receipt`](benchmark-results/live-ink-responsive-tail-2026-08-17/RECEIPT.md)). Sixteenth-world authority still cuts joint_p95 30–40% ([`UNITS16_EXPERIMENT.md`](benchmark-results/settled-aa-prototype/UNITS16_EXPERIMENT.md)). Owner glass feel and formal optical latency remain open. |
| Cold 400% | **Hold-the-line accepted (owner 2026-08-16)** | Stage B walls: 437.9/428.4/488.0 ms at 50/100/200% under the ≤500 line; 400% was 507.0 ms. Owner accepted the residual until autosave exists; the gate holds at 520 ms and the ≤500 requirement still governs the final autosave-enabled 20-run closure. On 2026-08-17, unrelated minimap code layout moved two runs to 524.243/526.063 ms red; the queued 6.3 KiB producer IRAM pin removed that cache-placement lottery and produced a 496.693 ms green wall ([`receipt`](benchmark-results/minimap-navigation-2026-08-17/RECEIPT.md)). **Separate standing red, owner-ruled 2026-08-16:** the `overlap` workload 50% cold gate (8 stacked fat strokes; 628 ms vs 500, red since wave-3, invisible in every prior scorecard) is **binding and will be fixed**, sequenced strictly after the ink lag fix, the AA prototype review, and the déjà-vu fix. Ship-contract process rule 8 now forbids undocumented verdict-vector reds. |
| Revisit retention | **Fixed in harness — glass acceptance pending** | The déjà-vu fix landed 2026-08-16: idle absorption retains resident raw tiles at every zoom and materializes revisit-bound uniforms inside remembered views under a 25 ms idle budget. The mixed_draw revisit gate went from missing 4/9/16 tiles (188–326 ms visible refill per zoom) to **zero missing, ~0.4 ms** ([`DEJAVU_FIX_RECEIPT.md`](benchmark-results/committed-overlay/DEJAVU_FIX_RECEIPT.md)). Live ledger cause deltas now print during glass sessions (`TINYDRAW_LIVE_LEDGER`). Residuals: slot eviction under pressure, XL-stroke off-view budget skips (idle repair covers), high-water fallback path. Pure-revisit tour amplification stays 1.000. Owner glass verdict: "extremely impressed — way better, basically no redraws"; residual stray re-renders tracked as todo #15 (attribute via `TINYDRAW_LIVE_LEDGER` deltas next session). |
| Exactness | **Green for implemented scope** | Host exactness and fuzz tests pass. V2 persistence/Undo authority is not implemented. |
| Settled AA | **On device — provisional, speed round 2 queued** | Landed same-day from prototype to device: idle settle pass republishes tiles at the settled quality tier (revisit-ledger-safe); 25% settles presentation pixels only (the overview stays hard-edged replay authority). Per-tile 1.7–5.4 ms mean / 9.3 ms max after the annulus + batching round (was 5–11/17). Owner: "an improvement" but the settle progression is still perceptible, thick strokes slowest; speed ideas queued. Full battery moved zero gates. SVG exports exact ribbon outlines; settled AA remains a presentation derivative rather than export authority. |
| SVG export | **Logical paths green; mounted timestamp follow-up pending** | The owner opened a physical `DRAWING.SVG` in Inkscape and confirmed the corrected paths look good. Export streams one painter-ordered filled path per physical Stroke and omits the synthetic background rectangle without document-sized storage. The physical readback gate now validates this contract rather than comparing paths with internal chunks: 52 chunks grouped into one path, `path_only=1 pass=1` ([original device receipt](benchmark-results/svg-export-2026-08-17/RECEIPT.md), [stroke grouping](benchmark-results/stroke-svg-minimap-acquire-2026-08-17/RECEIPT.md), [logical gate](benchmark-results/svg-logical-gate-2026-08-17/RECEIPT.md)). RTC-derived FAT modification time is implemented; a fresh host-mounted timestamp check remains. |
| Color dialog | **Green — device gate** | Exact span rasterization plus color-only frame re-presentation cut open time 132.466 → 27.568 ms (4.81×); chrome work fell 82.364 → 9.396 ms (8.77×). A ≤40 ms physical guard and bit-exact snapshot/reference tests are live ([`RECEIPT.md`](benchmark-results/color-dialog-2026-08-17/RECEIPT.md)). |
| Overlay gesture arbitration | **Green — host + device classifier gate** | Zoom In/Out still own taps. With the pan tool active, an 8 px drag starting anywhere on the zoom rail promotes to the existing boundary-drained canvas pan from the original Down point; other tools/popups do not promote ([`RECEIPT.md`](benchmark-results/zoom-overlay-pan-2026-08-17/RECEIPT.md)). |
| Zoom-cycle return | **Green — host + device classifier gate** | Each tiled zoom remembers its explored origin and associated focus. The complete 400→25→50→100→200→400 button route returns exactly to `(2300,3100)`; stale views still yield to a changed focus. The normal 16 KiB product image boots with 6,504 bytes of measured stack margin ([`RECEIPT.md`](benchmark-results/zoom-cycle-return-2026-08-17/RECEIPT.md)). |
| Minimap navigation | **Input-leak guard built; combined glass follow-up pending** | The owner correctly linked harder 400% targeting with one blue and overlapping yellow short marks. The supplied SVG proves those marks are committed paths, not AA/export decoration: exact-frame misses fell through to drawing. A no-draw guard now extends around the frame to panel edges/toolbar, while viewport grab targets scale 28/36/44 px at ≤100/200/400%; outside-box starts still directly acquire. Intent remains 4/3/2 px ([navigation receipt](benchmark-results/minimap-navigation-2026-08-17/RECEIPT.md), [acquisition receipt](benchmark-results/stroke-svg-minimap-acquire-2026-08-17/RECEIPT.md), [input-leak receipt](benchmark-results/minimap-input-leak-2026-08-17/RECEIPT.md)). |
| RTC / one-shot NTP | **Network success observed; combined UI follow-up pending** | Document → Clock launches a low-priority asynchronous connect/NTP/RTC-write attempt with modal `CONNECTING`/`SYNCING` and terminal `TIME SET`/`TIME ERROR` feedback. The owner observed a successful sequence through `TIME SET`; subsequent boot retains the synchronized RTC. Missing glyphs, centered labels, three-second terminal expiry, and startup retry are now in the combined product. Credentials stay in the ignored local header; timezone is UTC. A serial-captured second network attempt plus final toast-centering glass check remains ([`RECEIPT.md`](benchmark-results/v2-ntp-sync-2026-08-17/RECEIPT.md)). |
| Feature parity | **Open** | Undo/Redo, autosave/recovery, power lifecycle parity, remaining failure UI, and release soak remain. |
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

1. **Overlap-50 cold fix** (#10): 628 ms vs 500; overdraw replay pays every
   stroke's row geometry even when occluded.
2. **AA speed round 2**: make settle progression imperceptible, especially on
   thick strokes; candidates remain in the overnight handover.
3. Establish generation-checked active-prefix history, then Undo/Redo and
   autosave/recovery. Cold stays hold-the-line until the autosave-enabled
   re-measure; the 20-run closure statistic waits there too.
4. Then power/lifecycle parity, capacity/failure UI, physical NTP and USB SVG
   receipts, and all-on release closure.

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
- Separate Raster V1 and Vector V2 firmware targets.

Foundation receipts and architectural history live in
[`vector_v2/README.md`](vector_v2/README.md),
[`vector_v2/hardware-receipts/`](vector_v2/hardware-receipts/), and
[`docs/archive/`](docs/archive/). Superseded results remain valuable evidence,
but do not override this scorecard or the frozen contract.

## Guardrails and validation

- No rewrite, camera-aligned atlas, hidden V2 allocation, or speculative
  second-core concurrency.
- Keep V2 state out of `WorldCanvas`, `FirmwareCanvas`, and the V1 interaction
  loop. Share stable platform-neutral mechanisms through narrow dependencies.
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

ESP integration must build both firmware variants. Final promotion additionally
requires the gate harness, physical optical/ink checks, interrupted-write tests,
long-session soak, and explicit V1/V2 parity review.
