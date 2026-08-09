# Developing TinyDraw

For the current architecture, implemented milestones, known limitations, and exact next steps, read
[`PROJECT_STATE.md`](PROJECT_STATE.md).

The native ARM64 macOS build is the primary development loop. `tinydraw_core` has no SDL or
ESP-IDF dependency. SDL is only a thin host shell around a 368×448 RGB565 framebuffer.

## One-time host setup

Prerequisites are captured in `Brewfile`:

```sh
./scripts/bootstrap-macos
```

The supported baseline is CMake 3.28+, Ninja, a C++20 Clang toolchain, and SDL2. The current
machine was bootstrapped with Apple Clang 21 and SDL 2.32 through `sdl2-compat`.

## Daily loop

```sh
./scripts/dev test          # debug build + all native tests
./scripts/dev run           # SDL host; floating tool dock, Cmd-Z undo, C new, Esc quit
./scripts/dev asan          # AddressSanitizer + UndefinedBehaviorSanitizer on SDL-free core
./scripts/dev release       # optimized build + tests
./scripts/dev format-check
./scripts/dev format
```

The wrapper is intentionally thin. Raw commands also work:

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

Build products and `compile_commands.json` stay below `out/build/<preset>/`. Editors should use
`out/build/host-debug/compile_commands.json`.

## End-to-end replay

The host executable can run without a window and drive the real input/filter/PF-ribbon/render path
from a stroke recording:

```sh
out/build/host-debug/host/tinydraw_host \
  --replay testdata/strokes/zigzag.stroke \
  --output /tmp/zigzag.ppm

out/build/host-debug/host/tinydraw_host \
  --ui-preview \
  --output /tmp/ui-preview.ppm
```

CTest regenerates these images and compares it byte-for-byte with the approved host snapshot. This
is a characterization test: it proves determinism and catches unintended changes, but it is not an
independent correctness oracle. PF-derived reference geometry will provide that oracle later. The
same recordings will drive QEMU and hardware, using structural/tolerance comparisons instead of
cross-target pixel equality.

The interactive host opens at roughly the real physical size of the 1.8-inch display on a
14-inch 2021 MacBook Pro at default scaling; resize the window when a larger inspection view is
needed. Its direct-drawn floating dock follows tldraw's visual hierarchy. Undo, pen, eraser,
selected color, selected size, and new-drawing fill one row; tapping color or size opens one row of
large choices above it.

The interactive host renders PF-style ribbons as convex spans and round joins/caps into 8-bit
64×64 coverage tiles. `StrokeRaster` max-unions stable geometry into an active coverage plane and
regenerates only tiles touched by the changing tail. This keeps long-stroke work bounded and
prevents self-overlap holes or repeated edge darkening. The persistent RGB565 canvas remains
unchanged until lift; core tests compare the incremental result with one-pass coverage union.

The sanitizer preset excludes the host executable because Homebrew's `sdl2-compat` loader aborts
under Apple's sanitizer runtime before application code starts. All SDL-free project code remains
sanitized; full host replay runs in both debug and release presets.

The TypeScript `perfect-freehand` reference and pinned commit are documented in
`reference/PERFECT_FREEHAND.md`. It is cloned locally but is not a build dependency or submodule.

## ESP-IDF (deferred until the native interfaces exist)

ESP-IDF is intentionally not mixed into the host CMake project or global shell startup. The
pinned version is in `.idf-version`. Install it separately when `esp32/` work starts:

```sh
./scripts/bootstrap-idf
eim run "idf.py --version"
```

PlatformIO is not used. Espressif's installation manager owns IDF's Python and toolchain, keeping
the host loop independent from the system Python. QEMU will be installed through IDF tooling when
the ESP target is scaffolded.

## Dependency policy

- `core/`: C++ standard library only; no SDL, ESP-IDF, or desktop assumptions.
- `host/`: SDL2-compatible APIs, found through pkg-config.
- `tests/`: pinned doctest 2.5.3 under `third_party/doctest`.
- Add image-reference, fuzz, and benchmark dependencies only when those loops have real consumers.

Do not assess ESP32 performance from QEMU. Host tests establish correctness, QEMU establishes
integration and structural agreement, and physical hardware establishes timing and drawing feel.
