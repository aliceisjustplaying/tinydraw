#!/usr/bin/env node

import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repository = resolve(here, "..");
const modulePath = process.argv[2] ?? resolve(repository, "out/build/puck/puck/emu.wasm");
const ownerPath = process.argv[3] ??
  resolve(repository, "testdata/documents/captured-drawing-2026-08-19.tdoc");
const decoder = new TextDecoder();
const moduleBytes = await readFile(modulePath);
const ownerBytes = new Uint8Array(await readFile(ownerPath));

let instance;
const guestLog = [];
const imports = {
  env: {
    js_log(pointer, length) {
      if (instance !== undefined) {
        guestLog.push(
          decoder.decode(new Uint8Array(instance.exports.memory.buffer, pointer, length)),
        );
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

instance = await WebAssembly.instantiate(await WebAssembly.compile(moduleBytes), imports);
const emu = instance.exports;
emu._initialize();

function check(condition, message) {
  if (!condition) throw new Error(message);
}

function parseOwnerDocument(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  check(decoder.decode(bytes.subarray(0, 4)) === "TDOC", "owner corpus has no TDOC magic");
  const operationCount = view.getUint32(4, true);
  const sampleCount = view.getUint32(8, true);
  check(operationCount === 102, `owner corpus operation count is ${operationCount}, expected 102`);
  check(sampleCount === 2706, `owner corpus sample count is ${sampleCount}, expected 2706`);
  const metadataEnd = 12 + operationCount * 5;
  check(metadataEnd + sampleCount * 8 === bytes.byteLength, "owner corpus length is inconsistent");
  let metadataAt = 12;
  let sampleAt = metadataEnd;
  const operations = [];
  for (let index = 0; index < operationCount; index += 1) {
    const tool = bytes[metadataAt];
    const color = view.getUint16(metadataAt + 1, true);
    const count = view.getUint16(metadataAt + 3, true);
    metadataAt += 5;
    check(tool <= 1 && count > 0, `owner corpus operation ${index} is invalid`);
    const samples = [];
    for (let sample = 0; sample < count; sample += 1) {
      samples.push({
        xQuarter: view.getUint16(sampleAt, true),
        yQuarter: view.getUint16(sampleAt + 2, true),
        radius256: view.getUint16(sampleAt + 4, true),
        elapsedMs: view.getUint16(sampleAt + 6, true),
      });
      sampleAt += 8;
    }
    operations.push({ tool, color, samples });
  }
  return { operationCount, sampleCount, operations };
}

const owner = parseOwnerDocument(ownerBytes);
const panelWidth = 368;
const panelHeight = 448;
const panelPixels = panelWidth * panelHeight;
let clockMs = 0;

function framebuffer() {
  const pointer = emu.emu_fb();
  check(pointer > 0 && pointer % 2 === 0, `invalid framebuffer pointer ${pointer}`);
  return new Uint16Array(emu.memory.buffer, pointer, panelPixels);
}

function frame() {
  return new Uint16Array(framebuffer());
}

function diffPixels(left, right) {
  check(left.length === right.length, "frame sizes differ");
  let changed = 0;
  for (let index = 0; index < left.length; index += 1) {
    changed += Number(left[index] !== right[index]);
  }
  return changed;
}

function equalPixels(left, right) {
  return diffPixels(left, right) === 0;
}

function diffCanvas(left, right) {
  let changed = 0;
  for (let y = 0; y < 300; y += 1) {
    for (let x = 0; x < 330; x += 1) {
      const index = y * panelWidth + x;
      changed += Number(left[index] !== right[index]);
    }
  }
  return changed;
}

function canvasColorCount(pixels) {
  const colors = new Set();
  for (let y = 0; y < 300; y += 1) {
    for (let x = 0; x < 260; x += 1) colors.add(pixels[y * panelWidth + x]);
  }
  return colors.size;
}

function tick(nextClock = clockMs + 16) {
  clockMs = Math.max(clockMs + 1, nextClock);
  emu.emu_tick(clockMs);
  const count = emu.emu_push_count();
  check(count >= 0 && count <= 256, `invalid push count ${count}`);
  for (let index = 0; index < count; index += 1) {
    const x = emu.emu_push_x(index);
    const y = emu.emu_push_y(index);
    const width = emu.emu_push_w(index);
    const height = emu.emu_push_h(index);
    check(
      x >= 0 && y >= 0 && width > 0 && height > 0 &&
        x + width <= panelWidth && y + height <= panelHeight,
      `push ${index} is outside the panel`,
    );
  }
  return count;
}

function timedTick(nextClock = clockMs + 16) {
  const started = performance.now();
  const pushes = tick(nextClock);
  return { pushes, elapsedMs: performance.now() - started };
}

function percentile(values, fraction) {
  const sorted = [...values].sort((left, right) => left - right);
  return sorted[Math.min(sorted.length - 1, Math.ceil(sorted.length * fraction) - 1)] ?? 0;
}

function initialize() {
  guestLog.length = 0;
  clockMs = 0;
  check(emu.emu_init() === 1, "emu_init failed");
  tick(1);
  check(emu.emu_push_count() === 1, "initial frame was not published exactly once");
}

function tap(x, y) {
  emu.emu_touch(1, x, y);
  emu.emu_touch(0, 0, 0);
  tick();
}

function zoomNext() {
  emu.emu_button(0, 1);
  emu.emu_button(0, 0);
  emu.emu_button_verdict(0, 0);
  return tick();
}

function coldZoom() {
  emu.emu_button(0, 1);
  emu.emu_button(0, 0);
  emu.emu_button_verdict(0, 0);
  const request = timedTick();
  if (request.pushes > 0) {
    return { ticks: 1, timings: [request.elapsedMs] };
  }
  const convergence = drainUntilPublished();
  return { ticks: 1 + convergence.ticks, timings: [request.elapsedMs, ...convergence.timings] };
}

function draw(points) {
  check(points.length >= 2, "a test Stroke needs at least two points");
  emu.emu_touch(1, points[0].x, points[0].y);
  tick();
  for (const point of points.slice(1)) {
    emu.emu_touch(1, point.x, point.y);
    tick();
  }
  emu.emu_touch(0, 0, 0);
  tick();
}

function drawBatched(points) {
  check(points.length >= 2, "a test Stroke needs at least two points");
  emu.emu_touch(1, points[0].x, points[0].y);
  for (const point of points.slice(1)) emu.emu_touch(1, point.x, point.y);
  emu.emu_touch(0, 0, 0);
  return timedTick();
}

function drainUntilPublished(maximumTicks = 256) {
  const timings = [];
  for (let count = 1; count <= maximumTicks; count += 1) {
    const result = timedTick();
    timings.push(result.elapsedMs);
    if (result.pushes > 0) return { ticks: count, timings };
  }
  throw new Error(`view did not publish within ${maximumTicks} ticks`);
}

function drainUntilStable(requiredStableTicks = 64, maximumTicks = 4096) {
  let stableTicks = 0;
  let previous = frame();
  for (let count = 1; count <= maximumTicks; count += 1) {
    const pushes = tick();
    const current = frame();
    if (pushes === 0 && equalPixels(previous, current)) {
      stableTicks += 1;
      if (stableTicks === requiredStableTicks) return count;
    } else {
      stableTicks = 0;
    }
    previous = current;
  }
  throw new Error(`view did not remain stable for ${requiredStableTicks} ticks`);
}

const diagnosticExports = [
  "tinydraw_diag_production_enabled",
  "tinydraw_diag_slot_capacity",
  "tinydraw_diag_resident_raw_tiles",
  "tinydraw_diag_visible_tiles_remaining",
  "tinydraw_diag_recent_view_count",
  "tinydraw_diag_last_fallback_pixels",
  "tinydraw_diag_current_raw",
  "tinydraw_diag_current_uniform",
  "tinydraw_diag_current_fallback",
  "tinydraw_diag_zoom100_raw",
  "tinydraw_diag_zoom100_uniform",
  "tinydraw_diag_zoom100_fallback",
  "tinydraw_diag_rerender_renders",
  "tinydraw_diag_rerender_unique",
  "tinydraw_diag_rerender_stale",
  "tinydraw_diag_rerender_unexplained",
  "tinydraw_diag_maintenance_pending",
];

function diagnostics() {
  for (const name of diagnosticExports) {
    check(typeof emu[name] === "function", `missing production diagnostic export ${name}`);
  }
  return {
    production: emu.tinydraw_diag_production_enabled(),
    slots: emu.tinydraw_diag_slot_capacity(),
    resident: emu.tinydraw_diag_resident_raw_tiles(),
    visibleRemaining: emu.tinydraw_diag_visible_tiles_remaining(),
    recentViews: emu.tinydraw_diag_recent_view_count(),
    fallbackPixels: emu.tinydraw_diag_last_fallback_pixels(),
    currentRaw: emu.tinydraw_diag_current_raw(),
    currentUniform: emu.tinydraw_diag_current_uniform(),
    currentFallback: emu.tinydraw_diag_current_fallback(),
    zoom100Raw: emu.tinydraw_diag_zoom100_raw(),
    zoom100Uniform: emu.tinydraw_diag_zoom100_uniform(),
    zoom100Fallback: emu.tinydraw_diag_zoom100_fallback(),
    renders: emu.tinydraw_diag_rerender_renders(),
    uniqueRenders: emu.tinydraw_diag_rerender_unique(),
    staleRenders: emu.tinydraw_diag_rerender_stale(),
    unexplainedRenders: emu.tinydraw_diag_rerender_unexplained(),
    maintenancePending: emu.tinydraw_diag_maintenance_pending(),
  };
}

function drainMaterialization(maximumTicks = 8192) {
  for (let count = 1; count <= maximumTicks; count += 1) {
    tick();
    const receipt = diagnostics();
    if (receipt.visibleRemaining === 0 && receipt.fallbackPixels === 0 &&
        receipt.maintenancePending === 0) return { ticks: count, receipt };
  }
  throw new Error(`materialization did not quiesce within ${maximumTicks} ticks`);
}

function buildStressDocument(strokeCount) {
  const timings = [];
  for (let index = 0; index < strokeCount; index += 1) {
    const column = index % 24;
    const row = Math.floor(index / 24) % 26;
    const x = 12 + column * 12;
    const y = 12 + row * 11;
    timings.push(drawBatched([
      { x, y },
      { x: x + 4 + (index % 5), y: y + 2 + (index % 3) },
    ]).elapsedMs);
  }
  return timings;
}

function selectTool(tool) {
  tap(150, 410);
  tap(tool === 1 ? 184 : tool === 2 ? 306 : 60, 331);
}

const results = [];
async function gate(name, action) {
  try {
    const detail = await action();
    check(guestLog.length === 0, `guest reported: ${guestLog.join("; ")}`);
    results.push({ name, state: "PASS", reason: typeof detail === "string" ? detail : "" });
  } catch (error) {
    results.push({ name, state: "FAIL", reason: error.message });
  }
}

function skip(name, reason) {
  results.push({ name, state: "SKIP", reason });
}

function blocked(name, reason) {
  results.push({ name, state: "BLOCKED", reason });
}

await gate("color_dialog", () => {
  initialize();
  const closed = frame();
  tap(210, 410);
  check(diffPixels(closed, frame()) > 1_000, "color popup did not visibly open");
});

await gate("live_overlay", () => {
  initialize();
  const blank = frame();
  emu.emu_touch(1, 70, 100);
  tick();
  emu.emu_touch(1, 100, 115);
  tick();
  emu.emu_touch(1, 130, 85);
  tick();
  emu.emu_touch(1, 160, 105);
  tick();
  const live = frame();
  emu.emu_touch(0, 0, 0);
  tick();
  const committed = frame();
  check(diffPixels(blank, live) > 50, "live Stroke was not visible");
  let obsolete = 0;
  for (let y = 0; y < 372; y += 1) {
    for (let x = 0; x < panelWidth; x += 1) {
      const index = y * panelWidth + x;
      obsolete += Number(live[index] !== 0xffff && committed[index] === 0xffff);
    }
  }
  check(obsolete === 0, `live preview left ${obsolete} obsolete pixels`);
  tap(30, 410);
  tap(90, 410);
  check(equalPixels(committed, frame()), "whole-Stroke Undo/Redo changed committed pixels");
});

await gate("overlay_canvas_purity", () => {
  initialize();
  const blank = frame();
  draw([{ x: 90, y: 130 }, { x: 120, y: 80 }, { x: 150, y: 140 }, { x: 180, y: 90 }]);
  tap(30, 410);
  check(diffCanvas(blank, frame()) === 0, "Undo exposed pixels accumulated by the live overlay");
});

await gate("edge_ink", () => {
  initialize();
  const blank = frame();
  draw([{ x: 1, y: 1 }, { x: 12, y: 4 }, { x: 35, y: 12 }, { x: 60, y: 20 }]);
  check(diffPixels(blank, frame()) > 20, "top/left edge Stroke was clipped away");
});

await gate("long_gesture", () => {
  initialize();
  const blank = frame();
  const points = [];
  for (let index = 0; index < 192; index += 1) {
    points.push({ x: 40 + (index % 220), y: 100 + (index % 9) });
  }
  draw(points);
  const committed = frame();
  check(diffPixels(blank, committed) > 100, "long Stroke left no ink");
  tap(30, 410);
  check(diffCanvas(blank, frame()) === 0, "one Undo did not remove the whole long Stroke");
  tap(90, 410);
  check(equalPixels(committed, frame()), "Redo did not exactly restore the long Stroke");
});

await gate("mixed_draw", () => {
  for (let zoom = 0; zoom < 5; zoom += 1) {
    initialize();
    for (let step = 0; step < zoom; step += 1) zoomNext();
    const before = frame();
    draw([{ x: 70, y: 100 }, { x: 110, y: 120 }, { x: 160, y: 95 }]);
    check(diffPixels(before, frame()) > 20, `Stroke was invisible at zoom step ${zoom}`);
  }
});

await gate("draw_fill", () => {
  initialize();
  for (let index = 0; index < 40; index += 1) {
    const y = 30 + (index % 20) * 12;
    draw([{ x: 30, y }, { x: 80, y: y + 2 }]);
  }
  zoomNext();
  const before = frame();
  draw([{ x: 180, y: 120 }, { x: 220, y: 145 }, { x: 250, y: 130 }]);
  check(diffPixels(before, frame()) > 20, "foreground Stroke was lost during view convergence");
});

await gate("minimap_navigation", () => {
  initialize();
  draw([{ x: 60, y: 80 }, { x: 220, y: 180 }]);
  zoomNext();
  zoomNext();
  const before = frame();
  emu.emu_touch(1, 352, 356);
  tick();
  emu.emu_touch(1, 340, 340);
  tick();
  emu.emu_touch(0, 0, 0);
  tick();
  check(diffPixels(before, frame()) > 100, "minimap drag did not change the view");
});

async function panGate(name, zoomSteps) {
  await gate(name, () => {
    initialize();
    draw([{ x: 40, y: 60 }, { x: 260, y: 210 }]);
    for (let step = 0; step < zoomSteps; step += 1) zoomNext();
    selectTool(2);
    const before = frame();
    emu.emu_touch(1, 180, 180);
    tick();
    emu.emu_touch(1, 110, 130);
    tick();
    emu.emu_touch(0, 0, 0);
    tick();
    check(diffPixels(before, frame()) > 0, `${name} did not change the view`);
  });
}

await panGate("pan_100", 2);
await panGate("pan_400", 4);

await gate("pan_sequence", () => {
  initialize();
  draw([{ x: 40, y: 60 }, { x: 260, y: 210 }]);
  zoomNext();
  zoomNext();
  selectTool(2);
  let previous = frame();
  emu.emu_touch(1, 240, 220);
  tick();
  for (let step = 1; step <= 8; step += 1) {
    emu.emu_touch(1, 240 - step * 10, 220 - step * 7);
    tick();
    const current = frame();
    check(diffPixels(previous, current) > 0, `pan sequence frame ${step} did not advance`);
    previous = current;
  }
  emu.emu_touch(0, 0, 0);
  tick();
});

await gate("return_overview", () => {
  initialize();
  const overview = frame();
  for (let step = 0; step < 5; step += 1) zoomNext();
  check(equalPixels(overview, frame()), "five ZoomNext events did not restore overview exactly");
});

const traceSpecs = [
  ["fast-curve-dense-25.csv", 0],
  ["fast-curve-400.csv", 4],
  ["fast-curve-400-xl.csv", 4],
  ["slow-precise-100.csv", 2],
  ["scribble-multistroke.csv", 2],
];
await gate("ink_trace_replay", async () => {
  for (const [filename, zoomSteps] of traceSpecs) {
    initialize();
    for (let step = 0; step < zoomSteps; step += 1) zoomNext();
    const before = frame();
    const csv = await readFile(resolve(repository, "testdata/ink-traces", filename), "utf8");
    const lines = csv.trim().split(/\r?\n/).slice(3);
    let down = 0;
    let up = 0;
    for (const line of lines) {
      const [timeText, kind, xText, yText] = line.split(",");
      const eventMs = Math.ceil(Number(timeText) / 1_000);
      if (kind === "Down") {
        down += 1;
        emu.emu_touch(1, Number(xText), Number(yText));
      } else if (kind === "Move") {
        emu.emu_touch(1, Number(xText), Number(yText));
      } else if (kind === "Up") {
        up += 1;
        emu.emu_touch(0, 0, 0);
      } else {
        throw new Error(`${filename}: unsupported trace event ${kind}`);
      }
      tick(Math.max(clockMs + 1, eventMs + 1));
    }
    check(down === up && down > 0, `${filename}: Down/Up counts are not conserved`);
    check(diffPixels(before, frame()) > 20, `${filename}: replay left no visible change`);
  }
});

let stressFrame100;
let stressFrame400;
let stressColdTimings = [];
await gate("stress_100", () => {
  initialize();
  const buildTimings = buildStressDocument(1_000);
  check(guestLog.length === 0, "1,000-Stroke stress document exceeded authority capacity");
  const firstZoom = coldZoom();
  const secondZoom = coldZoom();
  stressColdTimings = [...firstZoom.timings, ...secondZoom.timings];
  stressFrame100 = frame();
  check(diffCanvas(new Uint16Array(panelPixels).fill(0xffff), stressFrame100) > 1_000,
    "1,000-Stroke document was not visible at 100%");
  return `strokes=1000 build_tick_p95=${percentile(buildTimings, 0.95).toFixed(3)}ms ` +
    `cold_ticks=${firstZoom.ticks + secondZoom.ticks}`;
});

await gate("stress_400", () => {
  check(stressFrame100 !== undefined, "stress_100 did not produce a source frame");
  const thirdZoom = coldZoom();
  const fourthZoom = coldZoom();
  stressColdTimings.push(...thirdZoom.timings, ...fourthZoom.timings);
  stressFrame400 = frame();
  check(diffPixels(stressFrame100, stressFrame400) > 100,
    "1,000-Stroke document did not produce a distinct 400% view");
  return `cold_ticks=${thirdZoom.ticks + fourthZoom.ticks}`;
});

await gate("hard_100", () => {
  check(stressFrame100 !== undefined && diffCanvas(new Uint16Array(panelPixels).fill(0xffff),
    stressFrame100) > 1_000, "stress corpus has no visible 100% raster");
  return "framebuffer-visible stress-corpus receipt; no TileProducer claim";
});

await gate("hard_400", () => {
  check(stressFrame400 !== undefined && diffPixels(stressFrame100, stressFrame400) > 100,
    "stress corpus has no distinct 400% raster");
  return "framebuffer-visible stress-corpus receipt; no TileProducer claim";
});

await gate("paced_cold", () => {
  check(stressColdTimings.length > 0, "stress cold timings were not recorded");
  const p95 = percentile(stressColdTimings, 0.95);
  check(p95 <= 16.7, `Puck cold-work tick p95 ${p95.toFixed(3)}ms exceeds 16.7ms`);
  return `Puck tick p95=${p95.toFixed(3)}ms; ESP glass threshold is not asserted`;
});

await gate("overlap_cold", () => {
  initialize();
  for (let index = 0; index < 160; index += 1) {
    const offset = index % 12;
    drawBatched([
      { x: 80 + offset, y: 80 },
      { x: 210 - offset, y: 210 },
      { x: 80 + offset, y: 210 },
    ]);
  }
  const receipt = coldZoom();
  check(receipt.ticks <= 6, `overlap cold view took ${receipt.ticks} ticks`);
  check(diffCanvas(new Uint16Array(panelPixels).fill(0xffff), frame()) > 100,
    "overlap corpus disappeared after cold zoom");
  return `projected overlap corpus, convergence=${receipt.ticks} ticks`;
});

await gate("general_cold", () => {
  initialize();
  buildStressDocument(192);
  const first = coldZoom();
  const second = coldZoom();
  check(first.ticks <= 7 && second.ticks <= 7,
    `general cold convergence was ${first.ticks}/${second.ticks} ticks`);
  check(diffCanvas(new Uint16Array(panelPixels).fill(0xffff), frame()) > 500,
    "general corpus disappeared after cold zooms");
  return `synthetic mixed-position corpus, convergence=${first.ticks}/${second.ticks} ticks`;
});

await gate("workload_touch_projection", () => {
  initialize();
  coldZoom();
  coldZoom();
  let currentTool = 0;
  let replayedSamples = 0;
  for (const operation of owner.operations) {
    if (operation.tool !== currentTool) {
      selectTool(operation.tool);
      currentTool = operation.tool;
    }
    const points = operation.samples.map((sample) => ({
      x: Math.round(sample.xQuarter / 8),
      y: Math.round(sample.yQuarter / 8),
    }));
    emu.emu_touch(1, points[0].x, points[0].y);
    tick();
    for (const point of points.slice(1)) {
      emu.emu_touch(1, point.x, point.y);
      tick();
      replayedSamples += 1;
    }
    emu.emu_touch(0, 0, 0);
    tick();
    replayedSamples += 1;
  }
  check(replayedSamples === owner.sampleCount,
    `projected ${replayedSamples} samples, expected ${owner.sampleCount}`);
  return "102/2706 TDOC schedule projected through integer touch; radii/colors are not imported";
});

await gate("cooperative_compose_input_progress", () => {
  initialize();
  buildStressDocument(96);
  emu.emu_button(0, 1);
  emu.emu_button(0, 0);
  emu.emu_button_verdict(0, 0);
  const request = timedTick();
  const before = frame();
  const foreground = drawBatched([
    { x: 180, y: 100 },
    { x: 240, y: 150 },
  ]);
  const convergence = drainUntilPublished();
  check(diffPixels(before, frame()) > 20, "foreground input was lost while compose was pending");
  return `input accepted (request_push=${request.pushes}, ` +
    `foreground_push=${Number(foreground.pushes > 0)}); ` +
    `authority view published after ${convergence.ticks} more ticks`;
});

await gate("pan_boundary_navigation", () => {
  initialize();
  drawBatched([{ x: 30, y: 30 }, { x: 280, y: 240 }]);
  coldZoom();
  coldZoom();
  selectTool(2);
  const start = frame();
  emu.emu_touch(1, 180, 180);
  emu.emu_touch(1, 179, 180);
  emu.emu_touch(0, 0, 0);
  tick();
  const small = frame();
  emu.emu_touch(1, 180, 180);
  emu.emu_touch(1, 20, 20);
  emu.emu_touch(0, 0, 0);
  tick();
  const large = frame();
  check(diffPixels(start, small) > 0, "one-pixel Pan did not update navigation");
  check(diffPixels(small, large) > 0, "large Pan did not update navigation");
  return "navigation clamping only; cached-pan delta boundary is not asserted";
});

await gate("frame_retention_without_cache", () => {
  initialize();
  buildStressDocument(80);
  const home = frame();
  for (let step = 0; step < 5; step += 1) coldZoom();
  check(equalPixels(home, frame()), "zoom tour did not restore the exact overview frame");
  return "exact framebuffer retention; no cache-residency claim";
});

await gate("idle_stability_proxy", () => {
  initialize();
  buildStressDocument(96);
  coldZoom();
  drainUntilStable();
  const settled = frame();
  for (let idle = 0; idle < 32; idle += 1) {
    check(tick() === 0, `idle tick ${idle} published unexpected damage`);
    check(equalPixels(settled, frame()), `idle tick ${idle} changed the framebuffer`);
  }
  return "32 stable no-input ticks; cache repair is not observed";
});

await gate("small_brush_hairline_visibility", () => {
  initialize();
  tap(270, 410);
  tap(46, 331);
  for (let step = 0; step < 4; step += 1) coldZoom();
  const before = frame();
  for (let line = 0; line < 24; line += 1) {
    const y = 40 + line * 8;
    drawBatched([{ x: 50, y }, { x: 280, y: y + 1 }]);
  }
  check(diffCanvas(before, frame()) > 250, "small-brush lines were not retained at 400%");
  return "smallest interactive brush; evil subpixel authority corpus is not asserted";
});

await gate("history_latency_puck", () => {
  initialize();
  buildStressDocument(128);
  const committed = frame();
  const timings = [];
  const move = (x) => {
    emu.emu_touch(1, x, 410);
    emu.emu_touch(0, 0, 0);
    const first = timedTick();
    timings.push(first.elapsedMs);
    if (first.pushes === 0) timings.push(...drainUntilPublished().timings);
  };
  for (let count = 0; count < 8; count += 1) move(30);
  for (let count = 0; count < 8; count += 1) move(90);
  check(equalPixels(committed, frame()), "8 Undo/Redo transitions changed the final frame");
  const p95 = percentile(timings, 0.95);
  check(p95 <= 16.7, `Puck history tick p95 ${p95.toFixed(3)}ms exceeds 16.7ms`);
  return `Puck tick p95=${p95.toFixed(3)}ms; evil-hairline ESP corpus is not asserted`;
});

await gate("settled_aa", () => {
  initialize();
  draw([{ x: 60, y: 70 }, { x: 145, y: 151 }, { x: 230, y: 221 }]);
  const { ticks: settledAfter } = drainMaterialization();
  const converged = frame();
  const colorCount = canvasColorCount(converged);
  check(colorCount > 2,
    `converged canvas has ${colorCount} colors; expected RGB565 AA coverage shades`);
  for (let idle = 0; idle < 8; idle += 1) {
    check(tick() === 0 && equalPixels(converged, frame()),
      `post-convergence tick ${idle} was not stable`);
  }
  return `settled after ${settledAfter} ticks; RGB565 canvas colors=${colorCount}`;
});

await gate("raster_framebuffer_census", () => {
  initialize();
  buildStressDocument(96);
  const counts = [];
  for (let zoom = 0; zoom < 5; zoom += 1) {
    const pixels = frame();
    let ink = 0;
    for (let y = 0; y < 300; y += 1) {
      for (let x = 0; x < 330; x += 1) {
        ink += Number(pixels[y * panelWidth + x] !== 0xffff);
      }
    }
    counts.push(ink);
    if (zoom < 4) coldZoom();
  }
  check(counts.every((count) => count > 100), `empty raster census: ${counts.join(",")}`);
  check(new Set(counts).size >= 3, `zoom census lacked variation: ${counts.join(",")}`);
  return `framebuffer ink pixels by zoom=${counts.join("/")}; no tile census claim`;
});

await gate("rerender_determinism", () => {
  const render = () => {
    initialize();
    buildStressDocument(128);
    coldZoom();
    coldZoom();
    return frame();
  };
  const first = render();
  const second = render();
  check(equalPixels(first, second), "identical authority/input schedules produced different frames");
  return "byte-exact repeat render; no rerender-ledger amplification claim";
});

const ownerExports = ["tinydraw_owner_buffer", "tinydraw_owner_capacity", "tinydraw_owner_load"];
if (ownerExports.every((name) => typeof emu[name] === "function")) {
  await gate("owner_document", () => {
    initialize();
    const capacity = emu.tinydraw_owner_capacity();
    const pointer = emu.tinydraw_owner_buffer();
    check(capacity >= ownerBytes.byteLength, `owner import capacity ${capacity} is too small`);
    check(pointer > 0 && pointer + ownerBytes.byteLength <= emu.memory.buffer.byteLength,
      "owner import buffer is outside WebAssembly memory");
    new Uint8Array(emu.memory.buffer, pointer, ownerBytes.byteLength).set(ownerBytes);
    check(emu.tinydraw_owner_load(ownerBytes.byteLength) === 1, "owner import rejected TDOC");
    if (tick() === 0) drainUntilPublished();
    const overview = frame();
    for (let step = 0; step < 4; step += 1) {
      coldZoom();
      check(diffPixels(overview, frame()) > 20, `owner corpus did not render at zoom ${step + 1}`);
    }
  });
} else {
  blocked(
    "owner_document",
    `missing exact TDOC import exports: ${ownerExports.join(", ")} (corpus parsed as ` +
      `${owner.operationCount} operations/${owner.sampleCount} samples)`,
  );
}

skip("ring_local", "Puck currently publishes full-panel Application damage; partial ring submits are unavailable");
skip("pan_boundary_cache", "Application does not expose the ESP frame-scroller's 96-pixel reuse boundary");

await gate("cache_retention", () => {
  initialize();
  check(diagnostics().production === 1, "Puck is not using the production canvas");
  buildStressDocument(64);
  for (let zoom = 0; zoom < 4; zoom += 1) {
    coldZoom();
    drainMaterialization();
  }
  coldZoom();
  const revisitMissing = [];
  for (let zoom = 0; zoom < 4; zoom += 1) {
    zoomNext();
    revisitMissing.push(diagnostics().visibleRemaining);
    drainMaterialization();
  }
  check(revisitMissing.every((remaining) => remaining === 0),
    `warm zoom revisit misses=${revisitMissing.join("/")}`);
  const receipt = diagnostics();
  check(receipt.resident <= receipt.slots, "resident raw tiles exceed caller-funded slots");
  return `warm revisit misses=${revisitMissing.join("/")}, raw=${receipt.resident}/${receipt.slots}`;
});

await gate("full_world_cache", () => {
  initialize();
  buildStressDocument(32);
  coldZoom();
  coldZoom();
  const { ticks, receipt } = drainMaterialization();
  const identities = receipt.zoom100Raw + receipt.zoom100Uniform + receipt.zoom100Fallback;
  check(identities === 644, `100% census has ${identities} identities, expected 644`);
  check(receipt.zoom100Fallback === 0, `${receipt.zoom100Fallback} 100% identities remain fallback`);
  check(receipt.zoom100Raw <= receipt.slots, "100% raw census exceeds slot capacity");
  return `raw=${receipt.zoom100Raw} uniform=${receipt.zoom100Uniform} fallback=0 ticks=${ticks}`;
});

await gate("cache_tour", () => {
  initialize();
  buildStressDocument(48);
  coldZoom();
  coldZoom();
  drainMaterialization();
  coldZoom();
  coldZoom();
  drainMaterialization();
  selectTool(2);
  for (const [dx, dy] of [[-120, -90], [150, -120], [-170, 140], [130, 110]]) {
    emu.emu_touch(1, 180, 160);
    emu.emu_touch(1, 180 + dx, 160 + dy);
    emu.emu_touch(0, 0, 0);
    tick();
    drainMaterialization();
  }
  const receipt = diagnostics();
  check(receipt.visibleRemaining === 0 && receipt.fallbackPixels === 0,
    "cache tour did not leave the active view sharp");
  check(receipt.recentViews >= 2, `cache tour remembered only ${receipt.recentViews} zooms`);
  check(receipt.resident <= receipt.slots, "cache tour exceeded slot capacity");
  return `recent_zooms=${receipt.recentViews} raw=${receipt.resident}/${receipt.slots}`;
});

await gate("idle_repair", () => {
  initialize();
  buildStressDocument(32);
  coldZoom();
  coldZoom();
  drainMaterialization();
  coldZoom();
  coldZoom();
  coldZoom();
  drawBatched([{ x: 30, y: 70 }, { x: 300, y: 250 }]);
  const damaged = diagnostics().zoom100Fallback;
  check(damaged > 0, "overview edit did not invalidate any warm 100% identity");
  coldZoom();
  coldZoom();
  const { ticks, receipt } = drainMaterialization();
  check(receipt.zoom100Fallback === 0,
    `${receipt.zoom100Fallback} 100% identities remain after idle repair`);
  return `damaged=${damaged} repaired=0 ticks=${ticks}`;
});

skip("export_encode", "Puck exposes no export command or output sink in its device ABI");

await gate("rerender_ledger", () => {
  initialize();
  buildStressDocument(48);
  for (let zoom = 0; zoom < 4; zoom += 1) {
    coldZoom();
    drainMaterialization();
  }
  const receipt = diagnostics();
  check(receipt.renders > 0 && receipt.uniqueRenders > 0, "producer recorded no render groups");
  check(receipt.unexplainedRenders === 0,
    `${receipt.unexplainedRenders} same-revision renders were unexplained`);
  check(receipt.staleRenders === 0, `${receipt.staleRenders} undamaged groups rerendered stale`);
  return `renders=${receipt.renders} unique=${receipt.uniqueRenders} unexplained=0 stale=0`;
});

skip("tearing_probe", "TE synchronization and panel tearing are physical-display behavior");
skip("pan_sequence_wire", "SPI wire, first-submit, and glass-complete budgets are ESP32-only");
skip("color_dialog_40ms", "the 40 ms receipt measures ESP32 panel completion");
skip("export_reserve", "PSRAM autosave/export coexistence is hardware allocation behavior");

for (const result of results) {
  console.log(`${result.state.padEnd(7)} ${result.name}${result.reason ? ` — ${result.reason}` : ""}`);
}
const failed = results.filter((result) => result.state === "FAIL");
const requiredBlocked = results.filter((result) => result.state === "BLOCKED");
console.log(
  `PUCK_GATE_BATTERY pass=${results.filter((result) => result.state === "PASS").length} ` +
    `fail=${failed.length} blocked=${requiredBlocked.length} ` +
    `skip=${results.filter((result) => result.state === "SKIP").length}`,
);
process.exitCode = failed.length === 0 && requiredBlocked.length === 0 ? 0 : 1;
