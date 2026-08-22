# ESP32-S3 native disassembly and performance round — 2026-08-22

## Result

This round optimized the physical ESP32-S3 firmware, not the WebAssembly build.
Against the original native gate image, the final measured image improves:

- cold render compute by **18.92–41.87%** across all 15 corpora/zooms;
- direct pan composition by **51.59–51.65%**;
- RGB565 ring staging by **57.63–57.75%**;
- the saturated 604-slot cache tour by **39.15%**;
- undo/redo worst-case totals by **21.78–24.24%**;
- settled anti-aliased rendering by **17.23–26.45%**.

The final native gate is bit exact, its export CRC is still `e40499d1`, all
hardware guard zones pass, and normal product firmware was rebuilt, flashed,
and observed running on the board after the benchmark image.

## Scope and method

The reference device is an ESP32-S3 revision 0.2 at 240 MHz with 8 MiB octal
PSRAM, connected as `/dev/cu.usbmodem1101`. The toolchain is ESP-IDF 6.0.2 and
Espressif Xtensa GCC 15.2.0 (`esp-15.2.0_20251204`). Native compilation uses
`-O2`, `-mlongcalls`, function/data sections, and the project's no-builtin
memory-function policy. The effective language mode is `gnu++26`; this
supersedes the earlier C++20 argument.

Every accepted change passed four filters:

1. inspect the actual linked ESP32-S3 ELF and hot-loop disassembly;
2. prove exact output and guard zones on the host and/or physical S3;
3. measure an isolated A/B where possible;
4. keep only changes that improved the full device workloads.

Measurements use the same physical gate work, tile counts, corpus checksums,
and presentation invariants. `compute_us` excludes paced panel presentation.
Compiler-only conclusions below come from linked disassembly, not source-level
expectations.

## Complete experiment ledger

This is the consolidated list of experiments performed in this round, including
work delegated to the cache, masked-raster, and settled-raster investigations.
“Accepted” means the exact implementation is present in the final product;
“rejected” means it was measured, inspected, and removed or never promoted.

### Accepted experiments

1. **Native settled AA math:** replaced inner `sqrtf`/floor/ceil/round calls
   with exact S3 instructions and integer-domain helpers; bit exact and faster
   at every zoom.
2. **Raster operation IRAM placement:** kept the two raster operation objects
   executable-internal; linked hot loops no longer pay flash-cache fetches.
3. **Raster IRA loop pressure:** applied `-fira-loop-pressure` only to the
   operation raster TU; reduced masked-loop register churn without widening the
   code-growth risk.
4. **Telemetry-free spatial queries:** skipped `__popcountdi2` when no stats
   sink exists; candidate order and results are unchanged.
5. **Inline scalar metadata/zoom accessors:** removed small ABI calls from hot
   producer, canvas, and log paths.
6. **Exact shift/mask zoom bounds:** replaced division/reciprocal scaling for
   the five fixed zooms; exhaustive coordinate tests prove equivalence.
7. **Equal-phase panel PIE staging:** vectorized aligned RGB565 byte swapping;
   this became the 28-byte linear kernel.
8. **Producer PIE initialization:** fused surface fill and finalized-mask clear;
   the 34-byte kernel removes two large scalar initialization walks.
9. **Presenter row/ring invariant hoists:** removed repeated modulo and geometry
   work, then split the vertical wrap once per sweep.
10. **Single-pass cache victim classification:** computed each protection rank
    once and used direct half-open tile bounds; avoided repeated tuple/comparator
    work in the exact scan fallback.
11. **24-byte cache-slot packing:** reordered fields to remove alignment holes;
    the established 19,328-byte allocation footprint was deliberately retained.
12. **Selective fixed-copy expansion:** enabled `-finline-stringops=memcpy` only
    for materialized composition, operation log, and history replay, where the
    linked fixed 20–36-byte copies improved.
13. **Selective settled caller-save suppression:** used
    `-fno-caller-saves` only for settled rendering; this won without contaminating
    pan, cache, and cold paths.
14. **64-byte D-cache lines:** retained 32 KiB/8-way capacity while improving
    sequential PSRAM access.
15. **Rolling curve sample windows:** converted only the sample entering each
    adjacent endpoint window and reused the exact previous midpoint.
16. **Curve and chord out-parameters:** wrote `CurveUnit`, segments, row seeds,
    tapered tables, and chord batches in place; six ROM aggregate copies and
    large stack frames disappeared.
17. **Settled chord invariant hoists:** cached row geometry, moved dirty-bound
    publication out of the pixel loop, and removed redundant clears/clamps.
18. **Fused replay/settled initialization:** paired or fused buffer setup where
    ownership made it exact, avoiding duplicate passes.
19. **Direct `ActiveGroup` lifecycle:** initialized and reset reusable producer
    fields explicitly; removed whole-aggregate clears/copies.
20. **PIE tile publication:** aligned the tile pool and copied a 64x64 tile in
    64-byte vectors; removed 64 row `memmove` calls per full publication.
21. **Row-specialized coverage:** copied immutable chord geometry into a
    fixed-row record, eliminating per-pixel calls and window-register traffic
    while preserving the original MADD evaluation order.
22. **Word-at-a-time finalized-mask scans:** skipped saturated aligned words
    and retained byte handling for every head/tail phase.
23. **Scalar leaf cache probes:** cached `ready()`, used fixed grid tables and a
    `uint16_t` sentinel, improving host probes by 4–35%.
24. **Arbitrary-phase panel PIE staging:** used the documented `USAR`/`SRC.Q`
    stream assembly; exact at every source phase and seam.
25. **Generic PIE RGB565 copy/fill:** shared exact 2D kernels across raw tile
    composition, uniform fills, and toroidal ring writes.
26. **Handwritten settled composite:** a 178-byte, 16-byte-frame assembly leaf
    performs exact accumulation with no calls or spills.
27. **PIE tile-uniform scanner:** early-rejected nonuniform vectors and handled
    the tail scalarly; roughly 1.9x faster in targeted host cases.
28. **Division-free `ring_scroll`:** replaced four remainders and a 36-byte
    aggregate copy; the frame fell from 96 to 48 bytes and host time by 51%.
29. **Borrowed history replay:** exposed the record and sample span in place;
    removed the 44-byte optional/accessor copy and improved sparse replay ~34%.
30. **Caller-owned producer/log results:** wrote stored operations and production
    results directly into retained state; producer hot paths have no ROM state
    copy and settled removed five 40-byte copies.
31. **Hybrid chord ordering:** insertion-sorted small/tall batches, word-bucketed
    dense short ranges, and ordered equal rows broad-first; z400 work fell 5.44%.
32. **Intrusive indexed LRU:** stored two 16-bit links per slot in existing tail
    padding; normal eviction became O(1) and matched the scan oracle exactly.
33. **Exact masked-span word stores:** used four aligned word stores, or peeled
    halfwords around three words; device A/B improved 1.091x with every guard.
34. **Direct recent-view stores:** replaced a 21-byte ROM `memcpy`; frame 80 to
    48 bytes and host time improved 11–16%.
35. **Native kernel gate suite:** added phase, seam, overlap, guard, and exactness
    A/Bs for panel, initialization, publication, copy/fill, uniform, composite,
    and masked-span kernels; all final families pass on the physical S3.

### Rejected or superseded experiments

1. **Broad string-operation expansion:** cold work regressed 1–38% and settled
   3–8% from code growth/I-cache pressure; only three proven TUs remain enabled.
2. **Presenter scheduling flags:** physical pan regressed about 1.6%; removed.
3. **Wide `-fno-caller-saves`:** cold, pan, and cache regressed; narrowed to the
   settled TU.
4. **32 KiB I-cache:** consumed 16 KiB more internal RAM and the real presenter
   could not bootstrap.
5. **64 KiB D-cache:** projected export slack fell to roughly 5 KiB, below the
   safe operating margin.
6. **Four-way D-cache:** recovered no memory and increased conflict risk; no
   compensating measured benefit.
7. **IRAM C++ ring copy:** did not improve the PSRAM-dominated loop; removed.
8. **Whole/main-component LTO:** the whole build hit an Espressif GCC IPA-ICF
   ICE; component LTO broke hot linker placement and grew text.
9. **Packed opaque composite:** no repeatable device win and +222 bytes of hot
   text; removed.
10. **Packed final fold:** mixed/slight results and +335 bytes of hot text;
    removed.
11. **Arbitrary five-plane settled initializer:** projected gain was below
    0.02%; not worth another phase-sensitive kernel.
12. **First stable counting sort:** adversarial rows regressed up to 2.1%; the
    final hybrid broad-first ordering superseded it.
13. **Preserving five FP registers across settled rows:** every zoom regressed
    0.13–1.09% despite fewer static loads; reverted.
14. **Settled row pointer/count induction:** saved eight bytes but slowed 200%
    and 400% by about 0.36% and 0.68%; reverted.
15. **`-mextra-l32r-costs`:** added nine bytes and more instructions; removed.
16. **`-mno-serialize-volatile`:** changed no raster code and weakens required
    ISR/atomic ordering; rejected.
17. **`-mforce-l32`:** unsupported by this Espressif compiler.
18. **Target-alignment variants:** `-mno-target-align` was smaller but prior
    hardware was slower; alignment expansion also failed to produce a win.
19. **Unrolling, peeling, modulo scheduling, `-O3`, automatic literal pools,
    and Xtensa compiler vector switches:** linked code or hardware results did
    not improve enough to retain them.
20. **`-fno-math-errno` and reciprocal-math:** produced no useful linked raster
    change; exact division sites remain per chord.
21. **`-flate-combine-instructions`:** triggered a GCC internal compiler error.
22. **Other compile-only flag screens:** float relaxation/no fused MADD,
    loop-pattern/vectorization, register renaming, web/live-range/IRA changes,
    GCSE/scheduling, jump-table/text-literal, block-layout/if-conversion,
    IPA-RA/cross-jump, store-forwarding, and tree-LRS families produced no
    promotable linked win.
23. **Small settled static rewrites:** a clamp rewrite (-8 B), derived counter
    (-1 B), and prior-alpha variant (+5 B) were discarded after inspection;
    none was claimed as a hardware regression or accepted speedup.

Early alignment-gate failures were implementation bugs found by the new phase
oracles, not failed optimization ideas; corrected implementations were retested
and are represented in the accepted entries above.

## Product image layout

The final table comes from `idf.py size` on the normal product image after all
gate measurements.

| Region | Baseline | Final product | Delta |
|---|---:|---:|---:|
| executable internal text | 120,599 B | 127,587 B | +6,988 B |
| flash text | 781,600 B | 779,444 B | -2,156 B |
| flash rodata | 149,140 B | 149,748 B | +608 B |
| DRAM data | 22,104 B | 22,104 B | 0 B |
| DRAM BSS | 20,208 B | 20,208 B | 0 B |
| total image | — | 1,080,219 B | — |

The application binary is `0x107c10` bytes and leaves `0xb83f0` bytes (41%) in
the smallest app partition. The size report leaves 187,217 bytes of DIRAM; the
dedicated 16 KiB IRAM region is fully allocated. The cache remains 16 KiB
I-cache and 32 KiB D-cache, both 8-way; D-cache lines changed from 32 to 64 B.

## Final linked assembly inventory

These are symbol sizes from the final product ELF, not gate-only objects.

| Kernel / hot symbol | Bytes | Key linked property |
|---|---:|---|
| `tinydraw_stage_pixels_swapped_pie` | 28 | aligned RGB565 byte swap, 16 px/iteration |
| `tinydraw_stage_pixels_swapped_unaligned_pie` | 109 | arbitrary source phase via `USAR`/`SRC.Q` |
| `tinydraw_stage_full_ring_rows_swapped_pie` | 465 | row wrap split once; 352/368 px vectorized |
| `tinydraw_initialize_raster_buffers_pie` | 34 | fused RGB565 surface fill and mask clear |
| `tinydraw_publish_tile_64x64_stride128_pie` | 64 | 64-byte vector publication loop |
| `tinydraw_copy_pixel_rows_pie` | 101 | generic pixel-row copy |
| `tinydraw_fill_pixel_rows_pie` | 78 | generic pixel-row fill |
| `tinydraw_settled_composite_esp32s3` | 178 | exact coverage/RGB565 composite, 16 B frame |
| `tinydraw_tile_uniform_color_pie` | 184 | vector uniform scan, 32 B frame |
| `paint_masked_exact_span` | 285 | exact aligned word/halfword mask stores |
| `prepare_warm_chords` | 276 | no aggregate-copy calls, 80 B frame |
| `ChordBatchBuilder::append` | 592 | no aggregate-copy calls, 128 B frame |
| `MaterializedCanvas::remember_view` | 130 | direct stores, 48 B frame |
| `SettledRenderCursor::raster_chord_row` | 510 | leaf; no calls or libm; five FP invariant spill/reloads |
| `MaterializedCanvas::choose_slot` | 643 | indexed LRU plus exact recovery scan |

The row kernel contains `sqrt0.s`, its Newton-style refinement, fused
`madd.s`/`maddn.s`, and `utrunc.s`. It has no calls to `sqrtf`, `floorf`,
`ceilf`, `lroundf`, `memcpy`, or any other routine. The final composite kernel
is 178 bytes with a 16-byte frame and no spills or calls.

## Accepted renderer and anti-aliasing changes

### Exact AA math in the hot row

Flash libm calls for square root, floor, ceil, and round were removed from
settled row/pixel loops. The S3 path uses `sqrt0.s` followed by the same
refinement shape exercised by ESP-IDF, exact integer-domain floor/ceil helpers,
and positive-domain `alpha * 255 + 0.5` conversion. Output is bit identical.

`settled_tile.cpp` uses selective `-fno-caller-saves`, and
`incremental_rasterizer_operations.cpp` uses selective
`-fira-loop-pressure`. Both raster operation objects are assigned to executable
internal memory. Wider use of either flag failed physical A/B and was removed.

### Row-specialized coverage

The settled row loop now carries the row-specific chord values directly. This
removes per-pixel `covers_pixel` calls, window-register churn, and repeated
loads. The floating multiply-add order is fixed to preserve the existing pixel
oracle. Invariant chord terms are hoisted, x is induction-driven, redundant
clears and clamps are gone, and mask scans skip complete words.

A trial that preserved five floating-point registers across rows removed more
loads statically but regressed every device zoom by 0.13–1.09%; it is not in
the final image. A pointer/count induction trial saved eight bytes but regressed
200% and 400% by about 0.36% and 0.68%; it is also absent.

### Curves without aggregate copies

Curve sampling streams through caller-owned output records. A curved unit is a
`bool` plus out-parameter operation, and rolling midpoint reuse removes three
float additions and three multiplications per forward unit. Six hot ROM
`memcpy` calls disappeared: two from `prepare_warm_chords` and four from
`ChordBatchBuilder::append`. Their frames fell from 192 to 80 B and 240 to
128 B. The curve oracle executed 5,251,680 exact assertions.

The five settled scan/stream sites also materialize each `StoredOperation`
directly into caller-owned storage. This removes five 40-byte ROM copies and
about 424 bytes of hot internal text. A physical no-outparam control was slower
at 25/50/100/200/400% by 100/129/451/1,370/4,002 us, respectively.

### Chord ordering

The final ordering path is hybrid: insertion sort handles 12 or fewer chords
and tall rows; a range-limited word-bucket pass handles dense short ranges.
Equal-`y0` ties put broad chords first, so coverage becomes useful sooner. It
has no ROM sort or string calls. Host z400 row work improved 5.44% and covers
improved 1.49%; the 976-byte symbol remains about 702 bytes smaller than the
original `std::sort` implementation.

### Mask writes and settled composite

Free eight-pixel mask spans use four aligned 32-bit stores. Halfword-phased
spans use two 16-bit and three 32-bit stores, preserving all neighboring bits.
The host oracle checked 2,173,952 cases; the device checked 8,192 cases and all
guards. Physical A/B over 49,152 calls measured 85,657 us versus 93,502 us for
the frozen halfword implementation, a 1.091x speedup.

The final settled RGB565 fold is isolated hand assembly. It passed a 524,288
exact-pixel oracle. Packed opaque and packed final-fold alternatives grew hot
text by 222 and 335 bytes without a repeatable hardware win and were removed.

## Accepted initialization, publication, and composition changes

Cold 2x2 groups initialize a 32 KiB RGB565 surface and a 2 KiB mask. The 34-byte
PIE initializer scalar-aligns the ranges, broadcasts the RGB565 pair into four
lanes, clears the mask vector, and uses `EE.VST.128.IP` in zero-overhead loops.
Fused replay initialization, paired settled initialization, and direct
`ActiveGroup` initialization remove additional duplicated clears and copies.

Tile publication is a 64-byte PIE kernel operating in 64-byte chunks. The tile
pool is aligned for it, removing 64 row `memmove` calls per publication. Generic
PIE row-copy and row-fill kernels cover raw/uniform composition and ring writes.
Producer publish, gate render, and produce paths now contain no `memcpy` or
`memmove`; important frames fell from 208 to 128 B and 128 to 64 B.

The 184-byte tile-uniform scanner rejects nonuniform vectors early and uses an
exact scalar tail. It is about 1.9x faster for late-difference and uniform host
cases, costs a net 79 product bytes, and passed 133,000 device cases.

## Accepted panning and panel changes

The panel path has three complementary PIE loops:

- equal-phase linear spans use `VLD.128.IP`, byte `VUNZIP`/`VZIP`, and
  `VST.128.IP` for 16 RGB565 pixels per iteration;
- unequal phases assemble streams with the official S3 `USAR`/`SRC.Q`
  technique used by ESP-DSP;
- full ring rows hoist panel/ring invariants, split the vertical wrap once, and
  vectorize 352 of 368 pixels while handling the exact seam scalarly.

The physical panel oracle covers 103,664 phase, length, seam, and guard cases.
An isolated A/B measured linear staging at 12,647 versus 68,212 us (5.393x)
and ring staging at 165,813 versus 302,850 us (1.826x), with equal checksums.
FreeRTOS owns and switches the CP3/PIE state; these kernels run in task context.

`ring_scroll` has a C++ fast path with no four-way remainder sequence and no
36-byte library copy. Its frame fell from 96 to 48 B and its host microbenchmark
improved 51%. Presenter row modulo and fixed panel geometry are hoisted; the
remaining row wrap is a split range rather than a per-row `% height`. The
stronger invariant form saves about 2,500–2,900 scalar instructions per sweep.

## Accepted cache and panning-reuse changes

The 604 slots now have an exact intrusive LRU index. Two 16-bit links per slot
consume 2,416 bytes from the existing allocation's tail padding; the public
slot ABI stays 24 bytes and the 19,328-byte allocation and static DRAM footprint
do not grow. Publication, touch, protection, and eviction update the list in
constant time. The exact slow scan remains only as corruption recovery.

The ready/probe path caches readiness in existing tail padding and uses scalar
leaf probes. Host probes improve 4–35% depending on occupancy. Full-pool
publication improves about 64% and a view tour about 72% in isolation. The
indexed structure passed 1,930,034 randomized exact assertions; the physical
full cache tour improves 39.15% end to end.

`remember_view` now writes fields directly, removing a 21-byte ROM copy. Its
frame falls from 80 to 48 B, linked code from 140 to 130 B, and host time by
11–16%. Eviction classification computes protection rank once per candidate
and tests fixed coordinates directly against half-open footprints.

## Accepted undo/redo and history changes

History replay borrows the stored operation and sample span directly. This
removes a 44-byte optional/accessor copy, improves sparse host replay about 34%
and dense replay about 2%, and reduces linked code by 80 bytes.

`OperationLog` and `MaterializedCanvas` scalar accessors, materialized access,
zoom lookup, and fixed zoom scaling are inline. Zoom bounds use shifts and ceil
masks for 25, 50, 100, 200, and 400%; exhaustive world-coordinate tests prove
the mapping. The linked hot paths contain no zoom division or reciprocal
multiply.

Spatial-index history queries no longer call `__popcountdi2` for telemetry when
`stats == nullptr`. Candidate order remains identical. Selective
`-finline-stringops=memcpy` is limited to materialized composition, operation
log, and history replay, where fixed 20–36-byte values expand profitably.
Variable and overlapping ranges retain `memmove` semantics.

## Physical results: cold rendering

| Cold corpus | Zoom | Baseline | Final | Change |
|---|---:|---:|---:|---:|
| overlap | 50% | 379,054 us | 289,841 us | -23.54% |
| overlap | 100% | 201,998 us | 140,989 us | -30.20% |
| overlap | 200% | 178,487 us | 116,424 us | -34.77% |
| overlap | 400% | 137,267 us | 89,225 us | -35.00% |
| adversarial tapered + hairlines | 50% | 306,891 us | 235,757 us | -23.18% |
| adversarial tapered + hairlines | 100% | 302,716 us | 236,321 us | -21.93% |
| adversarial tapered + hairlines | 200% | 382,066 us | 309,762 us | -18.92% |
| adversarial tapered + hairlines | 400% | 418,428 us | 334,164 us | -20.14% |
| owner torture | 50% | 52,619 us | 31,758 us | -39.65% |
| owner torture | 100% | 66,845 us | 44,323 us | -33.69% |
| owner torture | 200% | 127,976 us | 89,265 us | -30.25% |
| owner torture | 400% | 263,077 us | 184,721 us | -29.78% |
| seed 7 | 400% | 142,572 us | 96,882 us | -32.05% |
| capacity hairlines | 100% | 167,185 us | 115,977 us | -30.63% |
| capacity hairlines | 400% | 85,163 us | 49,502 us | -41.87% |

## Physical results: pan, cache, and history

| Workload | Baseline | Final | Change |
|---|---:|---:|---:|
| direct pan compose, 100% | 18,796 us | 9,088 us | -51.65% |
| direct pan compose, 400% | 18,749 us | 9,076 us | -51.59% |
| ring staging, 100% | 8,445 us | 3,578 us | -57.63% |
| ring staging, 400% | 8,445 us | 3,568 us | -57.75% |
| saturated 604-slot cache tour | 1,101,367 us | 670,196 us | -39.15% |
| history max, 400% per-publication | 427,143 us | 327,120 us | -23.42% |
| history max, 400% holdback | 113,255 us | 86,352 us | -23.75% |
| history max, 200% per-publication | 197,958 us | 149,973 us | -24.24% |
| history max, 200% holdback | 62,333 us | 48,757 us | -21.78% |

All 48 pan-sequence frames reuse the ring. At 100%/400%, exposed composition
averages 4,094/4,180 us, chrome 2,045/1,923 us, and ring staging 3,578/3,568 us.
Mean frame time is about 33.4 ms under the panel's tear-synchronized pacing;
every tear edge is observed and every strip stays inside its wire budget.

## Physical results: settled anti-aliasing

| Zoom | Baseline | Final | Change |
|---:|---:|---:|---:|
| 25% | 77,964 us | 64,532 us | -17.23% |
| 50% | 90,633 us | 74,900 us | -17.36% |
| 100% | 209,463 us | 162,682 us | -22.33% |
| 200% | 525,648 us | 393,859 us | -25.07% |
| 400% | 1,458,597 us | 1,072,829 us | -26.45% |

Long-stroke full-bounds and narrowed renders retain identical checksums at all
five zooms. The final maximum cooperative slice is 1,588 us in the primary
settled sweep.

## Exactness and hardware gates

The final physical kernel marker reports:

| Kernel family | Device cases |
|---|---:|
| panel staging | 103,664 |
| tile publication | 64 |
| producer initialization | 7,352 |
| pixel row copy | 3,456 |
| pixel row fill | 144 |
| tile uniform scan | 133,000 |
| settled composite | 2,048 gate vectors plus 524,288 host pixels |
| masked exact spans | 8,192 device plus 2,173,952 host cases |

All guards, checksums, cache invariants, exports, history mechanics, touch
latency guards, and native verdicts pass. The gate's export uses 291,484 bytes
of peak workspace and leaves 37,772 bytes of internal heap at that point.

Host verification after the final changes:

- debug: 31/31 CTest targets pass;
- release: 31/31 CTest targets pass;
- ASan/UBSan: 13/13 selected targets pass with no diagnostics;
- formatting and `git diff --check`: clean.

## Compiler and assembly experiments rejected by measurement

### String operations

Broad `-finline-stringops`/`memcpy` expansion across raster, settled, producer,
and presenter code regressed cold rendering by 1–38% and settled work by 3–8%.
Code growth and instruction-cache pressure outweighed the removed ROM calls.
The final policy expands only three translation units with proven fixed-size
copies; producer and panel bulk work uses explicit PIE kernels.

### Register allocation and scheduling

Presenter scheduling flags regressed physical pan. Wide `-fno-caller-saves`
regressed cold, pan, and cache workloads. Target alignment, no-target-align,
loop unrolling/peeling, modulo scheduling, `-O3`, automatic literal pools, and
Xtensa vector compiler switches did not survive hardware A/B.

`-mextra-l32r-costs` added nine bytes and synthesized internal constants with
more instructions. `-mforce-l32` is rejected by the Espressif compiler.
`-mno-serialize-volatile` does not change raster code and is unsafe for the
firmware's ISR/atomic boundaries. These flags are absent.

### Cache and IRAM configurations

A 32 KiB I-cache consumes another 16 KiB of internal RAM and makes the real
presenter fail bootstrap. A 64 KiB D-cache would leave about 5 KiB projected
export slack. Four-way cache associativity has no RAM benefit here. Moving the
ring-copy C++ symbol into IRAM did not improve its PSRAM-dominated loop and was
removed. The accepted 64-byte D-cache line keeps cache capacity and associativity
constant and improved sequential PSRAM traffic.

### LTO

ESP-IDF 6.0.2 supplies `-fno-lto` and no supported `CONFIG_COMPILER_LTO` for
this build. Forced whole-project LTO hit an Espressif GCC IPA-ICF internal
compiler error. Main-component-only LTO broke object-granularity linker
fragments, moved hot raster text back to flash, and grew total text. LTO is not
used.

### Algorithmic trials

The first stable counting sort was mixed and regressed adversarial rows by up
to about 2.1%; it was superseded by the broad-first hybrid. An arbitrary
five-plane settled initializer was projected below 0.02%. Packed opaque
composite, packed final fold, extra preserved FP state, row pointer/count
induction, and an IRAM ring-copy mapping all failed physical measurement.

## Remaining native opportunities and constraints

- GCC does not autovectorize for ESP32-S3 PIE. Further SIMD wins require small,
  ABI-isolated assembly kernels with exact phase/tail oracles.
- Remaining floating divides are per chord, not per row or pixel. Reciprocal
  approximations need checksum-proven refinement before they are competitive.
- Cache eviction is O(1) in normal operation; the 604-slot scan remains as an
  exact recovery path, so further work there has low dynamic priority.
- The settled comparison tables remain branches because ESP-IDF supplies
  `-fno-jump-tables`; executable-internal pressure makes table relocation a
  poor current trade.
- Panel transfer and tear pacing now dominate reused-frame wall time. CPU-only
  staging work is near 3.6 ms, while the 40 MHz SPI/panel schedule sets the
  roughly 33.4 ms frame cadence.
- CP3/PIE state is managed by FreeRTOS and costs context space on first use.
  Assembly kernels must remain task-only and preserve the configured ABI.

## Glass-test follow-up — 2026-08-23

The first post-optimization glass test found no tearing, raster corruption, or
PNG mismatch. It did expose one pre-existing SVG defect and one independent
history/presentation integration defect. The follow-up experiments were:

### Accepted fixes and coverage

1. **Attached sharp-reversal SVG caps:** the downloaded SVG contained six
   detached internal cap circles (two in path 36 and four in path 37). A
   three-sample reversal reproduced the defect deterministically. Shared-boundary
   export now emits both tails through the sharp sample before its round joint,
   so the joint intersects the ribbon. The PNG/live raster path is unchanged.
2. **Proved the SVG defect predates this optimization round:** none of the
   optimized commits changed ribbon geometry or SVG export before the fix. Git
   history identifies `0a5fad8` (`fix: round sharp curved stroke joins`,
   2026-08-10) as the introduction point.
3. **Committed repeated mixed-history coverage:** six alternating pen/eraser
   Strokes, each split across two operations, survive 32 rounds of five Undo
   and five Redo moves. All 320 intermediate states match independent replay,
   tiled occupancy, counts, revisions, epochs, timeline, and availability.
4. **Repaired history after immediate feedback failure:** deterministic Puck
   transport fault injection proved that a committed Undo could return false
   only because its dock and fallback presentations failed. The app then
   skipped autosave, damage handoff, and the 25% hard-pixel refill, leaving the
   old stroke on screen for at least 992 ms. A committed history move now
   reports semantic success; presentation failure remains in telemetry and the
   normal held refill repairs the frame.
5. **Restored the Puck aligned-allocation shim:** the native tile-pool alignment
   change exposed a missing `heap_caps_aligned_alloc` implementation in the
   emulator. The shim validates power-of-two alignment, rounds size safely,
   and restores the Puck build used by the history fault test.

### Investigated and not changed

1. **Empty SVG G2/G3 groups:** these are the first two logical eraser gestures,
   nested before any of the 43 pen gestures. They are valid, visually inert
   masks. Empty wrappers cost 58 bytes (0.0046%); removing the complete no-op
   erasers would save about 1.08% but break the one-path-per-gesture export
   contract and change Inkscape's path count from 45 to 43.
2. **SVG-wide geometry scan:** all 17,496 subpaths parse. The six sharp-turn
   circles above are the only detached internal circles; path 19 is a valid
   standalone dot. No second SVG defect was found.
3. **Core Undo/Redo corruption:** not reproduced. Three fixed corpora survived
   2,000 cycles, a temporary mixed harness survived 10,000 byte-exact
   transitions, 128 randomized documents survived 1,280 differential
   transitions, and the committed 320-transition property test passes under
   Debug, Release, and ASan/UBSan.
4. **Six-poll touch lift debounce:** two contacts separated by fewer than six
   no-touch polls intentionally merge into one gesture; an Undo-to-Redo jump
   can therefore resolve as one averaged tap. This was reproduced but not
   changed because the same debounce prevents documented controller dropouts
   from splitting one finger into two actions.
5. **Sixteen-event touch queue saturation:** one through eight queued taps keep
   every edge; a ninth edge-only tap invokes the documented resynchronization
   policy and drops older backlog. This needs much more extreme input than the
   narrated five-step sequence and was not changed. The ordinary Puck history
   trace, full Puck suite, and semantic touch-buffer tests remain green.

The follow-up commits are intentionally granular: `e327864` fixes SVG caps,
`11762a1` adds mixed-history stress coverage, `a601f3b` repairs the Puck
allocation shim, and `de3345c` fixes history presentation recovery with its
fault-injection regression.

## Evidence retained for this session

Baseline and accepted physical logs:

- `/tmp/tinydraw-perf-baseline.log`
- `/tmp/tinydraw-perf-third-pass.log`
- `/tmp/tinydraw-perf-fourth-pass.log`
- `/tmp/tinydraw-perf-fourth-aligned.log`
- `/tmp/tinydraw-perf-fifth-pass.log`
- `/tmp/tinydraw-perf-sixth-pass.log`
- `/tmp/tinydraw-perf-sixth-repeat.log`
- `/tmp/tinydraw-perf-seventh-pass.log`
- `/tmp/tinydraw-perf-eighth-pass.log`
- `/tmp/tinydraw-perf-ninth-pass.log`
- `/tmp/tinydraw-perf-tenth-pass.log`
- `/tmp/tinydraw-perf-eleventh-pass.log`
- `/tmp/tinydraw-perf-twelfth-repeat.log`
- `/tmp/tinydraw-perf-thirteenth-final-gate.log` — final accepted gate image
- `/tmp/tinydraw-perf-fourteenth-settled-control.log` — no-outparam A/B control
- `/tmp/tinydraw-product-final.log` — normal product boot/run after restoration

Rejected physical A/B logs retained include
`/tmp/tinydraw-perf-inline-stringops.log`,
`/tmp/tinydraw-perf-selective-gcc-flags.log`,
`/tmp/tinydraw-perf-no-caller-saves.log`,
`/tmp/tinydraw-perf-icache32.log`,
`/tmp/tinydraw-perf-iram-ring-copy.log`, and
`/tmp/tinydraw-perf-fold-pair.log`. Compile-only candidates were decided from
their linked ELF/objects; they are labeled separately in the ledger above.

Host logs are `/tmp/tinydraw-host-debug-20260822.log`,
`/tmp/tinydraw-host-release-20260822.log`, and
`/tmp/tinydraw-host-asan-20260822.log`.

## Architecture and toolchain references

- [ESP32-S3 technical reference manual](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [ESP-DSP ESP32-S3 arbitrary-phase PIE memcpy](https://github.com/espressif/esp-dsp/blob/master/modules/support/mem/esp32s3/dsps_memcpy_aes3.S)
- [ESP-DSP kernels](https://github.com/espressif/esp-dsp)
- [ESP-IDF 6.0.2 Xtensa project flags](https://github.com/espressif/esp-idf/blob/v6.0.2/components/soc/project_include.cmake)
- [ESP-IDF ESP32-S3 floating-point refinement test](https://github.com/espressif/esp-idf/blob/v6.0.2/components/esp_hw_support/test_apps/esp_hw_support_unity_tests/main/test_fp.c)
- [ESP-IDF ESP32-S3 cache Kconfig](https://github.com/espressif/esp-idf/blob/v6.0.2/components/esp_system/port/soc/esp32s3/Kconfig.cache)
- [ESP-IDF external RAM guide](https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32s3/api-guides/external-ram.html)
- [ESP-IDF ESP32-S3 performance guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/performance/speed.html)
- [ESP-IDF ESP32-S3 CP3/PIE definitions](https://github.com/espressif/esp-idf/blob/v6.0.2/components/xtensa/esp32s3/include/xtensa/config/tie.h)
- [FreeRTOS Xtensa coprocessor context](https://github.com/espressif/esp-idf/blob/v6.0.2/components/freertos/FreeRTOS-Kernel/portable/xtensa/include/freertos/xtensa_rtos.h)
- [ESP-IDF SPI LCD API](https://github.com/espressif/esp-idf/blob/v6.0.2/components/esp_lcd/include/esp_lcd_io_spi.h)
- [ESP-IDF SPI LCD implementation](https://github.com/espressif/esp-idf/blob/v6.0.2/components/esp_lcd/spi/esp_lcd_panel_io_spi.c)
- [GCC Xtensa options](https://gcc.gnu.org/onlinedocs/gcc/Xtensa-Options.html)
- [Espressif GCC Xtensa backend](https://github.com/espressif/gcc/blob/esp-15.2.0_20251204/gcc/config/xtensa/xtensa.cc)
- [Espressif GCC Xtensa machine description](https://github.com/espressif/gcc/blob/esp-15.2.0_20251204/gcc/config/xtensa/xtensa.md)
