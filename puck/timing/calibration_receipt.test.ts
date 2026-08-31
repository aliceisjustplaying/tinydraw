import { describe, expect, test } from "bun:test";
import { join } from "node:path";

import {
  CalibrationReceiptError,
  parseCalibrationReceipt,
  parseCalibrationReceiptValue,
  validateCalibrationReceipt,
} from "./calibration_receipt";

const fixtures = join(import.meta.dir, "fixtures");

async function fixture(path: string): Promise<string> {
  return Bun.file(join(fixtures, path)).text();
}

describe("calibration receipt golden fixtures", () => {
  test("accepts the CCOUNT kernel fixture, including a 32-bit wrap", async () => {
    const receipt = parseCalibrationReceipt(await fixture("valid/ccount-kernel.json"), {
      allowSchemaFixtures: true,
    });
    expect(receipt.measurement.kind).toBe("ccount-kernel");
    expect(receipt.measurement.samples[0]?.cycles).toBe(11);
  });

  test("accepts the CCOUNT panel fixture", async () => {
    const receipt = parseCalibrationReceipt(await fixture("valid/ccount-panel.json"), {
      allowSchemaFixtures: true,
    });
    expect(receipt.measurement.kind).toBe("ccount-panel");
  });

  test("rejects a kernel sample whose recorded delta does not match CCOUNT", async () => {
    const json = await fixture("invalid/ccount-kernel-bad-delta.json");
    expect(() =>
      parseCalibrationReceipt(json, {
        allowSchemaFixtures: true,
      }),
    ).toThrow("unsigned 32-bit CCOUNT delta 11");
  });

  test("rejects panel payload and transaction-count drift", async () => {
    try {
      parseCalibrationReceipt(await fixture("invalid/ccount-panel-bad-shape.json"), {
        allowSchemaFixtures: true,
      });
      throw new Error("expected fixture rejection");
    } catch (error) {
      expect(error).toBeInstanceOf(CalibrationReceiptError);
      expect((error as CalibrationReceiptError).issues).toContain(
        "$.measurement.payloadBytes must equal width * height * bitsPerPixel / 8 (8)",
      );
      expect((error as CalibrationReceiptError).issues).toContain(
        "$.measurement.transactionsPerSample must equal ceil(height / stripRows) (2)",
      );
    }
  });

  test("rejects the golden receipt with missing boot provenance", async () => {
    const json = await fixture("invalid/ccount-kernel-missing-boot.json");
    expect(() => parseCalibrationReceipt(json, { allowSchemaFixtures: true })).toThrow(
      "$.boot is required",
    );
  });
});

describe("calibration receipt provenance boundary", () => {
  test("production parsing rejects schema fixtures", async () => {
    const json = await fixture("valid/ccount-kernel.json");
    expect(() => parseCalibrationReceipt(json)).toThrow(
      "schema-fixture is not accepted for hardware calibration",
    );
  });

  test("hardware calibration requires at least 100 raw samples", async () => {
    const candidate = JSON.parse(await fixture("valid/ccount-kernel.json")) as {
      captureMode: string;
    };
    candidate.captureMode = "hardware";
    expect(() => parseCalibrationReceiptValue(candidate)).toThrow(
      "at least 100 samples for hardware calibration",
    );
  });

  test("git, toolchain, sdkconfig, and boot metadata are mandatory", async () => {
    const valid = JSON.parse(await fixture("valid/ccount-kernel.json")) as Record<string, unknown>;
    for (const field of ["git", "toolchain", "sdkconfig", "boot"] as const) {
      const candidate = structuredClone(valid);
      delete candidate[field];
      expect(validateCalibrationReceipt(candidate)).toContain(`$.${field} is required`);
    }
  });

  test("rejects unknown fields at every strict object boundary", async () => {
    const candidate = JSON.parse(await fixture("valid/ccount-kernel.json")) as Record<
      string,
      Record<string, unknown>
    >;
    candidate.git.extra = "not allowed";
    expect(() => parseCalibrationReceiptValue(candidate, { allowSchemaFixtures: true })).toThrow(
      "$.git.extra is not allowed",
    );
  });

  test("requires counter frequency to match captured sdkconfig CPU frequency", async () => {
    const candidate = JSON.parse(await fixture("valid/ccount-kernel.json")) as {
      counter: { hz: number };
    };
    candidate.counter.hz += 1;
    expect(() => parseCalibrationReceiptValue(candidate, { allowSchemaFixtures: true })).toThrow(
      "$.counter.hz must equal $.sdkconfig.cpuHz",
    );
  });

  test("rejects a sample that crosses per-core CCOUNT domains", async () => {
    const candidate = JSON.parse(await fixture("valid/ccount-kernel.json")) as {
      measurement: { samples: Array<{ endCore: number }> };
    };
    candidate.measurement.samples[0]!.endCore = 1;
    expect(() => parseCalibrationReceiptValue(candidate, { allowSchemaFixtures: true })).toThrow(
      "crossed CPU cores",
    );
  });

  test("retains optional ESP32-S3 cache performance counters", async () => {
    const candidate = JSON.parse(await fixture("valid/ccount-kernel.json")) as {
      measurement: { samples: Array<Record<string, unknown>> };
    };
    for (const sample of candidate.measurement.samples) {
      sample.cacheCounters = {
        ibus: { accesses: 43, misses: 2 },
        dbus: { accesses: 10, flashMisses: 0, psramMisses: 0 },
      };
    }
    const receipt = parseCalibrationReceiptValue(candidate, { allowSchemaFixtures: true });
    expect(receipt.measurement.samples[0]!.cacheCounters).toEqual({
      ibus: { accesses: 43, misses: 2 },
      dbus: { accesses: 10, flashMisses: 0, psramMisses: 0 },
    });
  });

  test("requires optional cache counters on the whole measurement", async () => {
    const candidate = JSON.parse(await fixture("valid/ccount-kernel.json")) as {
      measurement: { samples: Array<Record<string, unknown>> };
    };
    candidate.measurement.samples[0]!.cacheCounters = {
      ibus: { accesses: 43, misses: 2 },
      dbus: { accesses: 10, flashMisses: 0, psramMisses: 0 },
    };
    expect(validateCalibrationReceipt(candidate)).toContain(
      "$.measurement.samples.cacheCounters must be present on every sample or absent from every sample",
    );
  });

  test("rejects malformed or out-of-range cache performance counters", async () => {
    const candidate = JSON.parse(await fixture("valid/ccount-kernel.json")) as {
      measurement: { samples: Array<Record<string, unknown>> };
    };
    candidate.measurement.samples[0]!.cacheCounters = {
      ibus: { accesses: 0x1_0000_0000, misses: 2 },
      dbus: { accesses: 10, flashMisses: 0 },
    };
    const issues = validateCalibrationReceipt(candidate);
    expect(issues).toContain(
      "$.measurement.samples[0].cacheCounters.ibus.accesses must be an integer from 0 through 4294967295",
    );
    expect(issues).toContain(
      "$.measurement.samples[0].cacheCounters.dbus.psramMisses is required",
    );
  });

  test("reports malformed JSON as a receipt error", () => {
    expect(() => parseCalibrationReceipt("{")).toThrow(CalibrationReceiptError);
  });

});

describe("machine-readable schema", () => {
  test("declares the same version and provenance sections as the parser", async () => {
    const schema = (await Bun.file(join(import.meta.dir, "calibration-receipt.schema.json")).json()) as {
      properties: { schemaVersion: { const: number } };
      required: string[];
      $defs: {
        ccountSample: { properties: Record<string, unknown> };
        samples: { allOf: unknown[] };
      };
    };
    expect(schema.properties.schemaVersion.const).toBe(1);
    expect(schema.required).toEqual(
      expect.arrayContaining(["git", "toolchain", "sdkconfig", "boot", "counter", "measurement"]),
    );
    expect(schema.$defs.ccountSample.properties).toHaveProperty("cacheCounters");
    expect(schema.$defs.samples.allOf).toHaveLength(1);
  });
});
