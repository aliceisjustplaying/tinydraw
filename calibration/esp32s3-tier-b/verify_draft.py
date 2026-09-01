#!/usr/bin/env python3
"""Static review gate for the Tier-B probe draft."""

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
manifest = json.loads((ROOT / "probe-cells.json").read_text())
source = (ROOT / "main" / "tier_b_probe.cpp").read_text()
readme = (ROOT / "README.md").read_text()
capture = (ROOT.parents[1] / "tools" / "tier-b-capture.py").read_text()
validator = (ROOT.parents[1] / "tools" / "tier_b_ndjson.py").read_text()
component = (ROOT / "main" / "CMakeLists.txt").read_text()
elf_gate = (ROOT / "verify_elf.py").read_text()
assembly = (ROOT / "main" / "instruction_ladders.S").read_text()

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
assert manifest["protocolVersion"] == 2
assert all(cell["samples"] > 0 for cell in cells)
assert all(set(cell["variants"]) <= {"normal", "xip-psram"} for cell in cells)
gpio = next(cell for cell in cells if cell["id"] == "gpio21_edge")
assert gpio["status"] == "open-refusal"

table_ids = re.findall(r'CELL\("([a-z0-9_]+)"', source)
assert table_ids == ids, "C++ cell table must match probe-cells.json in order"
assert "TIER_B_SELECT" in source, "runtime selective cohort command is missing"
assert "xTaskCreatePinnedToCore" in source, "cross-core probes must be structurally dual-core"
assert "ESP_CACHE_MSYNC_FLAG_DIR_C2M" in source, "writeback probes are missing"
assert "ESP_CACHE_MSYNC_FLAG_DIR_M2C" in source, "clean invalidation probe is missing"
assert "ESP_CACHE_MSYNC_FLAG_INVALIDATE" in source, "invalidate probes are missing"
assert "GPIO21 electrical edge timestamp is unavailable" in source
assert "SPI2_HOST" in source and "esp_async_memcpy" in source, "DMA/SPI2 probes are missing"
assert "spi_config.spics_io_num = GPIO_NUM_NC" in source, "raw SPI must not select the panel"
assert "tier_b_store_issue_block" in source and "baseline_cycles" in source
assert "entry a1, 16" in assembly and "retw.n" in assembly
assert ".begin no-transform" in assembly and ".end no-transform" in assembly
assert re.search(r"^\s*ret\.n\s*$", assembly, re.MULTILINE) is None
assert "tier_b_first_line_i_0" in source and "fresh one-line" in source
assert "aggressor_iterations" in source and "runtime evidence failed" in source
assert "g_aggressor_active" in source
assert "struct AggressorReport" in source and "isolatedAttributionCounters" in source
assert "g_aggressor_report" in source and "g_aggressor_iterations" not in source
assert "kAttributionIterations = 128" in source
assert "validate_attribution" in source and "clear_cache_counters();" in source
assert "isolated flash attribution" in validator
assert "isolated PSRAM attribution" in validator
assert "internal attribution has external data-cache traffic" in validator
assert "aggressor runtime fields must appear together" in validator
assert "aggressorCore" not in source and "aggressorCore" not in validator
assert "EXTMEM_DBUS_TO_FLASH_START_VADDR_REG" in source
assert "EXTMEM_DBUS_TO_FLASH_END_VADDR_REG" in source
assert 'asm volatile("memw"' in source
assert "configure_dbus_flash_classifier" in source and "ranges_equal" in source
assert '#error "Tier-B flash attribution requires CONFIG_SPIRAM_RODATA disabled"' in source
assert "verify_spiram_rodata_disabled" in elf_gate
assert "verify_flash_pool" in elf_gate and '"xipPsram": False' in elf_gate
for probe in ("probe_arbitration", "probe_cross_core_bandwidth"):
    body_start = source.index(f"Sample {probe}")
    body_end = source.find("\nSample ", body_start + 1)
    if body_end < 0:
        body_end = source.index("\n#define CELL", body_start + 1)
    body = source[body_start:body_end]
    timed_start = body.index("const std::uint32_t start = read_ccount();")
    victim = body.index("const std::uint32_t sum = read_stride", timed_start)
    timed_end = body.index("const std::uint32_t end = read_ccount();", victim)
    counters = body.index("const CacheCounters counters = read_cache_counters();", timed_end)
    report = body.index("const AggressorReport aggressor = stop_aggressor();", counters)
    assert timed_start < victim < timed_end < counters < report
    assert "g_aggressor" not in body[timed_start:timed_end]
assert "(!cold && counters.ibus_misses != 0)" in source
assert "mmu_psram_check_ptr_addr_in_xip_psram_instruction_region" in source
assert "kExpanderPoweredDown" in source and "kExpectedIdentity" in source
for field in (
    "gitCommit",
    "gitDirty",
    "sdkconfigSha256",
    "compilerVersion",
    "elfSha256",
    "dbusFlashClassifier",
    "spiramRodata",
    "chipRevision",
    "bootId",
):
    assert field in source, f"runtime receipt field {field} is missing"
assert capture.index("READY in line") < capture.index("device.write")
assert "done_at + args.tail_s" in capture
assert "write_receipt" in capture and "--preflight" in capture
assert "fixture ELF preflight cannot authorize capture" in capture
assert "--elf" in capture and "archived_elf" in capture
assert 'payload["manifestSha256"] != manifest_sha256' in capture
assert 'payload["dbusFlashClassifier"]' in capture
assert 'payload["spiramRodata"] is not False' in capture
store_start = source.index("Sample probe_store_hit")
store_end = source.index("\nSample probe_writeback", store_start)
store_body = source[store_start:store_end]
baseline_call = store_body.index("tier_b_store_issue_block(internal_word, ordinal + 1);")
baseline_end = store_body.index("const std::uint32_t baseline_end = read_ccount();")
baseline_counters = store_body.index("const CacheCounters baseline_counters", baseline_end)
psram_call = store_body.index("tier_b_store_issue_block(psram_word, ordinal + 1);")
psram_end = store_body.index("const std::uint32_t end = read_ccount();", psram_call)
psram_counters = store_body.index("const CacheCounters counters", psram_end)
assert baseline_call < baseline_end < baseline_counters
assert psram_call < psram_end < psram_counters
range_start = source.index("Sample call_instruction_range")
range_end = source.index("\nSample probe_instruction_psram", range_start)
range_body = source[range_start:range_end]
range_call = range_body.index("range.function();")
range_cycle_end = range_body.index("const std::uint32_t end = read_ccount();", range_call)
range_counters = range_body.index("const CacheCounters counters", range_cycle_end)
assert range_call < range_cycle_end < range_counters
first_line_start = source.index("Sample probe_first_line_i_flash")
first_line_end = source.index("\nSample probe_first_line_data", first_line_start)
first_line_body = source[first_line_start:first_line_end]
first_line_call = first_line_body.index("function();")
first_line_cycle_end = first_line_body.index(
    "const std::uint32_t cycle_end = read_ccount();", first_line_call
)
first_line_counters = first_line_body.index(
    "const CacheCounters counters", first_line_cycle_end
)
assert first_line_call < first_line_cycle_end < first_line_counters
assert "RESTART_MARKERS" in capture and "restart_marker(line, selection_sent)" in capture
assert 'REQUIRED_IDF_VERSION = "v6.1"' in validator
assert 'IDF_VERSION}" STREQUAL "6.1.0"' in component
assert (ROOT / "verify_elf.py").exists()
assert "TINYDRAW_TIER_B_XIP_PSRAM" in (ROOT / "CMakeLists.txt").read_text()
assert "CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y" in (
    ROOT / "sdkconfig.xip-psram.defaults"
).read_text()
assert "draft" in readme.lower() and "unmeasured" in readme.lower()
assert "serial" in readme.lower() and "flash" in readme.lower()
assert "`.idf-version` remains `v6.0.2`" in readme
assert readme.count('flash" v6.1') == 4
print(f"tier-b draft verified: {len(ids)} cells across {len(families)} families")
