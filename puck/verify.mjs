import { readFile } from "node:fs/promises";

const wasmPath = process.argv[2];
if (!wasmPath) throw new Error("usage: node puck/verify.mjs EMU_WASM");

const traceUrl = new URL("../testdata/puck/pinned_trace.txt", import.meta.url);
const [bytes, traceText] = await Promise.all([
  readFile(wasmPath),
  readFile(traceUrl, "utf8").catch((error) => {
    throw new Error(`missing pinned Puck trace ${traceUrl.pathname}: ${error.message}`);
  }),
]);
const module = await WebAssembly.compile(bytes);

const expectedImports = [
  "wasi_snapshot_preview1.fd_close:function",
  "wasi_snapshot_preview1.fd_seek:function",
  "wasi_snapshot_preview1.fd_write:function",
];
const actualImports = WebAssembly.Module.imports(module)
  .map(({ module: namespace, name, kind }) => `${namespace}.${name}:${kind}`)
  .sort();
if (JSON.stringify(actualImports) !== JSON.stringify(expectedImports)) {
  throw new Error(`unexpected imports: ${JSON.stringify(actualImports)}`);
}

const abiExports = [
  "emu_device", "emu_init", "emu_tick", "emu_fb", "emu_push_count", "emu_push_x",
  "emu_push_y", "emu_push_w", "emu_push_h", "emu_touch", "emu_button",
  "emu_button_verdict", "emu_sensor_event",
];
const expectedExports = ["memory:memory", "_initialize:function",
  ...abiExports.map((name) => `${name}:function`)].sort();
const actualExports = WebAssembly.Module.exports(module)
  .map(({ name, kind }) => `${name}:${kind}`)
  .sort();
if (JSON.stringify(actualExports) !== JSON.stringify(expectedExports)) {
  throw new Error(`unexpected exports: ${JSON.stringify(actualExports)}`);
}

function malformedTrace(detail) {
  throw new Error(`malformed pinned Puck trace: ${detail}`);
}

function integer(text, label, maximum = Number.MAX_SAFE_INTEGER) {
  if (!/^(0|[1-9][0-9]*)$/.test(text)) malformedTrace(`invalid ${label}: ${text}`);
  const value = Number(text);
  if (!Number.isSafeInteger(value) || value > maximum) {
    malformedTrace(`${label} is out of range: ${text}`);
  }
  return value;
}

function parseTrace(text) {
  const tokens = text.trim().split(/\s+/);
  let cursor = 0;
  const take = () => {
    if (cursor >= tokens.length) malformedTrace("unexpected end of file");
    return tokens[cursor++];
  };
  if (take() !== "tinydraw-puck-trace-v1") malformedTrace("missing version header");

  const wasmIntMaximum = 0x7fff_ffff;
  const events = [];
  let token = take();
  while (token === "event") {
    const timeText = take();
    const downText = take();
    const event = {
      time: integer(timeText, "event time"),
      down: 0,
      x: 0,
      y: 0,
    };
    event.down = integer(downText, "touch level", 1);
    event.x = integer(take(), "event x", wasmIntMaximum);
    event.y = integer(take(), "event y", wasmIntMaximum);
    events.push(event);
    token = take();
  }
  const hashText = take();
  if (events.length === 0 || token !== "hash" || !/^[0-9a-f]{8}$/.test(hashText)) {
    malformedTrace("events must be followed by an eight-digit lowercase hash");
  }
  const framebufferHash = Number.parseInt(hashText, 16) >>> 0;

  if (take() !== "frames") malformedTrace("missing frame count");
  const frameCount = integer(take(), "frame count");
  if (frameCount !== events.length) malformedTrace("frame count must equal event count");
  const frames = [];
  for (let frame = 0; frame < frameCount; ++frame) {
    if (take() !== "frame" || integer(take(), "frame number") !== frame + 1) {
      malformedTrace(`invalid frame ${frame + 1} header`);
    }
    const rectCount = integer(take(), "rect count");
    const rects = [];
    for (let rect = 0; rect < rectCount; ++rect) {
      if (take() !== "rect") malformedTrace("invalid rect");
      rects.push({
        x: integer(take(), "rect x", wasmIntMaximum),
        y: integer(take(), "rect y", wasmIntMaximum),
        w: integer(take(), "rect width", wasmIntMaximum),
        h: integer(take(), "rect height", wasmIntMaximum),
      });
    }
    frames.push(rects);
  }
  if (take() !== "end" || cursor !== tokens.length) {
    malformedTrace("missing end marker or trailing content");
  }
  return { events, framebufferHash, frames };
}

const pinned = parseTrace(traceText);

function createImports(getMemory, logs) {
  const errnoBadf = 8;
  return {
    wasi_snapshot_preview1: {
      fd_close() { return errnoBadf; },
      fd_seek() { return errnoBadf; },
      fd_write(fd, iovs, iovsLen, written) {
        const memory = getMemory();
        if (!memory) return errnoBadf;
        const view = new DataView(memory.buffer);
        const chunks = [];
        let byteCount = 0;
        for (let index = 0; index < iovsLen; ++index) {
          const pointer = view.getUint32(iovs + index * 8, true);
          const length = view.getUint32(iovs + index * 8 + 4, true);
          chunks.push(new Uint8Array(memory.buffer, pointer, length));
          byteCount += length;
        }
        view.setUint32(written, byteCount, true);
        if (fd === 1 || fd === 2) {
          const joined = new Uint8Array(byteCount);
          let offset = 0;
          for (const chunk of chunks) { joined.set(chunk, offset); offset += chunk.length; }
          logs.push(new TextDecoder().decode(joined));
        }
        return 0;
      },
    },
  };
}

function readCString(memory, pointer) {
  const bytes = new Uint8Array(memory.buffer);
  let end = pointer;
  while (end < bytes.length && bytes[end] !== 0) ++end;
  return new TextDecoder().decode(bytes.subarray(pointer, end));
}

function hashFramebuffer(exports) {
  const pixels = new Uint8Array(exports.memory.buffer, exports.emu_fb(), 368 * 448 * 2);
  let hash = 2166136261 >>> 0;
  for (const byte of pixels) hash = Math.imul(hash ^ byte, 16777619) >>> 0;
  return hash;
}

function pushes(exports) {
  const count = exports.emu_push_count();
  return Array.from({ length: count }, (_, index) => ({
    x: exports.emu_push_x(index), y: exports.emu_push_y(index),
    w: exports.emu_push_w(index), h: exports.emu_push_h(index),
  }));
}

async function instantiateFresh() {
  let memory;
  const logs = [];
  const instance = await WebAssembly.instantiate(module, createImports(() => memory, logs));
  memory = instance.exports.memory;
  instance.exports._initialize();
  if (instance.exports.emu_init() !== 1) throw new Error(`emu_init failed: ${logs.join("")}`);
  const descriptor = JSON.parse(readCString(memory, instance.exports.emu_device()));
  const expectedButtons = [
    { id: "boot", label: "BOOT", edge: "right", at: 0.38 },
    { id: "power", label: "Power", edge: "right", at: 0.62, longPressMs: 4000 },
  ];
  if (descriptor.name !== "TinyDraw Raster V1 interactive core" || descriptor.touch?.points !== 1 ||
      JSON.stringify(descriptor.buttons) !== JSON.stringify(expectedButtons)) {
    throw new Error(`bad TinyDraw descriptor: ${JSON.stringify(descriptor)}`);
  }
  if (memory.buffer.byteLength < 16 * 1024 * 1024) {
    throw new Error(`linear memory below 16 MiB: ${memory.buffer.byteLength}`);
  }
  return { exports: instance.exports, descriptor };
}

function runTrace(exports) {
  const before = hashFramebuffer(exports);
  const pushesBefore = pushes(exports);
  const firstEvent = pinned.events[0];
  exports.emu_touch(firstEvent.down, firstEvent.x, firstEvent.y);
  if (hashFramebuffer(exports) !== before ||
      JSON.stringify(pushes(exports)) !== JSON.stringify(pushesBefore)) {
    throw new Error("emu_touch mutated output before emu_tick");
  }

  const frames = [];
  for (const event of pinned.events) {
    exports.emu_touch(event.down, event.x, event.y);
    exports.emu_tick(event.time);
    frames.push(pushes(exports));
  }
  const after = hashFramebuffer(exports);
  if (after !== pinned.framebufferHash || JSON.stringify(frames) !== JSON.stringify(pinned.frames)) {
    throw new Error(`drawing regression: ${JSON.stringify({ after, frames })}`);
  }

  exports.emu_tick(pinned.events.at(-1).time + 1);
  if (hashFramebuffer(exports) !== after || pushes(exports).length !== 0) {
    throw new Error("idle tick changed the settled frame");
  }
  return { before, after, frames };
}

const first = await instantiateFresh();
const second = await instantiateFresh();
const firstTrace = runTrace(first.exports);
const secondTrace = runTrace(second.exports);
if (JSON.stringify(firstTrace) !== JSON.stringify(secondTrace)) {
  throw new Error("fresh instances diverged for the same trace");
}

console.log(JSON.stringify({
  ok: true,
  bytes: bytes.byteLength,
  memoryBytes: first.exports.memory.buffer.byteLength,
  imports: actualImports,
  exports: ["memory", "_initialize", ...abiExports],
  framebufferHash: firstTrace.after.toString(16).padStart(8, "0"),
  pushFrames: firstTrace.frames.map((frame) => frame.length),
  pushRects: firstTrace.frames,
}));
