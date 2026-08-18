# TinyDraw V2 memory map

Last updated: 2026-08-18 (604-slot pool, colonized flash). Sources: product
`TINYDRAW_VECTOR_V2_READY` boot receipts, `idf.py size`, `AppStorage::allocate()`
(`esp32/main/vector_v2/vector_v2_app_storage.cpp` — allocation **order is
contract**: dcache-set placement receipts are inline there), `memory_layout.h`,
`esp32/partitions.csv`, `esp32/main/linker.lf`.

## PSRAM — 8 MiB octal

| Allocation (boot order) | Bytes | Notes |
|---|---:|---|
| overview + frame + overview/region scratch | 1,318,912 | 4 × 329,728 full 368×448 RGB565 surfaces |
| tile pool (604 × 8,192) | 4,947,968 | 448-slot working-set heritage + 156 owner-funded slots; preserved Undo/Redo pre-images are the first-evicted class inside this one pool |
| chrome staging cache | 107,912 | sprite strips |
| tile metadata (13,692 uniforms + 604 slots + occupancy) | ~102,768 | |
| operation records + samples (4,000 / 80,000) | 720,000 | vector authority |
| input samples, affected keys, live scratch | ~113,704 | |
| raw-slot directory | 32,768 | 27,384 used, padded to the 8 KiB dcache-way multiple (receipt in source) |
| settled-AA workspace (dead-last) | 40,960 | mid-heap placement once cost +9 ms cold |
| spatial index (dead-last) | 93,176 | 168-cell bitset + large-op set |
| **free at boot** | **≈870,792** | largest block ≈868,352 |

Standing envelope contracts (battery-enforced, sequential by product design):
autosave full-capacity staging **704,512 B**; export workspace **327,680 B**
(measured peak 291,484 B, document-size-independent). Residual slack above
the autosave line ≈166 KiB → at most ~8 more tile slots exist without
shrinking a surface or the autosave line. The historical 1.5 MiB contiguous
export reserve is retired
([receipt](../benchmark-results/export-memory-math-2026-08-18/RECEIPT.md)).

## Internal SRAM — DIRAM pool 341,760 B

Static: 142,399 B used — `.text` 100,039 B is IRAM-pinned hot code
(`co5300_panel_transport`, `tile_producer`, `incremental_rasterizer` [F24],
`settled_tile` [F13] + mandatory IDF IRAM); `.data` 22,104; `.bss` 20,256.
Runtime heap ≈223 KiB free in steady product, funding: 32 KiB producer
supertask (hottest pixel scratch), 12,384 B chord plans, 2 × 23,552 B DMA
staging, masks/summaries, 16 KiB main stack. Export mode dips to ~84–90 KiB
(TinyUSB + PNGenc) and returns. The 16,384 B vector/IRAM region is a fixed
cache split, not pressure.

## Flash — 16 MiB, fully colonized (no unallocated bytes)

| Partition | Offset | Size | Notes |
|---|---:|---:|---|
| nvs / phy | 0x9000 | 28 KiB | |
| factory app | 0x10000 | 1,792 KiB | product ~1,045 KiB, gate ~1,225 KiB; one layout for both variants |
| drawing (journal) | 0x1D0000 | 4 MiB | append-only authority; fails closed at capacity |
| export (FAT16) | 0x5D0000 | 10.125 MiB | synthetic disk 20,736 × 512 B; covers worst-case ~7.3 MiB SVG |
| coredump | 0xFF0000 | 64 KiB | |

## Cache-policy balance (owner question, 2026-08-18)

`choose_slot` eviction order: (1) free slots, (2) **preserved history
pre-images, stalest first**, (3) LRU current tiles. Pan/zoom retention
therefore always wins over the Undo/Redo runway: heavy panning consumes
preserved slots (history falls back to exact rebuild), but history caching
can never evict a current pan tile. The runway is strictly opportunistic
slack.
