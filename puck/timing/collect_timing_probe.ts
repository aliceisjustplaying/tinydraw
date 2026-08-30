import { existsSync } from "node:fs";
import { mkdir } from "node:fs/promises";
import { join } from "node:path";

import { assembleTimingProbeReceipts } from "./timing_probe_protocol";

const [logPath, outputDirectory] = Bun.argv.slice(2);
if (!logPath || !outputDirectory) {
  console.error(
    JSON.stringify({
      ok: false,
      issues: ["usage: bun collect_timing_probe.ts <serial-capture.log> <receipt-directory>"],
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
  const receipts = assembleTimingProbeReceipts(log, {
    capturedAt: new Date(file.lastModified).toISOString(),
    bootLogSha256,
  });
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
    }),
  );
} catch (error) {
  console.error(JSON.stringify({ ok: false, issues: [String(error)] }));
  process.exit(1);
}
