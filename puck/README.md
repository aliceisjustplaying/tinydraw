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

The stripped `-Oz` reactor is currently 215,983 bytes (about 211 KiB). It
reserves 48 MiB initially and is capped at 64 MiB, so startup does not grow
WebAssembly memory and detach host views. Its host surface is the Puck ABI plus
WASI `fd_write`, `fd_close`, and `fd_seek`; a guest-local `fd_fdstat_get`
answer keeps wasi-libc's stdout probe from widening that contract. Puck owns
`_initialize`; `emu_init` only
constructs and starts the application session.

Virtual time advances at the board's 1 ms application polling cadence and at
explicit delay and semaphore-wait boundaries. Reading `esp_timer_get_time()`
is side-effect free. One-shot ESP timers fire deterministically during those
advances, including demo replay. Performance numbers from this build are not
hardware timing claims.

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
