export const CALIBRATION_RECEIPT_SCHEMA_VERSION = 1 as const;

export type CaptureMode = "hardware" | "schema-fixture";

export interface GitMetadata {
  repository: string;
  commit: string;
  dirty: boolean;
}

export interface ToolchainMetadata {
  target: "esp32s3";
  espIdfVersion: string;
  compiler: string;
  compilerVersion: string;
}

export interface SdkconfigMetadata {
  path: string;
  sha256: string;
  cpuHz: number;
  psramMode: string;
  psramBusHz: number | null;
  flashMode: string;
  flashBusHz: number | null;
}

export interface BootMetadata {
  bootId: string;
  bootLogSha256: string;
  resetReason: string;
  chipModel: "ESP32-S3";
  chipRevision: number;
  cpuCores: 2;
  psramBytes: number;
  flashBytes: number;
}

export interface CounterMetadata {
  source: "xtensa-ccount";
  bits: 32;
  hz: number;
  core: 0 | 1;
}

export interface CachePerformanceCounters {
  ibus: {
    accesses: number;
    misses: number;
  };
  dbus: {
    accesses: number;
    flashMisses: number;
    psramMisses: number;
  };
}

export interface CcountSample {
  ordinal: number;
  startCore: 0 | 1;
  endCore: 0 | 1;
  startCcount: number;
  endCcount: number;
  cycles: number;
  cacheCounters?: CachePerformanceCounters;
}

export interface CcountKernelMeasurement {
  kind: "ccount-kernel";
  kernel: string;
  memoryPath:
    | "internal-to-internal"
    | "psram-to-internal"
    | "internal-to-psram"
    | "psram-to-psram"
    | "flash-to-internal"
    | "other";
  bytesPerIteration: number;
  iterationsPerSample: number;
  warmupIterations: number;
  samples: CcountSample[];
}

export interface CcountPanelMeasurement {
  kind: "ccount-panel";
  operation: string;
  width: number;
  height: number;
  bitsPerPixel: number;
  payloadBytes: number;
  stripRows: number;
  transactionsPerSample: number;
  warmupFrames: number;
  samples: CcountSample[];
}

export type CalibrationMeasurement = CcountKernelMeasurement | CcountPanelMeasurement;

export interface CalibrationReceipt {
  schemaVersion: typeof CALIBRATION_RECEIPT_SCHEMA_VERSION;
  receiptKind: "esp32s3-hardware-calibration";
  captureMode: CaptureMode;
  capturedAt: string;
  git: GitMetadata;
  toolchain: ToolchainMetadata;
  sdkconfig: SdkconfigMetadata;
  boot: BootMetadata;
  counter: CounterMetadata;
  measurement: CalibrationMeasurement;
}

export interface ParseCalibrationReceiptOptions {
  /** Test fixtures are never accepted by the production parser unless explicitly enabled. */
  allowSchemaFixtures?: boolean;
}

export class CalibrationReceiptError extends Error {
  readonly issues: readonly string[];

  constructor(issues: readonly string[]) {
    super(`invalid calibration receipt:\n${issues.map((issue) => `- ${issue}`).join("\n")}`);
    this.name = "CalibrationReceiptError";
    this.issues = issues;
  }
}

type JsonObject = Record<string, unknown>;

const UINT32_MAX = 0xffff_ffff;
const UINT32_MODULUS = 0x1_0000_0000;
const SHA256_PATTERN = /^[0-9a-f]{64}$/;
const GIT_COMMIT_PATTERN = /^(?:[0-9a-f]{40}|[0-9a-f]{64})$/;
const ISO_UTC_PATTERN = /^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(?:\.(\d{1,3}))?Z$/;

function isIsoUtcTimestamp(value: string): boolean {
  const match = ISO_UTC_PATTERN.exec(value);
  if (!match) return false;
  const parsed = Date.parse(value);
  if (Number.isNaN(parsed)) return false;
  const canonical = `${match[1]}.${(match[2] ?? "").padEnd(3, "0")}Z`;
  return new Date(parsed).toISOString() === canonical;
}

function asObject(value: unknown, path: string, issues: string[]): JsonObject | null {
  if (value === null || typeof value !== "object" || Array.isArray(value)) {
    issues.push(`${path} must be an object`);
    return null;
  }
  return value as JsonObject;
}

function exactKeys(
  object: JsonObject,
  path: string,
  required: readonly string[],
  issues: string[],
  optional: readonly string[] = [],
): void {
  const allowed = new Set([...required, ...optional]);
  for (const key of required) {
    if (!Object.hasOwn(object, key)) {
      issues.push(`${path}.${key} is required`);
    }
  }
  for (const key of Object.keys(object)) {
    if (!allowed.has(key)) {
      issues.push(`${path}.${key} is not allowed`);
    }
  }
}

function validateCacheCounters(value: unknown, path: string, issues: string[]): void {
  const counters = asObject(value, path, issues);
  if (!counters) return;
  exactKeys(counters, path, ["ibus", "dbus"], issues);

  const ibus = asObject(counters.ibus, `${path}.ibus`, issues);
  if (ibus) {
    exactKeys(ibus, `${path}.ibus`, ["accesses", "misses"], issues);
    requireInteger(ibus.accesses, `${path}.ibus.accesses`, issues, 0, UINT32_MAX);
    requireInteger(ibus.misses, `${path}.ibus.misses`, issues, 0, UINT32_MAX);
  }

  const dbus = asObject(counters.dbus, `${path}.dbus`, issues);
  if (dbus) {
    exactKeys(
      dbus,
      `${path}.dbus`,
      ["accesses", "flashMisses", "psramMisses"],
      issues,
    );
    requireInteger(dbus.accesses, `${path}.dbus.accesses`, issues, 0, UINT32_MAX);
    requireInteger(dbus.flashMisses, `${path}.dbus.flashMisses`, issues, 0, UINT32_MAX);
    requireInteger(dbus.psramMisses, `${path}.dbus.psramMisses`, issues, 0, UINT32_MAX);
  }
}

function requireString(value: unknown, path: string, issues: string[]): value is string {
  if (typeof value !== "string" || value.length === 0) {
    issues.push(`${path} must be a non-empty string`);
    return false;
  }
  return true;
}

function requireBoolean(value: unknown, path: string, issues: string[]): value is boolean {
  if (typeof value !== "boolean") {
    issues.push(`${path} must be a boolean`);
    return false;
  }
  return true;
}

function requireInteger(
  value: unknown,
  path: string,
  issues: string[],
  minimum: number,
  maximum = Number.MAX_SAFE_INTEGER,
): value is number {
  if (!Number.isSafeInteger(value) || (value as number) < minimum || (value as number) > maximum) {
    issues.push(`${path} must be an integer from ${minimum} through ${maximum}`);
    return false;
  }
  return true;
}

function requireEnum<T extends string | number>(
  value: unknown,
  path: string,
  allowed: readonly T[],
  issues: string[],
): value is T {
  if (!allowed.includes(value as T)) {
    issues.push(`${path} must be one of ${allowed.map(String).join(", ")}`);
    return false;
  }
  return true;
}

function requireNullablePositiveInteger(value: unknown, path: string, issues: string[]): void {
  if (value !== null) {
    requireInteger(value, path, issues, 1);
  }
}

function validateGit(value: unknown, issues: string[]): void {
  const object = asObject(value, "$.git", issues);
  if (!object) return;
  exactKeys(object, "$.git", ["repository", "commit", "dirty"], issues);
  requireString(object.repository, "$.git.repository", issues);
  if (requireString(object.commit, "$.git.commit", issues) && !GIT_COMMIT_PATTERN.test(object.commit)) {
    issues.push("$.git.commit must be a lowercase 40- or 64-hex object id");
  }
  requireBoolean(object.dirty, "$.git.dirty", issues);
}

function validateToolchain(value: unknown, issues: string[]): void {
  const object = asObject(value, "$.toolchain", issues);
  if (!object) return;
  exactKeys(
    object,
    "$.toolchain",
    ["target", "espIdfVersion", "compiler", "compilerVersion"],
    issues,
  );
  requireEnum(object.target, "$.toolchain.target", ["esp32s3"], issues);
  requireString(object.espIdfVersion, "$.toolchain.espIdfVersion", issues);
  requireString(object.compiler, "$.toolchain.compiler", issues);
  requireString(object.compilerVersion, "$.toolchain.compilerVersion", issues);
}

function validateSdkconfig(value: unknown, issues: string[]): void {
  const object = asObject(value, "$.sdkconfig", issues);
  if (!object) return;
  exactKeys(
    object,
    "$.sdkconfig",
    ["path", "sha256", "cpuHz", "psramMode", "psramBusHz", "flashMode", "flashBusHz"],
    issues,
  );
  requireString(object.path, "$.sdkconfig.path", issues);
  if (requireString(object.sha256, "$.sdkconfig.sha256", issues) && !SHA256_PATTERN.test(object.sha256)) {
    issues.push("$.sdkconfig.sha256 must be a lowercase SHA-256 digest");
  }
  requireInteger(object.cpuHz, "$.sdkconfig.cpuHz", issues, 1);
  requireString(object.psramMode, "$.sdkconfig.psramMode", issues);
  requireNullablePositiveInteger(object.psramBusHz, "$.sdkconfig.psramBusHz", issues);
  requireString(object.flashMode, "$.sdkconfig.flashMode", issues);
  requireNullablePositiveInteger(object.flashBusHz, "$.sdkconfig.flashBusHz", issues);
}

function validateBoot(value: unknown, issues: string[]): void {
  const object = asObject(value, "$.boot", issues);
  if (!object) return;
  exactKeys(
    object,
    "$.boot",
    [
      "bootId",
      "bootLogSha256",
      "resetReason",
      "chipModel",
      "chipRevision",
      "cpuCores",
      "psramBytes",
      "flashBytes",
    ],
    issues,
  );
  requireString(object.bootId, "$.boot.bootId", issues);
  if (
    requireString(object.bootLogSha256, "$.boot.bootLogSha256", issues) &&
    !SHA256_PATTERN.test(object.bootLogSha256)
  ) {
    issues.push("$.boot.bootLogSha256 must be a lowercase SHA-256 digest");
  }
  requireString(object.resetReason, "$.boot.resetReason", issues);
  requireEnum(object.chipModel, "$.boot.chipModel", ["ESP32-S3"], issues);
  requireInteger(object.chipRevision, "$.boot.chipRevision", issues, 0);
  requireEnum(object.cpuCores, "$.boot.cpuCores", [2], issues);
  requireInteger(object.psramBytes, "$.boot.psramBytes", issues, 0);
  requireInteger(object.flashBytes, "$.boot.flashBytes", issues, 1);
}

function validateCounter(value: unknown, issues: string[]): void {
  const object = asObject(value, "$.counter", issues);
  if (!object) return;
  exactKeys(object, "$.counter", ["source", "bits", "hz", "core"], issues);
  requireEnum(object.source, "$.counter.source", ["xtensa-ccount"], issues);
  requireEnum(object.bits, "$.counter.bits", [32], issues);
  requireInteger(object.hz, "$.counter.hz", issues, 1);
  requireEnum(object.core, "$.counter.core", [0, 1], issues);
}

function validateSamples(value: unknown, path: string, issues: string[]): void {
  if (!Array.isArray(value) || value.length === 0) {
    issues.push(`${path} must be a non-empty array`);
    return;
  }
  value.forEach((sampleValue, index) => {
    const samplePath = `${path}[${index}]`;
    const sample = asObject(sampleValue, samplePath, issues);
    if (!sample) return;
    exactKeys(
      sample,
      samplePath,
      ["ordinal", "startCore", "endCore", "startCcount", "endCcount", "cycles"],
      issues,
      ["cacheCounters"],
    );
    const { ordinal, startCore, endCore, startCcount, endCcount, cycles } = sample;
    const ordinalOk = requireInteger(ordinal, `${samplePath}.ordinal`, issues, 0);
    const startCoreOk = requireEnum(startCore, `${samplePath}.startCore`, [0, 1], issues);
    const endCoreOk = requireEnum(endCore, `${samplePath}.endCore`, [0, 1], issues);
    const startOk = requireInteger(startCcount, `${samplePath}.startCcount`, issues, 0, UINT32_MAX);
    const endOk = requireInteger(endCcount, `${samplePath}.endCcount`, issues, 0, UINT32_MAX);
    const cyclesOk = requireInteger(cycles, `${samplePath}.cycles`, issues, 1, UINT32_MAX);
    if (sample.cacheCounters !== undefined) {
      validateCacheCounters(sample.cacheCounters, `${samplePath}.cacheCounters`, issues);
    }
    if (ordinalOk && ordinal !== index) {
      issues.push(`${samplePath}.ordinal must equal its zero-based array index ${index}`);
    }
    if (startCoreOk && endCoreOk && startCore !== endCore) {
      issues.push(`${samplePath} crossed CPU cores and cannot produce a valid CCOUNT delta`);
    }
    if (startOk && endOk && cyclesOk) {
      const expected = (endCcount - startCcount + UINT32_MODULUS) % UINT32_MODULUS;
      if (cycles !== expected) {
        issues.push(`${samplePath}.cycles must equal the unsigned 32-bit CCOUNT delta ${expected}`);
      }
    }
  });
  const counterSamples = value.filter(
    (sampleValue) =>
      sampleValue !== null &&
      typeof sampleValue === "object" &&
      !Array.isArray(sampleValue) &&
      Object.hasOwn(sampleValue, "cacheCounters"),
  ).length;
  if (counterSamples !== 0 && counterSamples !== value.length) {
    issues.push(`${path}.cacheCounters must be present on every sample or absent from every sample`);
  }
}

function validateKernelMeasurement(object: JsonObject, issues: string[]): void {
  exactKeys(
    object,
    "$.measurement",
    [
      "kind",
      "kernel",
      "memoryPath",
      "bytesPerIteration",
      "iterationsPerSample",
      "warmupIterations",
      "samples",
    ],
    issues,
  );
  requireString(object.kernel, "$.measurement.kernel", issues);
  requireEnum(
    object.memoryPath,
    "$.measurement.memoryPath",
    [
      "internal-to-internal",
      "psram-to-internal",
      "internal-to-psram",
      "psram-to-psram",
      "flash-to-internal",
      "other",
    ],
    issues,
  );
  requireInteger(object.bytesPerIteration, "$.measurement.bytesPerIteration", issues, 0);
  requireInteger(object.iterationsPerSample, "$.measurement.iterationsPerSample", issues, 1);
  requireInteger(object.warmupIterations, "$.measurement.warmupIterations", issues, 0);
  validateSamples(object.samples, "$.measurement.samples", issues);
}

function validatePanelMeasurement(object: JsonObject, issues: string[]): void {
  exactKeys(
    object,
    "$.measurement",
    [
      "kind",
      "operation",
      "width",
      "height",
      "bitsPerPixel",
      "payloadBytes",
      "stripRows",
      "transactionsPerSample",
      "warmupFrames",
      "samples",
    ],
    issues,
  );
  requireString(object.operation, "$.measurement.operation", issues);
  const { width, height, bitsPerPixel, payloadBytes, stripRows, transactionsPerSample } = object;
  const widthOk = requireInteger(width, "$.measurement.width", issues, 1);
  const heightOk = requireInteger(height, "$.measurement.height", issues, 1);
  const bitsOk = requireInteger(bitsPerPixel, "$.measurement.bitsPerPixel", issues, 1);
  const payloadOk = requireInteger(payloadBytes, "$.measurement.payloadBytes", issues, 1);
  const stripOk = requireInteger(stripRows, "$.measurement.stripRows", issues, 1);
  const transactionsOk = requireInteger(
    transactionsPerSample,
    "$.measurement.transactionsPerSample",
    issues,
    1,
  );
  requireInteger(object.warmupFrames, "$.measurement.warmupFrames", issues, 0);
  validateSamples(object.samples, "$.measurement.samples", issues);

  if (widthOk && heightOk && bitsOk && payloadOk) {
    const payloadBits = width * height * bitsPerPixel;
    if (!Number.isSafeInteger(payloadBits) || payloadBits % 8 !== 0) {
      issues.push("$.measurement dimensions and bitsPerPixel must describe a whole-byte safe payload");
    } else if (payloadBytes !== payloadBits / 8) {
      issues.push(`$.measurement.payloadBytes must equal width * height * bitsPerPixel / 8 (${payloadBits / 8})`);
    }
  }
  if (heightOk && stripOk && transactionsOk) {
    const expectedTransactions = Math.ceil(height / stripRows);
    if (transactionsPerSample !== expectedTransactions) {
      issues.push(
        `$.measurement.transactionsPerSample must equal ceil(height / stripRows) (${expectedTransactions})`,
      );
    }
  }
}

function validateMeasurement(value: unknown, issues: string[]): void {
  const object = asObject(value, "$.measurement", issues);
  if (!object) return;
  if (object.kind === "ccount-kernel") {
    validateKernelMeasurement(object, issues);
  } else if (object.kind === "ccount-panel") {
    validatePanelMeasurement(object, issues);
  } else {
    issues.push("$.measurement.kind must be one of ccount-kernel, ccount-panel");
  }
}

export function validateCalibrationReceipt(value: unknown): string[] {
  const issues: string[] = [];
  const object = asObject(value, "$", issues);
  if (!object) return issues;
  exactKeys(
    object,
    "$",
    [
      "schemaVersion",
      "receiptKind",
      "captureMode",
      "capturedAt",
      "git",
      "toolchain",
      "sdkconfig",
      "boot",
      "counter",
      "measurement",
    ],
    issues,
  );
  requireEnum(object.schemaVersion, "$.schemaVersion", [CALIBRATION_RECEIPT_SCHEMA_VERSION], issues);
  requireEnum(object.receiptKind, "$.receiptKind", ["esp32s3-hardware-calibration"], issues);
  requireEnum(object.captureMode, "$.captureMode", ["hardware", "schema-fixture"], issues);
  if (requireString(object.capturedAt, "$.capturedAt", issues)) {
    if (!isIsoUtcTimestamp(object.capturedAt)) {
      issues.push("$.capturedAt must be an ISO-8601 UTC timestamp");
    }
  }
  validateGit(object.git, issues);
  validateToolchain(object.toolchain, issues);
  validateSdkconfig(object.sdkconfig, issues);
  validateBoot(object.boot, issues);
  validateCounter(object.counter, issues);
  validateMeasurement(object.measurement, issues);

  const sdkconfig = asObject(object.sdkconfig, "$.sdkconfig", []);
  const counter = asObject(object.counter, "$.counter", []);
  if (
    sdkconfig &&
    counter &&
    Number.isSafeInteger(sdkconfig.cpuHz) &&
    Number.isSafeInteger(counter.hz) &&
    sdkconfig.cpuHz !== counter.hz
  ) {
    issues.push("$.counter.hz must equal $.sdkconfig.cpuHz for Xtensa CCOUNT receipts");
  }
  const measurement = asObject(object.measurement, "$.measurement", []);
  if (counter && measurement && (counter.core === 0 || counter.core === 1) && Array.isArray(measurement.samples)) {
    measurement.samples.forEach((sampleValue, index) => {
      const sample = asObject(sampleValue, `$.measurement.samples[${index}]`, []);
      if (sample && (sample.startCore === 0 || sample.startCore === 1) && sample.startCore !== counter.core) {
        issues.push(`$.measurement.samples[${index}].startCore must equal $.counter.core`);
      }
    });
  }
  return issues;
}

export function parseCalibrationReceiptValue(
  value: unknown,
  options: ParseCalibrationReceiptOptions = {},
): CalibrationReceipt {
  const issues = validateCalibrationReceipt(value);
  const root = value as JsonObject;
  if (
    issues.length === 0 &&
    root.captureMode === "schema-fixture" &&
    options.allowSchemaFixtures !== true
  ) {
    issues.push("$.captureMode schema-fixture is not accepted for hardware calibration");
  }
  if (issues.length === 0 && root.captureMode === "hardware") {
    const measurement = root.measurement as JsonObject;
    const samples = measurement.samples as unknown[];
    if (samples.length < 100) {
      issues.push("$.measurement.samples must contain at least 100 samples for hardware calibration");
    }
  }
  if (issues.length > 0) {
    throw new CalibrationReceiptError(issues);
  }
  return value as CalibrationReceipt;
}

export function parseCalibrationReceipt(
  json: string,
  options: ParseCalibrationReceiptOptions = {},
): CalibrationReceipt {
  let value: unknown;
  try {
    value = JSON.parse(json);
  } catch (error) {
    const detail = error instanceof Error ? error.message : String(error);
    throw new CalibrationReceiptError([`$ is not valid JSON: ${detail}`]);
  }
  return parseCalibrationReceiptValue(value, options);
}
