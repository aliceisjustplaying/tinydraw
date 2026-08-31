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

test("RTC boot-register peers define matched 2048- and 4096-read cohorts", async () => {
  const [assembly, firmware] = await Promise.all([
    Bun.file(assemblyPath).text(),
    Bun.file(firmwarePath).text(),
  ]);
  const halfStart = assembly.indexOf(".macro DEFINE_MMIO_READ_2048_PROBE");
  const halfBody = assembly.slice(
    halfStart,
    assembly.indexOf(".endm", halfStart) + ".endm".length,
  );
  expect(halfBody).toContain(".begin no-transform");
  expect(halfBody.match(/\bl32i\s+a4, a8, 0/g)).toHaveLength(1);
  expect(halfBody.match(/\bxor\s+a2, a2, a4/g)).toHaveLength(1);
  expect(halfBody).toContain("slli    a9, a9, 11");
  expect(halfBody).toContain("loop    a9, .Lmmio_read_2048_done\\@");

  for (const [name, offset] of [
    ["sram", 32],
    ["rtc_date", 96],
    ["rtc_xtal_freq", 128],
  ] as const) {
    expect(assembly).toContain(
      `DEFINE_MMIO_READ_2048_PROBE tinydraw_mmio_read_${name}_2048, ${offset}`,
    );
    expect(firmware).toContain(`"mmio_read_${name}_2048_aligned"`);
  }
  expect(assembly).toContain(
    "DEFINE_MMIO_READ_PROBE tinydraw_mmio_read_rtc_xtal_freq, 128",
  );
  expect(firmware).toContain('static_assert(RTC_CNTL_STORE4_REG == 0x6000\'80c0U)');
  expect(firmware).toContain("offsetof(ProbeContext, mmio_rtc_xtal_freq) == 128U");
  expect(firmware).toContain("kRtcBootReadCaptureMode = true");
  expect(firmware).toContain("kRtcBootReadMeasurementCount = 6U");
  expect(firmware).toContain("passed && (kResetStateReadCaptureMode || kRtcBootReadCaptureMode)");
  expect(firmware).toContain("kRtcBoot4096ReadSignature");
  expect(firmware).toContain(".ibus_accesses = 176U");
  expect(firmware).toContain("kRtcBoot2048ReadSignature");
  expect(firmware).toContain(".ibus_accesses = 88U");
  expect(firmware).toContain("primed_kernel != kernel");
  expect(firmware).toContain("esp_rom_delay_us(5'000U)");
  expect(firmware).toContain("measure_rtc_boot_4096_once");
  expect(firmware).toContain("measure_rtc_boot_2048_once");
});

interface DecodedInstruction {
  readonly bytes: string;
  readonly mnemonic: string;
}

function disassemble(symbol: string): readonly DecodedInstruction[] {
  const result = Bun.spawnSync([objdumpPath, "-d", `--disassemble=${symbol}`, elfPath]);
  expect(result.exitCode).toBe(0);
  return result.stdout.toString().split("\n").flatMap((line) => {
    const match = line.match(/^\s*[0-9a-f]+:\s+([0-9a-f]+)\s+([a-z0-9.]+)/);
    return match === null ? [] : [{ bytes: match[1]!, mnemonic: match[2]! }];
  });
}

test.skipIf(!existsSync(elfPath) || !existsSync(objdumpPath))(
  "built RTC boot-register peers retain exact matched disassembly",
  () => {
    for (const [operations, suffix] of [[4096, ""], [2048, "_2048"]] as const) {
      const tail = [
        { bytes: "020c", mnemonic: "movi.n" },
        { bytes: "01a092", mnemonic: "movi" },
        { bytes: operations === 4096 ? "119940" : "119950", mnemonic: "slli" },
        { bytes: "058976", mnemonic: "loop" },
        { bytes: "002842", mnemonic: "l32i" },
        { bytes: "302240", mnemonic: "xor" },
        { bytes: "f01d", mnemonic: "retw.n" },
      ];
      for (const [name, pointerLoad] of [
        ["sram", "082282"],
        ["rtc_date", "182282"],
        ["rtc_xtal_freq", "202282"],
      ] as const) {
        expect(disassemble(`tinydraw_mmio_read_${name}${suffix}`)).toEqual([
          { bytes: "002136", mnemonic: "entry" },
          { bytes: pointerLoad, mnemonic: "l32i" },
          ...tail,
        ]);
      }
    }
  },
);
