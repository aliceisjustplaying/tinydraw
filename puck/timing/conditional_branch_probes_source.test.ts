import { expect, test } from "bun:test";
import { existsSync } from "node:fs";
import { join } from "node:path";

const root = join(import.meta.dir, "../..");
const assemblyPath = join(
  root,
  "esp32/main/timing_probe/dcache_burst_probes_esp32s3.S",
);
const firmwarePath = join(root, "esp32/main/timing_probe/timing_probe.cpp");
const elfPath = process.env.TINYDRAW_TIMING_PROBE_ELF ?? join(
  root,
  "out/build/esp32-timing-probe/tinydraw_esp32.elf",
);
const objdumpPath = process.env.ESP32S3_OBJDUMP ?? join(
  process.env.HOME ?? "/nonexistent",
  ".espressif/tools/xtensa-esp-elf/esp-15.2.0_20251204/xtensa-esp-elf/bin/xtensa-esp32s3-elf-objdump",
);

test("conditional branch peers repeat exact four-instruction paths", async () => {
  const [assembly, firmware] = await Promise.all([
    Bun.file(assemblyPath).text(),
    Bun.file(firmwarePath).text(),
  ]);
  const start = assembly.indexOf(".macro DEFINE_CONDITIONAL_BRANCH_PROBE");
  const body = assembly.slice(start, assembly.indexOf(".endm", start) + ".endm".length);
  expect(body).toContain(".begin no-transform");
  expect(body).toContain(".end no-transform");
  expect(body.match(/\bbeqz\s+a2, \.Lbranch_taken\\@/g)).toHaveLength(1);
  expect(body.match(/\bor\s+a4, a2, a2/g)).toHaveLength(1);
  expect(body.match(/\baddi\.n\s+a3, a3, 1/g)).toHaveLength(2);
  expect(body.match(/\bj\s+\.Lbranch_join\\@/g)).toHaveLength(2);
  expect(body.match(/\baddi\s+a3, a3, 0/g)).toHaveLength(1);
  expect(body).toContain("loop    a9, .Lbranch_done\\@");
  expect(body).toContain("slli    a9, a9, 12");

  for (const [name, condition, baseline] of [
    ["baseline", 1, 1],
    ["not_taken", 1, 0],
    ["taken", 0, 0],
  ] as const) {
    expect(assembly).toContain(
      `DEFINE_CONDITIONAL_BRANCH_PROBE tinydraw_branch_${name}, ${condition}, ${baseline}`,
    );
    expect(firmware).toContain(`"conditional_branch_${name}_4096_iterations"`);
  }
  expect(firmware).toContain("kConditionalBranchIterations = 4096U");
  expect(firmware).toContain("kConditionalBranchChecksum = kConditionalBranchIterations");
  expect(firmware).toContain("measure_conditional_branch_once");
  expect(firmware).toContain(
    "require_cache_counter_signature(sample, kInternalCacheHitSignature, collect_cache_counters)",
  );
});

interface DecodedInstruction {
  readonly bytes: string;
  readonly mnemonic: string;
}

function disassemble(symbol: string): readonly DecodedInstruction[] {
  const result = Bun.spawnSync([
    objdumpPath,
    "-d",
    `--disassemble=${symbol}`,
    elfPath,
  ]);
  expect(result.exitCode).toBe(0);
  return result.stdout.toString().split("\n").flatMap((line) => {
    const match = line.match(/^\s*[0-9a-f]+:\s+([0-9a-f]+)\s+([a-z0-9.]+)/);
    return match === null ? [] : [{ bytes: match[1]!, mnemonic: match[2]! }];
  });
}

test.skipIf(!existsSync(elfPath) || !existsSync(objdumpPath))(
  "built branch probes retain exact ESP32-S3 encodings and disassembly",
  () => {
    const suffix = [
      { bytes: "030c", mnemonic: "movi.n" },
      { bytes: "01a092", mnemonic: "movi" },
      { bytes: "119940", mnemonic: "slli" },
      { bytes: "0f8976", mnemonic: "loop" },
    ];
    const tail = [
      { bytes: "331b", mnemonic: "addi.n" },
      { bytes: "000106", mnemonic: "j" },
      { bytes: "331b", mnemonic: "addi.n" },
      { bytes: "ffffc6", mnemonic: "j" },
      { bytes: "00c332", mnemonic: "addi" },
      { bytes: "032d", mnemonic: "mov.n" },
      { bytes: "f01d", mnemonic: "retw.n" },
    ];
    expect(disassemble("tinydraw_branch_baseline")).toEqual([
      { bytes: "002136", mnemonic: "entry" },
      { bytes: "01a022", mnemonic: "movi" },
      ...suffix,
      { bytes: "204220", mnemonic: "or" },
      ...tail,
    ]);
    expect(disassemble("tinydraw_branch_not_taken")).toEqual([
      { bytes: "002136", mnemonic: "entry" },
      { bytes: "01a022", mnemonic: "movi" },
      ...suffix,
      { bytes: "004216", mnemonic: "beqz" },
      ...tail,
    ]);
    expect(disassemble("tinydraw_branch_taken")).toEqual([
      { bytes: "002136", mnemonic: "entry" },
      { bytes: "00a022", mnemonic: "movi" },
      ...suffix,
      { bytes: "004216", mnemonic: "beqz" },
      ...tail,
    ]);
  },
);
