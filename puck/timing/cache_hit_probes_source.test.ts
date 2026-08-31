import { expect, test } from "bun:test";
import { join } from "node:path";

const root = join(import.meta.dir, "../..");
const instructionSourcePath = join(
  root,
  "esp32/main/timing_probe/flash_instruction_bursts_esp32s3.S",
);
const dataSourcePath = join(
  root,
  "esp32/main/timing_probe/dcache_burst_probes_esp32s3.S",
);
const firmwarePath = join(root, "esp32/main/timing_probe/timing_probe.cpp");

test("I-cache hit peers execute the same exact 120-instruction assembly body", async () => {
  const [assembly, firmware] = await Promise.all([
    Bun.file(instructionSourcePath).text(),
    Bun.file(firmwarePath).text(),
  ]);
  const body = assembly.slice(
    assembly.indexOf(".macro DEFINE_FLASH_INSTRUCTION_BURST"),
    assembly.indexOf(".endm") + ".endm".length,
  );
  expect(body).toContain(".rept ((\\lines - 1) * 16)");
  expect(body).toContain(".rept 6");
  expect(body).toContain("movi.n      a2, \\lines");
  expect(body).toContain("ret.n");
  expect(body).toContain(".rept 8");
  expect((8 - 1) * 16 + 6 + 2).toBe(120);
  expect(assembly).toContain(
    "DEFINE_FLASH_INSTRUCTION_BURST tinydraw_iram_instruction_hit_8_lines, " +
      "tinydraw_iram_instruction_hit_8_lines_start, " +
      "tinydraw_iram_instruction_hit_8_lines_end, 8",
  );
  expect(assembly).toContain(
    "DEFINE_FLASH_INSTRUCTION_BURST tinydraw_flash_instruction_burst_8_lines, " +
      "tinydraw_flash_instruction_burst_8_lines_start, " +
      "tinydraw_flash_instruction_burst_8_lines_end, 8",
  );
  expect(firmware).toContain('"icache_hit_iram_120_instructions"');
  expect(firmware).toContain('"icache_hit_flash_120_instructions"');
  expect(firmware).toContain("kMatchedIcacheExecutedInstructions = 120U");
});

test("D-cache hit peers execute the same exact sixteen-load assembly body", async () => {
  const [assembly, firmware] = await Promise.all([
    Bun.file(dataSourcePath).text(),
    Bun.file(firmwarePath).text(),
  ]);
  const body = assembly.slice(
    assembly.indexOf(".macro DEFINE_DCACHE_BURST_PROBE"),
    assembly.indexOf(".endm") + ".endm".length,
  );
  expect(
    [...body.matchAll(/l32i\s+a4, a8, (\d+)/g)].map((match) => Number(match[1])),
  ).toEqual(Array.from({ length: 16 }, (_, index) => index * 64));
  expect(body).not.toContain("s32i");
  expect(body).not.toContain("loop");
  for (const [path, offset] of [
    ["sram", 8],
    ["psram", 12],
    ["flash", 20],
  ] as const) {
    expect(assembly).toContain(
      `DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_${path}_16_lines, ${offset}, 16`,
    );
    expect(firmware).toContain(`"dcache_hit_${path}_16_loads"`);
  }
  expect(firmware).toContain("kMatchedDcacheLoads = 16U");
});

test("single-core samples require the exact hot-hit counter signatures", async () => {
  const firmware = await Bun.file(firmwarePath).text();
  expect(firmware).toContain("kFlashIcacheHitSignature");
  expect(firmware).toContain(".ibus_accesses = 62U");
  expect(firmware).toContain("kExternalDcacheHitSignature");
  expect(firmware).toContain(".dbus_accesses = kMatchedDcacheLoads");
  expect(firmware).toContain(".ibus_misses = 0U");
  expect(firmware).toContain(".dbus_flash_misses = 0U");
  expect(firmware).toContain(".dbus_psram_misses = 0U");

  const icacheSampler = firmware.slice(
    firmware.indexOf("measure_matched_icache_target_once("),
    firmware.indexOf("void print_measurement_start"),
  );
  expect(icacheSampler.indexOf("warm_window")).toBeLessThan(
    icacheSampler.indexOf("clear_cache_counters()"),
  );
  expect(icacheSampler.indexOf("clear_cache_counters()")).toBeLessThan(
    icacheSampler.indexOf("FlashInstructionBurstWindow window{}"),
  );
  expect(icacheSampler).toContain(
    "require_cache_counter_signature(sample, expected_counters, collect_cache_counters)",
  );
  expect(firmware).toContain(
    "require_cache_counter_signature(sample, kInternalCacheHitSignature, collect_cache_counters)",
  );
  expect(firmware).toContain(
    "require_cache_counter_signature(sample, kExternalDcacheHitSignature,",
  );
});

test("dependent load-use peers repeat one exact 4,096-step address chain", async () => {
  const [assembly, firmware] = await Promise.all([
    Bun.file(dataSourcePath).text(),
    Bun.file(firmwarePath).text(),
  ]);
  const body = assembly.slice(
    assembly.indexOf(".macro DEFINE_DEPENDENT_LOAD_PROBE"),
    assembly.indexOf(".endm", assembly.indexOf(".macro DEFINE_DEPENDENT_LOAD_PROBE")) +
      ".endm".length,
  );
  expect(body.match(/\baddx4\b/g)).toHaveLength(1);
  expect(body.match(/\bl32i\s+a2, a10, 0\b/g)).toHaveLength(1);
  expect(body.match(/\bextui\s+a2, a2, 0, 12\b/g)).toHaveLength(1);
  expect(body).toContain("slli    a9, a9, 12");
  expect(body).toContain("loop    a9, .Ldependent_done\\@");
  for (const [path, offset] of [
    ["flash", 20],
    ["sram", 24],
    ["psram", 28],
  ] as const) {
    expect(assembly).toContain(
      `DEFINE_DEPENDENT_LOAD_PROBE tinydraw_dependent_load_${path}, ${offset}`,
    );
  }

  for (const id of [
    "dependent_load_sram_4096_steps",
    "dependent_load_psram_hot_4096_steps",
    "dependent_load_flash_hot_4096_steps",
  ]) {
    expect(firmware).toContain(`"${id}"`);
  }
  expect(firmware).toContain("kDependentExternalDcacheHitSignature");
  expect(firmware).toContain(".dbus_accesses = kDependentLoads");
  expect(firmware).toContain("g_prepare_checksum = kernel(context, seed);");
  expect(firmware).toContain("const std::uint32_t value = context.flash[index];");
  expect(firmware).toContain("context.sram_load_use[index] = value;");
  expect(firmware).toContain("context.psram_load_use[index] = value;");
  expect(firmware).toContain('print_error("initialize", "load-use-cache-sync"');
});
