# Adversarial Architecture Review — TinyDraw build system, tests, docs-vs-reality

Date: 2026-08-21. Scope: all project CMake files, `scripts/**`, test layout, static-analysis scopes, doc claims, repo hygiene. Method: configs/scripts first, then execution receipts (I actually ran the suites) and targeted greps. Every claim below carries a file:line receipt or a run log.

---

## (a) Actual target / test inventory

### Root CMake tree (`cmake --preset host-debug`, tests ON)

| Kind | Target | Deps | Notes |
|---|---|---|---|
| INTERFACE | `tinydraw_project_options` | — | `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion` + `-Werror` (CMakeLists.txt:19-23) |
| STATIC | `tinydraw_pngenc` | none of the above | **gets no project options** (core/CMakeLists.txt:17-31) |
| STATIC | `tinydraw_core` | options (PRIVATE), pngenc | 15 srcs; PUCK_ONLY variant drops 8 srcs + pngenc (core/CMakeLists.txt:34-77) |
| STATIC | `tinydraw_vector_v2` | core (PRIVATE), options (PUBLIC) | +3 diagnostic srcs and a PUBLIC define when `TINYDRAW_BUILD_TESTS` (vector_v2/CMakeLists.txt:14-20) |
| STATIC | `tinydraw_vector_prototype` | core, options | retired engine, compiled only under tests (core/CMakeLists.txt:81-93) |
| EXE | `tinydraw_host` | core, SDL2 (pkg-config) | Raster V1 only; never links vector_v2 (host/CMakeLists.txt:3-7) |
| EXE ×3 | vector_v2 authority/interaction/rendering suites | vector_v2, core | doctest via vendored header (vector_v2/CMakeLists.txt:63-99) |
| EXE ×6 | benchmarks (idle_repair, settled_aa, history, cache, occupancy, journal_corpus_check) | vector_v2 | **built in every test build, wired to zero ctest entries** |
| EXE | `tinydraw_ink_trace_check`, `tinydraw_vector_v2_raster_census` (+fuzz) | vector_v2/core | wired as tests ✓ |
| EXE ×4 | tinydraw_tests, fat16_image, perf_characterization, png_roundtrip (ZLIB-gated) | core/prototype | |

**ctest entries: 31 in host-debug (verified by run), 13 in host-asan (verified by run).**
The arithmetic is environment-dependent but holds on macOS: fsck_msdos + ZLIB found → 13 base + 18 host-only = 31.

### Other trees

- ESP-IDF (`esp32/`): one component target per variant — product, demo, raster-v1, gate, qemu(+graphics), panel-probe, tearing-probe, tile-census (esp32/CMakeLists.txt:8-33). Engine sources are re-listed by path into `idf_component_register`; the root CMake targets are never used.
- Puck (`puck/`): `tinydraw_puck` → emu.wasm; compiles firmware-app sources against shim headers (puck/CMakeLists.txt:11-16).
- RP2350 (`rp2350/`): standalone tree, hand-duplicated warning flags, no `-Werror` (rp2350/CMakeLists.txt:38-40).

### Layer mapping of tests

- `tests/` (25 files): Raster V1 product surface **plus 7 files pinning the retired prototype engine** (camera, raster_materializer, settled_renderer, stroke_lod, stroke_macrogrid, vector_benchmark, viewport_renderer ≈ 28% of the directory).
- `vector_v2/tests/` (29 files): engine layer. Recovery coverage is genuinely deep: "keeps the prior recovery point after every truncated tail byte" and "rejects every single-byte corruption" (authority_journal_test.cpp:257,287,459).
- Zero-direct-test boundary: `storage_overlap.h` — used by 8 engine source files for memory-safety-critical overlap math, no test file references it.
- Firmware app layer (`esp32/main/vector_v2/*.cpp`: presenter, background_pipeline, autosave/export stores): no ctest coverage. Its only host-runnable exercise is the Puck WASI port's pixel harness (`puck/verify.mjs`, bun + traces) — which is wired into neither CTest nor CI.
- Golden images: committed PPMs compared byte-for-byte with WILL_FAIL negative tests (tests/CMakeLists.txt:57-104). Approval process = one sentence in testdata/README.md ("Update fixtures only with the corresponding behavior change"); no regeneration/approval tooling exists.

---

## (b) Verified / refuted doc claims

| # | Claim (source) | Verdict | Receipt |
|---|---|---|---|
| 1 | "host Debug and Release suites pass 31/31 targets" (PROJECT_STATE.md:9) | ✅ VERIFIED by execution | My run today from clean `out/`: `100% tests passed out of 31`, exit 0 |
| 2 | "ASan/UBSan passes 13/13" (PROJECT_STATE.md:10) | ✅ VERIFIED by execution | My run: `100% tests passed out of 13`, exit 0, 76 s |
| 3 | "the format check passes" (PROJECT_STATE.md:10) | ✅ VERIFIED | `./scripts/dev format-check` → PASS (but scope omits puck/, see F5) |
| 4 | "Product firmware uses the 604-slot tile pool" (SHIP_CONTRACT.md:88) | ✅ VERIFIED | memory_layout.h:37 `#define TINYDRAW_VECTOR_V2_TILE_SLOTS 604`; esp32/CMakeLists.txt:44-52 FORCEs 604 for product/demo |
| 5 | "fixed 16 MiB partition map: 1.75 MiB app, 4 MiB journal, 10.125 MiB export, 64 KiB coredump" (DEVELOPING.md:96-98, SHIP_CONTRACT.md:89) | ✅ VERIFIED | partitions.csv: app `0x10000,0x1C0000`(=1.75 MiB), drawing `0x1D0000,0x400000`(=4 MiB), export `0x5D0000,0xA20000`(=10.125 MiB), coredump `0xFF0000,0x10000`; product/gate tables byte-identical (diffed) |
| 6 | "Undo and Redo retain at least ten levels" (SHIP_CONTRACT.md:26) | ✅ VERIFIED (trivially) | Raster V1: tile_undo_history.h:28 `kMaxEntries = 10U`. Vector V2 has **no depth constant at all** — history is bounded only by kOperationCapacity=4'000 (memory_layout.h:42); "at least ten" passes vacuously |
| 7 | "Zoom supports 25, 50, 100, 200, and 400 percent" (SHIP_CONTRACT.md:29) | ✅ VERIFIED | materialized_canvas.h:42-48 enum lists exactly those five levels |
| 8 | Settled AA numbers 75.102/87.647/176.885/383.594/946.849 ms (PROJECT_STATE.md:31) | ✅ VERIFIED cross-doc | AA_PERFORMANCE_EXPERIMENTS_2026_08_19.md:22 matches digit-for-digit |
| 9 | "`core/`: C++ standard library only… `vector_v2/`: platform-independent" (DEVELOPING.md:113-115) | ✅ VERIFIED | grep for SDL/esp/freertos/driver includes across core/ and vector_v2/: zero hits; only stdlib headers |
| 10 | "The final 604-slot physical battery passed every gate at `a5db58d`" (PROJECT_STATE.md:11-12) | ❌ REFUTED as verifiable | `git rev-parse a5db58d` → "unknown revision"; 0 hits across `--all`. The SHA exists only as prose (VECTOR_V2_RELEASE_2026_08_19.md:5,33) |
| 11 | CONTEXT.md "**Stroke** … *Avoid*: Operation" (CONTEXT.md:5-9) | ❌ REFUTED by codebase reality | ~28 `Operation*` type declarations in vector_v2/include alone; files operation.cpp/builder/log/spatial_index; PROJECT_STATE.md itself counts "102 active operations"; user-facing "Stroke" survives only as grouping key `StrokeIdentity{tool,color,gesture_id}` (operation_log.h:23-24) |
| 12 | Reproducibility: stranger runs `./scripts/bootstrap-macos && ./scripts/dev test` | ✅ VERIFIED (dev half) | `./scripts/dev test` cold-ran configure+build+test in 50.9 s on this machine; version pins consistent (.idf-version v6.0.2 = DEVELOPING.md text; .pico-sdk-version 2.1.1 used as clone tag in scripts/rp2350:11,71; wasi-sdk 33 with per-platform SHA256 in scripts/build-puck-wasm:9,18-27) |

---

## (c) Findings by severity

### HIGH

**H1 — No CI anywhere; every green number is an unenforced manual claim.**
No `.github/`, no other CI config (repo-wide search; git log shows none ever added). The entire evidence edifice — 31/31, 13/13, format, tidy, cppcheck, gate batteries — depends on someone remembering to run scripts locally before pushing. The scorecard can rot silently with nothing to notice. This is the single biggest gap between the repo's evidentiary ambition and its enforcement.

**H2 — Release-evidence revision anchors are broken by an unrecorded history rewrite.**
PROJECT_STATE.md:11 pins the release battery to `a5db58d` — not in the object database, even with `--all`. The `v2` tag dereferences to `9e6467e` ("Repair the Puck bundle after identity rewrite", Aug 20), not to any released source. `.codex-archaeology/git-history.md:312` still asserts "annotated tag `v2` dereferences to that commit [fd05d7d lineage]" — false since the rewrite. Code comments cite dead SHAs too: memory_layout.h:21 "Raised 320 -> 384 at 264b60e". A repo whose whole trust model is "dated receipts with SHAs" broke its own primary keys and left no old→new mapping. (Mitigation that does exist and deserves credit: EVIDENCE_ARCHIVE.md records SHA-256 checksums for each compressed gate log, and the `v2-feature-complete-pre-cleanup` tag survived.)

**H3 — Warning/error policy is not uniform, and the shipped firmware layer sits outside it.**
- Host trees: strict (`tinydraw_project_options`, `-Werror`, CMakeLists.txt:19-23).
- `third_party/pngenc`: **no** project options at all (core/CMakeLists.txt:17-31) — the PNG encoder that produces user exports compiles unwarned in host builds too.
- RP2350: flags hand-duplicated, **no `-Werror`** (rp2350/CMakeLists.txt:38-40).
- ESP-IDF: none of it. `idf_component_register` takes raw engine paths (esp32/main/CMakeLists.txt:36-118) under IDF defaults; `TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS` etc. are the only custom defines. The code that actually ships to glass is the least compiler-checked code in the repo.

### MEDIUM

**M1 — The tested binary is not the shipped binary.**
`TINYDRAW_BUILD_TESTS=ON` appends diagnostic sources to the library and adds a PUBLIC define that changes class layout — `MaterializedCanvas` gains a member under `#ifdef TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS` (materialized_canvas.h:439-444, 571-573; vector_v2/CMakeLists.txt:14-20). Product firmware compiles without it; gate harness with it (esp32/main/CMakeLists.txt:157-160). Separately, the ASan preset sets `TINYDRAW_BUILD_HOST=OFF` (CMakePresets.json:30-35), so the determinism snapshot suite and undo/pan e2e are never sanitized. DEVELOPING.md:11 is honest ("SDL-free code") but the headline "ASan/UBSan suite" reads stronger than what it covers.

**M2 — CONTEXT.md ubiquitous language is contradicted by the entire implementation.**
CONTEXT.md:9 says avoid "Operation"; the code's persisted unit *is* `Operation`/`StoredOperation` (operation_log.h:14), the design doc says plainly "one ordered list of vector operations… publishes one operation for each complete Stroke" (docs/design/VECTOR_V2_AUTHORITY_UNDO_DESIGN.md:17-20), and PROJECT_STATE.md:14 reports "102 active operations" as a user-relevant figure. Meanwhile descriptor.md uses "Stroke" throughout. The repo is mid-migration between two languages and the doc that forbids the dominant one is dated *after* the code existed (git log CONTEXT.md: ad5e5f1, post-vector_v2).

**M3 — Source-list and magic-number duplication between IDF and CMake trees invites drift.**
Every engine source is listed twice: once in core/vector_v2 CMake lists, again in `TINYDRAW_APP_SRCS` (esp32/main/CMakeLists.txt:8-28, 84-105). `MEM_SHRINK=3` and `PNG_MAX_BUFFERED_PIXELS=4417` appear in both core/CMakeLists.txt:23-28 and esp32/main/CMakeLists.txt:150-155, with comment wording already diverging. Currently in sync; nothing enforces staying that way.

**M4 — Static-analysis scopes exclude load-bearing code.**
tidy = `vector_v2/src/*.cpp` only (scripts/dev:97-99) — no headers, no core, no host, no esp32/main, no puck. cppcheck = `vector_v2/src + include` (scripts/dev:117-119). format = `core vector_v2 host tests esp32/main rp2350/src` (scripts/dev:66) — **all 18 puck/ C++ files excluded**, so the WASI port is format-unchecked despite being checksum-pinned and released. I verified the tidy wiring itself works (ran clang-tidy on tile_uniform.cpp with the generated compile_commands: exit 0, clean).

**M5 — Golden-image approval is tribal.**
Byte-exact PPM oracles are excellent, but there is no script to regenerate/review/approve them; the only codified rule is one README sentence (testdata/README.md:8-9). Combined with H1 (no CI), a silent snapshot update would be invisible.

### LOW

**L1 — Six benchmark executables compile in every test build yet run in no test** (vector_v2/CMakeLists.txt:24-61). Build-time tax; tribal invocation.
**L2 — `storage_overlap.h` used by 8 engine files, zero direct tests** (grep across vector_v2/tests: no hits).
**L3 — 7 of 25 files in `tests/` pin a self-described "retired" prototype engine** kept alive only under `TINYDRAW_BUILD_TESTS` (core/CMakeLists.txt:79-93).
**L4 — Untracked working material**: blogpost.md and .codex-archaeology/ sit outside git while being the substrate for published claims.
**L5 — Largest tracked blob is a 9.1 MB datasheet PDF** (`reference/CO5300_Datasheet_V0.00.pdf`). Otherwise hygiene is genuinely good: managed_components ignored, raw logs/videos excluded by policy with retrieval via tag (EVIDENCE_ARCHIVE.md), benchmark-results trimmed to 896 KB of receipts.

### Things the adversarial pass could not break (credit where due)

- Both headline test counts reproduce exactly, from scratch, in ~51 s and ~92 s.
- Journal recovery tests are adversarial in the best sense (every truncation byte, every corruption byte, capacity-with-prior-point: authority_journal_test.cpp:257-459).
- Determinism fuzz seeds document *why* they exist inline ("Seed 987654321 case 3965 caught the coalesced-capsule float-exactness bug", vector_v2/CMakeLists.txt:110-112).
- Toolchain reproducibility for embedded targets is exemplary: IDF via eim + `.idf-version`, pico-sdk cloned at `.pico-sdk-version`, two independent SHA256-pinned toolchains (rp2350 gcc, wasi-sdk).
- Partition tables product/gate are enforced identical by construction and diff-clean.

---

## (d) Top 3 recommendations by leverage

1. **Add CI (GitHub Actions, macOS runner) running `./scripts/dev test`, `asan`, and `format-check` on every push.** Everything is already script-wired; this converts the scorecard from memoir to contract and protects findings M4/M5 automatically. One afternoon of work; eliminates the entire "claims can silently rot" class (H1).
2. **Re-anchor the evidence chain.** Document the identity rewrite in EVIDENCE_ARCHIVE.md with an old-SHA→new-SHA mapping (recoverable from reflogs or `.codex-archaeology/git-history.md`), state explicitly which current commit corresponds to `a5db58d`'s content, add a policy that release tags are immutable, and fix stale in-code SHA citations (memory_layout.h:21). Cheap, and it repairs the repo's central trust mechanism (H2).
3. **Unify the quality perimeter.** Give the IDF component and rp2350 the same `-Wall…-Werror` set (a shared `.cmake`/idf wrapper or sdkconfig `CONFIG_COMPILER_WARN_WRITE_STRINGS`-style knobs), fold pngenc under the project options with targeted suppressions, add `puck` to the format find-list and esp32/main to cppcheck's. Then dedupe the pngenc defines and consider a `TINYDRAW_BUILD_BENCHMARKS` option or ctest-wired benchmark budgets so built ≠ orphaned (H3, M3, M4, L1).

*Bonus (small but symbolic): either finish the Operation→Stroke rename the language doc demands, or amend CONTEXT.md to describe the actual two-level model (Stroke = gesture identity over ordered Operations) that VECTOR_V2_AUTHORITY_UNDO_DESIGN.md already documents correctly.*
