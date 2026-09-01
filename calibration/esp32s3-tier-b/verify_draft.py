#!/usr/bin/env python3
"""Static review gate for the Tier-B probe draft."""

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
manifest = json.loads((ROOT / "probe-cells.json").read_text())
source = (ROOT / "main" / "tier_b_probe.cpp").read_text()
readme = (ROOT / "README.md").read_text()

required_families = {
    "arbitration-aggressors",
    "store-hit",
    "writeback-ladders",
    "instruction-psram",
    "first-line-pooling",
    "display-path",
}
cells = manifest["cells"]
ids = [cell["id"] for cell in cells]
families = {cell["family"] for cell in cells}
assert len(ids) == len(set(ids)), "probe cell IDs must be unique"
assert families == required_families, f"unexpected family coverage: {sorted(families)}"

table_ids = re.findall(r'CELL\("([a-z0-9_]+)"', source)
assert table_ids == ids, "C++ cell table must match probe-cells.json in order"
assert "TIER_B_SELECT" in source, "runtime selective cohort command is missing"
assert "xTaskCreatePinnedToCore" in source, "cross-core probes must be structurally dual-core"
assert "ESP_CACHE_MSYNC_FLAG_DIR_C2M" in source, "writeback probes are missing"
assert "ESP_CACHE_MSYNC_FLAG_INVALIDATE" in source, "invalidate probes are missing"
assert "GPIO_NUM_21" in source, "touch interrupt edge probe is missing"
assert "SPI2_HOST" in source and "esp_async_memcpy" in source, "DMA/SPI2 probes are missing"
assert "TINYDRAW_TIER_B_XIP_PSRAM" in (ROOT / "CMakeLists.txt").read_text()
assert "CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y" in (
    ROOT / "sdkconfig.xip-psram.defaults"
).read_text()
assert "draft" in readme.lower() and "unmeasured" in readme.lower()
assert "serial" in readme.lower() and "flash" in readme.lower()
print(f"tier-b draft verified: {len(ids)} cells across {len(families)} families")
