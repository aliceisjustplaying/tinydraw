#!/usr/bin/env bun

import assert from "node:assert/strict";
import { existsSync } from "node:fs";
import { resolve, join } from "node:path";
import { pathToFileURL } from "node:url";

const [moduleArgument, puckArgument] = process.argv.slice(2);
if (!moduleArgument || !puckArgument) {
  console.error("usage: bun puck/loader_verify.mjs <emu.wasm> <PUCK_REPO>");
  process.exit(2);
}
const modulePath = resolve(moduleArgument);
const puck = resolve(puckArgument);
assert(existsSync(modulePath), `module not found: ${modulePath}`);
assert(existsSync(join(puck, "src", "wasm.ts")), `not a Puck checkout: ${puck}`);

const { instantiate, readDeviceDescriptor, readFramebufferPointer } = await import(
  pathToFileURL(join(puck, "src", "wasm.ts")).href
);
const { pixelReaderFor, readFramebufferRGB } = await import(
  pathToFileURL(join(puck, "src", "panel.ts")).href
);
const { SUPPORTED_WASI_IMPORTS } = await import(
  pathToFileURL(join(puck, "src", "wasiLite.ts")).href
);
// Keep this verifier compatible with the public ABI of current Puck. These
// names come from wasm/emu_abi.h and src/wasiLite.ts; they intentionally live
// here because Puck no longer exports an abiSurface.ts policy module.
const ENV_IMPORT_NAMES = [
  "sinf", "cosf", "atan2f", "sqrtf", "fabsf", "floorf", "fmodf", "powf", "expf", "js_log",
];
const REQUIRED_EMU_EXPORT_NAMES = [
  "emu_button",
  "emu_button_verdict",
  "emu_device",
  "emu_fb",
  "emu_init",
  "emu_push_count",
  "emu_push_h",
  "emu_push_w",
  "emu_push_x",
  "emu_push_y",
  "emu_sensor_event",
  "emu_tick",
  "emu_touch",
];
const bytes = await Bun.file(modulePath).arrayBuffer();
const module = await WebAssembly.compile(bytes);
const expectedExports = [...REQUIRED_EMU_EXPORT_NAMES, "memory"];
const exports = new Set(WebAssembly.Module.exports(module).map(({ name }) => name));
assert.deepEqual(expectedExports.filter((name) => !exports.has(name)), [], "missing ABI exports");
const timingExports = ["emu_timing_schema", "emu_timing_snapshot", "emu_timing_snapshot_size"];
assert.deepEqual(timingExports.filter((name) => !exports.has(name)), [], "missing timing-lab exports");
const allowedExports = new Set([...expectedExports, ...timingExports, "_initialize"]);
assert.deepEqual([...exports].filter((name) => !allowedExports.has(name)), [],
  "unexpected public exports");

const allowedWasi = new Set(SUPPORTED_WASI_IMPORTS);
const allowedEnv = new Set(ENV_IMPORT_NAMES);
for (const imported of WebAssembly.Module.imports(module)) {
  assert(
    (imported.module === "wasi_snapshot_preview1" && allowedWasi.has(imported.name)) ||
      (imported.module === "env" && allowedEnv.has(imported.name)),
    `unsupported import ${imported.module}.${imported.name}`,
  );
}

const log = [];
const emu = await instantiate(bytes, (line) => log.push(line));
assert.equal(emu.emu_init(), 1, "emu_init failed");
const startupPushes = Array.from({ length: emu.emu_push_count() }, (_, index) => ({
  x: emu.emu_push_x(index),
  y: emu.emu_push_y(index),
  w: emu.emu_push_w(index),
  h: emu.emu_push_h(index),
}));
assert.equal(startupPushes.length, 11, "startup did not use hardware-sized panel strips");
assert.deepEqual(
  startupPushes,
  Array.from({ length: 11 }, (_, index) => ({
    x: 0,
    y: index * 44,
    w: 368,
    h: index === 10 ? 8 : 44,
  })),
  "Puck push geometry differs from the 16,384-pixel hardware transport",
);
const device = readDeviceDescriptor(emu);
assert.deepEqual(device.panel, { w: 368, h: 448, format: "rgb565be" });
assert.equal(device.buttons?.length, 1);
assert.equal(device.buttons?.[0]?.id, "zoom");
assert.equal(device.buttons?.[0]?.longPressMs, undefined);
assert.equal(device.touch?.points, 1);
assert.equal(device.sensors, undefined);

const framebuffer = readFramebufferPointer(emu, device.panel);
for (let now = 0; now <= 400; now += 16) emu.emu_tick(now);
const reader = pixelReaderFor(device.panel.format);
const rgb = readFramebufferRGB(emu.memory, framebuffer, device.panel.w, reader, {
  x: 0, y: 0, w: device.panel.w, h: device.panel.h,
});
let paper = 0;
let dark = 0;
for (let i = 0; i < rgb.length; i += 3) {
  if (rgb[i] === 255 && rgb[i + 1] === 255 && rgb[i + 2] === 255) ++paper;
  if (rgb[i] < 96 && rgb[i + 1] < 96 && rgb[i + 2] < 96) ++dark;
}
assert(paper > 0.7 * device.panel.w * device.panel.h, "startup panel is not mostly paper");
assert(dark > 500, "firmware chrome did not reach the panel");
assert(log.some((line) => line.startsWith("TINYDRAW_VECTOR_V2_READY")), "firmware never became ready");
assert(emu.memory.buffer.byteLength <= 48 * 1024 * 1024, "linear memory grew past the initial reservation");

console.log(
  `PASS canonical Puck loader: ${bytes.byteLength} bytes, ` +
    `${emu.memory.buffer.byteLength / 1024 / 1024} MiB, ${paper} paper pixels, ${dark} dark pixels`,
);
