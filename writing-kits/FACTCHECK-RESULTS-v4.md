## D4

**Overall verdict: NUANCED.** The P.S. block is mostly receipted, but two bullets overstate what the evidence proves: the app-side “two squares” detail is not recoverable, and the evil-hairline near-doubling is not an isolated A/B.

1. **Wi-Fi blamed for psychedelic stripes — CONFIRMED; this was ESP32, not RP2350.** The ESP32-S3 V1 record says random colored lines appeared at requested 80 MHz, remained after Wi-Fi was removed, and led to a stable lower-clock build. Wi-Fi was removed before the RP2350 port. Receipt: `docs/archive/2026-08-raster-and-vector-prototypes/FINDINGS.md:159-175`.
2. **Requested 40/50/60 MHz all actually 40 MHz — CONFIRMED.** All three requests measured 17.998–17.999 ms; the GPSPI divider made each an actual 40 MHz configuration. Receipt: `docs/receipts/hardware/CO5300_PANEL_LIMITS_2026-08-15.md:30-51`.
3. **GETSCANLINE and control reads all zero — CONFIRMED for every control read that was probed.** GETSCANLINE, RDDID, RDDST, RDDPM, RDDMADCTL, RDDCOLMOD, and brightness readback all returned zero. Receipt: `docs/receipts/hardware/CO5300_PANEL_LIMITS_2026-08-15.md:103-112`.
4. **Internal scratch predicted ≥40%; measured −0.36% raster — CONFIRMED.** The controlled identical-workload A/B measured raster 470.262→468.570 ms (0.36%) and total wall 588.240→578.317 ms (1.69%), decisively missing the preregistered ≥40% raster prediction. Receipt: `docs/archive/2026-08-raster-and-vector-prototypes/PROTOTYPE_EXIT.md:31-57`.
5. **“Sacred” 1.5 MiB export reserve; actual peak 291,484 B — CONFIRMED.** The 1.5 MiB gate was a synthetic allocate/free, while the all-ones SVG+PNG export measured a 291,484-byte concurrent peak and a structural worst case around 320 KiB. Receipt: `benchmark-results/export-memory-math-2026-08-18/RECEIPT.md:1-45`.
6. **“512-slot” run was actually 384 — CONFIRMED for the mislabeled `efb1586` run.** The receipt says the log was renamed from a mislabeled `-512` name and the harness default had silently selected 384 slots. A separate real 512-slot run also existed and failed the reserve gate, so retain the “mislabeled run” context. Receipt: `docs/receipts/vector-v2/PAN_DESIGN_EXPERIMENTS_2026_08_15.md:19-27`.
7. **Word-mask scans 7–13% slower; `callx8` memcpy loads — CONFIRMED.** Device performance regressed 7–13%; disassembly showed GCC-Xtensa refusing to inline unaligned `memcpy` word loads and emitting `callx8` per load. Receipt: `benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md:86-96`.
8. **4-sample SSAA: 808 ms, rejected — CONFIRMED by the retained campaign record.** Receipt: `docs/PERFORMANCE_CHRONICLE.md:23-29`; the release contract also calls it a measured 808 ms/frame probe at `docs/archive/2026-08-vector-v2-performance/SHIP_CONTRACT_PRE_RELEASE_2026-08-19.md:85-92`.
9. **Color popup magenta; black/white hid the byte swap — CONFIRMED.** The photo showed hue-rotated colors and a magenta dark-gray swatch, while black/white were unchanged because 0x0000 and 0xFFFF are byte-swap invariant. Receipt: `docs/receipts/vector-v2/COLOR_POPUP_BYTESWAP_INCIDENT_2026_08_18.md:14-34`.
10. **Pen-size selector also fired Redo and corrupted the UI — CONFIRMED.** The second physical occurrence fired both actions in one gesture; the receipt quotes “this time I tapped redo” and “the whole UI just gets fucked.” Receipt: `docs/receipts/vector-v2/TRANSIENT_CHROME_POPUP_INCIDENT_2026_08_18.md:72-92`.
11. **Two SVG dots, none in PNG, two app squares; then parity + top-edge fixes — NUANCED.** Two one-sample SVG dots absent from PNG are confirmed, as are the cross-renderer fix and the separate top-edge contact-admission fix. The surviving receipts describe dots/raw hard-tile marks, not “two squares on the actual app”; that shape detail is **unsupported / not recoverable**. Receipts: `docs/receipts/vector-v2/ONE_SAMPLE_STROKE_DOT_BUG_2026_08_19.md:12-40,52-67`; `docs/receipts/vector-v2/PHANTOM_TOP_EDGE_CONTACT_2026_08_19.md:3-14,17-38,59-74`.
12. **Fuji classifier detour; “over an hour?” — CONFIRMED.** The automated path spent about 40 minutes flailing on registration/metadata/counters before the owner said “we've been at this for... over an hour?”; two contact sheets supplied the sufficient instrument. Receipt: `.codex-archaeology/session-history.md:139-146`.
13. **Tear near zoom-minus moved two-thirds down the minimap; last closure before tear-free — CONFIRMED.** The exact observations occurred at 22:59 and 23:06; at 23:41 the owner reported tearing gone. Receipt: `.codex-archaeology/session-history.md:157-163`.
14. **Flash-I-cache layout moves timing ±2–3% — CONFIRMED as the documented build-to-build law.** The ledger records unrelated layout changing the byte-swap loop and strip staging, with ±2–3% cold variation. Receipt: `.codex-archaeology/docs-performance.md:57-60`.
15. **40 KiB PSRAM workspace cost +9 ms mid-heap and 0 ms dead-last — CONFIRMED.** Receipt: `docs/MEMORY_MAP.md:9-23`; the performance chronicle records the same allocation-order result at `docs/PERFORMANCE_CHRONICLE.md:42-48`.
16. **Sub-500 ms benchmark stopped before pixels reached glass — CONFIRMED, but it was an invalid early LOD result.** The fixed-spacing LOD appeared below 500 ms while timing cache-ready before final display transfer. Receipt: `docs/archive/2026-08-raster-and-vector-prototypes/VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md:94-96`.
17. **“Mathematically exact AA optimization; slower on almost every case” — NUANCED because the label is ambiguous.** The shift/add `/255` identity was exhaustive/checksum exact and regressed most representative host cases by 0.3–2.2%; the separate blank-pixel final-fold branch regressed every representative case by about 2–11%. Receipts: `docs/receipts/vector-v2/AA_PERFORMANCE_EXPERIMENTS_2026_08_19.md:24-25,34-41`.
18. **Evil hairlines absent from the good benchmark; almost doubled cold time — NUANCED.** The later combined corpus added 782 hairline operations/8,061 samples and measured 1,269.157 ms, while the older retained result was 663.829 ms (1.91×). However, the receipt says the older result used straight authority and was superseded by curved authority before the combined corpus, so this is chronology, not an isolated hairlines-only A/B. Receipt: `benchmark-results/wave2-compositor/COLD_GENERAL_BASELINE_RECEIPT.md:9-31,39-41`.
19. **1×2 supertasks hit the watchdog before timing — CONFIRMED.** Duplicated setup starved CPU0's idle task for five seconds and tripped the task watchdog before any result. Receipt: `docs/receipts/vector-v2/COLD_RENDER_EXPERIMENTS_2026_08_19.md:22-27`.
20. **Fast LOD deleted loops, hairpins, pressure peaks, and eraser dabs — CONFIRMED.** Receipt: `docs/archive/2026-08-raster-and-vector-prototypes/VECTOR_CANVAS_OPTIMIZATION_CHRONICLE.md:94-96`.

## D5

**Verdict: NUANCED.** Two recoverable ledgers exist with different scopes. They must not be added together: the pre-8/22 file is a curated cross-campaign cold inventory, while the 8/22 receipt is a separately numbered native/compiler/assembly round. The latter explicitly defines accepted/rejected at `docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:40-45`.

### Landed / accepted

**Pre-8/22 curated inventory — 18 named interventions:**

1. Stateless windowed span search
2. Device-native arithmetic
3. Internal-SRAM producer scratch
4. Once-per-endpoint prepared curve units
5. Unit-merged masked row sweeps
6. Caller-split painters
7. Strided publish from the supertask surface
8. H7 operation-level chord sweep
9. O(1) raw-slot metadata directory
10. Honest work-budget slices
11. IRAM transport pin
12. Incremental-rasterizer IRAM placement
13. Settled-code IRAM placement
14. Settled-AA saturated-destination skip
15. Settled-AA long-chord row narrowing
16. Overlap cold fix
17. Redundant-preflight removal
18. Settled-AA dense saturation aggregation

Exact inventory rows: `.pi/plans/2026-08-21-cold-optimization-inventory/scout-context.md:9-18,20-28,30-40`. Major direct receipts: Wave 3 at `benchmark-results/wave3-cold-compute/COLD_COMPUTE_CAMPAIGN_RECEIPT.md:45-84`; Stage B at `benchmark-results/cold-stage-b-2026-08-16/RECEIPT.md:102-128`; raster IRAM at `docs/receipts/vector-v2/F24_RASTER_IRAM_AB_2026_08_18.md:3-43`.

**8/22 native round — 35 accepted experiment rows, of which 34 are optimization/code changes and row 35 is a gate suite:**

1. Native settled-AA math
2. Raster-operation IRAM placement
3. Raster IRA loop pressure
4. Telemetry-free spatial queries
5. Inline scalar metadata/zoom accessors
6. Exact shift/mask zoom bounds
7. Equal-phase panel PIE staging
8. Producer PIE initialization
9. Presenter row/ring invariant hoists
10. Single-pass cache-victim classification
11. 24-byte cache-slot packing
12. Selective fixed-copy expansion
13. Selective settled caller-save suppression
14. 64-byte D-cache lines
15. Rolling curve-sample windows
16. Curve/chord out-parameters
17. Settled chord invariant hoists
18. Fused replay/settled initialization
19. Direct `ActiveGroup` lifecycle
20. PIE tile publication
21. Row-specialized coverage
22. Word-at-a-time finalized-mask scans
23. Scalar leaf cache probes
24. Arbitrary-phase panel PIE staging
25. Generic PIE RGB565 copy/fill
26. Handwritten settled composite
27. PIE tile-uniform scanner
28. Division-free `ring_scroll`
29. Borrowed history replay
30. Caller-owned producer/log results
31. Hybrid chord ordering
32. Intrusive indexed LRU
33. Exact masked-span word stores
34. Direct recent-view stores
35. Native kernel gate suite (verification infrastructure, not itself a speed optimization)

Exact numbered ledger: `docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:47-127`.

### Rejected / reverted

**Pre-8/22 curated inventory — 20 listed rows, not the summary's claimed 22:**

1. 4-sample SSAA
2. Word-mask window scans
3. Summary-bitmap row saturation probe
4. 6×2-tile band replay
5. Shared hybrid warm/seeded search
6. Word-mask retry with `l32i`
7. Flat 128-row work slices
8. Scanline recurrence
9. Publication batching
10. Prepared geometry cache
11. Adaptive AA bands
12. Cross-tile candidate reuse
13. 2×4 producer supertask
14. 1×2 producer supertask
15. 96-operation scan batches
16. Internal scratch for settled raster
17. Blank-pixel final-fold AA path
18. Shift/add `/255`
19. Row-local touched-span merge
20. Early conservative per-row capsule spans

Exact inventory rows: `.pi/plans/2026-08-21-cold-optimization-inventory/scout-context.md:44-79`. The file's “22” appears only in its inconsistent summary at lines 83-86. Additional no-gos do exist elsewhere—including all-opaque contribution shortcut (`docs/receipts/vector-v2/AA_PERFORMANCE_EXPERIMENTS_2026_08_19.md:27-30`), completed-group deferral removal, and curved segment-chunk metadata (`docs/receipts/vector-v2/PERFORMANCE_REVIEW_ROUND_2026_08_18.md:453-468`)—but no recorded rule selects exactly two of them, so 22 is **unsupported / not recoverable**.

**8/22 native round — 23 rejected/superseded ledger rows:**

1. Broad string-operation expansion
2. Presenter scheduling flags
3. Wide `-fno-caller-saves`
4. 32 KiB I-cache
5. 64 KiB D-cache
6. Four-way D-cache
7. IRAM C++ ring copy
8. Whole/main-component LTO
9. Packed opaque composite
10. Packed final fold
11. Arbitrary five-plane settled initializer
12. First stable counting sort
13. Preserving five FP registers
14. Settled-row pointer/count induction
15. `-mextra-l32r-costs`
16. `-mno-serialize-volatile`
17. `-mforce-l32`
18. Target-alignment variants
19. Unrolling/peeling/modulo scheduling/`-O3`/literal pools/vector switches
20. `-fno-math-errno` and reciprocal math
21. `-flate-combine-instructions`
22. Other compile-only flag screens
23. Small settled static rewrites

Exact numbered ledger: `docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:129-176`; retained physical-log paths are recorded at lines 595-602.

## D6

**Verdict: NUANCED. The sharp-reversal SVG fix landed and host tests pass; the old device CRC is not a post-fix receipt, and “proper paths for all shapes” is too broad.**

- **Fix commit:** `e3278646d74d1f692ca412656ecc8264655405a0` (`Fix detached SVG caps at sharp reversals`), currently an ancestor of HEAD `915d71cd67a6765b9fdd4890eb92fef89726a249`. The commit changes `core/src/ribbon_geometry.cpp` and adds the three-sample sharp-reversal regression in `vector_v2/tests/svg_export_test.cpp:472-493`.
- **Failure and fix contract:** six detached cap circles were found; shared-boundary export now emits both legs through the sharp sample so its joint intersects the ribbon; PNG/live raster geometry is unchanged. Receipt: `docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:515-529`.
- **Host result on current HEAD:** authority/export suite **95/95 tests, 39,291/39,291 assertions**; FAT16 export suite **4/4 tests, 54/54 assertions**. Durable command/output receipt: `writing-kits/receipts/d6-svg-host-tests-2026-08-23.log:1-17`.
- **8/22 CRC claim after the fix:** **unsupported as a post-fix hardware claim.** The recorded device gate was bit exact with export CRC `e40499d1` before this targeted SVG geometry correction (`docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:15-17`). A sharp-reversal document's SVG bytes intentionally change after the fix, and no post-fix device export/CRC run was performed here. Raster/PNG bit-exactness is not challenged because the fix is limited to `RibbonSpanJoin::kSharedBoundary` and the receipt explicitly says PNG/live raster is unchanged (`...ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:519-523`).
- **“Our SVG exports have proper paths for all shapes”: NUANCED / not literally supportable.** Vector V2 has only pen and eraser operation types, not a general shape-tool model (`vector_v2/include/tinydraw/vector_v2/operation.h:12-15`). Current tests cover exact pen/eraser ordering, operation-chunk continuity, shared boundaries, sharp reversals, dots, empty output, 300 randomized authority documents, and maximum-capacity streaming (`vector_v2/tests/svg_export_test.cpp:309-644`). The inspected real SVG had 17,496 parseable subpaths and no second defect beyond the six corrected sharp-turn caps (`docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:547-552`). That supports “proper paths for the supported pen/eraser geometries we tested,” not universal “all shapes.”

## D9

**Verdict: CONTRADICTED as a combined count.** “18 landed” has a recoverable curated list; “22 rejected” does not. The 8/22 round does not reconcile 16→18.

- The pre-8/22 inventory visibly contains **18** landed rows: 6 Wave 3 + 5 Stage B + 7 additional (`.pi/plans/2026-08-21-cold-optimization-inventory/scout-context.md:9-18,20-28,30-40`). The definitive names are listed under D5 above.
- The same inventory visibly contains **20**, not 22, rejected rows: 5 Wave 3 + 2 Stage B + 13 general (`.pi/plans/2026-08-21-cold-optimization-inventory/scout-context.md:44-79`). Its summary nevertheless says “16” and “22” without deriving either (`.pi/plans/2026-08-21-cold-optimization-inventory/scout-context.md:83-86`). `EDITOR_HANDOVER_2026_08_21.md:16-24` repeats 16/22 as verified but supplies no reconciliation.
- **18 is not “16 plus two post-8/21 additions.”** The 8/22 native ledger contains 35 accepted experiment rows—34 optimization/code changes plus one gate suite—not two (`docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:47-127`). Counts are not safely additive because this later ledger has a different scope and refines earlier interventions such as IRAM placement.
- Publication-safe count: **“18 named landed interventions in the curated pre-assembly inventory.”** A definitive project-lifetime rejected count is **unsupported / not recoverable**. For the separately scoped 8/22 native round, the exact count is 35 accepted experiment rows and 23 rejected/superseded rows (`...ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:47-176`).

## D10

**Verdict: CONTRADICTED for “320kb SRAM” and “200kb IRAM”; CONFIRMED for this board's 8 MiB PSRAM; NUANCED for the 7–11% speed claim.** Also use **KB/KiB**, not lowercase `kb` (which denotes bits).

- **“320 KB SRAM” — CONTRADICTED.** Espressif specifies **512 KB on-chip SRAM total**, shared among instruction, data, and cache uses; it is not a fixed 320 KB data-SRAM bank. Authoritative datasheet: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf (Features / Internal Memory). ESP-IDF explains that SRAM not used as IRAM is available as DRAM: https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/memory-types.html. TinyDraw's own dated application map reports a 341,760-byte DIRAM pool, not 320 KB (`docs/MEMORY_MAP.md:32-41`).
- **“200 KB IRAM” — CONTRADICTED.** ESP32-S3 has no separate fixed 200 KB IRAM bank; instruction and data use share internal SRAM. The final TinyDraw product reports 127,587 B executable internal text and a dedicated 16 KiB IRAM region, with 187,217 B DIRAM remaining—not a 200 KB hardware IRAM allotment (`docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:182-199`).
- **“8 MB PSRAM” — CONFIRMED for the ESP32-S3R8 board.** The project enables octal 80 MHz PSRAM (`esp32/sdkconfig.defaults:1-6`), and the physical-device receipt states 8 MiB octal PSRAM (`docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:19-26`). Espressif's S3R8 part table specifies 8 MB Octal SPI PSRAM: https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf.
- **“Moving the rasterization core here gave an immediate 7–11% boost” — NUANCED.** Whole-object placement in executable internal memory is confirmed by `esp32/main/linker.lf:20-28`. The same-tree 11-case physical A/B improved **compute** by 6.93–11.68%, median 8.70%, with no regression (`docs/receipts/vector-v2/F24_RASTER_IRAM_AB_2026_08_18.md:20-37`). Conventionally rounded, the range is **7–12%**, not 7–11%. It cost 10,836 B product/13,108 B gate IRAM and roughly 13 KiB internal heap (`...F24_RASTER_IRAM_AB_2026_08_18.md:10-18,39-43`). The retained baseline explicitly says this is compute time, not paced wall or panel time, and no same-tree wall result survived (`docs/receipts/vector-v2/FINAL_PERFORMANCE_BASELINE_2026_08_18.md:15-20,40-44`). “Immediate” is supportable only as a direct isolated A/B result, not as an immediate visible/end-to-end speedup.

## D14

**Verdict: NUANCED, high confidence.** The narrow claim is confirmed: Espressif Xtensa GCC does not auto-vectorize ordinary C/C++ into ESP32-S3 PIE/SIMD instructions. The broad wording “Xtensa GCC built-in vectorization has none” is misleading because GCC does have generic loop and SLP vectorizer passes.

- **GCC has generic built-in vectorizers.** GCC documents `-ftree-loop-vectorize` and `-ftree-slp-vectorize` as enabled at `-O2`: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#index-ftree-loop-vectorize and https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#index-ftree-slp-vectorize.
- **The exact Espressif GCC 15.2 backend used by TinyDraw lacks ESP32-S3 PIE vector lowering.** Project toolchain/flags: `docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:21-26`. The pinned tag resolves to GCC commit `0dbf584943ac179894690b389f3a37926bb4cd33`. GCC's default target hook contributes no auto-vector modes: https://github.com/espressif/gcc/blob/0dbf584943ac179894690b389f3a37926bb4cd33/gcc/targhooks.cc#L1583-L1589. The Xtensa backend tree contains no override, PIE vector modes, or `EE.*` patterns: https://github.com/espressif/gcc/tree/0dbf584943ac179894690b389f3a37926bb4cd33/gcc/config/xtensa. Its target builtin table contains only `__builtin_umulsidi3`, not PIE intrinsics: https://github.com/espressif/gcc/blob/0dbf584943ac179894690b389f3a37926bb4cd33/gcc/config/xtensa/xtensa.cc#L4671-L4694.
- **The absence persists in later audited trees.** Espressif GCC `esp-16_1_0` at `345fca0a988a78fd3bc05b132e3dabfdec33a96f`: https://github.com/espressif/gcc/tree/345fca0a988a78fd3bc05b132e3dabfdec33a96f/gcc/config/xtensa. Upstream GCC master at `34762afa322a6556adfc7a83381a6f6ba5c3137b`: https://github.com/gcc-mirror/gcc/tree/34762afa322a6556adfc7a83381a6f6ba5c3137b/gcc/config/xtensa.
- **ESP32-S3 does have 128-bit PIE hardware; the practical route is assembly/library kernels.** Espressif's TRM describes PIE SIMD/vector operations: https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf. ESP-DSP distinguishes ANSI C from `_aes3` ESP32-S3 assembly: https://github.com/espressif/esp-dsp/blob/3c12d05/docs/en/esp-dsp-library.rst#L68-L78. Its memcpy kernel contains `ee.vld.128.ip`, `ee.vst.128.ip`, and `ee.src.q`: https://github.com/espressif/esp-dsp/blob/3c12d05/modules/support/mem/esp32s3/dsps_memcpy_aes3.S#L52-L61.
- **TinyDraw's local evidence matches the backend audit.** Six hand-written `.S` kernels are in the firmware (`esp32/main/vector_v2/sources.cmake:9-14`); the panel kernel contains explicit 128-bit PIE loads/unzip/zip/stores (`esp32/main/vector_v2/panel_staging_esp32s3.S:1-24`). The linked inventory and physical A/B record those kernels and 5.393× linear / 1.826× ring staging gains (`docs/receipts/vector-v2/ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:201-218,310-322`). The same campaign tried compiler vector switches without retaining a win and concludes that further PIE SIMD requires ABI-isolated assembly (`...ESP32S3_NATIVE_DISASSEMBLY_PERFORMANCE_2026_08_22.md:456-466,493-508`).

**Publication-safe statement:** “GCC has generic auto-vectorization, but its Xtensa backend does not generate the ESP32-S3's PIE SIMD instructions; those wins required hand-written assembly.” This can be stated flat with high confidence; no hedge is needed if the scope remains “auto-vectorization to ESP32-S3 PIE.”
## D3

**VERDICT: unsupported — no recoverable 1,024-sample cap.** The dedicated inking archaeology explicitly classifies the remembered hard numeric cap as unsupported and identifies the nearby confirmed mechanism as an every-64-samples defensive tile-copy pause of roughly 70 ms (`.codex-archaeology/origin-v1-v2-inking-quote-pack-2026-08-22.md:425-435`). The contemporaneous engineering receipt records `append_max_us=70214/72144` at each 64-sample chunk boundary (`docs/receipts/vector-v2/LONGSTROKE_COLDRENDER_INVESTIGATION_2026_08_14.md:1-8`) and attributes it to synchronous copy-out/replay/copy-back plus repeated 8 KiB scans (`docs/receipts/vector-v2/LONGSTROKE_COLDRENDER_INVESTIGATION_2026_08_14.md:85-106`). The retained design explicitly kept the 64-sample chunk while replacing those copies with validate-first in-place commit (`docs/receipts/vector-v2/LONGSTROKE_COLDRENDER_INVESTIGATION_2026_08_14.md:128-148`). The only documentation hit connecting 1,024 to a relevant capacity is an unrelated temporary **measurement ring** raised to 1,024 entries (`docs/archive/2026-08-code-reviews/review-findings/2026-08-12-noon/RESPONSE.md:99`), not an ink/sample limit. Recommend telling this at remembered depth without “1,024.”

## D8

**VERDICT: unsupported / not recoverable for Sunday delivery; Monday testing is confirmed.** The first physical-board statement in the surviving chronology is Sarah’s “good morning it arrived display CO5300 touch CST820 is what i see” at **2026-08-10 08:22 BST**, message `8717f762`, raw Pi-session line 2590 (`.codex-archaeology/origin-v1-v2-inking-quote-pack-2026-08-22.md:59-61`). The quote-pack’s explicit evidence boundary says the logs establish only that hardware was absent August 9 and first identified August 10 at 08:22 (`.codex-archaeology/origin-v1-v2-inking-quote-pack-2026-08-22.md:425-429`). No receipt establishes that it was delivered Sunday but left unopened. Therefore “would not arrive until Sunday” is not fact-checkable from local evidence; “I first had/started testing it Monday morning” is receipted.

## D11

**VERDICT: nuanced.** Most items were contemporaneous targets; “infinite undo and redo” is the exception and should not be stated literally.

- **Cold renders under 500 ms at 25%–400% — receipted.** The August 12 production handoff defines the committed zoom range as 25%–400% and the target as visible-settled p95 `<500 ms` at every committed zoom (`docs/archive/2026-08-vector-v2-foundation/PRODUCTION_CONTINUATION_HANDOFF_2026_08_12_NIGHT.md:51,264`). The ship contract later sharpened the binding worst-case gate to current-viewport exactness ≤500 ms at 400% (`docs/archive/2026-08-vector-v2-performance/SHIP_CONTRACT_PRE_RELEASE_2026-08-19.md:45-58`).
- **Tear-free panning — receipted.** The contract frozen August 15 makes zero tears/notches/stale bands/seams on glass the required correctness gate (`docs/archive/2026-08-vector-v2-performance/SHIP_CONTRACT_PRE_RELEASE_2026-08-19.md:1-24`). Sarah’s August 15 Pro prompt also asks whether “24 fps tear free panning” is viable (`.pro/ChatGPT-Review Packet Analysis-20260822-2133.md:1-10,408`).
- **24 FPS target / 30 FPS stretch — receipted with wording nuance.** The August 15 Pro review records Sarah’s finish-line goal as “24-30fps tear free panning” (`.pro/ChatGPT-Review Packet Analysis-20260822-2133.md:935`) and formalizes a 24 FPS floor plus a 30 FPS stretch target (`.pro/ChatGPT-Review Packet Analysis-20260822-2133.md:1005-1006,1343-1348`). The frozen ship contract calls ≥24 FPS required and ~29.8 FPS the stretch, matching the hardware ceiling rather than a literal sustained 30 (`docs/archive/2026-08-vector-v2-performance/SHIP_CONTRACT_PRE_RELEASE_2026-08-19.md:18-29`).
- **“Infinite undo and redo” — contradicted literally / supported only as opportunistic framing.** The ship contract requires ≥10 guaranteed levels and says “opportunistically unlimited within document capacity” (`docs/archive/2026-08-vector-v2-performance/SHIP_CONTRACT_PRE_RELEASE_2026-08-19.md:102-109`). V2 document capacity is finite at 4,000 operations (`vector_v2/include/tinydraw/vector_v2/memory_layout.h:44`); V1 is explicitly fixed at 10 entries (`core/include/tinydraw/graphics/tile_undo_history.h:28`). Thus the contemporaneous target was effectively “all retained V2 history within capacity,” not mathematically infinite undo/redo.
- **Anti-aliasing — receipted.** The August 13 gate plan states “Anti-aliased committed ink is mandatory” with a ≥4-sample-per-pixel-equivalent floor (`docs/archive/2026-08-vector-v2-foundation/PRODUCTION_GATE_PLAN_2026_08_13.md:18-21`); the ship contract requires idle settled AA while forbidding the measured-too-slow brute-force route (`docs/archive/2026-08-vector-v2-performance/SHIP_CONTRACT_PRE_RELEASE_2026-08-19.md:85-100`).
- **Proper SVG export — receipted if “proper” means the recorded fidelity contract.** SVG was a required ship item: exact variable-width Perfect-Freehand fidelity, one filled outline path per physical finger-down/up stroke, no synthetic background, and delivery through USB (`docs/archive/2026-08-vector-v2-performance/SHIP_CONTRACT_PRE_RELEASE_2026-08-19.md:111-120`). The word “proper” itself is retrospective shorthand; the underlying target was specific and contemporaneous.

## D13

**VERDICT: confirmed, with a distinction.** The draft is combining two separate bugs that the receipts explicitly distinguish.

- **Bug (a), cross-renderer “Schrödinger dots”: confirmed.** A one-sample authority operation was skipped by settled rendering: the physical export pair had two blue dots in SVG while PNG painted neither (`docs/receipts/vector-v2/ONE_SAMPLE_STROKE_DOT_BUG_2026_08_19.md:17-29`). Raw on-screen tiles could paint the operation and idle-settled tiles omit it, so it could appear as a tiny on-screen mark and then vanish (`docs/receipts/vector-v2/ONE_SAMPLE_STROKE_DOT_BUG_2026_08_19.md:32-42`). The fix made settled replay begin a one-sample operation at endpoint zero (`docs/receipts/vector-v2/ONE_SAMPLE_STROKE_DOT_BUG_2026_08_19.md:54-65`).
- **Bug (b), touch-down-then-no-draw admission: confirmed.** The input path began an operation on every down event; a top-fringe contact that never produced a drawable segment could finish as a valid one-sample operation (`docs/receipts/vector-v2/PHANTOM_TOP_EDGE_CONTACT_2026_08_19.md:60-68`). The receipt identifies these as contacts starting in the top four screen pixels and explicitly says this is separate from the cross-renderer fix (`docs/receipts/vector-v2/PHANTOM_TOP_EDGE_CONTACT_2026_08_19.md:3-13`). The repository’s surviving fix commit is `b53d42ce7d1e78e2f719804a17499993214a8e91` (2026-08-19 10:33:54 +0100, `fix(vector-v2): reject phantom top-edge taps`); the closure document is commit `2fbfb4790054261424a5cb5c0d270620df5a28c3` (2026-08-19 10:40:34 +0100).

## D12

**VERDICT: nuanced.** “Tagged the codebase as v2 on Wednesday” is historically confirmed, but “9 days after starting” means nine days and change / the eleventh calendar day, not day nine.

- **Historical tag:** Immediately before tagging, the log shows only `v2-feature-complete-pre-cleanup`; at **2026-08-19 11:19:05Z (12:19:05 BST, Wednesday)** Codex ran `git tag -a v2 ... 9d91b81...` and confirmed the annotated tag (`$HOME/.codex/sessions/2026/08/19/rollout-2026-08-19T00-33-26-01a01738-ff08-7f53-8c14-2c8abb4170f2.jsonl:6637-6644`). At **11:23:14Z (12:23:14 BST)** it force-retargeted `v2` to release commit `fd05d7de1bbf979c619332a8e4f9b78448114f2a` (`same session:6722,6727-6729`). So the coda’s Wednesday claim is correct for the original release tag.
- **Current rewritten tag caveat:** The tag now in this repository was recreated on **Thursday 2026-08-20 20:25:19 +01:00** and points to `9e6467e35253a14eafef02c5854cfe1f619651c6` (current `git cat-file -p v2` and `git show -s v2^{}`); this is later retag/rewrite metadata, not the first creation. The archaeology preserves the original release marker window as 2026-08-19 11:53–12:23 BST (`.codex-archaeology/supplement.md:119-125`).
- **Arithmetic:** From the earliest recovered TinyDraw prompt at **2026-08-09 17:31:06 BST** (`.pro/ChatGPT-Tiny tldraw on ESP32-S3-20260822-2138.md:4,10-16`) to initial tag creation at Aug 19 12:19:05 is **9 days 18 hours 48 minutes**. Counting Aug 9 as calendar day one, Aug 19 is **day 11**. The chronology has work entries on every date Aug 9–19 (`.codex-archaeology/supplement.md:9-125`), so “nine days of work” is misleading.
- **Honest phrasing options:** “less than ten days after starting,” “nine days and change after starting,” or “on the project’s eleventh calendar day.”

## D1

**VERDICT: nuanced.** “Sent at 6:48 p.m.” and “about twenty minutes until reset” are confirmed; the machine quota receipt says 18% remained, not 19% or 90%.

- The assembly/disassembly kickoff prompt was sent at **2026-08-22 18:48:08.635 BST** (logged as `17:48:08.635Z`), message `msg_01a02a96-4e3b-7510-b513-e3849e615465`: “i have a lot of tokens to burn... do a disassembly on the esp32s3 binary” (`$HOME/.codex/sessions/2026/08/22/rollout-2026-08-22T18-48-06-01a02a96-44b8-7ee2-ac71-2771198aedaa.jsonl:9`).
- At **18:48:19.291 BST**, Codex recorded weekly-window `used_percent: 82.0`, i.e. **18% nominally remaining**, with `resets_at: 1787422176` = **19:09:36 BST**, 21m27s after send (`same session:17`). At **19:10:17.787 BST**, it recorded `used_percent: 0.0`, confirming the reset (`same session:1079`).
- Therefore **6:48 p.m. is exact enough; 19% is a one-point remembered approximation; 90% left is contradicted; and 8 p.m. was not this Codex reset**. The session’s quota object identifies only `limit_id: "codex"`; no Fable quota percentage survives there (`same session:17`), so any separate Fable quota is unsupported / not recoverable.

## D2

**VERDICT: confirmed in surviving local evidence — the Pro prompt is the first TinyDraw artifact.**

- The exported Pro conversation was created **2026-08-09 17:31:07 BST** and its first prompt at **17:31:06** asks, “can we make a tiny tldraw...” (`.pro/ChatGPT-Tiny tldraw on ESP32-S3-20260822-2138.md:4,10-16`).
- **Correction to the dispatch premise:** message `43135de2` at 18:08 was not the first Pi session. An earlier Pi session starts **17:45:30.821 BST** (`$HOME/.pi/agent/sessions/--Users-sarah-src-tries-2026-08-09-espdraw--/2026-08-09T16-45-30-821Z_019fe76a-4b45-727a-8fc9-b2f13bec7c9b.jsonl:1`), and message `dddcbb11` at **17:46:47.371 BST** says “we are building a tiny tldraw...” (`same session:6`). It still postdates the Pro prompt by more than 15 minutes.
- Git begins later: root commit `4486705024f025f9596d92b464fbddfb0ab28323`, authored/committed **2026-08-09T18:20:27+01:00**, `chore: bootstrap native development loop` (repository `git show --format=fuller 4486705`). The inspected Grok TinyDraw prompt history begins 2026-08-12 (`$HOME/.grok/sessions/%2FUsers%2Fsarah%2Fsrc%2Ftries%2F2026-08-09-espdraw/prompt_history.jsonl:4`), and inspected standalone Claude records also begin August 12 (`$HOME/.claude/projects/-Users-sarah-src-tries-2026-08-09-espdraw/4da7cc5c-67a7-43a7-85b7-370558256951.jsonl:12`).
- A Codex prompt on **2026-08-08 21:29:48 BST** discusses preparing the specific Waveshare board for its next-day arrival (`$HOME/.codex/history.jsonl:3692`, `ts=1786220988`), but contains no TinyDraw/drawing-app idea; it is hardware prehistory, not earlier TinyDraw content.

## D7

**Finding:** At the 2026-08-10 17:30 BST cutoff, the build had a **2×2 raster world (736×896) viewed through one 368×448 screen**, not a 3×3 world; it did have a pen, eraser, twelve colors, four line sizes, and ten Undo levels.

- **Cutoff state:** `git log --until="2026-08-10 17:30 +0100"` ends at `fccb68ad51ca7b3cce68529b8c79461a81d6d032`, authored **2026-08-10 17:27:33 +0100** (`feat: add captive Wi-Fi drawing export`). The source at that commit defines the panel viewport as 368×448 (`fccb68a:core/include/tinydraw/geometry.h:5-6`).
- **Canvas: 2×2, not single-screen and not 3×3.** The cutoff source calls it “A fixed 2x2 drawing world around one screen-sized raster viewport” and defines each world dimension as twice the panel dimension (`fccb68a:core/include/tinydraw/graphics/world_canvas.h:17-25`), yielding 736×896 from the receipted 368×448 viewport. The contemporaneous session likewise proposed “2×2 canvas … 736×896 world” at **2026-08-10 16:25 BST**, message `e60c01d8` (`$HOME/.pi/agent/sessions/--Users-sarah-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:4171`), and after Sarah asked at **17:17 BST** whether it could be bigger, the response still identified “the current 2× world” and declined to enlarge it before export, message `10c15c8e` at **17:18 BST** (`same session:4505,4510`).
- **The 3×3 world came the next day.** Commit `582f317322fc60f43622403aacd1eae389e392ed` (`feat: expand drawing world to 3x3`) is authored **2026-08-11 16:20:38 +0100** and changes the dimensions to three times the panel size (`582f317:core/include/tinydraw/graphics/world_canvas.h:17-25`). This matches the archaeology’s statement that the completed 3×3 V1 arrived the following day (`.codex-archaeology/origin-v1-v2-inking-quote-pack-2026-08-22.md:11-13,151-155`; `.codex-archaeology/meetup-day-timeline.md:9-11`).
- **Colors: twelve.** The cutoff enum contains exactly twelve named colors, black through red (`fccb68a:core/include/tinydraw/ui/toolbar.h:12-25`); the palette array exposes those same twelve (`fccb68a:core/src/toolbar.cpp:30-33`). The palette landed before the cutoff in `86c549c2727c4404daaaf0e192c224ce7ac98b75`, authored **2026-08-10 15:52:15 +0100** (`feat: add full tldraw color palette`).
- **Line sizes: four.** The cutoff enum is Small, Medium, Large, and Extra Large (`fccb68a:core/include/tinydraw/ui/toolbar.h:26`), mapped to brush sizes 5, 8, 13, and 20 (`fccb68a:core/src/toolbar.cpp:390-400`).
- **Pen and eraser: both present and wired on hardware.** The cutoff tool enum contains Pen, Pan, and Eraser (`fccb68a:core/include/tinydraw/ui/toolbar.h:11`); hardware selection switches to Eraser (`fccb68a:esp32/main/hardware_app.cpp:607-617`) and erasing draws with the background color (`fccb68a:esp32/main/hardware_app.cpp:727-730`).
- **Undo depth: ten levels.** The cutoff history class defines ten deterministic slots and `kMaxEntries = 10` (`fccb68a:core/include/tinydraw/graphics/tile_undo_history.h:23-28`). Its registered end-to-end receipt requires exact depth-10 Undo across drawing, erasing, and New, while the pan test requires Undo across 2×2 views (`fccb68a:tests/CMakeLists.txt:196-205`; cross-view test introduced by `2d30ba1f2e1bb1533af7d3d928f00c48a7822fa9`, authored **2026-08-10 16:47:29 +0100**).
- **Wi-Fi context:** captive export code had landed by 17:27 (`fccb68a`), but it was already demonstrably unreliable: at **17:29 BST**, message `a18d4d65` reports that the captive portal opened but showed “a ? instead of the image” (`$HOME/.pi/agent/sessions/--Users-sarah-src-tries-2026-08-09-espdraw--/2026-08-09T17-07-22-489Z_019fe77e-4ef9-747e-8aaf-43882a317433.jsonl:4608`). The surviving presentation guide is evidence of intended demo beats, not proof of exactly what was delivered live (`.codex-archaeology/meetup-day-timeline.md:9`).

**VERDICT: nuanced.**

**One-line receipt:** `fccb68a` at 17:27 contains the 2×2 world, 12-color/4-size pen-and-eraser toolbar, and 10-slot Undo; 3×3 first lands the next day in `582f317`.

