import { existsSync } from "node:fs";
import { mkdir } from "node:fs/promises";
import { join } from "node:path";

import {
  assembleTimingProbeReceipts,
  recoverCompleteTimingProbeReceipts,
  type OmittedTimingProbeMeasurement,
} from "./timing_probe_protocol";

const recoveryFlag = "--recover-complete-measurements";
const arguments_ = Bun.argv.slice(2);
const unknownOptions = arguments_.filter(
  (argument) => argument.startsWith("--") && argument !== recoveryFlag,
);
const recoverCompleteMeasurements = arguments_.includes(recoveryFlag);
const positionalArguments = arguments_.filter((argument) => argument !== recoveryFlag);
const [logPath, outputDirectory] = positionalArguments;
if (!logPath || !outputDirectory || positionalArguments.length !== 2 || unknownOptions.length > 0) {
  console.error(
    JSON.stringify({
      ok: false,
      issues: [
        `usage: bun collect_timing_probe.ts [${recoveryFlag}] <serial-capture.log> <receipt-directory>`,
      ],
    }),
  );
  process.exit(2);
}

try {
  const file = Bun.file(logPath);
  const log = await file.text();
  const hasher = new Bun.CryptoHasher("sha256");
  hasher.update(log);
  const bootLogSha256 = hasher.digest("hex");
  const assembleOptions = {
    capturedAt: new Date(file.lastModified).toISOString(),
    bootLogSha256,
  };
  let omittedMeasurements: OmittedTimingProbeMeasurement[] = [];
  const receipts = recoverCompleteMeasurements
    ? (() => {
        const recovered = recoverCompleteTimingProbeReceipts(log, assembleOptions);
        omittedMeasurements = recovered.omittedMeasurements;
        return recovered.receipts;
      })()
    : assembleTimingProbeReceipts(log, assembleOptions);
  if (recoverCompleteMeasurements && receipts.length === 0) {
    throw new Error(
      `recovery found no complete valid measurements; omissions: ${JSON.stringify(omittedMeasurements)}`,
    );
  }
  if (existsSync(outputDirectory)) {
    throw new Error(`receipt output directory already exists: ${outputDirectory}`);
  }
  await mkdir(outputDirectory, { recursive: true });
  for (const receipt of receipts) {
    if (receipt.measurement.kind !== "ccount-kernel") {
      throw new Error(`unexpected timing-probe measurement kind ${receipt.measurement.kind}`);
    }
    const filename = `${receipt.measurement.kernel.replace(/[^a-zA-Z0-9_.-]/g, "_")}.json`;
    await Bun.write(join(outputDirectory, filename), `${JSON.stringify(receipt, null, 2)}\n`);
  }
  console.log(
    JSON.stringify({
      ok: true,
      logPath,
      outputDirectory,
      bootLogSha256,
      receipts: receipts.length,
      ...(recoverCompleteMeasurements
        ? { recoveryMode: "complete-measurements", omittedMeasurements }
        : {}),
    }),
  );
} catch (error) {
  console.error(JSON.stringify({ ok: false, issues: [String(error)] }));
  process.exit(1);
}
