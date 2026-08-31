import { expect, test } from "bun:test";
import { join } from "node:path";

const sourcePath = join(
  import.meta.dir,
  "../../esp32/main/timing_probe/flash_instruction_bursts_esp32s3.S",
);
const firmwarePath = join(import.meta.dir, "../../esp32/main/timing_probe/timing_probe.cpp");
const componentPath = join(import.meta.dir, "../../esp32/main/CMakeLists.txt");

test("flash instruction burst source pins the call0 CCOUNT boundary", async () => {
  const source = await Bun.file(sourcePath).text();
  const helper = source.slice(
    source.indexOf("tinydraw_measure_flash_instruction_burst_window:"),
    source.indexOf(".size tinydraw_measure_flash_instruction_burst_window"),
  );
  expect(helper.match(/\bcallx0\s+a8\b/g)).toHaveLength(1);
  expect(helper).not.toContain("callx8");
  expect(helper.indexOf("mov         a8, a3")).toBeLessThan(helper.indexOf("rsr.ccount  a4"));
  expect(helper.indexOf("rsr.ccount  a4")).toBeLessThan(helper.indexOf("callx0      a8"));
  expect(helper.indexOf("callx0      a8")).toBeLessThan(helper.indexOf("rsr.ccount  a5"));
  expect(helper.indexOf("rsr.ccount  a5")).toBeLessThan(helper.indexOf("s32i        a4"));
});

test("flash instruction burst source declares exact aligned 1/2/4/8-line targets", async () => {
  const source = await Bun.file(sourcePath).text();
  expect(source).toContain('.section .text.tinydraw_flash_instruction_bursts, "ax"');
  expect(source).toContain(".balign 32");
  expect(source).toContain(".rept ((\\lines - 1) * 16)");
  expect(source).toContain("movi.n      a2, \\lines");
  for (const lines of [1, 2, 4, 8]) {
    const symbol = `tinydraw_flash_instruction_burst_${lines}_lines`;
    expect(source).toContain(
      `DEFINE_FLASH_INSTRUCTION_BURST ${symbol}, ${symbol}_start, ${symbol}_end, ${lines}`,
    );
  }
});

test("firmware routes every hot and cold burst through the dedicated helper sampler", async () => {
  const [source, component] = await Promise.all([
    Bun.file(firmwarePath).text(),
    Bun.file(componentPath).text(),
  ]);
  expect(component).toContain('"timing_probe/flash_instruction_bursts_esp32s3.S"');
  expect(source).toContain(
    "CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE == kIcacheLineBytes",
  );
  expect(source).toContain("tinydraw_measure_flash_instruction_burst_window(&window, range.start)");
  expect(source).toContain("warm_window.sentinel != Lines");
  expect(source).toContain("sample.checksum = window.sentinel");
  expect(source).toContain("sample.start_ccount = window.start_ccount");
  expect(source).toContain("sample.end_ccount = window.end_ccount");
  expect(source).toContain(
    "measure_flash_instruction_burst_once<lines, false>",
  );
  expect(source).toContain(
    "measure_flash_instruction_burst_once<lines, true>",
  );
  for (const lines of [1, 2, 4, 8]) {
    expect(source).toContain(`ICACHE_BURST_MEASUREMENTS(${lines})`);
  }
  expect(source).toMatch(
    /\{"icache_flash_burst_" #lines "_lines_hot",\s*\\?\s*"other",\s*\\?\s*0U/,
  );
  expect(source).toMatch(
    /"icache_flash_burst_" #lines "_lines_cold", "other", 0U/,
  );

  const sampler = source.slice(
    source.indexOf("RawSample IRAM_ATTR NOINLINE_ATTR measure_flash_instruction_burst_once"),
    source.indexOf("void print_measurement_start"),
  );
  expect(sampler).toContain("end - start != Lines * kIcacheLineBytes");
  expect(sampler).toContain("ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE |");
  expect(sampler).toContain("ESP_CACHE_MSYNC_FLAG_TYPE_INST");
  expect(sampler.indexOf("tinydraw_measure_flash_instruction_burst_window(&warm_window"))
    .toBeLessThan(sampler.indexOf("clear_cache_counters()"));
  expect(sampler.indexOf("esp_cache_msync("))
    .toBeLessThan(sampler.indexOf("clear_cache_counters()"));
  expect(sampler).not.toContain("esp_cpu_get_cycle_count");
  expect(sampler).not.toMatch(/reinterpret_cast<[^>]*\(\*\)/);
});
