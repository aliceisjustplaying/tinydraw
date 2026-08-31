#!/usr/bin/env python3
"""Compare two parsed runs and emit machine-readable reproducibility evidence."""

import argparse
import json
import pathlib


parser = argparse.ArgumentParser()
parser.add_argument("first", type=pathlib.Path)
parser.add_argument("second", type=pathlib.Path)
parser.add_argument("output", type=pathlib.Path)
args = parser.parse_args()

documents = [json.loads(path.read_text()) for path in (args.first, args.second)]
metrics = []
for first_metric in documents[0]["metrics"]:
    second_metric = next(
        metric for metric in documents[1]["metrics"] if metric["name"] == first_metric["name"]
    )
    values = [
        first_metric["statistics"]["median_cycles_per_operation"],
        second_metric["statistics"]["median_cycles_per_operation"],
    ]
    mean = sum(values) / 2
    metrics.append(
        {
            "name": first_metric["name"],
            "median_cycles_per_operation": values,
            "relative_delta": abs(values[1] - values[0]) / mean,
        }
    )

output = {
    "schema_version": "1.0.0",
    "runs": [str(args.first), str(args.second)],
    "maximum_relative_delta": max(metric["relative_delta"] for metric in metrics),
    "metrics": metrics,
}
args.output.write_text(json.dumps(output, indent=2) + "\n")
print(f"wrote {args.output}")
