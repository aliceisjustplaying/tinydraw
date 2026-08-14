import { readFile } from "node:fs/promises";

const wasmPath = process.argv[2];
if (!wasmPath) throw new Error("usage: node puck/verify.mjs EMU_WASM");
const bytes = await readFile(wasmPath);
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
  const length = 368 * 448 * 2;
  const pixels = new Uint8Array(exports.memory.buffer, exports.emu_fb(), length);
  let hash = 2166136261 >>> 0;
  for (const byte of pixels) hash = Math.imul(hash ^ byte, 16777619) >>> 0;
  return hash;
}

function pushes(exports) {
  const count = exports.emu_push_count();
  if (count < 0 || count > 256) throw new Error(`invalid push count: ${count}`);
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
  if (descriptor.name !== "TinyDraw Raster V1 interactive core" ||
      descriptor.panel?.w !== 368 || descriptor.panel?.h !== 448 ||
      descriptor.panel?.format !== "rgb565" || descriptor.touch?.points !== 1 ||
      JSON.stringify(descriptor.buttons) !== JSON.stringify(expectedButtons)) {
    throw new Error(`bad descriptor: ${JSON.stringify(descriptor)}`);
  }
  if (memory.buffer.byteLength < 16 * 1024 * 1024) {
    throw new Error(`linear memory below 16 MiB: ${memory.buffer.byteLength}`);
  }
  return { exports: instance.exports, logs, descriptor };
}

function runTrace(exports) {
  const before = hashFramebuffer(exports);
  const pushesBefore = pushes(exports);
  exports.emu_touch(1, 60, 80);
  if (hashFramebuffer(exports) !== before || JSON.stringify(pushes(exports)) !== JSON.stringify(pushesBefore)) {
    throw new Error("emu_touch mutated output before emu_tick");
  }
  const frames = [];
  for (const [time, down, x, y] of [
    [1, 1, 60, 80], [9, 1, 100, 100], [17, 1, 150, 130], [25, 0, 150, 130],
  ]) {
    exports.emu_touch(down, x, y);
    exports.emu_tick(time);
    const framePushes = pushes(exports);
    for (const push of framePushes) {
      if (push.x < 0 || push.y < 0 || push.w <= 0 || push.h <= 0 ||
          push.x + push.w > 368 || push.y + push.h > 448) {
        throw new Error(`out-of-bounds push: ${JSON.stringify(push)}`);
      }
    }
    frames.push(framePushes);
  }
  if (!frames.flat().some(({ w, h }) => w < 368 || h < 448)) {
    throw new Error("drawing trace produced no partial-refresh window");
  }
  const after = hashFramebuffer(exports);
  if (after === before) throw new Error("drawing trace did not change framebuffer");
  return { before, after, frames };
}

const first = await instantiateFresh();
const second = await instantiateFresh();
const firstTrace = runTrace(first.exports);
const secondTrace = runTrace(second.exports);
if (JSON.stringify(firstTrace) !== JSON.stringify(secondTrace)) {
  throw new Error("fresh instances diverged for the same trace");
}
const expectedTrace = {
  after: 0xf5122b6b,
  frames: [
    [{ x: 56, y: 76, w: 10, h: 10 }],
    [{ x: 56, y: 76, w: 38, h: 24 }],
    [{ x: 56, y: 76, w: 80, h: 48 }],
    [{ x: 106, y: 102, w: 50, h: 34 }, { x: 0, y: 0, w: 368, h: 448 }],
  ],
};
if (firstTrace.after !== expectedTrace.after ||
    JSON.stringify(firstTrace.frames) !== JSON.stringify(expectedTrace.frames)) {
  throw new Error(`drawing regression: ${JSON.stringify(firstTrace)}`);
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
