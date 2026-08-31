import { describe, expect, test } from "bun:test";
import { join } from "node:path";

import {
  assembleTimingProbeReceipts,
  MINIMUM_TIMING_PROBE_SAMPLES,
  recoverCompleteTimingProbeReceipts,
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

const fixtureCacheCounters = {
  ibus: { accesses: 43, misses: 2 },
  dbus: { accesses: 10, flashMisses: 0, psramMisses: 0 },
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
  includeCacheCounters: boolean | ((ordinal: number) => boolean) = false,
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
    const includeCounters =
      typeof includeCacheCounters === "function"
        ? includeCacheCounters(ordinal)
        : includeCacheCounters;
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
          ...(includeCounters ? { cacheCounters: fixtureCacheCounters } : {}),
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

function fixtureGroupRecords(
  id: string,
  samples = MINIMUM_TIMING_PROBE_SAMPLES,
  badDelta = false,
): string[] {
  const records = [
    record({
      protocolVersion: 1,
      record: "measurement-start",
      measurementId: id,
      measurement: {
        kind: "ccount-kernel",
        kernel: id,
        memoryPath: "internal-to-internal",
        bytesPerIteration: 32,
        iterationsPerSample: 1,
        warmupIterations: 8,
      },
    }),
  ];
  for (let ordinal = 0; ordinal < samples; ++ordinal) {
    const start = 1000 + ordinal * 20;
    records.push(
      record({
        protocolVersion: 1,
        record: "sample",
        measurementId: id,
        sample: {
          ordinal,
          startCore: 0,
          endCore: 0,
          startCcount: start,
          endCcount: start + 10,
          cycles: badDelta && ordinal === 0 ? 11 : 10,
        },
        checksum: ordinal,
      }),
    );
  }
  records.push(
    record({
      protocolVersion: 1,
      record: "measurement-complete",
      measurementId: id,
      samples,
    }),
  );
  return records;
}

function fixtureMultiMeasurementLog(
  groups: Array<{ id: string; badDelta?: boolean }>,
): string {
  return [
    "ROM boot text ignored by the protocol parser",
    record(fixtureMetadata),
    ...groups.flatMap(({ id, badDelta }) =>
      fixtureGroupRecords(id, MINIMUM_TIMING_PROBE_SAMPLES, badDelta),
    ),
    record({
      protocolVersion: 1,
      record: "run-complete",
      measurements: groups.length,
      samplesPerMeasurement: MINIMUM_TIMING_PROBE_SAMPLES,
      pass: true,
    }),
  ].join("\n");
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
    expect(receipts[0]!.measurement.samples[0]!.cacheCounters).toBeUndefined();
  });

  test("retains optional uint32 cache counters from sample records", () => {
    const receipts = assembleTimingProbeReceipts(
      fixtureLog(100, false, false, undefined, 0, true),
      assembleOptions,
    );
    expect(receipts[0]!.measurement.samples[0]!.cacheCounters).toEqual(fixtureCacheCounters);
  });

  test("requires cache counters on every sample when a measurement includes them", () => {
    expect(() =>
      assembleTimingProbeReceipts(
        fixtureLog(100, false, false, undefined, 0, (ordinal) => ordinal !== 50),
        assembleOptions,
      ),
    ).toThrow("cacheCounters must be present on every sample or absent from every sample");
  });

  test("rejects cache counters outside the uint32 domain", () => {
    const log = fixtureLog(100, false, false, undefined, 0, true).replace(
      '"accesses":43',
      '"accesses":4294967296',
    );
    expect(() => assembleTimingProbeReceipts(log, assembleOptions)).toThrow(
      "cacheCounters.ibus.accesses must be an integer from 0 through 4294967295",
    );
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

describe("timing-probe complete-measurement recovery", () => {
  test("omits the active group when a truncated sample is concatenated with the next prefixed sample", () => {
    const damagedId = "sram_aligned_stream_single_core";
    const retainedId = "sram_unaligned_stream_single_core";
    const intact = fixtureMultiMeasurementLog([{ id: damagedId }, { id: retainedId }]);
    const lines = intact.split("\n");
    const firstSampleIndex = lines.findIndex(
      (line) => line.includes(`\"measurementId\":\"${damagedId}\"`) && line.includes('"ordinal":49'),
    );
    expect(firstSampleIndex).toBeGreaterThan(-1);
    const truncated = lines[firstSampleIndex]!.slice(0, -8);
    lines.splice(firstSampleIndex, 2, `${truncated}${lines[firstSampleIndex + 1]!}`);
    const damagedLog = lines.join("\n");

    expect(() => assembleTimingProbeReceipts(damagedLog, assembleOptions)).toThrow(
      "invalid timing-probe JSON",
    );
    const recovered = recoverCompleteTimingProbeReceipts(damagedLog, assembleOptions);
    expect(
      recovered.receipts.map((receipt) =>
        receipt.measurement.kind === "ccount-kernel" ? receipt.measurement.kernel : "unexpected",
      ),
    ).toEqual([retainedId]);
    expect(recovered.receipts[0]!.boot.bootLogSha256).toBe(assembleOptions.bootLogSha256);
    expect(recovered.omittedMeasurements).toHaveLength(1);
    expect(recovered.omittedMeasurements[0]!.measurementId).toBe(damagedId);
    expect(recovered.omittedMeasurements[0]!.reasons.join(" ")).toContain(
      "malformed timing-probe fragment",
    );
  });

  test("runs each complete candidate through the existing strict receipt assembler", () => {
    const invalidId = "sram_aligned_stream_single_core";
    const retainedId = "sram_unaligned_stream_single_core";
    const recovered = recoverCompleteTimingProbeReceipts(
      fixtureMultiMeasurementLog([
        { id: invalidId, badDelta: true },
        { id: retainedId },
      ]),
      assembleOptions,
    );
    expect(
      recovered.receipts.map((receipt) =>
        receipt.measurement.kind === "ccount-kernel" ? receipt.measurement.kernel : "unexpected",
      ),
    ).toEqual([retainedId]);
    expect(recovered.omittedMeasurements[0]!.measurementId).toBe(invalidId);
    expect(recovered.omittedMeasurements[0]!.reasons.join(" ")).toContain(
      "strict validation failed",
    );
    expect(recovered.omittedMeasurements[0]!.reasons.join(" ")).toContain(
      "unsigned 32-bit CCOUNT delta 10",
    );
  });

  test("still requires a valid global metadata and pass=true run-complete envelope", () => {
    const valid = fixtureMultiMeasurementLog([
      { id: "sram_aligned_stream_single_core" },
    ]);
    expect(() =>
      recoverCompleteTimingProbeReceipts(
        valid.replace(record(fixtureMetadata), "malformed metadata"),
        assembleOptions,
      ),
    ).toThrow("missing valid metadata");
    expect(() =>
      recoverCompleteTimingProbeReceipts(valid.replace('"pass":true', '"pass":false'), assembleOptions),
    ).toThrow("missing valid pass=true run-complete");
    expect(() =>
      recoverCompleteTimingProbeReceipts(
        valid.replace('"cpuHz":240000000', '"cpuHz":160000000'),
        assembleOptions,
      ),
    ).toThrow("timing-probe metadata is invalid");
  });

  test("anchors recovery at the first valid metadata record after a torn prior-boot tail", () => {
    const id = "sram_aligned_stream_single_core";
    const valid = fixtureMultiMeasurementLog([{ id }]);
    const recovered = recoverCompleteTimingProbeReceipts(
      `${TIMING_PROBE_RECORD_PREFIX}{\n${valid}`,
      assembleOptions,
    );
    expect(recovered.receipts).toHaveLength(1);
    expect(recovered.receipts[0]!.measurement.kind).toBe("ccount-kernel");
    expect(recovered.omittedMeasurements).toEqual([]);
  });

  test("rejects a malformed protocol fragment that cannot be attributed to an active group", () => {
    const valid = fixtureMultiMeasurementLog([
      { id: "sram_aligned_stream_single_core" },
    ]);
    const damaged = valid.replace(
      record(fixtureMetadata),
      `${record(fixtureMetadata)}\n${TIMING_PROBE_RECORD_PREFIX}{`,
    );
    expect(() => recoverCompleteTimingProbeReceipts(damaged, assembleOptions)).toThrow(
      "rejected global envelope",
    );
  });

  test("rejects measurement records that appear after run-complete", () => {
    const id = "sram_aligned_stream_single_core";
    const valid = fixtureMultiMeasurementLog([{ id }]);
    const groupRecords = fixtureGroupRecords(id);
    for (const lateRecord of [groupRecords[0]!, groupRecords[1]!, groupRecords.at(-1)!]) {
      expect(() =>
        recoverCompleteTimingProbeReceipts(`${valid}\n${lateRecord}`, assembleOptions),
      ).toThrow("appeared after run-complete");
    }
  });

  test("omits an active incomplete group when run-complete arrives", () => {
    const completeId = "sram_aligned_stream_single_core";
    const incompleteId = "sram_unaligned_stream_single_core";
    const incompleteRecords = fixtureGroupRecords(incompleteId).slice(0, -1);
    const log = [
      record(fixtureMetadata),
      ...fixtureGroupRecords(completeId),
      ...incompleteRecords,
      record({
        protocolVersion: 1,
        record: "run-complete",
        measurements: 2,
        samplesPerMeasurement: MINIMUM_TIMING_PROBE_SAMPLES,
        pass: true,
      }),
    ].join("\n");

    const recovered = recoverCompleteTimingProbeReceipts(log, assembleOptions);
    expect(recovered.receipts).toHaveLength(1);
    expect(recovered.receipts[0]!.measurement.kind).toBe("ccount-kernel");
    expect(recovered.omittedMeasurements).toHaveLength(1);
    expect(recovered.omittedMeasurements[0]!.measurementId).toBe(incompleteId);
    expect(recovered.omittedMeasurements[0]!.reasons).toContain(
      "run-complete arrived before measurement-complete",
    );
    expect(recovered.omittedMeasurements[0]!.reasons).toContain(
      "measurement-complete is missing",
    );
  });
});

describe("standalone timing-probe firmware structure", () => {
  test("covers every requested memory shape in both contention modes", async () => {
    const root = join(import.meta.dir, "../..");
    const source = await Bun.file(join(root, "esp32/main/timing_probe/timing_probe.cpp")).text();
    const rgbWindowAssembly = await Bun.file(
      join(root, "esp32/main/timing_probe/rgb565_call_window_esp32s3.S"),
    ).text();
    const sramAssembly = await Bun.file(
      join(root, "esp32/main/timing_probe/sram_microprobes_esp32s3.S"),
    ).text();
    const dcacheAssembly = await Bun.file(
      join(root, "esp32/main/timing_probe/dcache_burst_probes_esp32s3.S"),
    ).text();
    for (const kernel of [
      "sram_aligned_dependent",
      "sram_unaligned_dependent",
      "sram_aligned_stream",
      "sram_unaligned_stream",
      "sram_instruction_issue",
      "sram_l32_dependent",
      "sram_l32_independent",
      "sram_s32_store_complete",
      "rgb565_stage_five_scalar_oracle_hot",
      "rgb565_stage_five_scalar_oracle_cold",
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
    expect(source).toContain("keeps its probe buffers alive until the next reset");
    expect(source).toContain("IRAM_ATTR NOINLINE_ATTR measure_once");
    expect(source).toContain("CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE");
    expect(source).toContain("EXTMEM_IBUS_ACS_MISS_CNT_REG");
    expect(source).toContain("EXTMEM_DBUS_ACS_FLASH_MISS_CNT_REG");
    expect(source).toContain("EXTMEM_DBUS_ACS_SPIRAM_MISS_CNT_REG");
    expect(source).toContain("EXTMEM_DBUS_TO_FLASH_START_VADDR_REG");
    expect(source).toContain("EXTMEM_DBUS_TO_FLASH_END_VADDR_REG");
    expect(source).toContain("TINYDRAW_TIMING_COUNTER_RANGE");
    expect(source).toContain("0x1234U, 0xabcdU, 0x00ffU, 0xf81fU, 0x07e0U");
    expect(source).toContain("kRgb565StageOutputChecksum = 0x471e'969fU");
    expect(source).toContain(
      "[[gnu::noipa, gnu::aligned(32)]] void stage_pixels_swapped_scalar_oracle",
    );
    expect(source).toContain("prepare_rgb565_stage_hot");
    expect(source).toContain("prepare_rgb565_stage_cold");
    expect(source).toContain("measure_rgb565_stage_once");
    expect(source).toContain("finalize_sram_store_complete");
    expect(source).toContain("kSramStoreCompletionChecksum");
    expect(source).toContain("CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE == kDcacheLineBytes");
    expect(source).toContain("Lines * kDcacheLineBytes");
    expect(source).toContain('print_error("initialize", "dcache-burst-alignment")');
    for (const [path, memoryPath] of [
      ["psram", "psram-to-internal"],
      ["flash", "flash-to-internal"],
    ]) {
      for (const lines of [1, 2, 4, 8, 16]) {
        expect(source).toContain(
          `DCACHE_BURST_MEASUREMENTS(${path}, "${memoryPath}", ${lines})`,
        );
        expect(dcacheAssembly).toContain(
          `DEFINE_DCACHE_BURST_PROBE tinydraw_dcache_${path}_${lines}_lines`,
        );
      }
    }
    expect(dcacheAssembly).toContain(".balign 32");
    expect(
      [...dcacheAssembly.matchAll(/l32i\s+a4, a8, (\d+)/g)].map((match) => Number(match[1])),
    ).toEqual(Array.from({ length: 16 }, (_, index) => index * 64));
    expect(dcacheAssembly).not.toContain("s32i");
    expect(dcacheAssembly).not.toContain("loop");
    for (const symbol of [
      "tinydraw_sram_instruction_issue",
      "tinydraw_sram_l32_dependent",
      "tinydraw_sram_l32_independent",
      "tinydraw_sram_s32_store_complete",
    ]) {
      expect(sramAssembly).toContain(`.global ${symbol}`);
    }
    const issueProbe = sramAssembly.slice(
      sramAssembly.indexOf("tinydraw_sram_instruction_issue:"),
      sramAssembly.indexOf(".size tinydraw_sram_instruction_issue"),
    );
    expect(issueProbe.match(/\baddi\b/g)).toHaveLength(9);
    expect(issueProbe).toContain("movi    a8, 1024");
    const dependentProbe = sramAssembly.slice(
      sramAssembly.indexOf("tinydraw_sram_l32_dependent:"),
      sramAssembly.indexOf(".size tinydraw_sram_l32_dependent"),
    );
    expect(dependentProbe.match(/\bl32i\b/g)).toHaveLength(2);
    expect(dependentProbe).not.toContain("memw");
    const independentProbe = sramAssembly.slice(
      sramAssembly.indexOf("tinydraw_sram_l32_independent:"),
      sramAssembly.indexOf(".size tinydraw_sram_l32_independent"),
    );
    expect(independentProbe.match(/\bl32i\b/g)).toHaveLength(9);
    expect(independentProbe.match(/\baddi\b/g)).toHaveLength(1);
    expect(independentProbe).toContain("movi    a14, 1024");
    expect(independentProbe).not.toContain("memw");
    const storeProbe = sramAssembly.slice(
      sramAssembly.indexOf("tinydraw_sram_s32_store_complete:"),
      sramAssembly.indexOf(".size tinydraw_sram_s32_store_complete"),
    );
    expect(storeProbe.match(/\bs32i\b/g)).toHaveLength(8);
    expect(storeProbe.match(/\bmemw\b/g)).toHaveLength(1);
    expect(storeProbe.indexOf("s32i    a3, a8, 28")).toBeLessThan(storeProbe.indexOf("memw"));
    const rgbSampler = source.slice(
      source.indexOf("RawSample IRAM_ATTR NOINLINE_ATTR measure_rgb565_stage_once"),
      source.indexOf("void print_measurement_start"),
    );
    expect(rgbSampler).toContain("tinydraw_measure_rgb565_call_window(&call_window");
    expect(rgbSampler).not.toContain("sample.start_ccount = esp_cpu_get_cycle_count();");
    expect(rgbSampler).not.toContain("sample.end_ccount = esp_cpu_get_cycle_count();");
    expect(rgbWindowAssembly.match(/callx8\s+a8/g)).toHaveLength(1);
    const startRead = rgbWindowAssembly.indexOf("rsr.ccount  a4");
    const oracleCall = rgbWindowAssembly.indexOf("callx8      a8");
    const endRead = rgbWindowAssembly.indexOf("rsr.ccount  a5");
    const firstStore = rgbWindowAssembly.indexOf("s32i        a4");
    expect(startRead).toBeGreaterThan(-1);
    expect(startRead).toBeLessThan(oracleCall);
    expect(oracleCall).toBeLessThan(endRead);
    expect(endRead).toBeLessThan(firstStore);
    expect(source).not.toContain("esp_cache_get_line_size_by_addr");
    expect(source).toContain("esp_partition_mmap");
    expect(source).not.toContain("esp_partition_erase");
    expect(source).not.toContain("esp_flash_erase");
    expect(source).toContain("flush_console();\n    vTaskDelay(pdMS_TO_TICKS(5));");
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
    expect(componentCmake).toContain('"timing_probe/timing_probe.cpp"');
    expect(componentCmake).toContain('"timing_probe/rgb565_call_window_esp32s3.S"');
    expect(componentCmake).toContain('"timing_probe/sram_microprobes_esp32s3.S"');
    expect(componentCmake).toContain('"timing_probe/dcache_burst_probes_esp32s3.S"');
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
