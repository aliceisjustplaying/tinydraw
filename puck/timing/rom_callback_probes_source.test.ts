import { expect, test } from "bun:test";
import { existsSync } from "node:fs";
import { join } from "node:path";

const root = join(import.meta.dir, "../..");
const assemblyPath = join(root, "esp32/main/timing_probe/rom_callback_probes_esp32s3.S");
const firmwarePath = join(root, "esp32/main/timing_probe/timing_probe.cpp");
const cmakePath = join(root, "esp32/main/CMakeLists.txt");
const elfPath = process.env.TINYDRAW_TIMING_PROBE_ELF ?? join(
  root,
  "out/build/esp32-timing-probe/tinydraw_esp32.elf",
);
const objdumpPath = process.env.ESP32S3_OBJDUMP ?? join(
  process.env.HOME ?? "/nonexistent",
  ".espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump",
);

test("ROM callback peers preserve exact replay arguments and state", async () => {
  const [assembly, firmware, cmake] = await Promise.all([
    Bun.file(assemblyPath).text(),
    Bun.file(firmwarePath).text(),
    Bun.file(cmakePath).text(),
  ]);

  expect(cmake).toContain('"timing_probe/rom_callback_probes_esp32s3.S"');
  expect(assembly).toContain(".begin no-transform");
  expect(assembly).toContain("l32i    a10, a2, 104");
  expect(assembly).toContain("l32i    a10, a2, 108");
  expect(assembly).toContain("movi    a12, \\length_words");
  expect(assembly).toContain("slli    a12, a12, 4");
  expect(assembly).toContain(
    "DEFINE_RESET_REASON_WRAPPER tinydraw_rom_reset_reason_core0, esp_rom_get_reset_reason, 0",
  );
  expect(assembly).toContain(
    "DEFINE_RESET_REASON_WRAPPER tinydraw_rom_reset_reason_core1, esp_rom_get_reset_reason, 1",
  );
  expect(assembly).toContain("DEFINE_MEMSET_WRAPPER tinydraw_rom_memset_zero, memset, 0");
  expect(assembly).toContain("DEFINE_MEMSET_WRAPPER tinydraw_rom_memset_52e0, memset, 0x52e");
  expect(assembly).toContain(
    "DEFINE_CPU_TICKS_WRAPPER tinydraw_rom_set_cpu_ticks, esp_rom_set_cpu_ticks_per_us",
  );

  expect(firmware).toContain("static_assert(offsetof(ProbeContext, rom_memset_buffer) == 104U)");
  expect(firmware).toContain("static_assert(offsetof(ProbeContext, rom_cpu_ticks_per_us) == 108U)");
  expect(firmware).toContain("kRomMemsetBytes = 0x52e0U");
  expect(firmware).toContain("heap_caps_aligned_alloc(");
  expect(firmware).toContain("16U, kRomMemsetBytes, MALLOC_CAP_INTERNAL");
  expect(firmware).toContain("esp_rom_get_cpu_ticks_per_us() == context.rom_cpu_ticks_per_us");
  expect(firmware).toContain("kRomCallbackCaptureMode = false");
  expect(firmware).toContain("kRomCallbackMeasurementCount = 10U");
  expect(firmware).toContain("measure_rom_callback_once");
  expect(firmware).toContain('asm volatile("rsil %0, 15"');
  expect(firmware).toContain('asm volatile("wsr %0, ps\\nrsync"');

  for (const id of [
    "rom_baseline_reset_reason_core0",
    "rom_reset_reason_core0",
    "rom_baseline_reset_reason_core1",
    "rom_reset_reason_core1",
    "rom_baseline_memset_zero_length",
    "rom_memset_zero_length",
    "rom_baseline_memset_0x52e0",
    "rom_memset_0x52e0",
    "rom_baseline_set_cpu_ticks_per_us",
    "rom_set_cpu_ticks_per_us_same_value",
  ]) {
    expect(firmware).toContain(`"${id}"`);
  }
});

function disassemble(symbol: string): string {
  const result = Bun.spawnSync([objdumpPath, "-d", `--disassemble=${symbol}`, elfPath]);
  expect(result.exitCode).toBe(0);
  return result.stdout.toString();
}

function mnemonics(disassembly: string): readonly string[] {
  return disassembly.split("\n").flatMap((line) => {
    const match = line.match(/^\s*[0-9a-f]+:\s+[0-9a-f]+\s+([a-z0-9.]+)/);
    return match === null ? [] : [match[1]!];
  });
}

test.skipIf(!existsSync(elfPath) || !existsSync(objdumpPath))(
  "built ROM callback peers call the exact replay entry PCs",
  () => {
    for (const [baseline, target, address] of [
      ["tinydraw_rom_baseline_reset_reason_core0", "tinydraw_rom_reset_reason_core0", "4000057c"],
      ["tinydraw_rom_baseline_reset_reason_core1", "tinydraw_rom_reset_reason_core1", "4000057c"],
      ["tinydraw_rom_baseline_memset_zero", "tinydraw_rom_memset_zero", "400011e8"],
      ["tinydraw_rom_baseline_memset_52e0", "tinydraw_rom_memset_52e0", "400011e8"],
      ["tinydraw_rom_baseline_set_cpu_ticks", "tinydraw_rom_set_cpu_ticks", "40001a4c"],
    ] as const) {
      const baselineDisassembly = disassemble(baseline);
      const targetDisassembly = disassemble(target);
      expect(mnemonics(targetDisassembly)).toEqual(mnemonics(baselineDisassembly));
      expect(targetDisassembly).toContain(`(${address} <`);
      expect(mnemonics(targetDisassembly).filter((mnemonic) => mnemonic === "callx8")).toHaveLength(1);
    }

    expect(mnemonics(disassemble("tinydraw_rom_reset_reason_core0"))).toEqual([
      "entry", "movi.n", "l32r", "callx8", "mov.n", "retw.n",
    ]);
    expect(mnemonics(disassemble("tinydraw_rom_memset_52e0"))).toEqual([
      "entry", "l32i", "movi.n", "movi", "slli", "l32r", "callx8", "mov.n", "retw.n",
    ]);
  },
);
