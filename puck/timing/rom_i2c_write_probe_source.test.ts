import { expect, test } from "bun:test";
import { existsSync } from "node:fs";
import { join } from "node:path";

const root = join(import.meta.dir, "../..");
const assemblyPath = join(root, "esp32/main/timing_probe/rom_i2c_write_probe_esp32s3.S");
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

test("ROM REGI2C write peer is one non-clock-changing same-value cell", async () => {
  const [assembly, firmware, cmake] = await Promise.all([
    Bun.file(assemblyPath).text(),
    Bun.file(firmwarePath).text(),
    Bun.file(cmakePath).text(),
  ]);

  expect(cmake).toContain('"timing_probe/rom_i2c_write_probe_esp32s3.S"');
  expect(assembly).toContain(".begin no-transform");
  expect(assembly).toContain("movi    a10, 0x61");
  expect(assembly).toContain("movi.n  a11, 1");
  expect(assembly).toContain("movi.n  a12, 5");
  expect(assembly).toContain("l32i    a13, a2, 120");
  expect(assembly).toContain(
    "DEFINE_ROM_I2C_WRITE_WRAPPER tinydraw_rom_i2c_write_same_bod_threshold, rom_i2c_writeReg",
  );
  expect(firmware).toContain("static_assert(I2C_BOD == 0x61)");
  expect(firmware).toContain("static_assert(I2C_BOD_HOSTID == 1)");
  expect(firmware).toContain("static_assert(I2C_BOD_THRESHOLD == 0x5)");
  expect(firmware).toContain("esp_rom_regi2c_read(I2C_BOD, I2C_BOD_HOSTID, I2C_BOD_THRESHOLD)");
  expect(firmware).toContain('asm volatile("rsil %0, 3"');
  expect(firmware).toContain('"rom_i2c_baseline_write_same_bod_threshold"');
  expect(firmware).toContain('"rom_i2c_write_same_bod_threshold"');
  expect(firmware).toContain("kRomI2cWriteCaptureMode = true");
  expect(firmware).toContain("kRomI2cWriteMeasurementCount = 2U");
});

function disassemble(symbol: string): string {
  const result = Bun.spawnSync([objdumpPath, "-d", `--disassemble=${symbol}`, elfPath]);
  expect(result.exitCode).toBe(0);
  return result.stdout.toString();
}

function instructions(disassembly: string): readonly string[] {
  return disassembly.split("\n").flatMap((line) => {
    const match = line.match(/^\s*[0-9a-f]+:\s+[0-9a-f]+\s+([a-z0-9.]+)/);
    return match === null ? [] : [match[1]!];
  });
}

test.skipIf(!existsSync(elfPath) || !existsSync(objdumpPath))(
  "built REGI2C peers retain exact arguments and ROM entry PC",
  () => {
    const baseline = disassemble("tinydraw_rom_i2c_baseline_write_same_bod_threshold");
    const target = disassemble("tinydraw_rom_i2c_write_same_bod_threshold");
    expect(instructions(target)).toEqual(instructions(baseline));
    expect(instructions(target)).toEqual([
      "entry", "movi", "movi.n", "movi.n", "l32i", "l32r", "callx8", "movi.n", "retw.n",
    ]);
    expect(target).toContain("(40005d60 <esp_rom_regi2c_write>)");
  },
);
