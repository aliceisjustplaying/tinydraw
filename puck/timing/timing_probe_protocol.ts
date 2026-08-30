import {
  parseCalibrationReceiptValue,
  type CalibrationReceipt,
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

interface MeasurementGroup {
  descriptor: MeasurementDescriptor;
  samples: CcountSample[];
  completedSamples: number | null;
}

function object(value: unknown, path: string): JsonObject {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    throw new Error(`${path} must be an object`);
  }
  return value as JsonObject;
}

function exactKeys(value: JsonObject, path: string, keys: readonly string[]): void {
  const allowed = new Set(keys);
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

function integer(value: unknown, path: string, minimum = 0): number {
  if (!Number.isSafeInteger(value) || (value as number) < minimum) {
    throw new Error(`${path} must be an integer >= ${minimum}`);
  }
  return value as number;
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
  exactKeys(sample, path, ["ordinal", "startCore", "endCore", "startCcount", "endCcount", "cycles"]);
  return {
    ordinal: integer(sample.ordinal, `${path}.ordinal`),
    startCore: integer(sample.startCore, `${path}.startCore`) as 0 | 1,
    endCore: integer(sample.endCore, `${path}.endCore`) as 0 | 1,
    startCcount: integer(sample.startCcount, `${path}.startCcount`),
    endCcount: integer(sample.endCcount, `${path}.endCcount`),
    cycles: integer(sample.cycles, `${path}.cycles`, 1),
  };
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
