#!/usr/bin/env bun

import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { basename, dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repository = resolve(here, "..");
const wasmPath = join(repository, "out", "build", "puck", "puck", "emu.wasm");

const scenarios = [
  {
    trace: "boot-draw.trace.json",
    captures: [16, 128, 256, 512],
    check(run) {
      const boot = run.frames.get(16);
      const immediate = run.frames.get(128);
      const settled = run.frames.get(512);
      assert(boot && immediate && settled, "boot/draw captures are missing");
      assert(diffPixels(boot, immediate) > 100, "drawing a Stroke did not visibly change the canvas");
      assert(inkPixels(immediate) > inkPixels(boot) + 50, "the Stroke left no visible ink in its canvas region");
      assert(inkPixels(settled) > inkPixels(boot) + 50, "background work lost the completed Stroke");
      assert(canvasColorCount(settled) > 2, "settled Stroke has no RGB565 coverage shades");
      const livePushes = run.ticks
        .filter(({ t }) => t >= 32 && t <= 96)
        .flatMap(({ pushes }) => pushes);
      assert(livePushes.length > 0, "live drawing reported no panel pushes");
      assert(livePushes.some(({ w, h }) => w * h < 368 * 448),
        "localized drawing never used a sub-frame panel push");
      assert(livePushes.every(({ x, y, w, h }) => x !== 0 || y !== 0 || w !== 368 || h !== 448),
        "localized drawing collapsed to a full-panel push");
    },
  },
  {
    trace: "history-new.trace.json",
    captures: [192, 288, 464, 544, 656],
    check(run) {
      const stroke = run.frames.get(192);
      const undo = run.frames.get(288);
      const redo = run.frames.get(464);
      const dialog = run.frames.get(544);
      const blank = run.frames.get(656);
      assert(stroke && undo && redo && dialog && blank, "history/new captures are missing");

      const strokeInk = inkPixels(stroke);
      const undoInk = inkPixels(undo);
      const redoInk = inkPixels(redo);
      assert(strokeInk > 50, "history scenario did not create a visible Stroke");
      assert(undoInk < strokeInk / 3, "Undo did not remove the Stroke from the canvas");
      assert(redoInk > undoInk + 50, "Redo did not restore the Stroke");
      assert.equal(diffPixels(stroke, redo, { x: 40, y: 80, w: 240, h: 160 }), 0,
        "Redo did not restore the exact drawing pixels");
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
    trace: "chrome-toggle.trace.json",
    captures: [160, 1024, 1200, 1408, 1776, 1840],
    check(run) {
      const hidden = run.frames.get(160);
      const drawn = run.frames.get(1024);
      const shown = run.frames.get(1200);
      const hiddenAgain = run.frames.get(1408);
      const hiddenFinal = run.frames.get(1776);
      const toolbarPopup = run.frames.get(1840);
      assert(hidden && drawn && shown && hiddenAgain && hiddenFinal && toolbarPopup,
        "HUD-toggle captures are missing");

      const hiddenHudRegions = [
        { name: "battery", rect: { x: 220, y: 16, w: 124, h: 44 } },
        { name: "zoom rail", rect: { x: 303, y: 71, w: 59, h: 158 } },
        { name: "minimap", rect: { x: 265, y: 251, w: 95, h: 118 } },
      ];
      for (const { name, rect } of hiddenHudRegions) {
        assert.equal(nonPaperPixels(hidden, rect), 0, `${name} remained visible after hiding the HUD`);
        assert(nonPaperPixels(drawn, rect) > 0, `drawing did not enter the former ${name} region`);
      }
      const toolbar = { x: 0, y: 372, w: 368, h: 76 };
      assert.equal(nonPaperPixels(hidden, toolbar), 0,
        "bottom toolbar remained visible after hiding chrome");
      assert(nonPaperPixels(drawn, toolbar) > 100,
        "drawing did not enter the former bottom-toolbar region");
      assert(nonPaperPixels(shown, toolbar) > 1_000,
        "showing chrome did not restore the bottom toolbar");
      assert.equal(diffPixels(hiddenAgain, hiddenFinal), 0,
        "repeated chrome show/hide cycles did not preserve the exact drawing");
      assert(diffPixels(hiddenFinal, toolbarPopup, toolbar) > 0,
        "a bottom-edge tap did not draw while chrome was hidden");
    },
  },
  {
    trace: "demo.trace.json",
    captures: [900, 980, 1940, 2772, 2804, 2884, 3044, 3060, 3220],
    check(run) {
      const drawn = run.frames.get(980);
      const recorded = run.frames.get(1940);
      const replayBaseline = run.frames.get(2772);
      const replayPointer = run.frames.get(2804);
      const replayPointerAfterLift = run.frames.get(2884);
      const replayPointerAfter200ms = run.frames.get(3044);
      const replaySideButton = run.frames.get(3060);
      const replayed = run.frames.get(3220);
      assert(drawn && recorded && replayBaseline && replayPointer && replayPointerAfterLift &&
        replayPointerAfter200ms && replaySideButton && replayed,
        "demo captures are missing");
      assert(inkPixels(drawn) > 50, "demo recording did not capture a visible Stroke");
      assert.equal(nonPaperPixels(recorded, { x: 0, y: 372, w: 368, h: 76 }), 0,
        "short BOOT left the bottom toolbar visible during demo recording");
      assert(inkPixels(recorded) > 50, "hiding the HUD lost the demo recording's Stroke");
      assert(inkPixels(replayBaseline) < inkPixels(drawn) / 3,
        `demo replay did not start from a blank authority baseline: ${inkPixels(replayBaseline)} versus ${inkPixels(drawn)}`);
      assert(nonPaperPixels(replayPointer, { x: 84, y: 99, w: 43, h: 43 }) > 300,
        "demo replay did not show the virtual finger at the replayed touch point");
      const lingeringPointerPixels = diffPixels(replayPointerAfterLift, replayed,
        { x: 124, y: 69, w: 43, h: 43 });
      assert(lingeringPointerPixels > 150,
        `demo replay pointer did not linger after the quick tap: ${lingeringPointerPixels} pixels`);
      assert(diffPixels(replayPointerAfter200ms, replayed, { x: 124, y: 69, w: 43, h: 43 }) > 150,
        "demo replay pointer did not remain visible for at least 200 ms");
      assert(diffPixels(replaySideButton, replayed, { x: 338, y: 40, w: 30, h: 60 }) > 300,
        "demo replay did not show the physical side-button press");
      assert.equal(diffPixels(recorded, replayed), 0,
        "demo replay did not reproduce the hidden-HUD recording exactly");
      assert.equal(run.log.some((line) => line.startsWith("TINYDRAW_DEMO_FAIL")), false,
        "demo controller reported a failure");
      const recordingBegin = run.timedLog.find(({ line }) =>
        line.startsWith("TINYDRAW_DEMO_RECORDING_BEGIN"));
      const recordingEnd = run.timedLog.find(({ line }) =>
        line.startsWith("TINYDRAW_DEMO_RECORDING_END"));
      const replayBegin = run.timedLog.find(({ line }) =>
        line.startsWith("TINYDRAW_DEMO_REPLAY_BEGIN"));
      const replayEnd = run.timedLog.find(({ line }) =>
        line.startsWith("TINYDRAW_DEMO_REPLAY_END"));
      assert.equal(recordingBegin?.t, 916,
        "recording did not begin on the release after the firmware hold deadline");
      assert.equal(recordingEnd?.t, 1940,
        "recording did not end on the release after the second hold deadline");
      assert.equal(replayBegin?.t, 2772,
        "replay start overshot the release after the firmware hold deadline");
      assert(replayEnd && replayEnd.t <= 3220,
        "demo replay did not finish within its recorded timing envelope");
    },
  },
];

function usage() {
  console.error("usage: bun puck/verify.mjs <PUCK_REPO> [--write-frames]");
  process.exit(2);
}

const args = process.argv.slice(2);
const puckArgument = args.find((arg) => !arg.startsWith("--"));
if (!puckArgument) usage();
const puck = resolve(puckArgument);
const writeFrames = args.includes("--write-frames");
if (!existsSync(join(puck, "src", "wasm.ts")) || !existsSync(join(puck, "harness", "png.ts"))) {
  throw new Error(`not a current Puck checkout: ${puck}`);
}

if (!existsSync(wasmPath)) throw new Error(`WebAssembly artifact not found: ${wasmPath}`);

const { instantiate, readDeviceDescriptor } = await import(
  pathToFileURL(join(puck, "src", "wasm.ts")).href
);
const { pixelReaderFor, readFramebufferRGB } = await import(
  pathToFileURL(join(puck, "src", "panel.ts")).href
);
const { encodeRGBPNG } = await import(pathToFileURL(join(puck, "harness", "png.ts")).href);
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

function nonPaperPixels(frame, rect) {
  let count = 0;
  for (let y = rect.y; y < rect.y + rect.h; ++y) {
    for (let x = rect.x; x < rect.x + rect.w; ++x) {
      const offset = (y * frame.width + x) * 3;
      if (frame.rgb[offset] !== 255 || frame.rgb[offset + 1] !== 255 || frame.rgb[offset + 2] !== 255) {
        ++count;
      }
    }
  }
  return count;
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

function canvasColorCount(frame) {
  const colors = new Set();
  for (let y = 0; y < 300; ++y) {
    for (let x = 0; x < 260; ++x) {
      const offset = (y * frame.width + x) * 3;
      colors.add(`${frame.rgb[offset]},${frame.rgb[offset + 1]},${frame.rgb[offset + 2]}`);
    }
  }
  return colors.size;
}

function regionColorCount(frame, rect) {
  const colors = new Set();
  for (let y = rect.y; y < rect.y + rect.h; ++y) {
    for (let x = rect.x; x < rect.x + rect.w; ++x) {
      const offset = (y * frame.width + x) * 3;
      colors.add(`${frame.rgb[offset]},${frame.rgb[offset + 1]},${frame.rgb[offset + 2]}`);
    }
  }
  return colors.size;
}

function nonPaperColumnSpan(frame, rect) {
  let first = null;
  let last = null;
  for (let x = rect.x; x < rect.x + rect.w; ++x) {
    if (nonPaperPixels(frame, { x, y: rect.y, w: 1, h: rect.h }) === 0) continue;
    if (first === null) first = x;
    last = x;
  }
  return first === null ? 0 : last - first + 1;
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
  const timedLog = [];
  let currentTick = -1;
  const emu = await instantiate(wasm, (line) => {
    log.push(line);
    timedLog.push({ t: currentTick, line });
  });
  assert.equal(emu.emu_init(), 1, "emu_init failed");
  const device = readDeviceDescriptor(emu);
  assert.deepEqual(device.panel, { w: 368, h: 448, format: "rgb565be" });
  assert.equal(device.buttons?.length, 1);
  assert.equal(device.buttons?.[0]?.longPressMs, undefined);
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
        currentTick = event.t;
        const tickStarted = performance.now();
        emu.emu_tick(event.t);
        const tickMs = performance.now() - tickStarted;
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
        ticks.push({ t: event.t, hash, pushes, tickMs });
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
  return { device, ticks, frames, log, timedLog, totalPushes, changedTicks };
}

async function verifyLongStrokeHistory() {
  const events = [{ t: 0, k: "tick" }, { t: 16, k: "tick" }];
  let now = 24;
  events.push({ t: now, k: "touch", down: 1, x: 60, y: 100 }, { t: now, k: "tick" });
  for (let sample = 1; sample <= 4_095; ++sample) {
    now += 8;
    events.push(
      {
        t: now,
        k: "touch",
        down: 1,
        x: 60 + (sample % 180),
        y: 100 + ((Math.floor(sample / 180) * 30) % 120),
      },
      { t: now, k: "tick" },
    );
  }
  now += 8;
  events.push({ t: now, k: "touch", down: 0, x: 212, y: 160 }, { t: now, k: "tick" });
  for (let i = 0; i < 20; ++i) {
    now += 16;
    events.push({ t: now, k: "tick" });
  }
  const drawnAt = now;
  now += 16;
  events.push({ t: now, k: "touch", down: 1, x: 30, y: 410 }, { t: now, k: "tick" });
  now += 16;
  events.push({ t: now, k: "touch", down: 0, x: 30, y: 410 }, { t: now, k: "tick" });
  for (let i = 0; i < 12; ++i) {
    now += 16;
    events.push({ t: now, k: "tick" });
  }
  const undoneAt = now;
  const trace = { events };
  const first = await replay(trace, [drawnAt, undoneAt]);
  const second = await replay(trace, [drawnAt, undoneAt]);
  assert.deepEqual(
    second.ticks.map(({ tickMs: _tickMs, ...tick }) => tick),
    first.ticks.map(({ tickMs: _tickMs, ...tick }) => tick),
    "long Stroke replay is nondeterministic",
  );
  const drawn = first.frames.get(drawnAt);
  const undone = first.frames.get(undoneAt);
  assert(drawn && undone, "long Stroke captures are missing");
  assert(inkPixels(drawn) > 1_000, "long Stroke left too little visible ink");
  assert(inkPixels(undone) < inkPixels(drawn) / 3,
    "one Undo did not remove the uninterrupted long Stroke as one history unit");
  const report = first.log.findLast((line) => line.startsWith("TINYDRAW_LIVE_STROKE "));
  assert(report?.includes("operations=1 "),
    `long gesture fragmented into multiple operations: ${report ?? "no stroke report"}`);
  const sampleCount = Number(report?.match(/ samples=([0-9]+)/)?.[1] ?? 0);
  assert.equal(sampleCount, 4_097,
    `near-capacity Stroke retained ${sampleCount} samples instead of 4097`);
  const worstTickMs = Math.max(...first.ticks.map(({ tickMs }) => tickMs));
  assert(worstTickMs < 20,
    `near-capacity Stroke blocked one host tick for ${worstTickMs.toFixed(1)} ms`);
  console.log(
    `PASS near-capacity Stroke: 4,097 samples, deterministic, one-step Undo, ` +
      `${worstTickMs.toFixed(1)} ms worst tick`,
  );
}

async function verifyHistoryPresentationFailure() {
  const events = [{ t: 0, k: "tick" }, { t: 16, k: "tick" }];
  for (const event of [
    { t: 32, k: "touch", down: 1, x: 80, y: 120 },
    { t: 48, k: "touch", down: 1, x: 120, y: 140 },
    { t: 64, k: "touch", down: 1, x: 160, y: 160 },
    { t: 80, k: "touch", down: 1, x: 200, y: 180 },
    { t: 96, k: "touch", down: 1, x: 240, y: 200 },
    { t: 112, k: "touch", down: 0, x: 240, y: 200 },
  ]) {
    events.push(event, { t: event.t, k: "tick" });
  }
  for (let t = 128; t <= 192; t += 16) events.push({ t, k: "tick" });
  events.push(
    { t: 208, k: "sensor", i: -3 },
    { t: 208, k: "touch", down: 1, x: 30, y: 410 },
    { t: 208, k: "tick" },
    { t: 224, k: "touch", down: 0, x: 30, y: 410 },
    { t: 224, k: "tick" },
  );
  for (let t = 240; t <= 1_200; t += 16) events.push({ t, k: "tick" });

  const run = await replay({ events }, [192, 1_200]);
  const drawn = run.frames.get(192);
  const settled = run.frames.get(1_200);
  assert(drawn && settled, "history presentation-failure captures are missing");
  assert(inkPixels(drawn) > 50, "presentation-failure scenario did not draw a Stroke");
  assert(inkPixels(settled) < inkPixels(drawn) / 3,
    "failed history feedback left the 25% frame stale after authority Undo");
  assert(run.log.some((line) => line.includes("kind=undo-dock") && line.includes("pass=0")),
    "history scenario did not force the dock presentation failure");
  console.log("PASS failed history feedback preserves semantic Undo and repairs the 25% frame");
}

async function verifyRecordedStrokeAaSurvivesHudToggle() {
  const events = [
    { t: 0, k: "tick" },
    { t: 100, k: "button", i: 0, down: 1 },
    { t: 100, k: "tick" },
    { t: 900, k: "tick" },
    { t: 916, k: "button", i: 0, down: 0 },
    { t: 916, k: "tick" },
  ];
  for (const event of [
    { t: 932, k: "touch", down: 1, x: 70, y: 100 },
    { t: 948, k: "touch", down: 1, x: 105, y: 120 },
    { t: 964, k: "touch", down: 1, x: 145, y: 90 },
    { t: 980, k: "touch", down: 0, x: 145, y: 90 },
  ]) {
    events.push(event, { t: event.t, k: "tick" });
  }
  const shortButtonPress = (downAt) => {
    events.push(
      { t: downAt, k: "button", i: 0, down: 1 },
      { t: downAt, k: "tick" },
      { t: downAt + 16, k: "button", i: 0, down: 0 },
      { t: downAt + 16, k: "tick" },
    );
  };
  const tap = (downAt, x, y) => {
    events.push(
      { t: downAt, k: "touch", down: 1, x, y },
      { t: downAt, k: "tick" },
      { t: downAt + 16, k: "touch", down: 0, x, y },
      { t: downAt + 16, k: "tick" },
    );
  };
  const idleThrough = (from, through) => {
    for (let t = from; t <= through; t += 8) events.push({ t, k: "tick" });
  };

  shortButtonPress(996);  // Hide the HUD immediately after lift.
  idleThrough(1_020, 1_200);
  shortButtonPress(1_216);  // Show it so zoom controls are available.
  idleThrough(1_240, 1_400);
  tap(1_416, 332, 98);      // Zoom in.
  idleThrough(1_440, 1_700);
  tap(1_716, 332, 200);     // Zoom back out to 1x.
  idleThrough(1_740, 2_000);

  const run = await replay({ events }, [1_196, 1_696, 1_996]);
  const hiddenAfterDraw = run.frames.get(1_196);
  const zoomed = run.frames.get(1_696);
  const afterZoomCycle = run.frames.get(1_996);
  assert(hiddenAfterDraw && zoomed && afterZoomCycle, "recording/HUD AA captures are missing");
  const strokeRegion = { x: 50, y: 70, w: 120, h: 75 };
  assert(regionColorCount(hiddenAfterDraw, strokeRegion) > 2,
    "hiding the HUD left the recorded 1x Stroke without AA coverage shades");
  assert(diffPixels(hiddenAfterDraw, zoomed) > 100, "zoom-in did not change the recorded Stroke view");
  assert.equal(diffPixels(hiddenAfterDraw, afterZoomCycle, strokeRegion), 0,
    "hiding the HUD presented a different 1x Stroke than a subsequent zoom-in/out settle");
  console.log("PASS recorded 1x Stroke keeps exact AA pixels across HUD hide and zoom cycle");
}

async function verifyStrokeAfterHudToggle() {
  for (const startDelay of [1, 2, 4, 8, 12, 16, 24, 32, 48, 64]) {
    const events = [{ t: 0, k: "tick" }, { t: 16, k: "tick" }];
    const touch = (t, down, x, y) => events.push(
      { t, k: "touch", down, x, y },
      { t, k: "tick" },
    );
    touch(32, 1, 70, 100);
    touch(48, 1, 140, 130);
    touch(64, 1, 220, 100);
    touch(80, 0, 220, 100);
    for (let t = 96; t <= 240; t += 8) events.push({ t, k: "tick" });
    events.push(
      { t: 256, k: "button", i: 0, down: 1 },
      { t: 256, k: "tick" },
      { t: 272, k: "button", i: 0, down: 0 },
      { t: 272, k: "tick" },
    );

    let now = 272 + startDelay;
    touch(now, 1, 40, 260);
    for (const x of [80, 120, 160, 200, 240, 280]) {
      now += 8;
      touch(now, 1, x, 260);
    }
    now += 8;
    touch(now, 0, 280, 260);
    for (let t = now + 8; t <= now + 400; t += 8) events.push({ t, k: "tick" });
    const beforeRepair = now + 400;

    now = beforeRepair + 16;
    touch(now, 1, 210, 410);
    now += 16;
    touch(now, 0, 210, 410);
    for (let t = now + 8; t <= now + 120; t += 8) events.push({ t, k: "tick" });
    now += 136;
    touch(now, 1, 210, 410);
    now += 16;
    touch(now, 0, 210, 410);
    for (let t = now + 8; t <= now + 400; t += 8) events.push({ t, k: "tick" });
    const afterRepair = now + 400;

    const run = await replay({ events }, [beforeRepair, afterRepair]);
    const before = run.frames.get(beforeRepair);
    const after = run.frames.get(afterRepair);
    assert(before && after, `HUD/draw captures are missing at ${startDelay} ms`);
    const strokeRegion = { x: 30, y: 245, w: 260, h: 30 };
    const beforeSpan = nonPaperColumnSpan(before, strokeRegion);
    const afterSpan = nonPaperColumnSpan(after, strokeRegion);
    assert(beforeSpan > 200,
      `post-HUD Stroke collapsed to ${beforeSpan}px at ${startDelay} ms; color-popup repair restored ${afterSpan}px`);
    assert(afterSpan > 200,
      `post-HUD Stroke authority retained only ${afterSpan}px at ${startDelay} ms`);
  }
  console.log("PASS post-HUD Stroke stays full-length across refresh timing windows");
}

async function verifyTwoLargestBrushSelections() {
  const events = [{ t: 0, k: "tick" }, { t: 16, k: "tick" }];
  const event = (t, value) => events.push(value, { t, k: "tick" });
  let now = 32;
  const tap = (x, y) => {
    event(now, { t: now, k: "touch", down: 1, x, y });
    now += 16;
    event(now, { t: now, k: "touch", down: 0, x, y });
    now += 16;
  };
  const horizontalStroke = (y) => {
    for (let x = 80; x <= 280; x += 4) {
      event(now, { t: now, k: "touch", down: 1, x, y });
      now += 8;
    }
    event(now, { t: now, k: "touch", down: 0, x: 280, y });
    now += 64;
  };

  tap(270, 410);
  tap(184, 329);
  horizontalStroke(150);
  tap(270, 410);
  tap(306, 329);
  horizontalStroke(250);
  const settledAt = now + 1_000;
  for (let t = now; t <= settledAt; t += 8) events.push({ t, k: "tick" });

  const run = await replay({ events }, [settledAt]);
  const frame = run.frames.get(settledAt);
  assert(frame, "large-brush selection capture is missing");
  const verticalInkSpan = (x, y0, y1) => {
    let first = null;
    let last = null;
    for (let y = y0; y < y1; ++y) {
      if (nonPaperPixels(frame, { x, y, w: 1, h: 1 }) === 0) continue;
      if (first === null) first = y;
      last = y;
    }
    return first === null ? 0 : last - first + 1;
  };
  const thirty = verticalInkSpan(180, 100, 200);
  const fortyFive = verticalInkSpan(180, 200, 300);
  assert(thirty >= 20, `30px brush rendered only ${thirty}px wide`);
  assert(fortyFive >= thirty + 8,
    `45px brush was not materially larger than 30px: ${fortyFive}px versus ${thirty}px`);
  console.log(`PASS 3x2 picker selects 30px (${thirty}px ink) and 45px (${fortyFive}px ink) brushes`);
}

async function verifyPhysicalEdgeInsetStillReachesCanvasEdges() {
  const events = [{ t: 0, k: "tick" }, { t: 16, k: "tick" }];
  const event = (t, value) => events.push(value, { t, k: "tick" });
  event(32, { t: 32, k: "button", i: 0, down: 1 });
  event(48, { t: 48, k: "button", i: 0, down: 0 });
  for (let t = 56; t <= 160; t += 8) events.push({ t, k: "tick" });

  let now = 168;
  const stroke = (points) => {
    for (let index = 0; index < points.length; ++index) {
      const [x, y] = points[index];
      event(now, { t: now, k: "touch", down: 1, x, y });
      now += 8;
    }
    const [x, y] = points.at(-1);
    event(now, { t: now, k: "touch", down: 0, x, y });
    now += 8;
  };
  const samples = 111;
  stroke(Array.from({ length: samples }, (_, index) => [
    7 + Math.round(index * 11 / (samples - 1)),
    40 + index * 2,
  ]));
  stroke(Array.from({ length: samples }, (_, index) => [
    363 - Math.round(index * 8 / (samples - 1)),
    40 + index * 2,
  ]));
  stroke(Array.from({ length: 145 }, (_, index) => [
    40 + index * 2,
    14,
  ]));
  stroke(Array.from({ length: 145 }, (_, index) => [
    40 + index * 2,
    433,
  ]));
  const settledAt = now + 1_000;
  for (let t = now; t <= settledAt; t += 8) events.push({ t, k: "tick" });

  const run = await replay({ events }, [settledAt]);
  const frame = run.frames.get(settledAt);
  assert(frame, "edge-inset Stroke capture is missing");
  const whitePixels = (rect) => rect.w * rect.h - nonPaperPixels(frame, rect);
  assert.equal(whitePixels({ x: 0, y: 60, w: 1, h: 180 }), 0,
    "physical left-edge inset left a white canvas gutter");
  assert.equal(whitePixels({ x: 367, y: 60, w: 1, h: 180 }), 0,
    "physical right-edge inset left a white canvas gutter");
  assert.equal(whitePixels({ x: 60, y: 0, w: 240, h: 1 }), 0,
    "physical top-edge inset left a white canvas gutter");
  assert.equal(whitePixels({ x: 60, y: 447, w: 240, h: 1 }), 0,
    "physical bottom-edge inset left a white canvas gutter while chrome was hidden");
  console.log("PASS physical edge insets still paint every canvas-edge pixel");
}

async function verifyHudHideDismissesColorsInputShield() {
  const events = [{ t: 0, k: "tick" }, { t: 16, k: "tick" }];
  const touch = (t, down, x, y) => events.push(
    { t, k: "touch", down, x, y },
    { t, k: "tick" },
  );
  touch(32, 1, 210, 410);
  touch(48, 0, 210, 410);
  for (let t = 64; t <= 160; t += 8) events.push({ t, k: "tick" });
  events.push(
    { t: 176, k: "button", i: 0, down: 1 },
    { t: 176, k: "tick" },
    { t: 192, k: "button", i: 0, down: 0 },
    { t: 192, k: "tick" },
  );
  for (let t = 200; t <= 320; t += 8) events.push({ t, k: "tick" });
  touch(336, 1, 40, 260);
  for (const [t, x] of [[344, 80], [352, 120], [360, 160], [368, 200], [376, 240], [384, 280]]) {
    touch(t, 1, x, 260);
  }
  touch(392, 0, 280, 260);
  for (let t = 400; t <= 800; t += 8) events.push({ t, k: "tick" });

  const run = await replay({ events }, [800]);
  const frame = run.frames.get(800);
  assert(frame, "Colors/HUD input-shield capture is missing");
  const span = nonPaperColumnSpan(frame, { x: 30, y: 245, w: 260, h: 30 });
  assert(span > 200,
    `HUD hide retained the Colors input shield; intended 240px Stroke rendered only ${span}px`);
  console.log("PASS HUD hide dismisses the Colors input shield before drawing");
}

for (const scenario of scenarios) {
  const tracePath = join(here, "traces", scenario.trace);
  const trace = JSON.parse(readFileSync(tracePath, "utf8"));
  const first = await replay(trace, scenario.captures);
  const second = await replay(trace, scenario.captures);
  assert.deepEqual(second.device, first.device, `${scenario.trace}: device descriptor is nondeterministic`);
  assert.deepEqual(
    second.ticks.map(({ tickMs: _tickMs, ...tick }) => tick),
    first.ticks.map(({ tickMs: _tickMs, ...tick }) => tick),
    `${scenario.trace}: pixels or push rectangles are nondeterministic`,
  );
  scenario.check(first);

  if (writeFrames) {
    const stem = basename(scenario.trace).replace(/\.trace\.json$/, "");
    for (const at of scenario.captures) {
      const frame = first.frames.get(at);
      assert(frame, `${scenario.trace}: missing requested frame at t=${at}`);
      await Bun.write(join(here, "frames", `${stem}.t${at}.png`), encodeRGBPNG(frame.width, frame.height, frame.rgb));
    }
  } else {
    const stem = basename(scenario.trace).replace(/\.trace\.json$/, "");
    for (const at of scenario.captures) {
      const frame = first.frames.get(at);
      assert(frame, `${scenario.trace}: missing requested frame at t=${at}`);
      const actual = Buffer.from(encodeRGBPNG(frame.width, frame.height, frame.rgb));
      const expectedPath = join(here, "frames", `${stem}.t${at}.png`);
      const expected = readFileSync(expectedPath);
      assert.equal(Buffer.compare(actual, expected), 0,
        `${scenario.trace}: frame t=${at} differs from its tolerance-0 baseline`);
    }
  }
  console.log(
    `PASS ${scenario.trace}: ${first.ticks.length} ticks, ${first.changedTicks} changed, ${first.totalPushes} pushes, deterministic`
  );
}

await verifyLongStrokeHistory();
await verifyHistoryPresentationFailure();
await verifyRecordedStrokeAaSurvivesHudToggle();
await verifyStrokeAfterHudToggle();
await verifyTwoLargestBrushSelections();
await verifyPhysicalEdgeInsetStillReachesCanvasEdges();
await verifyHudHideDismissesColorsInputShield();

console.log(writeFrames
  ? "PASS semantic trace assertions; recorded tolerance-0 baselines updated"
  : "PASS semantic trace assertions and tolerance-0 recorded frames");
