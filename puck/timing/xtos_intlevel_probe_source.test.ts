import { expect, test } from "bun:test";
import { existsSync } from "node:fs";
import { join } from "node:path";

const root = join(import.meta.dir, "../..");
const assemblyPath = join(root, "esp32/main/timing_probe/rom_callback_probes_esp32s3.S");
const firmwarePath = join(root, "esp32/main/timing_probe/timing_probe.cpp");
const elfPath = process.env.TINYDRAW_TIMING_PROBE_ELF ?? join(
  root,
  "out/build/esp32-timing-probe/tinydraw_esp32.elf",
);
const objdumpPath = process.env.ESP32S3_OBJDUMP ?? join(
  process.env.HOME ?? "/nonexistent",
  ".espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump",
);

test("_xtos_set_intlevel peer restores the raised caller PS immediately", async () => {
  const [assembly, firmware] = await Promise.all([
    Bun.file(assemblyPath).text(),
    Bun.file(firmwarePath).text(),
  ]);
  expect(assembly).toContain(".literal .Lxtos_intlevel_restore_ps_\\@, 0x00040c00");
  expect(assembly).toContain("entry   a1, 32");
  expect(assembly).toContain("rsr.ps  a9");
  expect(assembly).toContain("s32i    a9, a1, 16");
  expect(assembly).toContain("l32i    a9, a1, 16\n    wsr.ps  a9\n    rsync");
  expect(assembly).toContain(
    "DEFINE_XTOS_INTLEVEL_WRAPPER tinydraw_rom_xtos_set_intlevel_restore_ps_40c00, _xtos_set_intlevel",
  );
  expect(firmware).toContain("kXtosRestorePs = 0x0004'0c00U");
  expect(firmware).toContain('asm volatile("rsil %0, 3\\nrsr %1, ps"');
  expect(firmware).toContain("if (restored_ps != raised_ps) sample.checksum = 0U");
  expect(firmware).toContain("kXtosIntlevelCaptureMode = true");
  expect(firmware).toContain("kXtosIntlevelMeasurementCount = 2U");
});

function disassemble(symbol: string): string {
  const result = Bun.spawnSync([objdumpPath, "-d", `--disassemble=${symbol}`, elfPath]);
  expect(result.exitCode).toBe(0);
  return result.stdout.toString();
}

interface DecodedInstruction {
  readonly bytes: string;
  readonly mnemonic: string;
}

function instructions(disassembly: string): readonly DecodedInstruction[] {
  return disassembly.split("\n").flatMap((line) => {
    const match = line.match(/^\s*[0-9a-f]+:\s+([0-9a-f]+)\s+([a-z0-9.]+)/);
    return match === null ? [] : [{ bytes: match[1]!, mnemonic: match[2]! }];
  });
}

test.skipIf(!existsSync(elfPath) || !existsSync(objdumpPath))(
  "built peers retain the exact matched CALLINC2 replay shape and ROM stub PC",
  () => {
    const baseline = disassemble("tinydraw_rom_baseline_xtos_set_intlevel_restore_ps_40c00");
    const target = disassemble("tinydraw_rom_xtos_set_intlevel_restore_ps_40c00");
    expect(instructions(target).map(({ mnemonic }) => mnemonic)).toEqual(
      instructions(baseline).map(({ mnemonic }) => mnemonic),
    );
    expect(instructions(baseline)).toEqual([
      { bytes: "004136", mnemonic: "entry" },
      { bytes: "03e690", mnemonic: "rsr.ps" },
      { bytes: "046192", mnemonic: "s32i" },
      { bytes: "f0b4a1", mnemonic: "l32r" },
      { bytes: "f0b181", mnemonic: "l32r" },
      { bytes: "0008e0", mnemonic: "callx8" },
      { bytes: "042192", mnemonic: "l32i" },
      { bytes: "13e690", mnemonic: "wsr.ps" },
      { bytes: "002010", mnemonic: "rsync" },
      { bytes: "0a2d", mnemonic: "mov.n" },
      { bytes: "f01d", mnemonic: "retw.n" },
    ]);
    expect(instructions(target)).toEqual([
      { bytes: "004136", mnemonic: "entry" },
      { bytes: "03e690", mnemonic: "rsr.ps" },
      { bytes: "046192", mnemonic: "s32i" },
      { bytes: "f0aca1", mnemonic: "l32r" },
      { bytes: "f0ad81", mnemonic: "l32r" },
      { bytes: "0008e0", mnemonic: "callx8" },
      { bytes: "042192", mnemonic: "l32i" },
      { bytes: "13e690", mnemonic: "wsr.ps" },
      { bytes: "002010", mnemonic: "rsync" },
      { bytes: "0a2d", mnemonic: "mov.n" },
      { bytes: "f01d", mnemonic: "retw.n" },
    ]);
    expect(target).toContain("(40001c38 <_xtos_set_intlevel>)");
  },
);
