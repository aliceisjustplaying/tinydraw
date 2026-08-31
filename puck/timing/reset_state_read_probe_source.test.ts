import { expect, test } from "bun:test";
import { existsSync } from "node:fs";
import { join } from "node:path";

const root = join(import.meta.dir, "../..");
const assemblyPath = join(root, "esp32/main/timing_probe/mmio_probes_esp32s3.S");
const firmwarePath = join(root, "esp32/main/timing_probe/timing_probe.cpp");
const elfPath = process.env.TINYDRAW_TIMING_PROBE_ELF ?? join(
  root,
  "out/build/esp32-timing-probe/tinydraw_esp32.elf",
);
const objdumpPath = process.env.ESP32S3_OBJDUMP ?? join(
  process.env.HOME ?? "/nonexistent",
  ".espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump",
);

test("reset-state peers repeat the exact ROM read and core extraction shapes", async () => {
  const [assembly, firmware] = await Promise.all([
    Bun.file(assemblyPath).text(),
    Bun.file(firmwarePath).text(),
  ]);
  const macroStart = assembly.indexOf(".macro DEFINE_RESET_REASON_READ_PROBE");
  const macroBody = assembly.slice(
    macroStart,
    assembly.indexOf(".endm", macroStart) + ".endm".length,
  );
  expect(macroBody).toContain(".begin no-transform");
  expect(macroBody.match(/\bmemw\b/g)).toHaveLength(1);
  expect(macroBody.match(/\bl32i\s+a4, a8, 0/g)).toHaveLength(1);
  expect(macroBody.match(/\bextui\s+a4, a4, \\field_shift, 6/g)).toHaveLength(1);
  expect(macroBody.match(/\bxor\s+a2, a2, a4/g)).toHaveLength(1);
  expect(macroBody).toContain("slli    a9, a9, \\count_shift");
  expect(macroBody).toContain("loop    a9, .Lreset_reason_read_done\\@");

  for (const core of [0, 1] as const) {
    for (const operations of [4096, 2048] as const) {
      const countShift = operations === 4096 ? 12 : 11;
      const fieldShift = core === 0 ? 0 : 6;
      expect(assembly).toContain(
        `DEFINE_RESET_REASON_READ_PROBE tinydraw_reset_reason_core${core}_read_sram_${operations}, 32, ${fieldShift}, ${countShift}`,
      );
      expect(assembly).toContain(
        `DEFINE_RESET_REASON_READ_PROBE tinydraw_reset_reason_core${core}_read_rtc_state_${operations}, 124, ${fieldShift}, ${countShift}`,
      );
      expect(firmware).toContain(`"reset_reason_core${core}_read_sram_${operations}"`);
      expect(firmware).toContain(`"reset_reason_core${core}_read_rtc_state_${operations}"`);
    }
  }
  expect(firmware).toContain("kRtcResetStateRegister = 0x6000'8038U");
  expect(firmware).toContain("offsetof(ProbeContext, mmio_rtc_reset_state) == 124U");
  expect(firmware).toContain("kResetStateReadCaptureMode = true");
  expect(firmware).toContain("kResetStateReadMeasurementCount = 8U");
});

function disassemble(symbol: string): readonly string[] {
  const result = Bun.spawnSync([objdumpPath, "-d", `--disassemble=${symbol}`, elfPath]);
  expect(result.exitCode).toBe(0);
  return result.stdout.toString().split("\n").flatMap((line) => {
    const match = line.match(/^\s*[0-9a-f]+:\s+[0-9a-f]+\s+([a-z0-9.]+)/);
    return match === null ? [] : [match[1]!];
  });
}

test.skipIf(!existsSync(elfPath) || !existsSync(objdumpPath))(
  "built reset-state peers retain exact matched disassembly",
  () => {
    const expected = [
      "entry", "l32i", "movi.n", "movi", "slli", "loop", "memw", "l32i", "extui", "xor",
      "retw.n",
    ];
    for (const core of [0, 1] as const) {
      for (const operations of [4096, 2048] as const) {
        const baseline = disassemble(`tinydraw_reset_reason_core${core}_read_sram_${operations}`);
        const target = disassemble(`tinydraw_reset_reason_core${core}_read_rtc_state_${operations}`);
        expect(target).toEqual(baseline);
        expect(target).toEqual(expected);
      }
    }
  },
);
