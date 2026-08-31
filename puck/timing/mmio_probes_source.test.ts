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

test("MMIO peers repeat exact aligned read and write cells", async () => {
  const [assembly, firmware] = await Promise.all([
    Bun.file(assemblyPath).text(),
    Bun.file(firmwarePath).text(),
  ]);
  const readStart = assembly.indexOf(".macro DEFINE_MMIO_READ_PROBE");
  const readBody = assembly.slice(
    readStart,
    assembly.indexOf(".endm", readStart) + ".endm".length,
  );
  const writeStart = assembly.indexOf(".macro DEFINE_MMIO_WRITE_PROBE");
  const writeBody = assembly.slice(
    writeStart,
    assembly.indexOf(".endm", writeStart) + ".endm".length,
  );
  const sameValueWriteStart = assembly.indexOf(".macro DEFINE_MMIO_SAME_VALUE_WRITE_PROBE");
  const sameValueWriteBody = assembly.slice(
    sameValueWriteStart,
    assembly.indexOf(".endm", sameValueWriteStart) + ".endm".length,
  );
  const halfWriteStart = assembly.indexOf(".macro DEFINE_MMIO_SAME_VALUE_WRITE_2048_PROBE");
  const halfWriteBody = assembly.slice(
    halfWriteStart,
    assembly.indexOf(".endm", halfWriteStart) + ".endm".length,
  );

  expect(readBody).toContain(".begin no-transform");
  expect(readBody.match(/\bl32i\s+a4, a8, 0/g)).toHaveLength(1);
  expect(readBody.match(/\bxor\s+a2, a2, a4/g)).toHaveLength(1);
  expect(readBody).toContain("loop    a9, .Lmmio_read_done\\@");
  expect(writeBody).toContain(".begin no-transform");
  expect(writeBody.match(/\bs32i\s+a4, a8, 0/g)).toHaveLength(1);
  expect(writeBody.match(/\bmemw\b/g)).toHaveLength(1);
  expect(writeBody).toContain("movi.n  a4, 3");
  expect(writeBody).toContain("loop    a9, .Lmmio_write_done\\@");
  expect(sameValueWriteBody).toContain(".begin no-transform");
  expect(sameValueWriteBody.match(/\bl32i\s+a4, a2, \\value_offset/g)).toHaveLength(1);
  expect(sameValueWriteBody.match(/\bs32i\s+a4, a8, 0/g)).toHaveLength(1);
  expect(sameValueWriteBody.match(/\bmemw\b/g)).toHaveLength(1);
  expect(sameValueWriteBody.match(/\bnop\.n\b/g)).toHaveLength(1);
  expect(sameValueWriteBody).toContain("loop    a9, .Lmmio_same_value_write_done\\@");
  expect(halfWriteBody).toContain("slli    a9, a9, 11");
  expect(halfWriteBody.match(/\bs32i\s+a4, a8, 0/g)).toHaveLength(1);
  expect(halfWriteBody.match(/\bmemw\b/g)).toHaveLength(1);
  expect(halfWriteBody).toContain("loop    a9, .Lmmio_same_value_write_2048_done\\@");

  for (const [name, offset] of [
    ["read_sram", 32],
    ["read_system_cpu_per_conf", 36],
    ["read_rtc_store1", 40],
    ["read_extmem_cache_state", 44],
    ["read_system_sysclk_conf", 52],
    ["read_extmem_dcache_ctrl1", 56],
    ["read_extmem_dcache_autoload_ctrl", 60],
    ["read_extmem_icache_ctrl1", 64],
    ["read_extmem_icache_autoload_ctrl", 68],
    ["read_rtc_date", 96],
    ["write_sram", 32],
    ["write_extmem_cache_counter_clear", 48],
  ] as const) {
    const operation = name.startsWith("read_") ? "READ" : "WRITE";
    expect(assembly).toContain(
      `DEFINE_MMIO_${operation}_PROBE tinydraw_mmio_${name}, ${offset}`,
    );
    expect(firmware).toContain(`"mmio_${name}_4096_aligned"`);
  }

  for (const [name, pointerOffset, valueOffset] of [
    ["sram", 32, 72],
    ["system_sysclk_conf", 52, 76],
    ["extmem_dcache_ctrl1", 56, 80],
    ["extmem_icache_ctrl1", 64, 88],
  ] as const) {
    expect(assembly).toContain(
      `DEFINE_MMIO_SAME_VALUE_WRITE_PROBE tinydraw_mmio_write_same_value_${name}, ${pointerOffset}, ${valueOffset}`,
    );
    expect(firmware).toContain(`"mmio_write_same_value_${name}_4096_aligned"`);
    expect(firmware).toContain(`finalize_mmio_same_value_${name}`);
  }

  for (const [name, pointerOffset, valueOffset] of [
    ["sram", 32, 72],
    ["system_sysclk_conf", 52, 76],
    ["extmem_dcache_ctrl1", 56, 80],
    ["extmem_icache_ctrl1", 64, 88],
  ] as const) {
    expect(assembly).toContain(
      `DEFINE_MMIO_SAME_VALUE_WRITE_2048_PROBE tinydraw_mmio_write_same_value_${name}_2048, ${pointerOffset}, ${valueOffset}`,
    );
    expect(firmware).toContain(`"mmio_write_same_value_${name}_2048_aligned"`);
  }

  expect(firmware).toContain("static_assert(SYSTEM_CPU_PER_CONF_REG == 0x600c'0010U)");
  expect(firmware).toContain("static_assert(SYSTEM_SYSCLK_CONF_REG == 0x600c'0060U)");
  expect(firmware).toContain("static_assert(RTC_CNTL_STORE1_REG == 0x6000'8054U)");
  expect(firmware).toContain("static_assert(RTC_CNTL_DATE_REG == 0x6000'81fcU)");
  expect(firmware).toContain("static_assert(EXTMEM_DCACHE_CTRL1_REG == 0x600c'4004U)");
  expect(firmware).toContain("static_assert(EXTMEM_DCACHE_AUTOLOAD_CTRL_REG == 0x600c'404cU)");
  expect(firmware).toContain("static_assert(EXTMEM_ICACHE_CTRL1_REG == 0x600c'4064U)");
  expect(firmware).toContain("static_assert(EXTMEM_ICACHE_AUTOLOAD_CTRL_REG == 0x600c'40a0U)");
  expect(firmware).toContain("static_assert(EXTMEM_CACHE_STATE_REG == 0x600c'4130U)");
  expect(firmware).toContain("static_assert(EXTMEM_CACHE_ACS_CNT_CLR_REG == 0x600c'40c4U)");
  expect(firmware).toContain("kMmioOperations = 4096U");
  expect(firmware).toContain("kMmioHalfOperations = 2048U");
  expect(firmware).toContain("kMmioSlopeCaptureMode = false");
  expect(firmware).toContain("kMmioSlopeMeasurementCount = 10U");
  expect(firmware).toContain("measure_mmio_once");
  expect(firmware).toContain("kRtcMmioReadSignature");
  expect(firmware).toContain(".ibus_accesses = 176U");
  expect(firmware).toContain("measure_rtc_mmio_once");
  expect(firmware).toContain("tinydraw_mmio_read_rtc_date, prepare_none, nullptr, 0U,\n     measure_rtc_date_boot_4096_once");
  expect(firmware).toContain("autoload-clear-active");
  expect(firmware).toContain("TINYDRAW_MMIO_BOOT_VALUES system_sysclk_conf=0x%08");
  expect(firmware).toContain('asm volatile("rsil %0, 15"');
  expect(firmware).toContain('asm volatile("wsr %0, ps\\nrsync"');
  expect(
    firmware.indexOf('"mmio_write_extmem_cache_counter_clear_4096_aligned"'),
  ).toBeGreaterThan(firmware.indexOf('"flash_instruction_hot"'));
  expect(
    firmware.indexOf('"mmio_write_same_value_system_sysclk_conf_2048_aligned"'),
  ).toBeGreaterThan(firmware.indexOf('"flash_instruction_hot"'));
  expect(
    firmware.indexOf('"mmio_read_rtc_date_4096_aligned"'),
  ).toBeGreaterThan(firmware.indexOf('"flash_instruction_hot"'));
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
  "built MMIO probes retain exact ESP32-S3 encodings and disassembly",
  () => {
    const readTail = [
      { bytes: "020c", mnemonic: "movi.n" },
      { bytes: "01a092", mnemonic: "movi" },
      { bytes: "119940", mnemonic: "slli" },
      { bytes: "058976", mnemonic: "loop" },
      { bytes: "002842", mnemonic: "l32i" },
      { bytes: "302240", mnemonic: "xor" },
      { bytes: "f01d", mnemonic: "retw.n" },
    ];
    for (const [symbol, pointerLoad] of [
      ["tinydraw_mmio_read_sram", "082282"],
      ["tinydraw_mmio_read_system_cpu_per_conf", "092282"],
      ["tinydraw_mmio_read_rtc_store1", "0a2282"],
      ["tinydraw_mmio_read_extmem_cache_state", "0b2282"],
      ["tinydraw_mmio_read_system_sysclk_conf", "0d2282"],
      ["tinydraw_mmio_read_extmem_dcache_ctrl1", "0e2282"],
      ["tinydraw_mmio_read_extmem_dcache_autoload_ctrl", "0f2282"],
      ["tinydraw_mmio_read_extmem_icache_ctrl1", "102282"],
      ["tinydraw_mmio_read_extmem_icache_autoload_ctrl", "112282"],
      ["tinydraw_mmio_read_rtc_date", "182282"],
    ] as const) {
      expect(disassemble(symbol)).toEqual([
        { bytes: "002136", mnemonic: "entry" },
        { bytes: pointerLoad, mnemonic: "l32i" },
        ...readTail,
      ]);
    }

    const writeTail = [
      { bytes: "340c", mnemonic: "movi.n" },
      { bytes: "01a092", mnemonic: "movi" },
      { bytes: "119940", mnemonic: "slli" },
      { bytes: "028976", mnemonic: "loop" },
      { bytes: "006842", mnemonic: "s32i" },
      { bytes: "0020c0", mnemonic: "memw" },
      { bytes: "020c", mnemonic: "movi.n" },
      { bytes: "f01d", mnemonic: "retw.n" },
    ];
    expect(disassemble("tinydraw_mmio_write_sram")).toEqual([
      { bytes: "002136", mnemonic: "entry" },
      { bytes: "082282", mnemonic: "l32i" },
      ...writeTail,
    ]);
    expect(disassemble("tinydraw_mmio_write_extmem_cache_counter_clear")).toEqual([
      { bytes: "002136", mnemonic: "entry" },
      { bytes: "0c2282", mnemonic: "l32i" },
      ...writeTail,
    ]);

    const sameValueWriteTail = [
      { bytes: "01a092", mnemonic: "movi" },
      { bytes: "119940", mnemonic: "slli" },
      { bytes: "f03d", mnemonic: "nop.n" },
      { bytes: "028976", mnemonic: "loop" },
      { bytes: "006842", mnemonic: "s32i" },
      { bytes: "0020c0", mnemonic: "memw" },
      { bytes: "020c", mnemonic: "movi.n" },
      { bytes: "f01d", mnemonic: "retw.n" },
    ];
    for (const [symbol, pointerLoad, valueLoad] of [
      ["tinydraw_mmio_write_same_value_sram", "082282", "122242"],
      ["tinydraw_mmio_write_same_value_system_sysclk_conf", "0d2282", "132242"],
      ["tinydraw_mmio_write_same_value_extmem_dcache_ctrl1", "0e2282", "142242"],
      ["tinydraw_mmio_write_same_value_extmem_icache_ctrl1", "102282", "162242"],
    ] as const) {
      expect(disassemble(symbol)).toEqual([
        { bytes: "002136", mnemonic: "entry" },
        { bytes: pointerLoad, mnemonic: "l32i" },
        { bytes: valueLoad, mnemonic: "l32i" },
        ...sameValueWriteTail,
      ]);
    }

    const halfWriteTail = sameValueWriteTail.map((instruction) =>
      instruction.mnemonic === "slli"
        ? { bytes: "119950", mnemonic: "slli" }
        : instruction
    );
    for (const [symbol, pointerLoad, valueLoad] of [
      ["tinydraw_mmio_write_same_value_sram_2048", "082282", "122242"],
      ["tinydraw_mmio_write_same_value_system_sysclk_conf_2048", "0d2282", "132242"],
      ["tinydraw_mmio_write_same_value_extmem_dcache_ctrl1_2048", "0e2282", "142242"],
      ["tinydraw_mmio_write_same_value_extmem_icache_ctrl1_2048", "102282", "162242"],
    ] as const) {
      expect(disassemble(symbol)).toEqual([
        { bytes: "002136", mnemonic: "entry" },
        { bytes: pointerLoad, mnemonic: "l32i" },
        { bytes: valueLoad, mnemonic: "l32i" },
        ...halfWriteTail,
      ]);
    }
  },
);
