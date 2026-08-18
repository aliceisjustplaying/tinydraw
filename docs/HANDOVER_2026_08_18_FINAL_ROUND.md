# Handover — final performance round, 2026-08-18 evening session

For the next agent. Owner intent: finish the queue below, then the project
is called. Priority order is owner-fixed: (1) faster anti-aliasing where
possible, (2) touch targets, (3) the byte-swap hunt, (4) SVG/PNG export
parity and angularity. Read `PROJECT_STATE.md` and
`docs/PERFORMANCE_CHRONICLE.md` first; this file covers only what this
session changed and where to pick up.

## What landed (commit chain `fe4c39a..b18eb2b`, all battery all-ones)

| Change | Result | Receipt |
|---|---|---|
| Deterministic Undo/Redo gate (`history_latency`), evil-hairline corpus, per-policy A/B | first-ever device history timing | `benchmark-results/history-latency-2026-08-18/RECEIPT.md` |
| History damage hold-back + always-on hourglass + merged rapid taps | 2 visible transitions instead of ~44; repair tick 13→6.3 ms | same receipt |
| **COW preserved-tile swap** (`commit_history_revision`, `history_timeline`) | revisit repair 338,998→**229 µs**; total 440→116 ms; byte-exact oracle incl. branch-aliasing guard | same receipt + `541e4a5` |
| 604-slot pool; 1.5 MiB reserve retired; honest sequential envelope gate | zero cold cost (A/B), autosave line intact | `benchmark-results/export-memory-math-2026-08-18/RECEIPT.md` |
| Flash colonized: 1792K app / 4M journal / 10.125M export, unified variant offsets, FAT disk 20,736 blocks | worst-case ~7.3 MiB SVG now fits | `e9abacd` |
| `settle_timing` gate: deterministic all-zoom settled-AA walls in the battery | closes the 50–400% coverage gap | `efff39b` |
| AA: no_ink white fast path + internal planes + **saturated-destination skip** (re-opened, prior rejection compared unlike corpora) | evil-corpus 400% settle 1,765→**1,010 ms** (−43%); real-doc 400% 429–552 ms | receipts in history-latency dir |
| `docs/MEMORY_MAP.md` | PSRAM/SRAM/flash ledger + cache-balance policy | in tree |
| Incidents recorded | popup/undo tap overlap (2nd occurrence); color-popup byte-swap (photo archived) | `docs/receipts/vector-v2/` |

Current head is flashed on the device as product firmware. Host 31/31,
ASan 13/13 throughout.

## 1. Anti-aliasing (the remaining performance work)

State: real-document full-pass settle is 429–552 ms at 400% (at/under the
500 ms class) but 690–922 ms at 50–200%. Attribution: ~26× raster overdraw
per dense window; saturated pixels now cost one byte load but still
*charge* full row work to the slice budget.

Next levers, in recommended order:

1. **Work-charge recalibration** (small): `raster_chord_row` charges
   `chord_x1_-chord_x0_` regardless of skips. Charging actual computed
   pixels lets each 512-px slice do more real work — fewer slices, less
   per-slice overhead. Measure with `settle_timing`; watch max_slice_us
   against the ~2.3 ms class.
2. **Edge-span recording** (structural, the real fix for 50–200%):
   immediate rasterization already computes exact span boundaries per row
   (`incremental_rasterizer.cpp` chord sweeps). Persist per-tile boundary
   spans (or a 1-px annulus mask) at publication; the settle pass then
   rasterizes coverage only for recorded boundary pixels instead of
   rediscovering all coverage. Interior stays alpha-255 spans. Key
   constraints: the zoom-dependent minimum-radius clamp is part of
   geometry identity; self-overlap union must be preserved; the frozen
   RGB565 blend model and 25-checksum oracle are the exactness gate.
   Rejected precedents to respect (`PERFORMANCE_REVIEW_ROUND_2026_08_18.md`):
   SSAA (808 ms), adaptive bands, cross-tile candidate reuse, prepared
   geometry cache. Edge spans differ from all of these: they eliminate
   coverage *rediscovery*, not discovery.
3. F29 perceptual ordering (center-out / active-stroke-first) — same
   compute, faster-looking; deferred but cheap.

Measurement discipline: `./scripts/esp32 vector-v2-gate-harness PORT 604
verify` — the `TINYDRAW_GATE1_SETTLE_TIMING` lines are the same-corpus A/B
instrument. Cold walls must stay ≤500 ms (adversarial 400% has only
~3–8 ms margin; the ±2–3% icache law applies to every build).

## 2. Touch targets

Reproducible-by-hand: selecting a pen size can simultaneously fire
Undo/Redo; both trigger and the UI corrupts. Second occurrence recorded in
`TRANSIENT_CHROME_POPUP_INCIDENT_2026_08_18.md`. Belongs with the roadmap
§3 full touch-target review (overlaps, pressed feedback, missed-tap glass
check). The diagnostic bar (ordered event capture) is defined in that
receipt.

## 3. Byte-swap hunt

`COLOR_POPUP_BYTESWAP_INCIDENT_2026_08_18.md` + archived photo. Signature:
one canvas-region present pushed bytes with one extra/missing RGB565 swap
(black/white invariant, dark-gray outlines magenta, corruption bounded at
the canvas/dock seam). Transient, self-healed, first-ever, appeared during
this session's presentation-path changes — treat today's commits as the
prime suspect window. Repro recipe: cached pan at 50–100%, long settle
pass running, open the color popup mid-pass; capture serial + photo.
Audit: every path that can push canvas-region bytes must pass
`stage_pixels_swapped`/`stage_ring_row` exactly once (check the color
dialog frame re-presentation fast path, ring-active popup presents, and
settle staging interleavings).

## 4. SVG/PNG export

Known deferred bugs (owner will close these last):
- PNG drops one-sample Strokes (receipted 2026-08-18: SVG emitted two
  blue dots, PNG painted neither — settled cursor completes one-sample
  operations without painting).
- SVG angularity beyond the expected AA difference (owner-reported;
  preserved physical pair exists).
- SVG erasers are white paths, only correct over white (semantic).
- Worst-case capacity: export volume is now 10.125 MiB, so ~7.3 MiB SVG
  fits; visible capacity/failure states remain roadmap §3 work.

## Operational gotchas (paid for in blood tonight)

- **Never arm a serial capture in the same command as a flash** — the
  port reset kills it silently. Arm separately, then verify the file
  exists and grows before telling the owner to test. One session's glass
  telemetry was lost this way.
- The gate script default is now 604 slots; partition tables are unified
  across variants (variant flips preserve the journal). A partition-table
  reflash relocates/erases the journal.
- `rg -r` is a *replace* flag — it has mangled grep output twice.
- Preserved-slot invariants: `claim_slot` consumes preservation; all
  wholesale reset paths call `drop_preserved_slots()`; validity rides
  `OperationLog::history_timeline()` (epoch changes on every history
  publish and must NOT be used).
- Internal SRAM is tight now: steady free ~182 KiB, export dip ~49 KiB.
  Anything new that wants internal memory must re-run the export gate.
- The owner measures; never accept a rejection or a win without a
  same-corpus, same-head A/B (the saturated skip was a 40% win wrongly
  rejected on unlike corpora).

## Definition of done (owner, verbatim intent)

Faster AA where possible → tap targets → byte-swap hunt → SVG/PNG parity
fixes → "and then I'm calling it a project."
