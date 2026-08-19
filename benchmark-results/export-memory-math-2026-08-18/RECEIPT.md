# Export memory math — the 1.5 MiB reserve was never a measurement

Date: 2026-08-18. Author question: how much space does the SVG+PNG export
*actually* need, best and worst case? Answer: about 300 KiB of PSRAM,
document-size-independent. The 1.5 MiB figure is uninherited insurance.

## Where the numbers come from

Measured, from tonight's all-ones gate run (`/tmp/gate-verify.log`):

```
TINYDRAW_GATE1_EXPORT formats=svg,png svg_bytes=155150 png_bytes=40728
  svg_workspace_bytes=4096 png_workspace_bytes=62108
  render_workspace_bytes=229376 peak_workspace_bytes=291484
  operations=52 free_psram=2187528 free_internal=90312 pass=1
```

Structural, from source:

- SVG streams one painter-ordered path per Stroke through a fixed
  transactional **4,096 B** workspace (`svg_workspace_bytes`). Independent
  of operation count by design.
- PNG streams settled-AA bands through PNGenc: **62,108 B** encoder/row
  state plus a fixed **229,376 B** render band = 1792 × 64 × 2 (one 64-row
  full-world-width RGB565 band), allocated at export time
  (`vector_v2_export.cpp:114`, `kExportRenderWorkspaceBytes`).
- Peak concurrent PSRAM: **291,484 B measured**; the structural ceiling is
  the same components (~295 KiB) plus allocator overhead. A 4,000-op /
  80,000-sample document changes CPU time, not RAM.

## What the 1.5 MiB actually is

`memory_layout.h:26` — `kTargetContiguousReserveBytes = 1536 KiB`, carried
forward from the archived pre-V2 raster PNG/USB evidence as a *target*.
The battery's `verify_export_reserve` "validates" it by allocating and
immediately freeing a synthetic 1.5 MiB block
(`vector_v2_gate_harness_stress.cpp:356-371`) — it never measures export.

Cost of the fiction, from `memory_layout.h:30`: **512 tile slots were
tried and rejected** because the synthetic check could no longer hold the
1.5 MiB block at peak harness state. An unmeasured constant cost the
product 64 cache slots (512 KiB of warm neighborhood).

## Best/worst case summary

| Case | PSRAM | Notes |
|---|---:|---|
| Best (sparse doc) | ~295 KiB | same fixed workspaces |
| Measured (52-op gate doc) | 291,484 B | receipt above |
| Worst (4,000 ops / 80,000 samples) | ~295 KiB + allocator slack ≈ **~320 KiB** | RAM flat; CPU/flash scale |

Worst-case *flash* is the real export constraint, not RAM: the gate doc
emits ~93 B/sample of SVG (155,150 B / ~1,660 samples), so 80,000 samples
project to ~7 MiB of `DRAWING.SVG` — a FAT-capacity concern that belongs
to the roadmap's visible capacity/failure-state work, and no amount of
PSRAM reserve helps it.

## Consequence (author decision 2026-08-18)

The contiguous reserve is not sacred. The replacement contract:

1. A single contiguous **expendable cache arena** (~1.28 MiB ≈ 156 extra
   tile slots) is allocated dead-last in PSRAM and funds Undo/Redo
   preserved-version tiles plus retention overflow. Budget receipts:
   free at boot 2,187,528 B; autosave full-capacity staging hard line
   704,512 B (`F20_AUTOSAVE_CALLER_LATENCY_2026_08_18.md`); ~200 KiB slack
   for Wi-Fi/NTP and new-slot metadata.
2. Export (already a modal flow) evicts the arena contents and frees the
   arena; the freed block is contiguous by construction and covers the
   ~320 KiB real need about 4× over. After **Return to Drawing** the arena
   is re-allocated and repopulates lazily.
3. The export-reserve gate is redefined: evict → allocate real export
   workspaces (or the ~320 KiB envelope) → run export → release → re-arm
   arena, replacing the synthetic 1.5 MiB malloc.
4. If the larger cache measurably slows anything (PSRAM dcache pressure on
   cold walls, commit/evict scans at 604 slots — F21 projects ~2.4 µs/tile
   from 1.8), the arena shrinks; the cold battery is the arbiter.

## 604-slot cache A/B — no measurable cost (2026-08-18, same head)

Run via the existing `TINYDRAW_VECTOR_V2_TILE_SLOTS` A/B mechanism:
`./scripts/esp32 vector-v2-gate-harness PORT 604 verify`. Every real gate
passed; only the synthetic 1.5 MiB `export_reserve` check went red
(`free_held=870792 largest_held=868352 pass=0`) — the same fictional
constraint that rejected 512 slots, now scheduled for replacement.

Paced cold walls vs the same-night 448-slot run: overlap 478.4→481.7 /
289.0→290.0 / 264.0→266.0 / 221.0→222.0 ms; adversarial 388.0→393.5 /
380.1→382.4 / 453.0→460.2 / 497.8→496.8 ms (400% still ≤500);
hairline-capacity 259.0→256.0 / 171.0→171.0 ms; seed7 unchanged. All
deltas are within the documented ±2–3% flash-layout dice; the worst is
+1.6% and the binding 400% case improved. History-latency summaries are
unchanged (total_max 440.3 vs 440.8 ms).

Memory at 604: raw_tile_bytes=4,947,968 (604×8,192), metadata +4,992 B,
free PSRAM 870,792 / largest 868,352 — covering the 704,512 B autosave
hard line with ~166 KiB slack. PSRAM dcache pressure from a larger pool
is measured absent. The production design remains 448 permanent + 156
expendable-arena slots so export can free one contiguous ~1.28 MiB block;
this A/B de-risks the total footprint.
