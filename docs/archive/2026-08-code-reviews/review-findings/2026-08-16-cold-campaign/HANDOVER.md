# Session handover — 2026-08-16 cold compute campaign

Written for the next session. Read this alongside
[`REVIEW.md`](REVIEW.md) (findings) and
[`COLD_COMPUTE_CAMPAIGN_RECEIPT.md`](../../../../../benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md)
(measurements). PROJECT_STATE.md and V2_ROADMAP.md are current as of commit
history below.

## 1. What happened

Cold 400% on the frozen `adversarial_tapered_4x+evil_hairlines` corpus went
from **1,269.157 ms to 668.980 ms** (three-run maximums, −47.3%), exactness
intact, every sibling gate held or improved, interaction ticks down to
~8.5 ms. All other zooms improved 25–30%.

Commits (all pushed to `feat/v2-performance-followup`):

| Commit | Content |
|---|---|
| `a9e43eb` | fix: host release build (`assert`-only variable under `-Werror`) |
| `ed23f9d` | perf: windowed span search + internal scratch + prepared units (−37%) |
| `d2f3988` | perf: unit-merged masked row sweep (−13% more) |
| `a3e8ff8` | perf: caller-split painters + warm append unit sweep |
| `abb780c` | docs: wave-3 receipt + PROJECT_STATE scorecard update |
| `c00635e` | docs: architecture/perf review |

## 2. Current state of the machine and tree

- Device `/dev/cu.usbmodem101` is flashed with the gate harness at HEAD.
- Working tree is clean. Untracked leftovers predate this session
  (`benchmark-results/wave1a-panel/`, a datasheet PDF, a review zip).
- Build dirs: `out/build/host-census` (host release + census counters ON),
  `out/build/esp32-vector-v2-gate-harness` (product harness),
  `out/build/esp32-vector-v2-gate-harness-census` (census firmware),
  `out/build/esp32-raster-v1` (V1 verified building at HEAD).

## 3. The exact A/B loop (recipe)

Host (fast iteration, exactness + counters; NOT authoritative for speed):

```sh
cmake --build out/build/host-census --target tinydraw_vector_v2_raster_census tinydraw_vector_v2_tests
./out/build/host-census/vector_v2/tinydraw_vector_v2_tests                  # 91K+ assertions
./out/build/host-census/vector_v2/tinydraw_vector_v2_raster_census --general indexed 5   # combined corpus, all zooms + census
./out/build/host-census/vector_v2/tinydraw_vector_v2_raster_census --fuzz-collinear 4000 987654321
./out/build/host-census/vector_v2/tinydraw_vector_v2_raster_census --fuzz-docs 150 48879
```

Device (authoritative):

```sh
./scripts/esp32 vector-v2-gate-harness /dev/cu.usbmodem101      # build + flash (~4 min)
uv run --script tools/esp32-capture.py /dev/cu.usbmodem101 /tmp/run.log 480 \
  --end-marker TINYDRAW_GATE1_AUTOMATED_DONE --failure-regex 'task_wdt|Guru Meditation'
# repeat runs without reflash: reset first, then capture again
python3 -m esptool --chip esp32s3 --port /dev/cu.usbmodem101 --before default-reset read-mac
```

Census firmware (attribution — counters cost ~5%, never use for receipts):

```sh
cd esp32 && eim run "idf.py -B '../out/build/esp32-vector-v2-gate-harness-census' \
  -DTINYDRAW_QEMU=OFF -DTINYDRAW_VECTOR_BENCHMARK=OFF -DTINYDRAW_PHASE2_PROTOTYPE=OFF \
  -DTINYDRAW_RASTER_PAN_BENCHMARK=OFF -DTINYDRAW_INTERACTIVE_PAN_BENCHMARK=OFF \
  -DTINYDRAW_VECTOR_V2_MEMORY_PROBE=OFF -DTINYDRAW_VECTOR_V2_OVERVIEW_WALK=OFF \
  -DTINYDRAW_VECTOR_V2_GATE_HARNESS=ON \
  -DTINYDRAW_VECTOR_V2_TILE_CENSUS=OFF -DTINYDRAW_VECTOR_V2_RASTER_CENSUS=ON \
  -DTINYDRAW_VECTOR_V2_TILE_SLOTS=448 build"
# then flash that dir and capture; census prints TINYDRAW_RASTER_CENSUS per cold gate
```

Log extraction one-liners:

```sh
grep -E "PACED_COLD corpus=adversarial" run.log          # cold walls per zoom
grep -E "MIXED_DRAW"                    run.log          # append budgets
grep    "AUTOMATED_DONE"                run.log          # verdict vector
grep    "PRODUCER_SCRATCH"              run.log          # internal-SRAM placement receipt
```

Discipline that worked: one hypothesis per flash; A and B in the same
session; boot-to-boot spread on this hardware is ~1–3 ms, so 10 ms deltas
are real. Host disagreed with device on two of five experiments (both
directions) — host census is a compass for exactness and counts, never a
speed verdict.

## 4. Remaining road to ≤500 ms (ranked, with estimates)

Compute is ~582 ms; it needs to reach ~410 ms. Presentation ~67 ms, pacing
~19 ms are not the whale.

1. **Op-level chord sweep (H7)** — est. 40–60 ms, medium effort. The unit
   sweep merged 2–3 chords; consecutive units of the same operation still
   overlap ~30% in rows (shared joints + caps) and each pays its own window
   scans. Plan: prepare ALL of an op's chords per group visit into a
   caller-funded internal chord table (~90 chords × ~56 B for the 32-sample
   corpus ops; ops beyond capacity process in chord batches — the union
   argument makes any within-op grouping exact), then one row sweep with a
   y-sorted active list. Resume state = batch index + row cursor. Budget by
   rows, not step areas.
2. **Publish direct copy** — est. 10–20 ms, small effort. `publish_surface_tile`
   copies supertask→packed, then the canvas copies packed→slot pool. Add a
   strided publish on `MaterializedCanvas` and skip `packed` (keep uniform
   analysis, which can read strided too).
3. **PIE 128-bit fixed-point probing** — est. ≤25–30 ms ceiling, high
   effort/risk. Probes are only ~150K/fill now; only worth it after 1–2
   land and if the gap is <30 ms. Would vectorize `covers_pixel` bands in
   Q-format; exactness must be re-proven, `covers_pixel` stays authority
   (SIMD as pre-filter only, like the seeds).
4. **Presentation/compute overlap** — est. up to ~40 ms of wall. Producer
   computes the next group while the previous group's DMA drains. Touches
   the presenter contract → reopens pan optical gates per the dependency
   matrix. Only if 1–3 fall short.
5. **Band-shaped replay units (6×2 tiles) + block-granular saturation** —
   compound experiment, previously rejected (host 2.7× regression at 400%)
   because full-width rows never saturate and the buried fat tapered ops
   un-bury. Prerequisite: extend `MaskedRowSummary` to per-(row, 32-px
   block) saturation (~768 B internal) so op/segment gates work at block
   ranges. Then bands amortize candidates/fill/publish ÷3 AND keep the
   saturation shield. Potentially large; measure block-summary alone on
   2×2 first.
6. **Word-mask window scan, done right** — previously rejected because
   GCC-Xtensa emitted `callx8` memcpy per load. Retest with
   `__builtin_assume_aligned` + an alignment check at painter entry (all
   product masks are 4-aligned). Est. 20–40 ms if loads inline to `l32i`.
   Verify by disassembly BEFORE flashing (see §6).

## 5. Rejected this session — do not redo without a new mechanism

See the wave-3 receipt for numbers. Summary-bitmap per-row saturation probe
(partial-saturation tax, twice-confirmed); word-mask via plain memcpy
(libcall per load); 6×2 bands without block saturation (kills the
saturation shield); hybrid warm/seeded search shared by all callers (lost
both ways — the caller split is the answer).

## 6. Device physics cheat sheet (hard-won)

- Xtensa toolchain emits **library calls** for float `/`, `floorf`,
  `ceilf`, `sqrtf`, and non-provably-aligned `memcpy`. Native and fast:
  `+ - *`, `trunc.s`-based floor/ceil (`fast_floor`/`fast_ceil` in
  `incremental_rasterizer.cpp`), the rsqrt bit hack
  (`conservative_sqrt_upper`), `nsau` (clz). Check any new hot loop with:
  `xtensa-esp-elf-objdump -d <obj> | grep callx8`.
- Internal SRAM had **319 KiB free** after all allocations (receipt line
  `TINYDRAW_PRODUCER_SCRATCH`). PSRAM writes/reads through the 32 KiB
  dcache are the enemy; placement > cleverness.
- The 0.75 px `kMinimumScreenRadius` clamp guarantees adjacent row chords
  overlap in x, which is what makes the warm-start masked search correct.
  Do not lower it without revisiting `paint_masked_constant_radius_segment`.
- The gate cascade **skips later gates after failures**, so a red gate can
  hide another (this concealed `mixed_draw` for at least one revision).
- In the census tool, snapshot `g_raster_census` immediately after the
  timed fill — `compose_equals_forward` pollutes counters otherwise.

## 7. Rasterizer map after this session

`vector_v2/src/incremental_rasterizer.cpp` now has two painter families:

- **Append/live family** (entry: `apply_masked_incremental_operation`,
  `_segment`, `_curve_step`): historical warm-start const search +
  full-bounds tapered walk, plus `paint_masked_curve_unit_warm` (one
  mask-scan per row across a unit's chords). Fresh per-tile masks are why
  warm start wins here.
- **Cold producer family** (entry: `apply_masked_prepared_curve_unit`,
  with `_curve_step` delegating as a 1-step unit): stateless windowed
  machinery — `mask_unset_window`, `RowSeed` (circle / midpoint-circle /
  parallelogram tiers), `paint_masked_const_row` monotone seeded probes,
  window-clamped tapered rows. Dense newest-first masks are why stateless
  wins here.
- Shared authority: `covers_pixel` (sole geometry truth),
  `paint_masked_exact_span`, `paint_masked_tapered_row`,
  `conservative_tapered_row_span` (+ per-segment `TaperedSpanTable`),
  `curved_unit`/`prepare_incremental_curve_unit` (subdivision).

`TileProducer::render_active_segment` consumes whole prepared units (one
bounds/saturation/budget gate per unit). Producer scratch is internal SRAM
with PSRAM fallback (`vector_v2_app.cpp`, `TINYDRAW_PRODUCER_SCRATCH`).

## 8. Open owner decisions

1. **mixed_draw 50% append budget** (18.8 vs 15 ms max): owner prefers a
   glass test — warm canvas at 50%, fast scribbles over dense content; if
   feel is fine, raise the budget with a dated contract note. Failure
   predates this campaign (dating evidence in the wave-3 receipt, §mixed).
2. **Ink smoothness — four-span / adaptive subdivision rematch**: the unit
   sweep destroyed the cost argument that killed four-span (extra chords in
   a unit are nearly free on the row axis; subdivision is paid once per
   endpoint). Adaptive 1–4 chords by flatness would target the visible
   angularity directly. Changes committed authority geometry → reopens
   cold exactness, SVG parity, and the frozen-corpus statistics; needs an
   owner go and a re-baseline.
3. **Settled AA prototype**: design in REVIEW.md §Settled AA. One-line
   version: keep live ink hard-edged; in idle, re-run the existing
   newest-first replay with the 1-bit mask widened to 8-bit accumulated
   alpha; interiors behave exactly as today, only span-boundary pixels
   (~2 per chord-row) get analytic capsule coverage
   (`α ≈ clamp(0.5 + (r − d), 0, 1)`) composited front-to-back; publish as
   higher-quality cached tiles (déjà-vu safe). Est. ~1.3× immediate replay
   per group. First step: host prototype on one group to validate cost and
   look (owner acceptance is explicitly "need to see it in action").

## 9. Standing follow-ups (small)

- Archive the pan torn-positive-control (pre-existing work-order item).
- The 20-run cold closure statistic has never been run on the new numbers;
  development characterization is 3 runs.
- All wave-3 numbers are pre-autosave by necessity; the contract requires
  re-measurement once autosave exists (Phase 5).
- Consider CI legs for `./scripts/dev release` and both firmware builds
  (the release build was broken at HEAD when this session started).
- `OperationLodStore` is dead code (guardrail-forbidden design); delete or
  label it.
- Census cycle buckets only cover painted units; rejected-step setup sits
  in the produce_next residue. Extend `setup_ticks` around the rejection
  path if the residue needs splitting again.
