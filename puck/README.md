# TinyDraw V2 for Puck

This target compiles the ESP32 Vector V2 application itself: its app session,
presenter, background pipeline, live-stroke path, chrome controller, demo
controller, touch sampler, and the shared Vector V2/core authority. The device
entry point remains `start`, then `step` forever; Puck calls the same start and
step functions from deterministic host ticks.

One active Stroke accepts 4,096 sampled contact points plus its distinct lift
(4,097 stored samples). Verification drives that exact boundary, checks it is
one operation, and proves one Undo removes it; the history trace separately
checks exact Redo restoration.

Only physical facilities are replaced. The Puck panel transport preserves the
firmware's strip staging, ring addressing, RGB565 byte swap, and partial push
windows. Browser touch is sampled through `PhysicalTouch`; raw GPIO0 press and
release timing drives the firmware's short-zoom and long demo-record controls.
Autosave, USB export, RTC sync, battery state, panel scan timing, and physical
touch-controller faults are reported unavailable because the browser has no
equivalent hardware.

The stripped `-Oz` reactor is currently 220,814 bytes (about 216 KiB). It
reserves 48 MiB initially and is capped at 64 MiB, so startup does not grow
WebAssembly memory and detach host views. Its host surface is the Puck ABI plus
WASI `fd_write`; guest-local answers keep wasi-libc's stdout probes from
widening that contract. Puck owns `_initialize`; `emu_init` only constructs
and starts the application session.

Virtual time advances at the board's 1 ms application polling cadence and at
explicit delay and semaphore-wait boundaries. Reading `esp_timer_get_time()`
is side-effect free. One-shot ESP timers fire deterministically during those
advances, including demo replay. Performance numbers from this build are not
hardware timing claims.

## ESP32-S3 timing ledger

Three optional exports expose a versioned 144-byte shadow ledger:
`emu_timing_schema`, `emu_timing_snapshot`, and
`emu_timing_snapshot_size`. Normal Puck does not call them. The ledger records
capability-classified live allocations and the explicitly instrumented
PSRAM-to-internal-staging-to-panel path. Each tick clears traffic observations
while retaining the live allocation census.

Version 1 derives its clocks from Puck's checked-in ESP32-S3 timing profile and
converts panel payload bytes to a raw 40 MHz quad-SPI wire floor. It
does not model instruction execution, cache behavior, SRAM or PSRAM latency,
flash traffic, DMA queue overlap, or panel scan-out, so its cycle field is not
a total execution-cycle claim. `puck/timing_verify.ts` checks two fresh
instances byte-for-byte and pins the startup transfer at 329,728 bytes, 11
submissions, 659,456 panel wire clocks, and a 3,956,736-cycle CPU-clock
equivalent without changing the tolerance-zero pixel traces.

## Build and verify

`./scripts/puck` discovers a nearby Puck checkout or uses `PUCK_REPO`. It builds
with CMake/Ninja, checks the ABI through Puck's canonical loader, replays the
semantic and deterministic traces, and compares every recorded frame at
tolerance 0.

```sh
./scripts/puck
PUCK_REPO=/path/to/puck ./scripts/puck
```

Passing a checkout explicitly also installs the module atomically, then runs
Puck's TypeScript, WASI, and real-browser verification. A failed verification
restores the previous module.

```sh
./scripts/puck /path/to/puck
```

The artifact is `out/build/puck/puck/emu.wasm`. `./scripts/puck-run` performs
the install/verification and starts Puck's development server on port 5340.
The checked WASI toolchain versions are in `.wasi-versions`;
`./scripts/bootstrap-wasm` installs them on macOS.
