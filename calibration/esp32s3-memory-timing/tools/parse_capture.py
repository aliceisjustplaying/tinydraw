#!/usr/bin/env python3
"""Extract CAL_RECORD lines and derive stable statistics from a serial capture."""

import argparse
import datetime as dt
import hashlib
import json
import math
import pathlib
import statistics


def percentile(values: list[int], fraction: float) -> float:
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return float(ordered[lower])
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


parser = argparse.ArgumentParser()
parser.add_argument("capture", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
parser.add_argument("--firmware-binary", type=pathlib.Path, required=True)
parser.add_argument("--firmware-source-commit", required=True)
args = parser.parse_args()

records = []
for line in args.capture.read_text(errors="replace").splitlines():
    marker = "CAL_RECORD "
    if marker in line:
        records.append(json.loads(line.split(marker, 1)[1]))

configs = [record for record in records if record.get("type") == "configuration"]
metrics = [record for record in records if record.get("type") == "metric"]
if len(configs) != 1 or not metrics:
    raise SystemExit(f"expected one configuration and metrics, got {len(configs)} and {len(metrics)}")

config = configs[0]
by_name = {metric["name"]: metric for metric in metrics}
cpu_hz = config["ccount_hz"]
for metric in metrics:
    samples = metric["ccount_samples"]
    operations = metric["operations_per_trial"]
    effective_samples = list(samples)
    baseline_name = metric["baseline"]
    if baseline_name:
        baseline = by_name[baseline_name]
        baseline_samples = baseline["ccount_samples"]
        if len(samples) != len(baseline_samples):
            raise SystemExit(f"sample count mismatch for {metric['name']} and {baseline_name}")
        effective_samples = [sample - base for sample, base in zip(samples, baseline_samples)]
    cycles_per_operation_samples = [sample / operations for sample in effective_samples]
    cycles_per_operation = statistics.median(cycles_per_operation_samples)
    metric["statistics"] = {
        "min_total_cycles": min(samples),
        "median_total_cycles": statistics.median(samples),
        "p90_total_cycles": percentile(samples, 0.9),
        "max_total_cycles": max(samples),
        "cycles_per_operation_samples": cycles_per_operation_samples,
        "min_cycles_per_operation": min(cycles_per_operation_samples),
        "median_cycles_per_operation": cycles_per_operation,
        "p90_cycles_per_operation": percentile(cycles_per_operation_samples, 0.9),
        "max_cycles_per_operation": max(cycles_per_operation_samples),
        "population_stdev_cycles_per_operation": statistics.pstdev(cycles_per_operation_samples),
        "median_nanoseconds_per_operation": cycles_per_operation * 1e9 / cpu_hz,
        "median_mib_per_second": (
            metric["bytes_per_operation"] * cpu_hz / cycles_per_operation / (1024 * 1024)
            if metric["bytes_per_operation"] and cycles_per_operation > 0
            else None
        ),
        "relative_sample_range": (
            max(cycles_per_operation_samples) - min(cycles_per_operation_samples)
        ) / cycles_per_operation,
    }

result = {
    "$schema": "../../../schema/result-v1.schema.json",
    "schema_version": config["schema_version"],
    "captured_at_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
    "source_capture": args.capture.name,
    "firmware_binary_sha256": hashlib.sha256(args.firmware_binary.read_bytes()).hexdigest(),
    "firmware_source_commit": args.firmware_source_commit,
    "configuration": config,
    "metrics": metrics,
}
args.output.write_text(json.dumps(result, indent=2) + "\n")
print(f"wrote {args.output} with {len(metrics)} metrics")
