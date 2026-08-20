#!/usr/bin/env bun

import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repository = resolve(here, "..");
const wasmPath = join(repository, "out", "build", "puck", "puck", "emu.wasm");
const bundlePath = join(here, "external-bundle.json");

const scenarios = [
  {
    trace: "boot-draw.trace.json",
    captures: [16, 128, 256],
    check(run) {
      const boot = run.frames.get(16);
      const immediate = run.frames.get(128);
      const settled = run.frames.get(256);
      assert(boot && immediate && settled, "boot/draw captures are missing");
      assert(diffPixels(boot, immediate) > 100, "drawing a Stroke did not visibly change the canvas");
      assert(inkPixels(immediate) > inkPixels(boot) + 50, "the Stroke left no visible ink in its canvas region");
      assert(inkPixels(settled) > inkPixels(boot) + 50, "background work lost the completed Stroke");
    },
  },
  {
    trace: "history-new.trace.json",
    captures: [192, 288, 384, 464, 576],
    check(run) {
      const stroke = run.frames.get(192);
      const undo = run.frames.get(288);
      const redo = run.frames.get(384);
      const dialog = run.frames.get(464);
      const blank = run.frames.get(576);
      assert(stroke && undo && redo && dialog && blank, "history/new captures are missing");

      const strokeInk = inkPixels(stroke);
      const undoInk = inkPixels(undo);
      const redoInk = inkPixels(redo);
      assert(strokeInk > 50, "history scenario did not create a visible Stroke");
      assert(undoInk < strokeInk / 3, "Undo did not remove the Stroke from the canvas");
      assert(redoInk > undoInk + 50, "Redo did not restore the Stroke");
      assert(diffPixels(redo, dialog) > 1_000, "New did not present its confirmation dialog");
      assert(inkPixels(blank) < redoInk / 3, "confirmed New did not clear drawing authority");
    },
  },
  {
    trace: "navigation.trace.json",
    captures: [160, 208, 368, 448],
    check(run) {
      const drawn = run.frames.get(160);
      const zoomed = run.frames.get(208);
      const panned = run.frames.get(368);
      const minimap = run.frames.get(448);
      assert(drawn && zoomed && panned && minimap, "navigation captures are missing");
      assert(diffPixels(drawn, zoomed) > 100, "zoom did not visibly change the view");
      assert(diffPixels(zoomed, panned) > 100, "canvas Pan did not visibly change the view");
      assert(diffPixels(panned, minimap) > 100, "minimap navigation did not visibly change the view");
    },
  },
  {
    trace: "demo.trace.json",
    captures: [900, 980, 1012, 1844, 2660, 2772],
    check(run) {
      const drawn = run.frames.get(980);
      const zoomed = run.frames.get(1012);
      const recorded = run.frames.get(1844);
      const replayBaseline = run.frames.get(2660);
      const replayed = run.frames.get(2772);
      assert(drawn && zoomed && recorded && replayBaseline && replayed, "demo captures are missing");
      assert(inkPixels(drawn) > 50, "demo recording did not capture a visible Stroke");
      assert(diffPixels(drawn, zoomed) > 100, "short BOOT did not zoom during demo recording");
      assert(inkPixels(replayBaseline) < inkPixels(drawn) / 3,
        `demo replay did not start from a blank authority baseline: ${inkPixels(replayBaseline)} versus ${inkPixels(drawn)}`);
      assert.equal(diffPixels(recorded, replayed), 0,
        "demo replay did not reproduce the recorded final framebuffer exactly");
    },
  },
];

function usage() {
  console.error("usage: bun puck/verify.mjs <PUCK_REPO> [--no-build] [--write-frames]");
  process.exit(2);
}

const args = process.argv.slice(2);
const puckArgument = args.find((arg) => !arg.startsWith("--"));
if (!puckArgument) usage();
const puck = resolve(puckArgument);
const noBuild = args.includes("--no-build");
const writeFrames = args.includes("--write-frames");
if (!existsSync(join(puck, "src", "wasm.ts")) || !existsSync(join(puck, "harness", "png.ts"))) {
  throw new Error(`not a current Puck checkout: ${puck}`);
}

if (!noBuild) {
  const built = Bun.spawnSync([join(repository, "scripts", "puck")], {
    cwd: repository,
    stdout: "inherit",
    stderr: "inherit",
    env: process.env,
  });
  if (!built.success) process.exit(built.exitCode ?? 1);
}
if (!existsSync(wasmPath)) throw new Error(`WebAssembly artifact not found: ${wasmPath}`);

const { instantiate, readDeviceDescriptor } = await import(
  pathToFileURL(join(puck, "src", "wasm.ts")).href
);
const { pixelReaderFor, readFramebufferRGB } = await import(
  pathToFileURL(join(puck, "src", "panel.ts")).href
);
const { encodeRGBPNG } = await import(pathToFileURL(join(puck, "harness", "png.ts")).href);
const { verifyPortFrames } = await import(pathToFileURL(join(puck, "harness", "portdiff.ts")).href);
const wasm = await Bun.file(wasmPath).arrayBuffer();

function frameHash(rgb) {
  return createHash("sha256").update(rgb).digest("hex");
}

function diffPixels(a, b, rect = { x: 0, y: 0, w: a.width, h: a.height }) {
  assert.equal(a.width, b.width);
  assert.equal(a.height, b.height);
  let changed = 0;
  for (let y = rect.y; y < rect.y + rect.h; ++y) {
    for (let x = rect.x; x < rect.x + rect.w; ++x) {
      const offset = (y * a.width + x) * 3;
      if (
        a.rgb[offset] !== b.rgb[offset] ||
        a.rgb[offset + 1] !== b.rgb[offset + 1] ||
        a.rgb[offset + 2] !== b.rgb[offset + 2]
      ) {
        ++changed;
      }
    }
  }
  return changed;
}

// The exercised Stroke stays inside this rectangle. It excludes every
// persistent chrome overlay, so non-paper pixels here are drawing pixels.
function inkPixels(frame) {
  const rect = { x: 40, y: 80, w: 240, h: 160 };
  let ink = 0;
  for (let y = rect.y; y < rect.y + rect.h; ++y) {
    for (let x = rect.x; x < rect.x + rect.w; ++x) {
      const offset = (y * frame.width + x) * 3;
      if (frame.rgb[offset] !== 255 || frame.rgb[offset + 1] !== 255 || frame.rgb[offset + 2] !== 255) {
        ++ink;
      }
    }
  }
  return ink;
}

function pushList(emu, width, height) {
  const count = emu.emu_push_count();
  assert(Number.isInteger(count) && count >= 0 && count <= 256, `invalid push count ${count}`);
  const pushes = [];
  for (let index = 0; index < count; ++index) {
    const push = {
      x: emu.emu_push_x(index),
      y: emu.emu_push_y(index),
      w: emu.emu_push_w(index),
      h: emu.emu_push_h(index),
    };
    assert(
      Number.isInteger(push.x) &&
        Number.isInteger(push.y) &&
        Number.isInteger(push.w) &&
        Number.isInteger(push.h) &&
        push.x >= 0 &&
        push.y >= 0 &&
        push.w > 0 &&
        push.h > 0 &&
        push.x + push.w <= width &&
        push.y + push.h <= height,
      `out-of-bounds push ${JSON.stringify(push)} for ${width}x${height}`
    );
    pushes.push(push);
  }
  return pushes;
}

function changedPixelsAreCovered(previous, current, pushes) {
  const { width, height } = current;
  for (let y = 0; y < height; ++y) {
    for (let x = 0; x < width; ++x) {
      const offset = (y * width + x) * 3;
      const changed =
        previous.rgb[offset] !== current.rgb[offset] ||
        previous.rgb[offset + 1] !== current.rgb[offset + 1] ||
        previous.rgb[offset + 2] !== current.rgb[offset + 2];
      if (!changed) continue;
      const covered = pushes.some(
        (push) => x >= push.x && x < push.x + push.w && y >= push.y && y < push.y + push.h
      );
      assert(covered, `framebuffer changed outside the push log at (${x}, ${y})`);
    }
  }
}

async function replay(trace, capturePoints) {
  const log = [];
  const emu = await instantiate(wasm, (line) => log.push(line));
  assert.equal(emu.emu_init(), 1, "emu_init failed");
  const device = readDeviceDescriptor(emu);
  assert.deepEqual(device.panel, { w: 368, h: 448, format: "rgb565" });
  assert.equal(device.buttons?.length, 1);
  assert.equal(device.buttons?.[0]?.longPressMs, 800);
  const reader = pixelReaderFor(device.panel.format);
  const framebuffer = emu.emu_fb();
  assert(framebuffer > 0, "emu_fb returned a null pointer");

  const readFrame = () => ({
    width: device.panel.w,
    height: device.panel.h,
    rgb: readFramebufferRGB(emu.memory, framebuffer, device.panel.w, reader, {
      x: 0,
      y: 0,
      w: device.panel.w,
      h: device.panel.h,
    }),
  });

  let previous = readFrame();
  let totalPushes = 0;
  let changedTicks = 0;
  const ticks = [];
  const frames = new Map();
  const wanted = new Set(capturePoints);

  for (const event of trace.events) {
    switch (event.k) {
      case "touch":
        emu.emu_touch(event.down, event.x, event.y);
        break;
      case "button":
        emu.emu_button(event.i, event.down);
        break;
      case "verdict":
        emu.emu_button_verdict(event.i, event.long);
        break;
      case "sensor":
        emu.emu_sensor_event(event.i);
        break;
      case "vector":
        emu.emu_sensor_vector?.(event.i, event.x, event.y, event.z);
        break;
      case "tick": {
        emu.emu_tick(event.t);
        const current = readFrame();
        const pushes = pushList(emu, device.panel.w, device.panel.h);
        const changed = diffPixels(previous, current);
        if (changed > 0) {
          ++changedTicks;
          assert(pushes.length > 0, `t=${event.t}: framebuffer changed without a push`);
          changedPixelsAreCovered(previous, current, pushes);
        }
        totalPushes += pushes.length;
        const hash = frameHash(current.rgb);
        ticks.push({ t: event.t, hash, pushes });
        if (wanted.has(event.t)) frames.set(event.t, current);
        previous = current;
        break;
      }
      default:
        throw new Error(`unsupported trace event ${JSON.stringify(event)}`);
    }
  }

  assert(changedTicks > 0, "trace never changed the framebuffer");
  assert(totalPushes > 0, "trace never reported a panel push");
  return { device, ticks, frames, log, totalPushes, changedTicks };
}

for (const scenario of scenarios) {
  const tracePath = join(here, "traces", scenario.trace);
  const trace = JSON.parse(readFileSync(tracePath, "utf8"));
  const first = await replay(trace, scenario.captures);
  const second = await replay(trace, scenario.captures);
  assert.deepEqual(second.device, first.device, `${scenario.trace}: device descriptor is nondeterministic`);
  assert.deepEqual(second.ticks, first.ticks, `${scenario.trace}: pixels or push rectangles are nondeterministic`);
  scenario.check(first);

  if (writeFrames) {
    const stem = basename(scenario.trace).replace(/\.trace\.json$/, "");
    for (const at of scenario.captures) {
      const frame = first.frames.get(at);
      assert(frame, `${scenario.trace}: missing requested frame at t=${at}`);
      await Bun.write(join(here, "frames", `${stem}.t${at}.png`), encodeRGBPNG(frame.width, frame.height, frame.rgb));
    }
  }
  console.log(
    `PASS ${scenario.trace}: ${first.ticks.length} ticks, ${first.changedTicks} changed, ${first.totalPushes} pushes, deterministic`
  );
}

const recordedFramesPresent = scenarios.every((scenario) =>
  scenario.captures.every((at) => {
    const stem = basename(scenario.trace).replace(/\.trace\.json$/, "");
    return existsSync(join(here, "frames", `${stem}.t${at}.png`));
  })
);
if (recordedFramesPresent) {
  const pixelExact = await verifyPortFrames({
    modulePath: wasmPath,
    tolerance: 0,
    traces: scenarios.map((scenario) => ({
      tracePath: join(here, "traces", scenario.trace),
      framesDir: join(here, "frames"),
    })),
  });
  assert.deepEqual(pixelExact.errors, [], `Puck frame verification errors: ${pixelExact.errors.join("; ")}`);
  assert(pixelExact.allMatch, "current WebAssembly output diverged from the recorded Puck frames");
  console.log(`PASS recorded frames: ${pixelExact.frames.length} pixel-exact matches at tolerance 0`);
} else if (!writeFrames) {
  console.log("SKIP recorded frames: rerun with --write-frames to create the pixel-exact baseline");
}

const bundle = JSON.parse(readFileSync(bundlePath, "utf8"));
const commit = bundle.ports?.[0]?.build?.commit;
if (/^0+$/.test(commit ?? "")) {
  console.log("SKIP external bundle reproduction: replace the all-zero commit after the implementation commit exists");
} else if (!recordedFramesPresent) {
  console.log("SKIP external bundle reproduction: recorded frames are missing; rerun with --write-frames");
} else {
  const verified = Bun.spawnSync(["bun", "run", join(puck, "tools", "verify-bundle.ts"), bundlePath], {
    cwd: puck,
    stdout: "inherit",
    stderr: "inherit",
    env: process.env,
  });
  if (!verified.success) process.exit(verified.exitCode ?? 1);
}
