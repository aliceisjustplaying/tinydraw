# Production tile-class census — 2026-08-13

## Question

Does the production cache need more permanently allocated 8 KiB RGB565 tile
blocks, or does the drawing workload contain enough implicit paper to justify
separating logical tile identities from raw pixel payloads?

This is a representation measurement, not a new cache implementation.

## Method

The opt-in `TINYDRAW_VECTOR_V2_TILE_CENSUS` firmware loads the deterministic
seed-7 handwriting workload through the production `OperationLog`, then cold
produces every bounded tile identity at 50%, 100%, 200%, and 400% using the
Gate 1 `TileProducer`.

Each tightly packed produced payload is classified as:

- paper: every RGB565 pixel is `0xFFFF`;
- uniform color: every pixel has one non-paper value;
- raw: more than one pixel value.

The same scan counts row-local runs. The reported row-RLE size is only a
three-byte-per-run estimate (one byte count plus two-byte RGB565 value). There
is no RLE encoder, decoder, variable-size allocator, or storage policy in this
change.

The census traverses complete zoom grids, so it is independent of the current
320-slot cache capacity. It yields between bounded producer units and tile
groups. Classification reads the existing PSRAM-backed packed tile scratch;
its timing therefore includes the memory placement used by the live app.

## Scope limitations

- The corpus is deterministic synthetic handwriting, not captured user data.
- It contains 1,000 operations and 19,844 compact samples in this revision.
- Produced tiles are current hard-edged `kImmediate` tiles. Antialiased settled
  pixels will have higher color entropy and require a separate census.
- Complete-grid results describe representation opportunity, not the number of
  raw payloads simultaneously active along every possible pan route.
- The row-run estimate is not evidence that a compressed payload allocator is
  worth its complexity.

## Hardware result

Load-bearing receipt:
[`hardware-receipts/tile-class-census-seed7.log`](hardware-receipts/tile-class-census-seed7.log).

| Zoom | Tile identities | Paper | Nonuniform | Paper share | Estimated row-RLE bytes for nonuniform tiles |
|---:|---:|---:|---:|---:|---:|
| 50% | 168 | 98 | 70 | 58.3% | 111,030 |
| 100% | 644 | 378 | 266 | 58.6% | 264,789 |
| 200% | 2,576 | 1,604 | 972 | 62.2% | 630,558 |
| 400% | 10,304 | 6,886 | 3,418 | 66.8% | 1,558,458 |
| **All** | **13,692** | **8,966** | **4,726** | **65.4%** | **2,564,835** |

No non-paper uniform tiles occurred in this corpus. Every nonuniform tile had a
smaller three-byte row-run estimate than its raw tightly packed payload, but
that is only a favorable hard-edged-data result.

The complete scan took 46.97 seconds of measured producer calls and 2.94
seconds of classification. Classification averaged approximately 214
microseconds per identity from PSRAM-backed scratch; clean maximum was 240
microseconds. The final run emitted no watchdog, panic, backtrace, or failure
markers.

## Decision

The paper tier passes the provisional go gate decisively:

- Fable proposed proceeding at 50% or more paper/uniform identities.
- The measured total is 65.4%.
- More importantly, the complete 100% world has only **266 non-paper tiles**.
  The existing 320 raw blocks could retain every non-paper 100% payload if
  paper identities stopped consuming blocks. Raising the raw pool to 448 would
  spend another 1 MiB without addressing the representation error.

Therefore:

1. keep the 448-slot experiment paused;
2. design the smallest paper/raw catalog seam next;
3. retain the existing raw slot/pin/LRU machinery for non-paper payloads;
4. do not add RLE yet;
5. repeat the census after antialiased settled rendering exists before making
   any compressed-payload decision;
6. physically re-prove the 1.5 MiB export/coexistence reserve after the catalog
   implementation changes the actual memory map.
