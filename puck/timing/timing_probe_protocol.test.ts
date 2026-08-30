import { describe, expect, test } from "bun:test";
import { join } from "node:path";

import {
  assembleTimingProbeReceipts,
  MINIMUM_TIMING_PROBE_SAMPLES,
  TIMING_PROBE_RECORD_PREFIX,
} from "./timing_probe_protocol";

const fixtureMetadata = {
  protocolVersion: 1,
  record: "metadata",
  schemaVersion: 1,
  receiptKind: "esp32s3-hardware-calibration",
  captureMode: "schema-fixture",
  git: {
    repository: "schema-fixture",
    commit: "0123456789abcdef0123456789abcdef01234567",
    dirty: true,
  },
  toolchain: {
    target: "esp32s3",
    espIdfVersion: "schema-fixture",
    compiler: "schema-fixture",
    compilerVersion: "schema-fixture",
  },
  sdkconfig: {
    path: "schema-fixture/sdkconfig",
    sha256: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
    cpuHz: 240000000,
    psramMode: "octal",
    psramBusHz: 80000000,
    flashMode: "qio",
    flashBusHz: 80000000,
  },
  boot: {
    bootId: "schema-fixture-probe",
    resetReason: "schema-fixture",
    chipModel: "ESP32-S3",
    chipRevision: 0,
    cpuCores: 2,
    psramBytes: 1,
    flashBytes: 1,
  },
  counter: { source: "xtensa-ccount", bits: 32, hz: 240000000, core: 0 },
  workingSets: {
    sramStreamBytes: 1,
    psramHotBytes: 1,
    psramColdBytes: 1,
    flashMapBytes: 1,
    contentionBytes: 1,
  },
};

function record(value: unknown): string {
  return `${TIMING_PROBE_RECORD_PREFIX}${JSON.stringify(value)}`;
}

function fixtureLog(
  samples = MINIMUM_TIMING_PROBE_SAMPLES,
  badDelta = false,
  badEndCore = false,
  descriptorKernel?: string,
  counterCore: 0 | 1 = 0,
): string {
  const id = "sram_aligned_stream_single_core";
  const lines = [
    "ROM boot text ignored by the protocol parser",
    record({ ...fixtureMetadata, counter: { ...fixtureMetadata.counter, core: counterCore } }),
    record({
      protocolVersion: 1,
      record: "measurement-start",
      measurementId: id,
      measurement: {
        kind: "ccount-kernel",
        kernel: descriptorKernel ?? id,
        memoryPath: "internal-to-internal",
        bytesPerIteration: 32,
        iterationsPerSample: 1,
        warmupIterations: 8,
      },
    }),
  ];
  for (let ordinal = 0; ordinal < samples; ++ordinal) {
    const start = 1000 + ordinal * 20;
    lines.push(
      record({
        protocolVersion: 1,
        record: "sample",
        measurementId: id,
        sample: {
          ordinal,
          startCore: 0,
          endCore: badEndCore && ordinal === 0 ? 1 : 0,
          startCcount: start,
          endCcount: start + 10,
          cycles: badDelta && ordinal === 0 ? 11 : 10,
        },
        checksum: ordinal,
      }),
    );
  }
  lines.push(
    record({
      protocolVersion: 1,
      record: "measurement-complete",
      measurementId: id,
      samples,
    }),
    record({
      protocolVersion: 1,
      record: "run-complete",
      measurements: 1,
      samplesPerMeasurement: MINIMUM_TIMING_PROBE_SAMPLES,
      pass: true,
    }),
  );
  return `${lines.join("\n")}\n`;
}

const assembleOptions = {
  capturedAt: "2000-01-01T00:00:00Z",
  bootLogSha256: "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  allowSchemaFixtures: true,
};

describe("timing-probe capture protocol", () => {
  test("assembles a 100-sample capture into a strict calibration receipt", () => {
    const receipts = assembleTimingProbeReceipts(fixtureLog(), assembleOptions);
    expect(receipts).toHaveLength(1);
    expect(receipts[0]!.measurement.samples).toHaveLength(100);
    expect(receipts[0]!.boot.bootLogSha256).toBe(assembleOptions.bootLogSha256);
  });

  test("rejects a truncated measurement below the sample floor", () => {
    expect(() => assembleTimingProbeReceipts(fixtureLog(99), assembleOptions)).toThrow(
      "has 99 samples; 100 required",
    );
  });

  test("passes raw CCOUNT deltas through the strict receipt verifier", () => {
    expect(() => assembleTimingProbeReceipts(fixtureLog(100, true), assembleOptions)).toThrow(
      "unsigned 32-bit CCOUNT delta 10",
    );
  });

  test("requires every raw sample to start and end on the counter core", () => {
    expect(() => assembleTimingProbeReceipts(fixtureLog(100, false, true), assembleOptions)).toThrow(
      "crossed CPU cores",
    );
  });

  test("requires the sample core to equal the metadata counter core", () => {
    expect(() =>
      assembleTimingProbeReceipts(fixtureLog(100, false, false, undefined, 1), assembleOptions),
    ).toThrow("startCore must equal $.counter.core");
  });

  test("requires descriptor kernel to equal its measurement id", () => {
    expect(() =>
      assembleTimingProbeReceipts(fixtureLog(100, false, false, "wrong_kernel"), assembleOptions),
    ).toThrow("descriptor kernel must equal measurementId");
  });

  test("rejects explicit firmware failures", () => {
    const log = [
      record(fixtureMetadata),
      record({
        protocolVersion: 1,
        record: "error",
        phase: "flash_instruction_cold_single_core",
        reason: "cache-prepare",
        espError: 261,
      }),
    ].join("\n");
    expect(() => assembleTimingProbeReceipts(log, assembleOptions)).toThrow(
      "firmware error during flash_instruction_cold_single_core: cache-prepare",
    );
  });
});

describe("standalone timing-probe firmware structure", () => {
  test("covers every requested memory shape in both contention modes", async () => {
    const root = join(import.meta.dir, "../..");
    const source = await Bun.file(join(root, "esp32/main/timing_probe/timing_probe.cpp")).text();
    for (const kernel of [
      "sram_aligned_dependent",
      "sram_unaligned_dependent",
      "sram_aligned_stream",
      "sram_unaligned_stream",
      "psram_hot_sequential",
      "psram_cold_sequential",
      "psram_hot_random",
      "psram_cold_random",
      "flash_mmap_hot_sequential",
      "flash_mmap_cold_sequential",
      "flash_mmap_hot_random",
      "flash_mmap_cold_random",
      "flash_instruction_hot",
      "flash_instruction_cold",
    ]) {
      expect(source).toContain(`\"${kernel}\"`);
    }
    expect(source).toContain("constexpr int kSamplesPerMeasurement = 100;");
    expect(source).toContain('run_suite(context, "single_core")');
    expect(source).toContain('run_suite(context, "core1_contended")');
    expect(source).toContain("IRAM_ATTR NOINLINE_ATTR measure_once");
    expect(source).toContain("esp_partition_mmap");
    expect(source).not.toContain("esp_partition_erase");
    expect(source).not.toContain("esp_flash_erase");
  });

  test("routes an independent variant with benchmark-only task-WDT settings", async () => {
    const root = join(import.meta.dir, "../..");
    const [projectCmake, componentCmake, script, sdkconfig, timingProbeSdkconfig] = await Promise.all([
      Bun.file(join(root, "esp32/CMakeLists.txt")).text(),
      Bun.file(join(root, "esp32/main/CMakeLists.txt")).text(),
      Bun.file(join(root, "scripts/esp32")).text(),
      Bun.file(join(root, "esp32/sdkconfig.defaults")).text(),
      Bun.file(join(root, "esp32/sdkconfig.timing-probe.defaults")).text(),
    ]);
    expect(projectCmake).toContain("timing-probe");
    expect(projectCmake).toContain('if(TINYDRAW_TIMING_PROBE)');
    expect(projectCmake).toContain('sdkconfig.timing-probe.defaults');
    expect(componentCmake).toContain('set(TINYDRAW_APP_SRCS "timing_probe/timing_probe.cpp")');
    expect(script).toContain("CONFIG_ESPTOOLPY_FLASHFREQ_80M=y");
    expect(script).toContain("assert_timing_probe_config");
    expect(script).toContain("TINYDRAW_FIRMWARE_VARIANT=timing-probe reconfigure");
    expect(script).toContain("# CONFIG_ESP_TASK_WDT_EN is not set");
    expect(script).toContain("CONFIG_ESP_INT_WDT=y");
    expect(script).toContain("timing-probe)");
    expect(sdkconfig).toContain("CONFIG_ESPTOOLPY_FLASHFREQ_80M=y");
    expect(sdkconfig).not.toContain("CONFIG_ESP_TASK_WDT_EN");
    expect(timingProbeSdkconfig).toContain("# CONFIG_ESP_TASK_WDT_EN is not set");
    expect(timingProbeSdkconfig).toContain("CONFIG_ESP_INT_WDT=y");
    expect(timingProbeSdkconfig).toContain("CONFIG_ESP_INT_WDT_CHECK_CPU1=y");
  });
});
