import { CalibrationReceiptError, parseCalibrationReceipt } from "./calibration_receipt";

const args = Bun.argv.slice(2);
const allowSchemaFixtures = args.includes("--allow-schema-fixture");
const paths = args.filter((arg) => arg !== "--allow-schema-fixture");

if (paths.length === 0) {
  console.error(
    JSON.stringify({ ok: false, issues: ["usage: bun verify_calibration_receipt.ts <receipt.json>"] }),
  );
  process.exit(2);
}

let failed = false;
for (const path of paths) {
  try {
    const receipt = parseCalibrationReceipt(await Bun.file(path).text(), { allowSchemaFixtures });
    console.log(
      JSON.stringify({
        ok: true,
        path,
        schemaVersion: receipt.schemaVersion,
        captureMode: receipt.captureMode,
        measurementKind: receipt.measurement.kind,
        sampleCount: receipt.measurement.samples.length,
      }),
    );
  } catch (error) {
    failed = true;
    const issues = error instanceof CalibrationReceiptError ? error.issues : [String(error)];
    console.error(JSON.stringify({ ok: false, path, issues }));
  }
}

if (failed) process.exit(1);
