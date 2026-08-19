# TinyDraw project state

Last updated: 2026-08-18 late night. Post-handover AA round: settled-AA
exterior-capsule row narrowing is device-accepted on a same-image per-policy
long-chord A/B (−24…−64% at 25–400%, byte-exact;
[`receipt`](benchmark-results/settled-edge-spans-2026-08-18/RECEIPT.md),
[`design`](docs/design/VECTOR_V2_SETTLED_EDGE_SPANS.md)); the work-charge
recalibration and saturated-source-skip levers are receipted no-gos. The dense
short-chord document is unmoved — real-document 50–200% walls remain the open
target (owner glass is the arbiter; H7-style row sweep or persisted spans are
the staged successors). Earlier the same day, the final performance round
landed:
COW preserved-tile Undo/Redo swaps (revisit repair 338,998→229 µs), a 604-slot
pool with the fictional 1.5 MiB export reserve retired, fully colonized 16 MiB
flash (10.125 MiB export volume), deterministic `history_latency` and
`settle_timing` battery gates, and three exact settled-AA treatments
(evil-corpus 400% settle 1,765→1,010 ms). See
[`docs/HANDOVER_2026_08_18_FINAL_ROUND.md`](docs/HANDOVER_2026_08_18_FINAL_ROUND.md)
for the complete session ledger, receipts, and the owner-fixed remaining
queue: faster AA → touch targets → byte-swap hunt → SVG/PNG parity → done.
Earlier same-day state (still valid where not superseded above): cleanup plus
stroke-logical SVG
export, settled-AA PNG export,
faster color dialog, zoom-overlay pan promotion, focus-centered zoom continuity,
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
[`NTP text size`](benchmark-results/ntp-text-size-2026-08-17/RECEIPT.md),
[`overlap cold closure`](benchmark-results/overlap-cold-fix-2026-08-17/RECEIPT.md))

State audited through:
[`feature-complete cleanup receipt`](benchmark-results/v2-cleanup-final-2026-08-17/RECEIPT.md)
and the later [`overlap cold closure`](benchmark-results/overlap-cold-fix-2026-08-17/RECEIPT.md).
The application-module split passed the current 448-slot physical gate and a
normal-product boot. The authority-only journal still needs a same-head
physical recovery receipt.

Raster V1 and Vector V2 are supported ESP32 product generations. V2 is feature
complete; cleanup is closed, and known bugs plus final performance closure
remain.
[`SHIP_CONTRACT.md`](SHIP_CONTRACT.md) owns V2 acceptance thresholds;
[`V2_ROADMAP.md`](V2_ROADMAP.md) is its forward queue.

## Finish-line scorecard

| Area | State | Current evidence / gap |
|---|---|---|
| Pan correctness | **Green — owner accepted** | Product pan is tear-free on glass at 50%, 100%, 200%, and 400%, including dense hairline content. Formal positive-control evidence still needs archiving for the release packet. |
| Pan pacing | **Provisional green** | PANSEQ p95 is 33.939 ms at 100% and 33.934 ms at 400% (~29.5 FPS), below the 38 ms guard. Camera motion caused zero persistent chrome redraws. |
| Ink latency | **Accepted for current scope — future 400% optimization noted** | The five-trace canonical corpus is recorded owner finger input replayed through the production `offer()` path: zero lost Down/Up. The committed overlay closed the lift hitch on glass: 87–199 ms → 10–34 ms ([`RECEIPT.md`](benchmark-results/committed-overlay/RECEIPT.md)). The 0.40 filter no longer constrains the replaceable visual tail: transient geometry reaches the raw clipped touch while operation/SVG authority remains filtered, and lift converges through `InkStream::finish`. Three device batteries passed every ink trace; 400% display p95 is 4.360 ms, a measured +0.419 ms cost for the extra responsive pixels versus the prior 0.40 median ([`responsive-tail receipt`](benchmark-results/live-ink-responsive-tail-2026-08-17/RECEIPT.md)). A controlled physical stroke isolated 190 distinct positions at a 12–14 ms controller cadence (73.758 Hz); product submit averaged 1.527 ms and DMA completion averaged 2.353 ms / maxed at 3.810 ms ([`glass-capture receipt`](benchmark-results/owner-glass-capture-2026-08-17/RECEIPT.md)). On ordinary firmware the owner judged circles, hairlines, long strokes, and thick strokes reasonably fast and asked to leave ink unchanged. Mild diagonal XL-brush lag remains a lower-priority future optimization; formal optical latency also remains open. Sixteenth-world authority still cuts joint_p95 30–40% ([`UNITS16_EXPERIMENT.md`](benchmark-results/settled-aa-prototype/UNITS16_EXPERIMENT.md)). |
| Cold rendering | **Development gates green; release closure open** | The formerly binding `overlap` 50% workload fell from 585.821 ms to 476.969 ms after refreshing each overlapping chord's finalized-pixel window; compute fell to 384.393 ms and the full gate verdict passed ([`receipt`](benchmark-results/overlap-cold-fix-2026-08-17/RECEIPT.md)). General cold measured 421.787/399.498/464.071/515.123 ms at 50/100/200/400%. The last value is inside the owner-accepted 520 ms development guard but not the contract's ≤500 ms release line. The gate initializes/restores the autosave service but runs before the product loop, so it does not measure concurrent journal writes. A 20-run reset-separated normal-product closure with real journal writes remains open. |
| Revisit retention | **Owner accepted; residual attribution open** | The déjà-vu fix landed 2026-08-16: idle absorption retains resident raw tiles at every zoom and materializes revisit-bound uniforms inside recent views under a 25 ms idle budget. The mixed_draw revisit gate went from missing 4/9/16 tiles (188–326 ms visible refill per zoom) to **zero missing, ~0.4 ms** ([`DEJAVU_FIX_RECEIPT.md`](benchmark-results/committed-overlay/DEJAVU_FIX_RECEIPT.md)). The gate-only `TINYDRAW_LIVE_LEDGER` attributes cold/damage/evict/stale/unexplained deltas; it is not present in ordinary product firmware. Pure-revisit amplification is 1.000 with zero unexplained renders. Owner verdict: "extremely impressed — way better, basically no redraws." Residual slot eviction, off-view budget skips, high-water fallback, and stray glass re-renders still need a gate-build attribution session. |
| Exactness | **Undo/Redo green — owner glass accepted** | The active/retained operation prefixes are one generation-checked authority. Whole-Stroke Undo/Redo spans adjacent chunks, preserves ≥10 levels, replaces Redo only when new ink publishes, and replays bounded damage transactionally. Mixed pen/eraser tests restore a line cut by an eraser byte-for-byte; producer rebuild and unaffected-tile retention are covered. All 31 host targets and all 13 ASan/UBSan targets pass after the cleanup split. Normal firmware was flashed without entering mass storage, and the owner’s cursory glass test confirmed Undo/Redo works. The 2026-08-18 evening round closed the binding latency requirement: whole-Stroke history now swaps preserved tile pre-images (`commit_history_revision`, validity via `OperationLog::history_timeline`), with hold-back single-swap presentation and an always-on hourglass. Revisited states: repair 229 µs, total ≤116 ms at 400%; first visits rebuild once behind the hourglass. Owner glass-accepted. |
| Autosave / recovery | **Authority-only contract implemented; current-head physical recheck pending** | By owner decision on 2026-08-17, V2 journals only vector authority: retained Redo, active prefix, generation, and epoch. Navigation and chrome restart from defaults; the next Stroke identity is derived from restored active authority. The earlier owner glass receipt exercised the superseded session-state schema. Current host fixtures preserve the prior Recovery point after truncation and corruption, and the complete 31-target host suite and all 13 ASan/UBSan targets pass. Two-arena compaction remains deferred; a full journal preserves existing commits and reports capacity. |
| Settled AA | **Functional appearance accepted; progression gate open** | Idle settle republishes tiles at the settled quality tier (revisit-ledger-safe); 25% settles presentation pixels only because the overview remains hard-edged replay authority. Earlier tiled work measured 1.7–5.4 ms mean / 9.3 ms max after the annulus + batching round, but that is not a current universal bound. The current 25% gate settled 42 tiles in 152.945 ms and reached 76.416 ms for one tile, breaking the nominal 8 ms cooperative slice. The owner accepted appearance and functional behavior on 2026-08-17 and closed the performance campaign on 2026-08-19. SVG preserves exact shared ribbon boundaries; PNG streams the settled derivative used on device. |
| SVG + PNG export | **Parity and appearance fixes host-green** | `DRAWING.SVG` retains one painter-ordered filled path per physical Stroke, no synthetic background, and byte-identical core generation ([original device receipt](benchmark-results/svg-export-2026-08-17/RECEIPT.md), [stroke grouping](benchmark-results/stroke-svg-minimap-acquire-2026-08-17/RECEIPT.md), [logical gate](benchmark-results/svg-logical-gate-2026-08-17/RECEIPT.md)). `DRAWING.PNG` stitches production settled-AA windows into bounded bands and streams them through PNGenc. A pinned epoch/revision/count and one metadata-last manifest expose only a same-snapshot pair. One-sample contacts now render consistently on screen, in hard replay, PNG, and SVG; SVG erasers use transparent painter-ordered masks; physical-gesture chunks share one curve stream. The extreme-zoom teeth found on 2026-08-19 were traced to the raster authority's 0.75 px seam overlap and removed from SVG by using exact shared span boundaries while retaining overlap for screen/PNG crack prevention ([receipt](docs/receipts/vector-v2/SVG_EXTREME_ZOOM_EDGE_TEETH_2026_08_19.md)). Final acceptance is an owner re-export on fixed firmware. |
| USB export exit | **Green — physical eject/serial loop verified** | SCSI eject latches the medium absent, tears down TinyUSB on its owning task, reacquires the shared internal PHY for USB Serial/JTAG, and returns to drawing automatically. Two macOS-eject cycles returned serial without screen interaction (581 ms, then the second 500 ms poll on the final image); explicit **Return to Drawing** also works. Export labels have complete glyph coverage and exact horizontal-centering regressions. Popup input is confined to its active layer, so Document actions cannot hit the pencil control below ([follow-up receipt](docs/receipts/vector-v2/USB_EXPORT_EXIT_AND_CHROME_FOLLOWUP_2026_08_19.md)). |
| Color dialog | **Green — device gate** | Exact span rasterization plus color-only frame re-presentation cut open time 132.466 → 27.568 ms (4.81×); chrome work fell 82.364 → 9.396 ms (8.77×). A ≤40 ms physical guard and bit-exact snapshot/reference tests are live ([`RECEIPT.md`](benchmark-results/color-dialog-2026-08-17/RECEIPT.md)). |
| Overlay gesture arbitration | **Green — host + device classifier gate** | Zoom In/Out still own taps. With the pan tool active, an 8 px drag starting anywhere on the zoom rail promotes to the existing boundary-drained canvas pan from the original Down point; other tools/popups do not promote ([`RECEIPT.md`](benchmark-results/zoom-overlay-pan-2026-08-17/RECEIPT.md)). |
| Zoom-cycle continuity | **Green — focus-centered model** | By owner decision on 2026-08-17, navigation stores one current origin and one world-space focus; every zoom derives and clamps its origin from that focus. Dormant per-zoom origins were removed during cleanup. The complete button cycle preserves explored focus within four quarter-world units. The older exact-origin receipt remains historical evidence for the superseded model ([`RECEIPT.md`](benchmark-results/zoom-cycle-return-2026-08-17/RECEIPT.md)). |
| Minimap navigation | **Green — owner accepted** | Absolute-pointer mapping spans the full world at every zoom: direct Down centers immediately, and dragging follows the finger without grabbing the tiny viewport box or applying the rejected 0.25 scale. The owner-preferred original visual position is restored. Bottom-map and dock ambiguity is resolved by intent: stationary presses remain size/document taps, deliberate upward movement promotes at 2 px, horizontal/downward movement promotes at 8 px, and drag candidacy covers the complete right-side dock through `y=448`. The final compact capture recorded 778 events from 4,074 offers with zero overflow or failure marker; dock-started drags promoted and remained captured through `y=441`, while four stationary `y=443..445` taps left the origin unchanged. Owner verdict: “this will do … consider this done”; optional refinement is pinned for the later all-target review ([receipt](benchmark-results/minimap-absolute-pointer-2026-08-17/RECEIPT.md)). |
| RTC / one-shot NTP | **Green — owner accepted** | Document → Clock launches a low-priority asynchronous connect/NTP/RTC-write attempt with modal `CONNECTING`/`SYNCING` and terminal `TIME SET`/`TIME ERROR` feedback. The owner confirmed unavailable-network handling terminates, successful sync reaches `TIME SET`, labels are centered, and the scale-3 text size is good. The NTP-only toast widens symmetrically to fit `CONNECTING`; the focused pixel test passes 16 assertions and the combined product booted `pass=1` ([text-size receipt](benchmark-results/ntp-text-size-2026-08-17/RECEIPT.md), [NTP receipt](benchmark-results/v2-ntp-sync-2026-08-17/RECEIPT.md)). Credentials stay in the ignored local header; timezone is UTC. |
| Feature parity | **Major drawing features green** | Undo/Redo is owner-accepted; authority-only autosave needs a current-head physical recovery recheck. The existing AXP2101 four-second full shutdown is active and owner-confirmed; explicit hold-triggered autosave flush is only a durability refinement. Remaining work is a combined SVG+PNG/eject-return receipt, failure UI, later touch refinement, final performance, and release soak. |
| Mixed-draw appends | **Green — harness and glass** | The committed-overlay / authority-revision split landed 2026-08-16: chunk commits publish authority only (worst input-path append **173 µs** vs the 15 ms budget, was 19,324 µs), the canvas drains in receipted idle absorptions behind a pending-ink overlay proven bit-exact on host, and lift defers its refresh to one exact swap after drain. `mixed_draw=1` for the first time; `visible_fallback=0`, drop counters zero, INKTRACE at baseline latency, ledger clean ([`RECEIPT.md`](benchmark-results/committed-overlay/RECEIPT.md)). Ordinary-firmware glass feel was later owner-accepted. The remaining sliceable stall is the 166–184 ms one-shot full-frame refresh class; band-sliced refreshes with input polls remain open. Prior diagnosis: [`ink-fallback-observability/RECEIPT.md`](benchmark-results/ink-fallback-observability/RECEIPT.md). |

Session continuity: Cold Stage B, committed overlay, responsive raw-tip ink,
settled AA, and the binding overlap workload are closed development milestones.
The current queue is high-zoom Undo/Redo reconstruction, faster AA progression,
residual revisit attribution, band-sliced full-frame refreshes,
known product bugs, and same-head release validation. Historical A/B recipes,
rejected experiments, and the device-physics cheat sheet remain in
[`HANDOVER.md`](docs/archive/2026-08-code-reviews/review-findings/2026-08-16-cold-campaign/HANDOVER.md)
and [`docs/PERFORMANCE_CHRONICLE.md`](docs/PERFORMANCE_CHRONICLE.md).

Latest permanent receipts:

- [`RECEIPT.md` — overlap cold closure](benchmark-results/overlap-cold-fix-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — feature-complete cleanup](benchmark-results/v2-cleanup-final-2026-08-17/RECEIPT.md)
- [`RECEIPT.md` — original session-state autosave/recovery hardware receipt (superseded schema)](benchmark-results/v2-autosave-2026-08-17/RECEIPT.md)
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
- [`RECEIPT.md` — historical exact-origin zoom-cycle model](benchmark-results/zoom-cycle-return-2026-08-17/RECEIPT.md)
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
- [`gate-invariant-final.log`](https://github.com/aliceisjustplaying/tinydraw/blob/v2-feature-complete-pre-cleanup/benchmark-results/wave2-compositor/gate-invariant-final.log)

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
- The final product allocation uses 448 raw 64×64 tile slots. In the latest
  gate, holding the 1.5 MiB export reserve left 709,256 bytes free and a
  704,512-byte largest PSRAM block. Broad viewport checkpoint caches remain
  unfunded.
- Product keeps a 16 KiB main-task stack; the latest normal-product boot
  measured 8,712 bytes free. The diagnostic gate uses a 20 KiB stack and
  retained 6,248 bytes after the latest 448-slot battery.
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

Feature implementation and the feature-complete cleanup are closed. The
remaining queue is release validation and the final measured optimization round:

1. **Done 2026-08-17:** build a non-perturbing exact minimap capture path,
   capture the rejected bottom-right→center interaction, and fix the minimap
   with absolute-pointer behavior. The final full-dock arbitration passed host,
   exact capture, and owner glass acceptance.
2. **Done 2026-08-17:** the authority spine and whole-Stroke Undo/Redo are
   host-, sanitizer-, compile-, and owner-glass-green. High-zoom cold rebuilding
   after Undo is deferred to the final optimization round.
3. **Implemented; current-head physical recheck pending:** the authority-only
   journal, background flash writes, exact recovery, export-time flush, and
   derived next-Stroke identity are host- and sanitizer-green. The older physical
   reset/Redo receipt covered the superseded session-state schema. Two-arena
   compaction/metadata is explicitly deferred by owner direction.
4. Review and physically validate every touch target and overlap.
5. **Done 2026-08-17 (software):** add a settled anti-aliased PNG alongside
   the editable path-based SVG; physical pair validation remains in release
   integration.
6. **Done 2026-08-19:** host eject remains latched, automatically returns to
   drawing, and reacquires USB Serial/JTAG without reset. Explicit **Return to
   Drawing** follows the same verified teardown path.
7. **Done 2026-08-17:** close the binding overlap 50% cold gate. The complete
   device battery passed at 476.969 ms; settled-AA progression remains open
   performance work, while high-zoom Undo/Redo latency remains the observed blocker and
   still needs a deterministic timing baseline.

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
[`docs/receipts/vector-v2/`](docs/receipts/vector-v2/), and
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
- The application split is landed: storage, diagnostics, background work,
  chrome actions, and live-stroke coordination have explicit modules;
  `vector_v2_app.cpp` is 831 lines and uses one interaction mode. Product-only
  diagnostic reservations are gone, reclaiming 357,264 fixed PSRAM bytes.
  Preserve these boundaries during the final performance round and validate
  the split on the same release head.

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
