#!/usr/bin/env node

import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const modulePath = process.argv[2] ?? resolve(here, "../out/build/puck/puck/emu.wasm");
const ownerPath = process.argv[3] ??
  resolve(here, "../testdata/documents/captured-drawing-2026-08-19.tdoc");
const moduleBytes = await readFile(modulePath);
const ownerBytes = new Uint8Array(await readFile(ownerPath));
const encoder = new TextDecoder();

let instance;
const imports = {
  env: {
    js_log(pointer, length) {
      if (instance !== undefined) {
        const message = encoder.decode(
          new Uint8Array(instance.exports.memory.buffer, pointer, length),
        );
        throw new Error(`guest log during performance run: ${message}`);
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

function fail(message) {
  throw new Error(`Puck performance verification failed: ${message}`);
}

function check(condition, message) {
  if (!condition) fail(message);
}

function elapsedMs(action) {
  const started = performance.now();
  action();
  return performance.now() - started;
}

function percentile(sorted, fraction) {
  if (sorted.length === 0) return 0;
  return sorted[Math.min(sorted.length - 1, Math.ceil(sorted.length * fraction) - 1)];
}

function stats(samples) {
  const sorted = [...samples].sort((left, right) => left - right);
  return {
    count: sorted.length,
    median: percentile(sorted, 0.5),
    p95: percentile(sorted, 0.95),
    max: sorted.at(-1) ?? 0,
  };
}

function formatStats(label, samples) {
  const value = stats(samples);
  return `${label.padEnd(25)} n=${String(value.count).padStart(6)}  median=${value.median
    .toFixed(3)
    .padStart(8)} ms  p95=${value.p95.toFixed(3).padStart(8)} ms  max=${value.max
    .toFixed(3)
    .padStart(8)} ms`;
}

const compileStarted = performance.now();
const compiled = await WebAssembly.compile(moduleBytes);
const compileMs = performance.now() - compileStarted;
const instantiateStarted = performance.now();
instance = await WebAssembly.instantiate(compiled, imports);
const instantiateMs = performance.now() - instantiateStarted;
const emu = instance.exports;
emu._initialize();

function cString(pointer) {
  const memory = new Uint8Array(emu.memory.buffer);
  let end = pointer;
  while (end < memory.length && memory[end] !== 0) end += 1;
  check(end < memory.length, "emu_device() returned an unterminated string");
  return encoder.decode(memory.subarray(pointer, end));
}

const device = JSON.parse(cString(emu.emu_device()));
const panelWidth = device.panel.w;
const panelHeight = device.panel.h;
const panelPixels = panelWidth * panelHeight;

function framebuffer() {
  const pointer = emu.emu_fb();
  check(Number.isInteger(pointer) && pointer > 0 && pointer % 2 === 0,
        `emu_fb() returned invalid pointer ${pointer}`);
  check(pointer + panelPixels * 2 <= emu.memory.buffer.byteLength,
        "framebuffer exceeds exported memory");
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

function canvasColorCount(frame) {
  const colors = new Set();
  for (let y = 0; y < 300; y += 1) {
    for (let x = 0; x < 260; x += 1) colors.add(frame[y * panelWidth + x]);
  }
  return colors.size;
}

function validatePushes() {
  const count = emu.emu_push_count();
  check(Number.isInteger(count) && count >= 0 && count <= 256,
        `emu_push_count() returned ${count}`);
  for (let index = 0; index < count; index += 1) {
    const x = emu.emu_push_x(index);
    const y = emu.emu_push_y(index);
    const width = emu.emu_push_w(index);
    const height = emu.emu_push_h(index);
    check(x >= 0 && y >= 0 && width > 0 && height > 0 &&
          x + width <= panelWidth && y + height <= panelHeight,
          `push ${index} is out of panel bounds`);
  }
  return count;
}

let clockMs = 0;
function timedTick(samples) {
  clockMs += 16;
  const duration = elapsedMs(() => emu.emu_tick(clockMs));
  const pushes = validatePushes();
  if (samples !== undefined) samples.push(duration);
  return pushes;
}

function initializeAtZero() {
  clockMs = 0;
  check(emu.emu_init() === 1, "emu_init() returned failure");
  framebuffer();
  timedTick();
  check(emu.emu_push_count() === 1, "first tick did not publish the initial frame");
}

const maximumConvergenceTicks = 4_096;
function drainUntilPublications(samples, requiredPushes = 1, label = "background work") {
  let pushes = 0;
  for (let ticks = 1; ticks <= maximumConvergenceTicks; ticks += 1) {
    pushes += timedTick(samples);
    if (pushes >= requiredPushes) return ticks;
  }
  fail(`${label} did not publish ${requiredPushes} frames within ${maximumConvergenceTicks} ticks`);
}

function buildDocument(strokeCount) {
  const live = [];
  const commit = [];
  const background = [];
  const convergenceTicks = [];
  const started = performance.now();

  for (let stroke = 0; stroke < strokeCount; stroke += 1) {
    const column = stroke % 16;
    const row = Math.floor(stroke / 16) % 32;
    // Stay clear of both bottom chrome and the right-side minimap hit target.
    const x = 24 + column * 15;
    const y = 20 + row * 10;

    emu.emu_touch(1, x, y);
    timedTick(live);
    emu.emu_touch(1, x + 4, y + 2);
    timedTick(live);
    emu.emu_touch(0, 0, 0);
    timedTick(commit);
  }
  // A foreground publication and the final 25% composition may collapse into
  // one full-frame push when the adapter funds enough quanta. Run a fixed idle
  // window and record the last later publication; the following 50% tour proves
  // exact high-operation convergence with distinct fallback/final frames.
  let lastBackgroundPush = 0;
  for (let tickIndex = 1; tickIndex <= 32; tickIndex += 1) {
    if (timedTick(background) > 0) lastBackgroundPush = tickIndex;
  }
  convergenceTicks.push(lastBackgroundPush);

  return {
    live,
    commit,
    background,
    convergenceTicks,
    totalMs: performance.now() - started,
  };
}

function requestAndConverge(request, requiredPushes = 2) {
  const requestSamples = [];
  const backgroundSamples = [];
  request();
  let pushes = timedTick(requestSamples);
  let fallbackTicks = pushes > 0 ? 0 : undefined;
  let convergenceTicks = 0;
  while (pushes < requiredPushes && convergenceTicks < maximumConvergenceTicks) {
    convergenceTicks += 1;
    const published = timedTick(backgroundSamples);
    if (published > 0 && fallbackTicks === undefined) fallbackTicks = convergenceTicks;
    pushes += published;
  }
  check(pushes >= requiredPushes,
        `request did not publish ${requiredPushes} frames within ${maximumConvergenceTicks} ticks`);
  return { requestSamples, backgroundSamples, fallbackTicks, convergenceTicks };
}

function shortButtonPress(index) {
  emu.emu_button(index, 1);
  emu.emu_button(index, 0);
  if (device.buttons?.[index]?.longPressMs !== undefined) {
    // Puck delivers the semantic short/long verdict after the physical
    // release for buttons that declare a hold threshold.
    emu.emu_button_verdict(index, 0);
  }
}

function settledAaRun() {
  initializeAtZero();
  emu.emu_touch(1, 60, 70);
  timedTick();
  emu.emu_touch(1, 145, 151);
  timedTick();
  emu.emu_touch(1, 230, 221);
  timedTick();
  emu.emu_touch(0, 0, 0);
  timedTick();
  check(typeof emu.tinydraw_diag_maintenance_pending === "function",
        "missing maintenance diagnostic export");
  const samples = [];
  let ticks = 0;
  let observed = emu.tinydraw_diag_maintenance_pending() !== 0;
  while ((!observed || emu.tinydraw_diag_maintenance_pending() !== 0) &&
         ticks < maximumConvergenceTicks) {
    timedTick(samples);
    observed ||= emu.tinydraw_diag_maintenance_pending() !== 0;
    ticks += 1;
  }
  check(observed && emu.tinydraw_diag_maintenance_pending() === 0,
        `settled AA did not converge within ${maximumConvergenceTicks} ticks`);
  const colors = canvasColorCount(snapshot());
  check(colors > 2, `settled AA canvas has only ${colors} RGB565 colors`);
  return { samples, ticks, colors };
}

function longActiveStrokeRun() {
  initializeAtZero();
  const logicalPointCapacity = 4_096;
  check(typeof emu.tinydraw_diag_active_stroke_point_capacity === "function" &&
        emu.tinydraw_diag_active_stroke_point_capacity() === logicalPointCapacity,
        "Puck did not expose its 4,096-point active-Stroke capacity");
  check(typeof emu.tinydraw_diag_operation_count === "function" &&
        typeof emu.tinydraw_diag_sample_count === "function" &&
        typeof emu.tinydraw_diag_stroke_active === "function",
        "missing active-Stroke performance diagnostics");

  const live = [];
  emu.emu_touch(1, 60, 80);
  timedTick(live);
  for (let move = 0; move < logicalPointCapacity - 2; move += 1) {
    emu.emu_touch(1, move % 2 === 0 ? 240 : 60, move % 2 === 0 ? 250 : 80);
    timedTick(live);
  }
  check(emu.tinydraw_diag_stroke_active() === 1,
        "near-capacity Stroke was discarded before performance release");
  check(emu.tinydraw_diag_operation_count() === 0,
        "active near-capacity Stroke entered authority");

  // Force the exact cold-render transition that used to expose provisional
  // chunks. Its publication must remain authority-neutral and within one
  // browser frame even at the declared active-Stroke capacity.
  shortButtonPress(0);
  const cold = [];
  let coldPushes = 0;
  let coldTicks = 0;
  for (; coldTicks < maximumConvergenceTicks && coldPushes < 2; coldTicks += 1) {
    coldPushes += timedTick(cold);
  }
  check(coldPushes >= 2,
        "near-capacity active Stroke did not publish cold zoom fallback and final frames");
  check(emu.tinydraw_diag_stroke_active() === 1 &&
        emu.tinydraw_diag_operation_count() === 0,
        "cold zoom published near-capacity provisional authority");

  const release = [];
  emu.emu_touch(0, 0, 0);
  timedTick(release);
  check(emu.tinydraw_diag_stroke_active() === 0,
        "near-capacity release left the Stroke active");
  check(emu.tinydraw_diag_operation_count() === 133 &&
        emu.tinydraw_diag_sample_count() === 4_228,
        "near-capacity release did not retain its exact provisional transaction");
  return { live, cold, coldTicks, release };
}

function ownerProductionRun() {
  initializeAtZero();
  for (const name of ["tinydraw_owner_buffer", "tinydraw_owner_capacity", "tinydraw_owner_load"]) {
    check(typeof emu[name] === "function", `missing owner import export ${name}`);
  }
  check(ownerBytes.byteLength <= emu.tinydraw_owner_capacity(),
        `owner fixture exceeds ${emu.tinydraw_owner_capacity()}-byte transfer buffer`);
  new Uint8Array(emu.memory.buffer, emu.tinydraw_owner_buffer(), ownerBytes.byteLength).set(ownerBytes);
  check(emu.tinydraw_owner_load(ownerBytes.byteLength) === 1, "owner fixture was rejected");

  const importTicks = [];
  let importConvergenceTicks = 0;
  for (; importConvergenceTicks < maximumConvergenceTicks; importConvergenceTicks += 1) {
    if (timedTick(importTicks) > 0) break;
  }
  check(importConvergenceTicks < maximumConvergenceTicks, "owner import did not publish");

  // At 50%, production publishes an overview fallback and later a finalized
  // tiled view. Measuring through both publications exercises the high-sample
  // producer quanta instead of stopping at its fast fallback.
  shortButtonPress(0);
  const tiledTicks = [];
  let tiledPushes = 0;
  let tiledConvergenceTicks = 0;
  for (; tiledConvergenceTicks < maximumConvergenceTicks && tiledPushes < 2;
       tiledConvergenceTicks += 1) {
    if (timedTick(tiledTicks) > 0) tiledPushes += 1;
  }
  check(tiledPushes === 2, "owner tiled rebuild did not publish fallback and final frames");
  return { importTicks, importConvergenceTicks: importConvergenceTicks + 1,
           tiledTicks, tiledConvergenceTicks };
}

// Warm the allocator and JIT before measuring application boot. A warm boot is
// the reusable-module path Puck takes when firmware state is reset in place.
for (let attempt = 0; attempt < 5; attempt += 1) initializeAtZero();
const warmBoot = [];
for (let attempt = 0; attempt < 30; attempt += 1) {
  clockMs = 0;
  warmBoot.push(elapsedMs(() => {
    check(emu.emu_init() === 1, "warm emu_init() returned failure");
    emu.emu_tick(0);
  }));
  validatePushes();
}

initializeAtZero();
const firstDocument = buildDocument(512);
const firstDocumentFrame = snapshot();

const zoom = requestAndConverge(() => {
  shortButtonPress(0);
});
const zoomedDocumentFrame = snapshot();
const undo = requestAndConverge(() => {
  emu.emu_touch(1, 30, 410);
  emu.emu_touch(0, 0, 0);
});
const redo = requestAndConverge(() => {
  emu.emu_touch(1, 90, 410);
  emu.emu_touch(0, 0, 0);
});
check(equalPixels(zoomedDocumentFrame, snapshot()),
      "Redo publication did not byte-exactly restore the pre-Undo frame");
const ownerProduction = ownerProductionRun();
const settledAa = settledAaRun();
const longActiveStroke = longActiveStrokeRun();

// Repeated initialization must release and reuse its heap without growing the
// WebAssembly memory on every lifetime. Measure after the largest document so
// the allocator has already observed the workload's high-water mark.
const pagesBeforeRepeatInit = emu.memory.buffer.byteLength / 65_536;
const maximumMemoryPages = (64 * 1_024 * 1_024) / 65_536;
check(pagesBeforeRepeatInit <= maximumMemoryPages,
      `peak memory ${pagesBeforeRepeatInit} pages exceeds the 64 MiB Puck limit`);
const repeatInit = [];
const framebufferPointers = new Set();
for (let attempt = 0; attempt < 32; attempt += 1) {
  clockMs = 0;
  repeatInit.push(elapsedMs(() => check(emu.emu_init() === 1, "repeat emu_init() failed")));
  framebufferPointers.add(emu.emu_fb());
  timedTick();
}
const pagesAfterRepeatInit = emu.memory.buffer.byteLength / 65_536;
check(pagesAfterRepeatInit === pagesBeforeRepeatInit,
      `repeat init grew memory from ${pagesBeforeRepeatInit} to ${pagesAfterRepeatInit} pages`);
check(pagesAfterRepeatInit <= maximumMemoryPages,
      `repeat-init memory ${pagesAfterRepeatInit} pages exceeds the 64 MiB Puck limit`);

initializeAtZero();
const secondDocument = buildDocument(512);
const deterministic = equalPixels(firstDocumentFrame, snapshot());
check(deterministic, "the second 512-stroke run produced different framebuffer bytes");

const interactive = [...firstDocument.live, ...firstDocument.commit,
                     ...secondDocument.live, ...secondDocument.commit];
const interactiveStats = stats(interactive);
const liveStats = stats([...firstDocument.live, ...secondDocument.live]);
const commitStats = stats([...firstDocument.commit, ...secondDocument.commit]);
const convergence = [...zoom.backgroundSamples, ...undo.backgroundSamples, ...redo.backgroundSamples];
const convergenceStats = stats(convergence);
const ownerQuantumStats = stats([...ownerProduction.importTicks, ...ownerProduction.tiledTicks]);
const settledAaStats = stats(settledAa.samples);
const longActiveStats = stats([
  ...longActiveStroke.live,
  ...longActiveStroke.cold,
  ...longActiveStroke.release,
]);
const warmBootStats = stats(warmBoot);
const repeatInitStats = stats(repeatInit);
const interactiveP95LimitMs = 16.7;
const warmBootP95LimitMs = 50;
const fallbackTickLimit = 32;
const finalConvergenceTickLimit = 32;
const documentWallLimitMs = 5_000;

console.log(`Puck performance: ${device.name}, ${panelWidth}x${panelHeight}`);
console.log(`runtime=${process.versions.bun ? `Bun ${process.versions.bun}` : `Node ${process.version}`}  ` +
            `wasm_compile=${compileMs.toFixed(3)} ms  instantiate=${instantiateMs.toFixed(3)} ms`);
console.log(formatStats("warm boot", warmBoot));
console.log(formatStats("live stroke ticks", [...firstDocument.live, ...secondDocument.live]));
console.log(formatStats("stroke commit ticks", [...firstDocument.commit, ...secondDocument.commit]));
console.log(formatStats("all interactive ticks", interactive));
console.log(formatStats("document background ticks",
                        [...firstDocument.background, ...secondDocument.background]));
console.log(formatStats("zoom request", zoom.requestSamples));
console.log(formatStats("zoom convergence", zoom.backgroundSamples));
console.log(formatStats("undo request", undo.requestSamples));
console.log(formatStats("undo convergence", undo.backgroundSamples));
console.log(formatStats("redo request", redo.requestSamples));
console.log(formatStats("redo convergence", redo.backgroundSamples));
console.log(formatStats("all convergence ticks", convergence));
console.log(formatStats("owner import quanta", ownerProduction.importTicks));
console.log(formatStats("owner tiled quanta", ownerProduction.tiledTicks));
console.log(formatStats("settled AA ticks", settledAa.samples));
console.log(formatStats(
  "long active Stroke",
  [...longActiveStroke.live, ...longActiveStroke.cold, ...longActiveStroke.release],
));
console.log(formatStats("long Stroke cold zoom", longActiveStroke.cold));
console.log(formatStats("long Stroke release", longActiveStroke.release));
console.log(formatStats("repeat init", repeatInit));
console.log(`document run 1           strokes=512  total=${firstDocument.totalMs.toFixed(1)} ms  ` +
            `background_ticks=${firstDocument.background.length}  ` +
            `max_convergence=${Math.max(...firstDocument.convergenceTicks)} ticks`);
console.log(`document run 2           strokes=512  total=${secondDocument.totalMs.toFixed(1)} ms  ` +
            `background_ticks=${secondDocument.background.length}  ` +
            `max_convergence=${Math.max(...secondDocument.convergenceTicks)} ticks`);
console.log(`zoom/history convergence zoom=${zoom.convergenceTicks} (fallback=${zoom.fallbackTicks})  ` +
            `undo=${undo.convergenceTicks} (fallback=${undo.fallbackTicks})  ` +
            `redo=${redo.convergenceTicks} (fallback=${redo.fallbackTicks}) ticks`);
console.log(`owner convergence        import=${ownerProduction.importConvergenceTicks}  ` +
            `tiled=${ownerProduction.tiledConvergenceTicks} ticks`);
console.log(`settled AA convergence   ticks=${settledAa.ticks}  colors=${settledAa.colors}`);
console.log(`long Stroke cold zoom    ticks=${longActiveStroke.coldTicks}`);
console.log(`lifetime memory          pages=${pagesBeforeRepeatInit}->${pagesAfterRepeatInit}  ` +
            `peak=${(pagesAfterRepeatInit / 16).toFixed(1)} MiB/64.0 MiB  ` +
            `framebuffer_addresses=${framebufferPointers.size}`);
console.log(`deterministic rerun      ${deterministic ? "PASS" : "FAIL"}`);

check(liveStats.p95 <= interactiveP95LimitMs,
      `live-stroke p95 ${liveStats.p95.toFixed(3)} ms exceeds ${interactiveP95LimitMs} ms`);
check(commitStats.p95 <= interactiveP95LimitMs,
      `commit p95 ${commitStats.p95.toFixed(3)} ms exceeds ${interactiveP95LimitMs} ms`);
check(interactiveStats.p95 <= interactiveP95LimitMs,
      `combined interactive p95 ${interactiveStats.p95.toFixed(3)} ms exceeds ${interactiveP95LimitMs} ms`);
check(convergenceStats.p95 <= interactiveP95LimitMs,
      `background convergence p95 ${convergenceStats.p95.toFixed(3)} ms exceeds ${interactiveP95LimitMs} ms`);
check(ownerQuantumStats.p95 <= interactiveP95LimitMs,
      `owner production p95 ${ownerQuantumStats.p95.toFixed(3)} ms exceeds ${interactiveP95LimitMs} ms`);
check(settledAaStats.p95 <= interactiveP95LimitMs,
      `settled-AA p95 ${settledAaStats.p95.toFixed(3)} ms exceeds ${interactiveP95LimitMs} ms`);
check(longActiveStats.p95 <= interactiveP95LimitMs,
      `long-active p95 ${longActiveStats.p95.toFixed(3)} ms exceeds ${interactiveP95LimitMs} ms`);
check(longActiveStats.max <= interactiveP95LimitMs,
      `long-active max ${longActiveStats.max.toFixed(3)} ms exceeds ${interactiveP95LimitMs} ms`);
check(warmBootStats.p95 <= warmBootP95LimitMs,
      `warm-boot p95 ${warmBootStats.p95.toFixed(3)} ms exceeds ${warmBootP95LimitMs} ms`);
check(repeatInitStats.p95 <= interactiveP95LimitMs,
      `repeat-init p95 ${repeatInitStats.p95.toFixed(3)} ms exceeds ${interactiveP95LimitMs} ms`);
check(Math.max(zoom.fallbackTicks, undo.fallbackTicks, redo.fallbackTicks) <= fallbackTickLimit,
      `zoom/history fallback exceeded ${fallbackTickLimit} ticks`);
check(Math.max(zoom.convergenceTicks, undo.convergenceTicks, redo.convergenceTicks) <=
          finalConvergenceTickLimit,
      `zoom/history final convergence exceeded ${finalConvergenceTickLimit} ticks`);
check(firstDocument.totalMs <= documentWallLimitMs && secondDocument.totalMs <= documentWallLimitMs,
      `512-stroke creation exceeded ${documentWallLimitMs} ms`);
console.log(`performance gates        PASS (interactive/background p95 <= ${interactiveP95LimitMs} ms, ` +
            `long-active max <= ${interactiveP95LimitMs} ms, ` +
            `warm boot p95 <= ${warmBootP95LimitMs} ms, fallback <= ${fallbackTickLimit} ticks, ` +
            `final <= ${finalConvergenceTickLimit} ticks)`);
