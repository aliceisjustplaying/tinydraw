#!/usr/bin/env bun

import assert from "node:assert/strict";
import { existsSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const [moduleArgument, puckArgument] = process.argv.slice(2);
if (!moduleArgument || !puckArgument) {
  console.error("usage: bun puck/timing_verify.ts <emu.wasm> <PUCK_REPO>");
  process.exit(2);
}

const modulePath = resolve(moduleArgument);
const puck = resolve(puckArgument);
const profilePath = join(puck, "packs", "esp32-s3-touch-amoled-18", "timing.json");
assert(existsSync(modulePath), `module not found: ${modulePath}`);
assert(existsSync(join(puck, "src", "wasm.ts")), `not a Puck checkout: ${puck}`);
assert(existsSync(profilePath), `timing profile not found: ${profilePath}`);

const profile = (await Bun.file(profilePath).json()) as {
  claimBoundary: { cycleAccurate: boolean; countsOnlyInstrumentedEvents: boolean };
  cpu: { hz: number };
  panel: { busHz: number; lanes: number; payloadBytesPerSecond: number };
};
assert.equal(profile.claimBoundary.cycleAccurate, false, "timing profile overclaims cycle accuracy");
assert.equal(
  profile.claimBoundary.countsOnlyInstrumentedEvents,
  true,
  "timing profile must bound the ledger to instrumented events",
);

const { instantiate, readCString } = await import(pathToFileURL(join(puck, "src", "wasm.ts")).href);
const bytes = await Bun.file(modulePath).arrayBuffer();

interface TimingExports {
  memory: WebAssembly.Memory;
  emu_init(): number;
  emu_tick(nowMs: number): void;
  emu_timing_schema(): number;
  emu_timing_snapshot(): number;
  emu_timing_snapshot_size(): number;
}

const fieldDescriptors = [
  ["observation_sequence", 8, "u64", "sequence"],
  ["internal_allocation_live_bytes", 16, "u64", "live"],
  ["psram_allocation_live_bytes", 24, "u64", "live"],
  ["unclassified_allocation_live_bytes", 32, "u64", "live"],
  ["internal_read_bytes", 40, "u64", "observation"],
  ["internal_write_bytes", 48, "u64", "observation"],
  ["psram_read_bytes", 56, "u64", "observation"],
  ["psram_write_bytes", 64, "u64", "observation"],
  ["flash_read_bytes", 72, "u64", "observation"],
  ["flash_write_bytes", 80, "u64", "observation"],
  ["unclassified_read_bytes", 88, "u64", "observation"],
  ["unclassified_write_bytes", 96, "u64", "observation"],
  ["panel_write_bytes", 104, "u64", "observation"],
  ["panel_submit_count", 112, "u64", "observation"],
  ["panel_wire_clocks", 120, "u64", "observation"],
  ["panel_payload_cpu_cycles", 128, "u64", "observation"],
  ["allocation_registry_overflow_count", 136, "u64", "lifetime"],
] as const;
const expectedSchemaFields = [
  ["version", 0, "u32", "schema"],
  ["size", 4, "u32", "schema"],
  ...fieldDescriptors,
];

type FieldName = (typeof fieldDescriptors)[number][0];
type TimingValues = Record<FieldName, bigint>;

function readSnapshot(emu: TimingExports): { raw: Uint8Array; values: TimingValues } {
  const size = emu.emu_timing_snapshot_size();
  const pointer = emu.emu_timing_snapshot();
  assert.equal(size, 144, "unexpected timing snapshot size");
  assert(Number.isInteger(pointer) && pointer >= 0, `invalid timing snapshot pointer ${pointer}`);
  assert(pointer + size <= emu.memory.buffer.byteLength, "timing snapshot exceeds wasm memory");

  const view = new DataView(emu.memory.buffer, pointer, size);
  assert.equal(view.getUint32(0, true), 1, "unexpected timing snapshot version");
  assert.equal(view.getUint32(4, true), size, "snapshot header size does not match export");
  const values = Object.fromEntries(
    fieldDescriptors.map(([name, offset]) => [name, view.getBigUint64(offset, true)]),
  ) as TimingValues;
  return { raw: new Uint8Array(emu.memory.buffer, pointer, size).slice(), values };
}

async function startupSnapshot(): Promise<{
  emu: TimingExports;
  raw: Uint8Array;
  values: TimingValues;
}> {
  const emu = (await instantiate(bytes, () => {})) as TimingExports;
  for (const name of ["emu_timing_schema", "emu_timing_snapshot", "emu_timing_snapshot_size"] as const) {
    assert.equal(typeof emu[name], "function", `missing ${name} export`);
  }
  assert.equal(emu.emu_init(), 1, "emu_init failed");

  const schemaText = readCString(emu.memory, emu.emu_timing_schema());
  const schema = JSON.parse(schemaText) as unknown;
  assert.deepEqual(
    schema,
    {
      schemaVersion: 1,
      byteOrder: "little",
      snapshotBytes: 144,
      claim: "accounted-events-only",
      cpuHz: profile.cpu.hz,
      panelBusHz: profile.panel.busHz,
      panelLanes: profile.panel.lanes,
      panelPayloadBytesPerSecond: profile.panel.payloadBytesPerSecond,
      observationReset: "start-of-emu_tick",
      scopes: {
        schema: "schema",
        sequence: "instance-monotonic",
        live: "instance-live",
        observation: "since-last-reset",
        lifetime: "instance-lifetime",
      },
      fields: expectedSchemaFields,
    },
    "timing schema drifted from its binary layout or authoritative Puck profile",
  );
  return { emu, ...readSnapshot(emu) };
}

const first = await startupSnapshot();
const second = await startupSnapshot();
assert.deepEqual(first.raw, second.raw, "fresh startup timing snapshots are not deterministic");

const expectedFrameBytes = 368n * 448n * 2n;
const cpuHz = BigInt(profile.cpu.hz);
const panelBusHz = BigInt(profile.panel.busHz);
const panelLanes = BigInt(profile.panel.lanes);
const panelPayloadBytesPerSecond = BigInt(profile.panel.payloadBytesPerSecond);
assert.equal(panelBusHz * panelLanes, panelPayloadBytesPerSecond * 8n, "panel profile is inconsistent");
assert.equal(cpuHz % panelPayloadBytesPerSecond, 0n, "panel payload does not divide the CPU clock");
assert.equal(8n % panelLanes, 0n, "panel lane count does not divide a byte");

assert.equal(first.values.observation_sequence, 0n, "startup observation sequence drifted");
assert.equal(first.values.psram_read_bytes, expectedFrameBytes, "startup PSRAM source traffic drifted");
assert.equal(first.values.internal_write_bytes, expectedFrameBytes, "startup staging writes drifted");
assert.equal(first.values.internal_read_bytes, expectedFrameBytes, "startup staging reads drifted");
assert.equal(first.values.panel_write_bytes, expectedFrameBytes, "startup panel payload drifted");
assert.equal(first.values.panel_submit_count, 11n, "startup panel submission count drifted");
assert.equal(
  first.values.panel_wire_clocks,
  (expectedFrameBytes * 8n) / panelLanes,
  "panel wire-clock floor drifted",
);
assert.equal(
  first.values.panel_payload_cpu_cycles,
  (expectedFrameBytes * cpuHz) / panelPayloadBytesPerSecond,
  "panel payload CPU-cycle equivalent drifted",
);
assert(first.values.internal_allocation_live_bytes > 0n, "no internal allocations were classified");
assert(first.values.psram_allocation_live_bytes > 0n, "no PSRAM allocations were classified");
assert.equal(first.values.unclassified_allocation_live_bytes, 0n, "an allocation lacks ESP capability classification");
assert.equal(first.values.unclassified_read_bytes, 0n, "startup panel reads include unclassified memory");
assert.equal(first.values.unclassified_write_bytes, 0n, "startup panel writes include unclassified memory");
assert.equal(first.values.flash_read_bytes, 0n, "flash reads must remain explicitly unmodeled in v1");
assert.equal(first.values.flash_write_bytes, 0n, "flash writes must remain explicitly unmodeled in v1");
assert.equal(first.values.allocation_registry_overflow_count, 0n, "allocation registry overflowed");

first.emu.emu_tick(0);
const afterTick = readSnapshot(first.emu).values;
assert.equal(afterTick.observation_sequence, 1n, "tick did not advance the observation sequence");
for (const name of [
  "internal_allocation_live_bytes",
  "psram_allocation_live_bytes",
  "unclassified_allocation_live_bytes",
  "allocation_registry_overflow_count",
] as const) {
  assert.equal(afterTick[name], first.values[name], `${name} did not retain its declared scope across a tick`);
}
for (const [name, , , scope] of fieldDescriptors) {
  if (scope === "observation") assert.equal(afterTick[name], 0n, `${name} was not reset at the tick boundary`);
}

console.log(
  `PASS timing ledger v1: ${expectedFrameBytes} PSRAM bytes -> SRAM staging -> panel, ` +
    `${first.values.panel_submit_count} submits, ${first.values.panel_wire_clocks} wire clocks, ` +
    `${first.values.panel_payload_cpu_cycles} CPU-cycle equivalent`,
);
