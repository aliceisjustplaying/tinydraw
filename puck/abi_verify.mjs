#!/usr/bin/env node

import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const repository = resolve(here, "..");
const modulePath = process.argv[2] ?? resolve(here, "../out/build/puck/puck/emu.wasm");
const ownerPath = process.argv[3] ??
  resolve(repository, "testdata/documents/captured-drawing-2026-08-19.tdoc");
const bytes = await readFile(modulePath);
const ownerBytes = new Uint8Array(await readFile(ownerPath));

let instance;
const logs = [];
const imports = {
  env: {
    js_log(pointer, length) {
      if (instance !== undefined) {
        const text = new TextDecoder().decode(
          new Uint8Array(instance.exports.memory.buffer, pointer, length),
        );
        logs.push(text);
      }
    },
  },
  wasi_snapshot_preview1: {
    fd_write(_fd, _iovs, _iovsLength, written) {
      if (instance !== undefined && written !== 0) {
        new DataView(instance.exports.memory.buffer).setUint32(written, 0, true);
      }
      return 0;
    },
  },
};

({ instance } = await WebAssembly.instantiate(bytes, imports));
const emu = instance.exports;
emu._initialize();

function fail(message) {
  throw new Error(`Puck ABI verification failed: ${message}`);
}

function check(condition, message) {
  if (!condition) fail(message);
}

function cString(pointer) {
  const memory = new Uint8Array(emu.memory.buffer);
  let end = pointer;
  while (end < memory.length && memory[end] !== 0) end += 1;
  check(end < memory.length, "emu_device() did not return a terminated string");
  return new TextDecoder().decode(memory.subarray(pointer, end));
}

const device = JSON.parse(cString(emu.emu_device()));
const panelWidth = device.panel.w;
const panelHeight = device.panel.h;
const panelPixels = panelWidth * panelHeight;
check(device.buttons?.[0]?.longPressMs === 800,
      "BOOT descriptor does not declare its 800 ms long-press verdict");

function framebuffer() {
  const pointer = emu.emu_fb();
  check(Number.isInteger(pointer) && pointer > 0 && pointer % 2 === 0,
        `emu_fb() returned invalid pointer ${pointer}`);
  check(pointer + panelPixels * 2 <= emu.memory.buffer.byteLength,
        "emu_fb() exceeds exported memory");
  return new Uint16Array(emu.memory.buffer, pointer, panelPixels);
}

function snapshot() {
  return new Uint16Array(framebuffer());
}

function equalPixels(left, right) {
  if (left.length !== right.length) return false;
  for (let index = 0; index < left.length; index += 1) {
    if (left[index] !== right[index]) return false;
  }
  return true;
}

function inkPixels(frame, x0, y0, x1, y1) {
  let count = 0;
  for (let y = y0; y < y1; y += 1) {
    for (let x = x0; x < x1; x += 1) {
      if (frame[y * panelWidth + x] !== 0xffff) count += 1;
    }
  }
  return count;
}

function validatePushes(requirePush = false) {
  const count = emu.emu_push_count();
  check(Number.isInteger(count) && count >= 0 && count <= 256,
        `emu_push_count() returned ${count}`);
  if (requirePush) check(count > 0, "tick published framebuffer changes without a push");
  for (let index = 0; index < count; index += 1) {
    const x = emu.emu_push_x(index);
    const y = emu.emu_push_y(index);
    const width = emu.emu_push_w(index);
    const height = emu.emu_push_h(index);
    check(x >= 0 && y >= 0 && width > 0 && height > 0 &&
          x + width <= panelWidth && y + height <= panelHeight,
          `push ${index} is outside ${panelWidth}x${panelHeight}: ${x},${y} ${width}x${height}`);
  }
}

function tick(nowMs, requirePush = false) {
  emu.emu_tick(nowMs);
  validatePushes(requirePush);
  return emu.emu_push_count();
}

function convergePublications(firstMs, requiredPushes = 1, maximumTicks = 4_096) {
  let pushes = 0;
  for (let attempt = 0; attempt < maximumTicks; attempt += 1) {
    const nowMs = firstMs + attempt * 16;
    pushes += tick(nowMs);
    if (pushes >= requiredPushes) return nowMs;
  }
  fail(`frame did not publish ${requiredPushes} times within ${maximumTicks} ticks` +
       `${logs.length ? `; last log: ${logs.at(-1)}` : ""}`);
}

function convergePush(firstMs, maximumTicks = 4_096) {
  return convergePublications(firstMs, 1, maximumTicks);
}

function initialize() {
  check(emu.emu_init() === 1, "emu_init() returned failure");
  framebuffer();
  check(emu.emu_push_count() === 0, "emu_init() did not clear prior pushes");
  tick(0, true);
  check(emu.emu_push_count() === 1, "first tick must publish one full-panel push");
  check(emu.emu_push_x(0) === 0 && emu.emu_push_y(0) === 0 &&
        emu.emu_push_w(0) === panelWidth && emu.emu_push_h(0) === panelHeight,
        "first tick push was not the full panel");
}

function touchStroke(y, moveCount) {
  emu.emu_touch(1, 48, y);
  for (let index = 0; index < moveCount; index += 1) {
    emu.emu_touch(1, 52 + (index % 88) * 3, y + (index % 3));
  }
  emu.emu_touch(0, 0, 0);
}

// Reinitialization is a supported lifetime boundary and must reset all input,
// push, and application state without moving the framebuffer outside memory.
initialize();
const firstBlank = snapshot();
check(emu.emu_init() === 1, "repeat emu_init() returned failure");
check(emu.emu_push_count() === 0, "repeat emu_init() retained pushes");
tick(0, true);
check(equalPixels(firstBlank, snapshot()), "repeat emu_init() did not restore the initial frame");

// Input calls only mutate the physical latch. Even a burst far beyond the
// fixed event capacity must preserve Down/latest Move/Up at the following tick.
const beforeBurst = snapshot();
touchStroke(82, 96);
check(equalPixels(beforeBurst, snapshot()), "emu_touch mutated the framebuffer before emu_tick");
tick(16, true);
check(inkPixels(snapshot(), 35, 60, 330, 112) > 0,
      "Down + more than 32 Moves + Up did not commit its stroke");

// A second stroke must remain a separate gesture. Undo removes it and leaves
// the saturated first gesture visible; a lost first Up would merge both and
// make this Undo erase everything.
touchStroke(190, 4);
tick(32, true);
check(inkPixels(snapshot(), 35, 165, 330, 220) > 0, "second gesture did not draw");
emu.emu_touch(1, 30, 410);
emu.emu_touch(0, 0, 0);
convergePush(48);
check(inkPixels(snapshot(), 35, 60, 330, 112) > 0,
      "queue saturation lost Up and merged two gestures");

// Application reports the invalid points, then applies the valid toolbar tap
// in the same serialized advance. The ABI must still publish that new frame.
initialize();
logs.length = 0;
emu.emu_touch(1, -1, -1);
emu.emu_touch(0, 0, 0);
emu.emu_touch(1, 150, 410);
emu.emu_touch(0, 0, 0);
tick(16, true);
check(logs.some((line) => line.includes("advance failed")),
      "invalid input did not exercise Application's recoverable-error path");

// The captured owner TDOC crosses the ABI as an explicit two-phase latch:
// load validates and snapshots bytes, then the next tick begins the bounded,
// atomic Application import. Failed loads preserve both active authority and a
// previously staged valid import.
check(ownerBytes.byteLength === 22_170, `owner TDOC is ${ownerBytes.byteLength} bytes`);
check(new TextDecoder().decode(ownerBytes.subarray(0, 4)) === "TDOC", "owner fixture has no TDOC magic");
for (const name of ["tinydraw_owner_buffer", "tinydraw_owner_capacity", "tinydraw_owner_load"]) {
  check(typeof emu[name] === "function", `missing owner import export ${name}`);
}
const ownerCapacity = emu.tinydraw_owner_capacity();
const ownerPointer = emu.tinydraw_owner_buffer();
check(ownerCapacity >= ownerBytes.byteLength, `owner capacity ${ownerCapacity} is too small`);
check(ownerPointer > 0 && ownerPointer + ownerCapacity <= emu.memory.buffer.byteLength,
      "owner buffer is outside WebAssembly memory");

function writeOwner(document) {
  new Uint8Array(emu.memory.buffer, emu.tinydraw_owner_buffer(), document.byteLength).set(document);
}

function convergeOwner(firstMs = 16) { return convergePush(firstMs); }

initialize();
const ownerBlank = snapshot();
writeOwner(ownerBytes);
check(emu.tinydraw_owner_load(ownerBytes.byteLength) === 1, "exact owner TDOC was rejected");
check(equalPixels(ownerBlank, snapshot()), "owner load mutated the frame before emu_tick");
// The public transfer buffer is host-owned again after load returns; mutation
// here must not affect the private pending snapshot.
new Uint8Array(emu.memory.buffer, emu.tinydraw_owner_buffer(), ownerBytes.byteLength).fill(0xa5);
convergeOwner();
const ownerFrame = snapshot();
check(!equalPixels(ownerBlank, ownerFrame), "owner TDOC converged to the blank frame");

const badMagic = new Uint8Array(ownerBytes);
badMagic[0] ^= 0xff;
writeOwner(badMagic);
check(emu.tinydraw_owner_load(badMagic.byteLength) === 0, "bad TDOC magic was accepted");
tick(512);
check(emu.emu_push_count() === 0 && equalPixels(ownerFrame, snapshot()),
      "malformed owner load changed active authority or frame");

const zeroRadius = new Uint8Array(ownerBytes);
const ownerOperationCount = new DataView(zeroRadius.buffer).getUint32(4, true);
const firstSample = 12 + ownerOperationCount * 5;
zeroRadius[firstSample + 4] = 0;
zeroRadius[firstSample + 5] = 0;
writeOwner(zeroRadius);
check(emu.tinydraw_owner_load(zeroRadius.byteLength) === 0, "invalid TDOC sample was accepted");
check(emu.tinydraw_owner_load(0) === 0 && emu.tinydraw_owner_load(-1) === 0 &&
      emu.tinydraw_owner_load(ownerCapacity + 1) === 0,
      "owner loader accepted an invalid byte length");

initialize();
const secondOwnerBlank = snapshot();
writeOwner(ownerBytes);
check(emu.tinydraw_owner_load(ownerBytes.byteLength) === 1, "second exact owner TDOC was rejected");
// A stale physical edge belongs to the document that was active when it was
// latched. Successful import must discard it before advancing the replacement.
emu.emu_touch(1, 80, 80);
emu.emu_touch(0, 0, 0);
writeOwner(badMagic);
check(emu.tinydraw_owner_load(badMagic.byteLength) === 0,
      "malformed replacement was accepted over a valid pending TDOC");
check(equalPixels(secondOwnerBlank, snapshot()), "pending owner import published before tick");
convergeOwner();
check(equalPixels(ownerFrame, snapshot()),
      "failed replacement or stale pre-import input changed the staged exact owner import");

// BOOT is a product zoom-cycle control. Five releases from the 25% baseline
// must visit 50/100/200/400 and then return to the bit-identical 25% frame.
initialize();
const zoomBaseline = snapshot();
emu.emu_button(0, 1);
emu.emu_button(0, 0);
tick(16);
check(equalPixels(zoomBaseline, snapshot()), "raw BOOT release fired before its short verdict");
emu.emu_button_verdict(0, 0);
let zoomNow = convergePublications(32, 2);
const oneZoom = snapshot();
emu.emu_button_verdict(0, 0);
tick(zoomNow + 16);
check(equalPixels(oneZoom, snapshot()), "duplicate short verdict fired BOOT twice");

initialize();
const zoomCycleBaseline = snapshot();
zoomNow = 16;
for (let release = 1; release <= 5; release += 1) {
  emu.emu_button(0, 1);
  emu.emu_button(0, 0);
  emu.emu_button_verdict(0, 0);
  emu.emu_button_verdict(0, 0);
  zoomNow = convergePublications(zoomNow, release < 5 ? 2 : 1) + 16;
}
check(equalPixels(zoomCycleBaseline, snapshot()), "five BOOT releases did not cycle 400% back to 25%");

// A long BOOT verdict records from a blank baseline. Short BOOT remains the
// ordinary zoom event inside the tape; the next two long verdicts stop and
// replay the take through the same Application event path.
initialize();
emu.emu_button(0, 1);
tick(100);
emu.emu_button_verdict(0, 1);
emu.emu_button_verdict(0, 1);
tick(900, true);
emu.emu_button(0, 0);
tick(916);

emu.emu_touch(1, 70, 100);
tick(932, true);
emu.emu_touch(1, 105, 120);
tick(948, true);
emu.emu_touch(1, 145, 90);
tick(964, true);
emu.emu_touch(0, 0, 0);
tick(980, true);
emu.emu_button(0, 1);
tick(996);
emu.emu_button(0, 0);
emu.emu_button_verdict(0, 0);
let demoNow = convergePublications(1012, 2);

emu.emu_button(0, 1);
tick(demoNow += 16);
emu.emu_button_verdict(0, 1);
emu.emu_button_verdict(0, 1);
tick(demoNow += 800, true);
emu.emu_button(0, 0);
tick(demoNow += 16);
const recordedDemo = snapshot();

emu.emu_button(0, 1);
tick(demoNow += 16);
emu.emu_button_verdict(0, 1);
emu.emu_button_verdict(0, 1);
tick(demoNow += 800);
emu.emu_button(0, 0);
tick(demoNow += 16);
let replayMatched = false;
for (let attempt = 0; attempt < 4_096; attempt += 1) {
  tick(demoNow += 16);
  if (equalPixels(recordedDemo, snapshot())) {
    replayMatched = true;
    break;
  }
}
check(replayMatched,
      "BOOT demo replay did not reproduce its recorded final framebuffer");

console.log(
  `Puck ABI verified: ${panelWidth}x${panelHeight}, edge-safe input, damage, lifetime, owner TDOC, zoom, demo`,
);
