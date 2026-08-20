# TinyDraw V2 for Puck

This target runs a host-neutral Vector V2 application on WebAssembly, reusing
TinyDraw's authority, ink, raster, navigation, and chrome modules. The ESP32
entry point has not yet been cut over to this top-level application seam, so
the two hosts do not currently share their presenter and background scheduler.
Puck receives deterministic ticks, a direct RGB565 framebuffer, and truthful
damage for each changed frame. Its WebAssembly host funds the production
604-slot materialized canvas and advances eight bounded rendering quanta per
tick. The module is capped at 64 MiB and currently remains at 32 MiB under the
full performance battery. Panel timing, PSRAM placement, touch-controller
defects, USB export, RTC sync, and autosave remain hardware concerns and are
not simulated.

Short BOOT presses cycle zoom. An 800 ms BOOT press starts a deterministic
demo recording from a blank document; the next long press stops, and the next
replays the take from the same blank baseline.

## Run it

Build, verify, install, start Puck, and open the emulator on macOS with one
attached command:

```sh
./scripts/puck-run
```

The launcher discovers a nearby Puck checkout. You can also pass one or set
`PUCK_REPO`:

```sh
./scripts/puck-run /path/to/puck
PUCK_REPO=/path/to/puck ./scripts/puck-run
```

It replaces a stale Puck server on port 5340 through Puck's guarded quit API,
then stays attached to the new development server; press Ctrl-C to stop it
cleanly. A non-Puck listener on that port is left untouched and reported.

Build and run the ABI, product, determinism, performance, and memory checks
under every locally available Bun/Node runtime:

```sh
./scripts/puck
```

Also install into a Puck checkout, replay pixel-exact traces, run Puck's
TypeScript and WASI suites, and launch its real-browser verification:

```sh
./scripts/puck /path/to/puck
```

The artifact is written to `out/build/puck/puck/emu.wasm`. The local command
does not require a Puck checkout.

The checked WASI toolchain versions are in `.wasi-versions`. On macOS,
`./scripts/bootstrap-wasm` installs and verifies them.
