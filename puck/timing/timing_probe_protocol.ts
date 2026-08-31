import {
  parseCalibrationReceiptValue,
  type CalibrationReceipt,
  type CachePerformanceCounters,
  type CcountKernelMeasurement,
  type CcountSample,
} from "./calibration_receipt";

export const TIMING_PROBE_RECORD_PREFIX = "TINYDRAW_TIMING_NDJSON ";
export const TIMING_PROBE_PROTOCOL_VERSION = 1;
export const MINIMUM_TIMING_PROBE_SAMPLES = 100;

type JsonObject = Record<string, unknown>;
type MeasurementDescriptor = Omit<CcountKernelMeasurement, "samples">;

export interface AssembleTimingProbeOptions {
  capturedAt: string;
  bootLogSha256: string;
  allowSchemaFixtures?: boolean;
}

export interface OmittedTimingProbeMeasurement {
  measurementId: string;
  reasons: string[];
}

export interface RecoveredTimingProbeReceipts {
  receipts: CalibrationReceipt[];
  omittedMeasurements: OmittedTimingProbeMeasurement[];
}

interface MeasurementGroup {
  descriptor: MeasurementDescriptor;
  samples: CcountSample[];
  completedSamples: number | null;
}

interface RecoveryGroup {
  records: JsonObject[];
  reasons: Set<string>;
  started: boolean;
  completed: boolean;
}

function object(value: unknown, path: string): JsonObject {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    throw new Error(`${path} must be an object`);
  }
  return value as JsonObject;
}

function exactKeys(
  value: JsonObject,
  path: string,
  keys: readonly string[],
  optional: readonly string[] = [],
): void {
  const allowed = new Set([...keys, ...optional]);
  for (const key of keys) {
    if (!Object.hasOwn(value, key)) throw new Error(`${path}.${key} is required`);
  }
  for (const key of Object.keys(value)) {
    if (!allowed.has(key)) throw new Error(`${path}.${key} is not allowed`);
  }
}

function string(value: unknown, path: string): string {
  if (typeof value !== "string" || value.length === 0) {
    throw new Error(`${path} must be a non-empty string`);
  }
  return value;
}

function integer(
  value: unknown,
  path: string,
  minimum = 0,
  maximum = Number.MAX_SAFE_INTEGER,
): number {
  if (!Number.isSafeInteger(value) || (value as number) < minimum || (value as number) > maximum) {
    throw new Error(`${path} must be an integer from ${minimum} through ${maximum}`);
  }
  return value as number;
}

function parseCacheCounters(value: unknown, path: string): CachePerformanceCounters {
  const counters = object(value, path);
  exactKeys(counters, path, ["ibus", "dbus"]);
  const ibus = object(counters.ibus, `${path}.ibus`);
  exactKeys(ibus, `${path}.ibus`, ["accesses", "misses"]);
  const dbus = object(counters.dbus, `${path}.dbus`);
  exactKeys(dbus, `${path}.dbus`, ["accesses", "flashMisses", "psramMisses"]);
  const uint32Max = 0xffff_ffff;
  return {
    ibus: {
      accesses: integer(ibus.accesses, `${path}.ibus.accesses`, 0, uint32Max),
      misses: integer(ibus.misses, `${path}.ibus.misses`, 0, uint32Max),
    },
    dbus: {
      accesses: integer(dbus.accesses, `${path}.dbus.accesses`, 0, uint32Max),
      flashMisses: integer(dbus.flashMisses, `${path}.dbus.flashMisses`, 0, uint32Max),
      psramMisses: integer(dbus.psramMisses, `${path}.dbus.psramMisses`, 0, uint32Max),
    },
  };
}

function protocolRecord(value: unknown, line: number): JsonObject {
  const record = object(value, `line ${line}`);
  if (record.protocolVersion !== TIMING_PROBE_PROTOCOL_VERSION) {
    throw new Error(`line ${line}.protocolVersion must be ${TIMING_PROBE_PROTOCOL_VERSION}`);
  }
  string(record.record, `line ${line}.record`);
  return record;
}

function parseDescriptor(value: unknown, path: string): MeasurementDescriptor {
  const descriptor = object(value, path);
  exactKeys(
    descriptor,
    path,
    [
      "kind",
      "kernel",
      "memoryPath",
      "bytesPerIteration",
      "iterationsPerSample",
      "warmupIterations",
    ],
  );
  if (descriptor.kind !== "ccount-kernel") throw new Error(`${path}.kind must be ccount-kernel`);
  return {
    kind: "ccount-kernel",
    kernel: string(descriptor.kernel, `${path}.kernel`),
    memoryPath: string(descriptor.memoryPath, `${path}.memoryPath`) as MeasurementDescriptor["memoryPath"],
    bytesPerIteration: integer(descriptor.bytesPerIteration, `${path}.bytesPerIteration`),
    iterationsPerSample: integer(descriptor.iterationsPerSample, `${path}.iterationsPerSample`, 1),
    warmupIterations: integer(descriptor.warmupIterations, `${path}.warmupIterations`),
  };
}

function parseSample(value: unknown, path: string): CcountSample {
  const sample = object(value, path);
  exactKeys(
    sample,
    path,
    ["ordinal", "startCore", "endCore", "startCcount", "endCcount", "cycles"],
    ["cacheCounters"],
  );
  const parsed: CcountSample = {
    ordinal: integer(sample.ordinal, `${path}.ordinal`),
    startCore: integer(sample.startCore, `${path}.startCore`) as 0 | 1,
    endCore: integer(sample.endCore, `${path}.endCore`) as 0 | 1,
    startCcount: integer(sample.startCcount, `${path}.startCcount`),
    endCcount: integer(sample.endCcount, `${path}.endCcount`),
    cycles: integer(sample.cycles, `${path}.cycles`, 1),
  };
  if (sample.cacheCounters !== undefined) {
    parsed.cacheCounters = parseCacheCounters(sample.cacheCounters, `${path}.cacheCounters`);
  }
  return parsed;
}

export function assembleTimingProbeReceipts(
  log: string,
  options: AssembleTimingProbeOptions,
): CalibrationReceipt[] {
  let metadata: JsonObject | null = null;
  let runComplete: JsonObject | null = null;
  const groups = new Map<string, MeasurementGroup>();

  const lines = log.split(/\r?\n/);
  for (let index = 0; index < lines.length; ++index) {
    const line = lines[index]!;
    const prefixIndex = line.indexOf(TIMING_PROBE_RECORD_PREFIX);
    if (prefixIndex < 0) continue;
    const lineNumber = index + 1;
    let value: unknown;
    try {
      value = JSON.parse(line.slice(prefixIndex + TIMING_PROBE_RECORD_PREFIX.length));
    } catch (error) {
      throw new Error(`line ${lineNumber} has invalid timing-probe JSON: ${String(error)}`);
    }
    const record = protocolRecord(value, lineNumber);

    switch (record.record) {
      case "metadata": {
        exactKeys(
          record,
          `line ${lineNumber}`,
          [
            "protocolVersion",
            "record",
            "schemaVersion",
            "receiptKind",
            "captureMode",
            "git",
            "toolchain",
            "sdkconfig",
            "boot",
            "counter",
            "workingSets",
          ],
        );
        if (metadata) throw new Error("timing-probe log contains multiple metadata records");
        metadata = record;
        break;
      }
      case "measurement-start": {
        exactKeys(record, `line ${lineNumber}`, ["protocolVersion", "record", "measurementId", "measurement"]);
        const id = string(record.measurementId, `line ${lineNumber}.measurementId`);
        if (groups.has(id)) throw new Error(`measurement ${id} started more than once`);
        const descriptor = parseDescriptor(record.measurement, `line ${lineNumber}.measurement`);
        if (descriptor.kernel !== id) {
          throw new Error(`measurement ${id} descriptor kernel must equal measurementId`);
        }
        groups.set(id, {
          descriptor,
          samples: [],
          completedSamples: null,
        });
        break;
      }
      case "sample": {
        exactKeys(record, `line ${lineNumber}`, ["protocolVersion", "record", "measurementId", "sample", "checksum"]);
        const id = string(record.measurementId, `line ${lineNumber}.measurementId`);
        const group = groups.get(id);
        if (!group) throw new Error(`sample for measurement ${id} appeared before measurement-start`);
        integer(record.checksum, `line ${lineNumber}.checksum`);
        group.samples.push(parseSample(record.sample, `line ${lineNumber}.sample`));
        break;
      }
      case "measurement-complete": {
        exactKeys(record, `line ${lineNumber}`, ["protocolVersion", "record", "measurementId", "samples"]);
        const id = string(record.measurementId, `line ${lineNumber}.measurementId`);
        const group = groups.get(id);
        if (!group) throw new Error(`measurement-complete for unknown measurement ${id}`);
        if (group.completedSamples !== null) throw new Error(`measurement ${id} completed more than once`);
        group.completedSamples = integer(record.samples, `line ${lineNumber}.samples`, 1);
        break;
      }
      case "run-complete": {
        exactKeys(
          record,
          `line ${lineNumber}`,
          ["protocolVersion", "record", "measurements", "samplesPerMeasurement", "pass"],
        );
        if (runComplete) throw new Error("timing-probe log contains multiple run-complete records");
        if (record.pass !== true) throw new Error("timing-probe firmware reported pass=false");
        runComplete = record;
        break;
      }
      case "error": {
        const phase = typeof record.phase === "string" ? record.phase : "unknown";
        const reason = typeof record.reason === "string" ? record.reason : "unknown";
        throw new Error(`timing-probe firmware error during ${phase}: ${reason}`);
      }
      default:
        throw new Error(`line ${lineNumber}.record has unknown value ${String(record.record)}`);
    }
  }

  if (!metadata) throw new Error("timing-probe log is missing metadata");
  if (!runComplete) throw new Error("timing-probe log is missing run-complete");
  if (groups.size === 0) throw new Error("timing-probe log contains no measurements");
  const reportedMeasurements = integer(runComplete.measurements, "run-complete.measurements", 1);
  const reportedSamples = integer(
    runComplete.samplesPerMeasurement,
    "run-complete.samplesPerMeasurement",
    MINIMUM_TIMING_PROBE_SAMPLES,
  );
  if (reportedMeasurements !== groups.size) {
    throw new Error(`run-complete reported ${reportedMeasurements} measurements, parsed ${groups.size}`);
  }

  const boot = object(metadata.boot, "metadata.boot");
  const receipts: CalibrationReceipt[] = [];
  for (const [id, group] of groups) {
    if (group.completedSamples === null) throw new Error(`measurement ${id} is incomplete`);
    if (group.samples.length < MINIMUM_TIMING_PROBE_SAMPLES) {
      throw new Error(
        `measurement ${id} has ${group.samples.length} samples; ${MINIMUM_TIMING_PROBE_SAMPLES} required`,
      );
    }
    if (group.completedSamples !== group.samples.length || reportedSamples !== group.samples.length) {
      throw new Error(`measurement ${id} sample counts disagree`);
    }
    const candidate = {
      schemaVersion: metadata.schemaVersion,
      receiptKind: metadata.receiptKind,
      captureMode: metadata.captureMode,
      capturedAt: options.capturedAt,
      git: metadata.git,
      toolchain: metadata.toolchain,
      sdkconfig: metadata.sdkconfig,
      boot: { ...boot, bootLogSha256: options.bootLogSha256 },
      counter: metadata.counter,
      measurement: { ...group.descriptor, samples: group.samples },
    };
    receipts.push(
      parseCalibrationReceiptValue(candidate, {
        allowSchemaFixtures: options.allowSchemaFixtures,
      }),
    );
  }
  return receipts;
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function recoveryFragments(log: string): Array<{ lineNumber: number; payload: string }> {
  const fragments: Array<{ lineNumber: number; payload: string }> = [];
  const lines = log.split(/\r?\n/);
  for (let index = 0; index < lines.length; ++index) {
    const line = lines[index]!;
    const prefixIndexes: number[] = [];
    let searchFrom = 0;
    while (true) {
      const prefixIndex = line.indexOf(TIMING_PROBE_RECORD_PREFIX, searchFrom);
      if (prefixIndex < 0) break;
      prefixIndexes.push(prefixIndex);
      searchFrom = prefixIndex + TIMING_PROBE_RECORD_PREFIX.length;
    }
    for (let fragment = 0; fragment < prefixIndexes.length; ++fragment) {
      const payloadStart = prefixIndexes[fragment]! + TIMING_PROBE_RECORD_PREFIX.length;
      const payloadEnd = prefixIndexes[fragment + 1] ?? line.length;
      fragments.push({ lineNumber: index + 1, payload: line.slice(payloadStart, payloadEnd) });
    }
  }
  return fragments;
}

function validateRecoveryMetadata(
  metadata: JsonObject,
  options: AssembleTimingProbeOptions,
): void {
  const boot = object(metadata.boot, "metadata.boot");
  const counter = object(metadata.counter, "metadata.counter");
  const core = counter.core === 1 ? 1 : 0;
  const samples: CcountSample[] = Array.from(
    { length: MINIMUM_TIMING_PROBE_SAMPLES },
    (_, ordinal) => ({
      ordinal,
      startCore: core,
      endCore: core,
      startCcount: ordinal,
      endCcount: ordinal + 1,
      cycles: 1,
    }),
  );
  parseCalibrationReceiptValue(
    {
      schemaVersion: metadata.schemaVersion,
      receiptKind: metadata.receiptKind,
      captureMode: metadata.captureMode,
      capturedAt: options.capturedAt,
      git: metadata.git,
      toolchain: metadata.toolchain,
      sdkconfig: metadata.sdkconfig,
      boot: { ...boot, bootLogSha256: options.bootLogSha256 },
      counter,
      measurement: {
        kind: "ccount-kernel",
        kernel: "timing_probe_recovery_metadata_validation",
        memoryPath: "internal-to-internal",
        bytesPerIteration: 1,
        iterationsPerSample: 1,
        warmupIterations: 0,
        samples,
      },
    },
    { allowSchemaFixtures: options.allowSchemaFixtures },
  );
}

/**
 * Recover independently complete measurements from a damaged USB capture.
 *
 * This is deliberately separate from the default strict assembler. A malformed
 * protocol fragment invalidates the measurement active at that point. Every
 * candidate that remains is then reassembled by the strict path in isolation.
 */
export function recoverCompleteTimingProbeReceipts(
  log: string,
  options: AssembleTimingProbeOptions,
): RecoveredTimingProbeReceipts {
  let metadata: JsonObject | null = null;
  let runComplete: JsonObject | null = null;
  let activeMeasurementId: string | null = null;
  let measurementStarts = 0;
  const groups = new Map<string, RecoveryGroup>();
  const globalProblems: string[] = [];

  const groupFor = (id: string): RecoveryGroup => {
    let group = groups.get(id);
    if (!group) {
      group = { records: [], reasons: new Set(), started: false, completed: false };
      groups.set(id, group);
    }
    return group;
  };
  const rejectActiveOrGlobal = (reason: string): void => {
    if (activeMeasurementId === null) {
      globalProblems.push(reason);
    } else {
      groupFor(activeMeasurementId).reasons.add(reason);
    }
  };

  for (const fragment of recoveryFragments(log)) {
    let record: JsonObject;
    try {
      record = protocolRecord(JSON.parse(fragment.payload), fragment.lineNumber);
    } catch (error) {
      if (metadata === null && groups.size === 0 && runComplete === null) {
        continue;
      }
      rejectActiveOrGlobal(
        `line ${fragment.lineNumber} has malformed timing-probe fragment: ${errorMessage(error)}`,
      );
      continue;
    }

    // A serial capture can begin with the torn tail of a previous boot. The
    // first valid metadata record is the recovery envelope's boot boundary.
    if (metadata === null && record.record !== "metadata") continue;

    if (
      runComplete !== null &&
      (record.record === "measurement-start" ||
        record.record === "sample" ||
        record.record === "measurement-complete")
    ) {
      globalProblems.push(
        `line ${fragment.lineNumber} ${String(record.record)} appeared after run-complete`,
      );
      continue;
    }

    try {
      switch (record.record) {
        case "metadata": {
          exactKeys(
            record,
            `line ${fragment.lineNumber}`,
            [
              "protocolVersion",
              "record",
              "schemaVersion",
              "receiptKind",
              "captureMode",
              "git",
              "toolchain",
              "sdkconfig",
              "boot",
              "counter",
              "workingSets",
            ],
          );
          if (metadata) throw new Error("timing-probe log contains multiple metadata records");
          if (activeMeasurementId !== null || groups.size > 0) {
            throw new Error("metadata appeared after measurements began");
          }
          metadata = record;
          break;
        }
        case "measurement-start": {
          exactKeys(record, `line ${fragment.lineNumber}`, [
            "protocolVersion",
            "record",
            "measurementId",
            "measurement",
          ]);
          const id = string(record.measurementId, `line ${fragment.lineNumber}.measurementId`);
          if (activeMeasurementId !== null) {
            groupFor(activeMeasurementId).reasons.add(
              `measurement was interrupted by measurement-start for ${id}`,
            );
          }
          const group = groupFor(id);
          if (group.started) group.reasons.add(`measurement ${id} started more than once`);
          group.started = true;
          group.records.push(record);
          activeMeasurementId = id;
          measurementStarts += 1;
          break;
        }
        case "sample": {
          exactKeys(record, `line ${fragment.lineNumber}`, [
            "protocolVersion",
            "record",
            "measurementId",
            "sample",
            "checksum",
          ]);
          const id = string(record.measurementId, `line ${fragment.lineNumber}.measurementId`);
          const group = groupFor(id);
          if (!group.started) group.reasons.add(`sample for ${id} appeared before measurement-start`);
          if (activeMeasurementId !== id) {
            group.reasons.add(`sample for ${id} appeared while ${activeMeasurementId ?? "no measurement"} was active`);
            if (activeMeasurementId !== null) {
              groupFor(activeMeasurementId).reasons.add(`unexpected sample for ${id}`);
            }
          }
          group.records.push(record);
          break;
        }
        case "measurement-complete": {
          exactKeys(record, `line ${fragment.lineNumber}`, [
            "protocolVersion",
            "record",
            "measurementId",
            "samples",
          ]);
          const id = string(record.measurementId, `line ${fragment.lineNumber}.measurementId`);
          const group = groupFor(id);
          if (!group.started) {
            group.reasons.add(`measurement-complete for unknown measurement ${id}`);
          }
          if (group.completed) group.reasons.add(`measurement ${id} completed more than once`);
          if (activeMeasurementId !== id) {
            group.reasons.add(
              `measurement ${id} completed while ${activeMeasurementId ?? "no measurement"} was active`,
            );
          }
          group.completed = true;
          group.records.push(record);
          if (activeMeasurementId === id) activeMeasurementId = null;
          break;
        }
        case "run-complete": {
          exactKeys(record, `line ${fragment.lineNumber}`, [
            "protocolVersion",
            "record",
            "measurements",
            "samplesPerMeasurement",
            "pass",
          ]);
          if (runComplete) throw new Error("timing-probe log contains multiple run-complete records");
          if (record.pass !== true) throw new Error("timing-probe firmware reported pass=false");
          integer(record.measurements, "run-complete.measurements", 1);
          integer(
            record.samplesPerMeasurement,
            "run-complete.samplesPerMeasurement",
            MINIMUM_TIMING_PROBE_SAMPLES,
          );
          if (activeMeasurementId !== null) {
            groupFor(activeMeasurementId).reasons.add(
              "run-complete arrived before measurement-complete",
            );
            activeMeasurementId = null;
          }
          runComplete = record;
          break;
        }
        case "error": {
          const phase = typeof record.phase === "string" ? record.phase : "unknown";
          const reason = typeof record.reason === "string" ? record.reason : "unknown";
          throw new Error(`timing-probe firmware error during ${phase}: ${reason}`);
        }
        default:
          throw new Error(
            `line ${fragment.lineNumber}.record has unknown value ${String(record.record)}`,
          );
      }
    } catch (error) {
      const reason = errorMessage(error);
      if (record.record === "metadata" || record.record === "run-complete" || record.record === "error") {
        globalProblems.push(reason);
      } else {
        rejectActiveOrGlobal(reason);
      }
    }
  }

  if (!metadata) globalProblems.push("timing-probe log is missing valid metadata");
  if (metadata) {
    try {
      validateRecoveryMetadata(metadata, options);
    } catch (error) {
      globalProblems.push(`timing-probe metadata is invalid: ${errorMessage(error)}`);
    }
  }
  if (!runComplete) globalProblems.push("timing-probe log is missing valid pass=true run-complete");
  if (groups.size === 0) globalProblems.push("timing-probe log contains no measurements");
  if (metadata && runComplete) {
    const reportedMeasurements = integer(
      runComplete.measurements,
      "run-complete.measurements",
      1,
    );
    if (reportedMeasurements !== measurementStarts) {
      globalProblems.push(
        `run-complete reported ${reportedMeasurements} measurements, found ${measurementStarts} measurement-start records`,
      );
    }
  }
  if (globalProblems.length > 0) {
    throw new Error(`timing-probe recovery rejected global envelope: ${globalProblems.join("; ")}`);
  }

  const receipts: CalibrationReceipt[] = [];
  const omittedMeasurements: OmittedTimingProbeMeasurement[] = [];
  for (const [id, group] of groups) {
    if (!group.started) group.reasons.add("measurement-start is missing");
    if (!group.completed) group.reasons.add("measurement-complete is missing");
    if (group.reasons.size === 0) {
      const canonicalLog = [
        `${TIMING_PROBE_RECORD_PREFIX}${JSON.stringify(metadata)}`,
        ...group.records.map(
          (record) => `${TIMING_PROBE_RECORD_PREFIX}${JSON.stringify(record)}`,
        ),
        `${TIMING_PROBE_RECORD_PREFIX}${JSON.stringify({ ...runComplete, measurements: 1 })}`,
      ].join("\n");
      try {
        const recovered = assembleTimingProbeReceipts(canonicalLog, options);
        if (
          recovered.length !== 1 ||
          recovered[0]!.measurement.kind !== "ccount-kernel" ||
          recovered[0]!.measurement.kernel !== id
        ) {
          throw new Error(`canonical group produced an unexpected receipt for ${id}`);
        }
        receipts.push(recovered[0]!);
      } catch (error) {
        group.reasons.add(`strict validation failed: ${errorMessage(error)}`);
      }
    }
    if (group.reasons.size > 0) {
      omittedMeasurements.push({ measurementId: id, reasons: [...group.reasons] });
    }
  }

  return { receipts, omittedMeasurements };
}
