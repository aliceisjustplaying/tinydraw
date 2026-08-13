# Overview publication strategy measurement

Date: 2026-08-13

Prototype branch: `prototype/overview-publication-measurement`

Prototype commit: `e019f5e`

Device: ESP32-S3 with 8 MiB octal PSRAM, production overview walk build at 240 MHz

Raw local capture: `/tmp/e019f5e-overview-publication-measurement.log`

## Question

Can bounded dirty-region publication remove the approximately 50 ms per-operation overview cost without giving up transactional preparation? How does it compare with swapping two complete overview buffers?

## Method

The throwaway hardware probe ran each strategy twenty times against two deterministic pen operations. All pixel storage was in PSRAM. Each iteration started from the result of the preceding iteration, matching cumulative append behavior.

- **Full copy:** copy all 164,864 RGB565 overview pixels from live to scratch, rasterize into scratch, then copy all pixels back to live. This matches the existing publication shape.
- **Double buffer:** copy all live pixels to the inactive overview, rasterize there, then swap active/inactive ownership. This removes the second complete copy.
- **Dirty region:** derive the conservative 25% rectangle from authoritative world bounds, copy only that rectangle to compact scratch, rasterize against that bounded surface, then copy only that rectangle into the live overview after preparation succeeds.

The short operation's dirty rectangle was 195 pixels. The wide diagonal's rectangle was 163,236 pixels, nearly the entire 164,864-pixel overview. Hashes had to match across all three strategies. The existing full production hardware walk then had to pass unchanged.

## Results

| Case | Dirty pixels | Full copy | Double buffer | Dirty region |
|---|---:|---:|---:|---:|
| Short operation | 195 | 48,780 us | 24,446 us | 152 us |
| Wide diagonal | 163,236 | 56,153 us | 31,823 us | 55,589 us |

All strategy hashes matched in both cases. The subsequent production walk passed with 168/168 ordered transfers and zero scheduler or panel rejects.

## Verdict

Use a bounded dirty-region publication in the production append path, with the existing full-overview-sized publication allocation retained as worst-case scratch for now.

This is preferable to making overview buffer identity swappable:

- typical bounded operations avoid both full-frame copies;
- the live overview address stays stable, preserving the current pin/source ownership model;
- all fallible raster work still happens outside live materialization;
- commit can validate first, then perform a no-fail bounded row copy and identity transition;
- worst-case operations do not regress materially relative to the current path.

Double buffering halves the current cost predictably, but requires active-buffer identity and pin lifetime to follow a moving backing store. It also still pays a complete live-to-inactive copy on every append. Keep it as a fallback option only if measured real documents are dominated by near-world-spanning operations.

## Limits

This probe used two deterministic operations, not representative captured input. It measured isolated PSRAM preparation/publication, not concurrent display DMA or touch-to-photon latency. Dirty-region dimensions still need strict validation, alias checks, transactional failure tests, and exact hardware regression hashes in production code.
