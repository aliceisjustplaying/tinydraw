#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
project="$root/calibration/esp32s3-memory-timing"
build="$root/out/build/esp32s3-memory-timing-calibration"
port="${1:?usage: $0 PORT OUTPUT_DIR}"
output_dir="${2:?usage: $0 PORT OUTPUT_DIR}"

mkdir -p "$output_dir"
cd "$project"
eim run "idf.py -B '$build' build"
eim run "idf.py -B '$build' -p '$port' flash"
cd "$root"
uv run --script tools/esp32-capture.py "$port" "$output_dir/serial.log" 90 \
  --end-marker CALIBRATION_DONE \
  --failure-regex 'Guru Meditation|assert failed|CALIBRATION_FAILED|task_wdt'
python3 "$project/tools/parse_capture.py" \
  "$output_dir/serial.log" "$output_dir/result.json" \
  --firmware-binary "$build/esp32s3_memory_timing_calibration.bin"
