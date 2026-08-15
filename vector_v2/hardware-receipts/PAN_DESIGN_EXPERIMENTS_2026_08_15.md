# Pan presentation design experiments — 2026-08-15 (glass round 3)

Full serial logs for the four overlay-seam presentation designs tried and
rejected during the morning session, plus the two hairline-gate slot runs.
The shipped state after triage is `efb1586` (region-sequential, 28.9 ms
PANSEQ average, rare overlay-edge seam residual — see todo/board item
"overlay-seam redesign, bench-first"). Every log is a full battery capture;
`TINYDRAW_GATE1_PANSEQ` is the comparison line.

| log | design | PANSEQ avg (100/400) | outcome |
|---|---|---|---|
| `pan-design-1-rowmajor-splits-2026-08-15.log` | row-major sweep, x-split at overlays | 36.6 / 37.0 ms | rejected: transaction bloat (~1 ms per sync window-command sequence) |
| `pan-design-2-scratch-strips-psram-2026-08-15.log` | overlay strips via PSRAM scratch | 56.9 / 57.1 ms | rejected: 3x PSRAM strip traffic |
| `pan-design-3-scratch-strips-internal-2026-08-15.log` | overlay strips via internal scratch | 48.4 / 48.9 ms | rejected: per-strip gather+draw serialized |
| `pan-design-4-ring-draw-overflow-2026-08-15.log` | draw-into-ring + restore, 16 K backup | 67.2 / 69.3 ms, gates red | backup overflow (overlays total 26,576 px): every frame fell back to full refresh |
| `pan-design-5-ring-draw-sized-2026-08-15.log` | draw-into-ring + restore, sized backup | 42.0 / 42.3 ms | rejected as-implemented: overlay prep serialized ~20 ms (each overlay rect redraws all three overlays); design remains the bench-first candidate |

Hairline-gate slot runs (gate code as landed at `8c222e8`):

| log | slots | note |
|---|---|---|
| `hairline-384-spotcheck-2026-08-15.log` | 384 | A/B: guard stops at 354/384; edge-tour worst stop 257 ms |
| `hairline-512-reserve-failure-2026-08-15.log` | 512 | export-reserve gate FAILS (largest block 1.44 MiB < 1.5 MiB promise): the run that corrected the pool to 448 |

Also note: `efb1586-full-gate-384.log` (renamed from a mislabeled `-512`
name) was captured at 384 slots; the harness script's slot default silently
overrode the CMake default until `8c222e8`.
