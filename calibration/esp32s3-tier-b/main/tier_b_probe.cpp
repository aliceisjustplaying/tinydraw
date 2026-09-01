/* Review-stage ESP32-S3 Tier-B timing probes. No values from this image are
 * adopted until two clean hardware boots become committed evidence. */

#include <array>
#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_app_desc.h"
#include "esp_async_memcpy.h"
#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_chip_info.h"
#include "esp_cpu.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_private/mmu_psram_flash.h"
#include "esp_private/mspi_timing_config.h"
#include "esp_psram.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/extmem_reg.h"
#include "soc/soc.h"
#include "soc/spi_mem_reg.h"

#if defined(CONFIG_SPIRAM_RODATA) && CONFIG_SPIRAM_RODATA
#error "Tier-B flash attribution requires CONFIG_SPIRAM_RODATA disabled"
#endif
#if !defined(CONFIG_SPIRAM_MODE_OCT) || !CONFIG_SPIRAM_MODE_OCT || \
    !defined(CONFIG_SPIRAM_SPEED_80M) || !CONFIG_SPIRAM_SPEED_80M
#error "Tier-B decomposition controls require 80 MHz octal PSRAM"
#endif

extern "C" {
void tier_b_instruction_1_lines(void);
void tier_b_instruction_2_lines(void);
void tier_b_instruction_4_lines(void);
void tier_b_instruction_8_lines(void);
void tier_b_instruction_16_lines(void);
void tier_b_store_issue_block(volatile std::uint32_t* word, std::uint32_t value);
void tier_b_first_line_i_0(void);
void tier_b_first_line_i_1(void);
void tier_b_first_line_i_2(void);
void tier_b_first_line_i_3(void);
void tier_b_first_line_i_4(void);
extern const std::uint8_t tier_b_instruction_1_lines_start[];
extern const std::uint8_t tier_b_instruction_1_lines_end[];
extern const std::uint8_t tier_b_instruction_2_lines_start[];
extern const std::uint8_t tier_b_instruction_2_lines_end[];
extern const std::uint8_t tier_b_instruction_4_lines_start[];
extern const std::uint8_t tier_b_instruction_4_lines_end[];
extern const std::uint8_t tier_b_instruction_8_lines_start[];
extern const std::uint8_t tier_b_instruction_8_lines_end[];
extern const std::uint8_t tier_b_instruction_16_lines_start[];
extern const std::uint8_t tier_b_instruction_16_lines_end[];
extern const std::uint8_t tier_b_store_issue_block_start[];
extern const std::uint8_t tier_b_store_issue_block_end[];
}

namespace {

constexpr char kPrefix[] = "TINYDRAW_TIER_B_NDJSON ";
constexpr char kHarnessVersion[] = "0.2.0-review";
constexpr char kRequiredIdfVersion[] = "v6.1";
constexpr std::uint32_t kProtocolVersion = 2;
constexpr std::size_t kIcacheLine = 32;
constexpr std::size_t kDcacheLine = 64;
constexpr std::size_t kPsramBytes = 1024 * 1024;
constexpr std::size_t kInternalBytes = 64 * 1024;
constexpr std::size_t kDmaBytes = 32 * 1024;
constexpr std::uint32_t kDefaultSamples = 9;
constexpr std::uint32_t kSweepSamples = 6;
constexpr std::uint32_t kFirstLineSamples = 5;
constexpr std::uint32_t kStoreIterations = 256;
constexpr std::uint32_t kBandwidthBytes = 256 * 1024;
constexpr std::uint32_t kAttributionIterations = 128;
constexpr std::size_t kPsramServiceBytes = 4 * 1024;
constexpr std::uint32_t kPsramFastClockHz = 80 * 1000 * 1000;
constexpr std::uint32_t kPsramSlowClockHz = 40 * 1000 * 1000;
constexpr std::uint32_t kMspiCoreClockHz = 160 * 1000 * 1000;
constexpr std::uint32_t kSpi2SlowClockHz = 20 * 1000 * 1000;
constexpr std::uint32_t kSpi2FastClockHz = 40 * 1000 * 1000;
constexpr std::uint8_t kExpanderOutputs = 0x87;
constexpr std::uint8_t kExpanderPoweredDown = 0x80;

static_assert(CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE == kIcacheLine);
static_assert(CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE == kDcacheLine);

alignas(kDcacheLine) const std::array<std::uint32_t, 64 * 1024> g_flash_pool = [] {
  std::array<std::uint32_t, 64 * 1024> values{};
  for (std::size_t index = 0; index < values.size(); ++index) {
    values[index] = static_cast<std::uint32_t>(index * 2'246'822'519U + 31U);
  }
  return values;
}();
static_assert(sizeof(g_flash_pool) == 0x40000);

struct DbusFlashClassifierRange {
  std::uint32_t start = 0;
  std::uint32_t end = 0;
};

DbusFlashClassifierRange expected_dbus_flash_classifier_range() {
  const auto start = reinterpret_cast<std::uintptr_t>(g_flash_pool.data());
  return {
      .start = static_cast<std::uint32_t>(start),
      .end = static_cast<std::uint32_t>(start + sizeof(g_flash_pool) - 1),
  };
}

DbusFlashClassifierRange read_dbus_flash_classifier_range() {
  return {
      .start = REG_READ(EXTMEM_DBUS_TO_FLASH_START_VADDR_REG),
      .end = REG_READ(EXTMEM_DBUS_TO_FLASH_END_VADDR_REG),
  };
}

DbusFlashClassifierRange configure_dbus_flash_classifier() {
  const DbusFlashClassifierRange expected = expected_dbus_flash_classifier_range();
  REG_WRITE(EXTMEM_DBUS_TO_FLASH_START_VADDR_REG, expected.start);
  REG_WRITE(EXTMEM_DBUS_TO_FLASH_END_VADDR_REG, expected.end);
  asm volatile("memw" ::: "memory");
  return read_dbus_flash_classifier_range();
}

bool ranges_equal(const DbusFlashClassifierRange& left, const DbusFlashClassifierRange& right) {
  return left.start == right.start && left.end == right.end;
}

volatile std::uint32_t g_sink;

struct CacheCounters {
  std::uint32_t ibus_accesses = 0;
  std::uint32_t ibus_misses = 0;
  std::uint32_t dbus_accesses = 0;
  std::uint32_t dbus_flash_misses = 0;
  std::uint32_t dbus_psram_misses = 0;
};

constexpr bool valid_instruction_counters(bool cold, std::uint32_t accesses,
                                          std::uint32_t misses) {
  return cold ? accesses != 0 && misses != 0 : misses == 0;
}

static_assert(valid_instruction_counters(false, 0, 0));
static_assert(valid_instruction_counters(false, 8, 0));
static_assert(!valid_instruction_counters(false, 8, 1));
static_assert(!valid_instruction_counters(true, 0, 0));

enum class Aggressor : std::uint32_t { kInternal, kFlash, kPsram };

struct AttributionEvidence {
  Aggressor source = Aggressor::kInternal;
  CacheCounters counters{};
  std::uint32_t iterations = 0;
  std::uint32_t checksum = 0;
};

struct Sample {
  bool ok = false;
  std::uint32_t cycles = 0;
  std::size_t bytes = 0;
  CacheCounters counters{};
  bool has_baseline = false;
  std::uint32_t baseline_cycles = 0;
  CacheCounters baseline_counters{};
  bool has_attribution = false;
  AttributionEvidence attribution{};
  std::uint32_t aggressor_iterations = 0;
  std::uint32_t aggressor_checksum = 0;
  bool has_msync_factors = false;
  std::size_t dirty_lines = 0;
  std::uint32_t psram_clock_hz = 0;
  std::uint32_t psram_clock_register = 0;
  std::uint32_t psram_core_clock_register = 0;
  std::uint32_t psram_service_cycles = 0;
  CacheCounters psram_service_counters{};
  bool has_spi2_phases = false;
  std::uint32_t spi2_clock_hz = 0;
  std::uint32_t submission_cycles = 0;
  std::uint32_t completion_cycles = 0;
  const char* reason = "probe failed";
  const char* tier_candidate = "exact";
  const char* note = nullptr;
};

struct Context;
struct Cell;
using Probe = Sample (*)(Context&, const Cell&, std::uint32_t);

struct Cell {
  const char* id;
  Probe probe;
  std::uint32_t samples;
  std::uint32_t parameter;
};

struct Context {
  std::uint8_t* psram = nullptr;
  std::uint8_t* internal = nullptr;
  std::uint8_t* dma_source = nullptr;
  std::uint8_t* dma_destination = nullptr;
  i2c_master_bus_handle_t i2c_bus = nullptr;
  i2c_master_dev_handle_t io_expander = nullptr;
  i2c_master_dev_handle_t touch = nullptr;
  esp_lcd_panel_io_handle_t panel_io = nullptr;
  spi_device_handle_t spi = nullptr;
  std::array<spi_device_handle_t, 2> phased_spi{};
  async_memcpy_handle_t gdma = nullptr;
  SemaphoreHandle_t panel_done = nullptr;
  SemaphoreHandle_t gdma_done = nullptr;
};

struct AggressorReport {
  AttributionEvidence attribution{};
  std::uint32_t iterations = 0;
  std::uint32_t checksum = 0;
};

struct AggressorStart {
  bool active = false;
  const char* failure = "core-1 aggressor task creation failed";
  AggressorReport report{};
};

std::atomic<bool> g_aggressor_ready{false};
std::atomic<bool> g_aggressor_start{false};
std::atomic<bool> g_aggressor_stop{false};
std::atomic<bool> g_aggressor_done{false};
std::atomic<bool> g_aggressor_active{false};
AggressorReport g_aggressor_report{};
const char* g_aggressor_failure = nullptr;
Context* g_aggressor_context = nullptr;
Aggressor g_aggressor_kind = Aggressor::kInternal;

inline std::uint32_t read_ccount() {
  std::uint32_t value;
  asm volatile("rsr.ccount %0" : "=a"(value));
  return value;
}

void clear_cache_counters() {
  REG_WRITE(EXTMEM_CACHE_ACS_CNT_CLR_REG, EXTMEM_ICACHE_ACS_CNT_CLR | EXTMEM_DCACHE_ACS_CNT_CLR);
}

CacheCounters read_cache_counters() {
  return {
      .ibus_accesses = REG_READ(EXTMEM_IBUS_ACS_CNT_REG),
      .ibus_misses = REG_READ(EXTMEM_IBUS_ACS_MISS_CNT_REG),
      .dbus_accesses = REG_READ(EXTMEM_DBUS_ACS_CNT_REG),
      .dbus_flash_misses = REG_READ(EXTMEM_DBUS_ACS_FLASH_MISS_CNT_REG),
      .dbus_psram_misses = REG_READ(EXTMEM_DBUS_ACS_SPIRAM_MISS_CNT_REG),
  };
}

constexpr std::uint32_t expected_psram_clock_register(std::uint32_t clock_hz) {
  const std::uint32_t divider = kMspiCoreClockHz / clock_hz;
  return ((divider - 1) << SPI_MEM_SCLKCNT_N_S) |
         ((divider / 2 - 1) << SPI_MEM_SCLKCNT_H_S) |
         ((divider - 1) << SPI_MEM_SCLKCNT_L_S);
}

static_assert(kMspiCoreClockHz % kPsramFastClockHz == 0);
static_assert(kMspiCoreClockHz % kPsramSlowClockHz == 0);
static_assert(expected_psram_clock_register(kPsramFastClockHz) !=
              expected_psram_clock_register(kPsramSlowClockHz));

bool set_psram_service_clock(std::uint32_t clock_hz, std::uint32_t& register_value,
                             std::uint32_t& core_register_value) {
  if (clock_hz != kPsramFastClockHz && clock_hz != kPsramSlowClockHz) return false;
  mspi_timing_config_set_psram_clock(clock_hz / 1000 / 1000,
                                     MSPI_TIMING_SPEED_MODE_NORMAL_PERF, false);
  asm volatile("memw" ::: "memory");
  register_value = REG_READ(SPI_MEM_SRAM_CLK_REG(0));
  core_register_value = REG_GET_FIELD(SPI_MEM_CORE_CLK_SEL_REG(0), SPI_MEM_CORE_CLK_SEL);
  return register_value == expected_psram_clock_register(clock_hz) && core_register_value == 2;
}

Sample timed_result(std::uint32_t start, std::uint32_t end, std::size_t bytes,
                    CacheCounters counters = {}, const char* note = nullptr) {
  if (end == start) {
    return {.reason = "zero CCOUNT delta", .tier_candidate = "exact"};
  }
  return {.ok = true, .cycles = end - start, .bytes = bytes, .counters = counters, .note = note};
}

void emit_cache_counters(const CacheCounters& counters) {
  std::printf("{\"ibusAccesses\":%" PRIu32 ",\"ibusMisses\":%" PRIu32 ",\"dbusAccesses\":%" PRIu32
              ",\"dbusFlashMisses\":%" PRIu32 ",\"dbusPsramMisses\":%" PRIu32 "}",
              counters.ibus_accesses, counters.ibus_misses, counters.dbus_accesses,
              counters.dbus_flash_misses, counters.dbus_psram_misses);
}

const char* attribution_source_name(Aggressor source) {
  switch (source) {
    case Aggressor::kInternal:
      return "internal";
    case Aggressor::kFlash:
      return "flash";
    case Aggressor::kPsram:
      return "psram";
  }
  return "unknown";
}

void emit_attribution(const Sample& sample) {
  if (!sample.has_attribution) return;
  std::printf(",\"attributionSource\":\"%s\",\"isolatedAttributionIterations\":%" PRIu32
              ",\"isolatedAttributionChecksum\":%" PRIu32 ",\"isolatedAttributionCounters\":",
              attribution_source_name(sample.attribution.source), sample.attribution.iterations,
              sample.attribution.checksum);
  emit_cache_counters(sample.attribution.counters);
  if (sample.aggressor_iterations != 0) {
    std::printf(",\"aggressorIterations\":%" PRIu32 ",\"aggressorChecksum\":%" PRIu32,
                sample.aggressor_iterations, sample.aggressor_checksum);
  }
}

void emit_control_evidence(const Sample& sample) {
  if (sample.has_msync_factors) {
    std::printf(
        ",\"dirtyLines\":%zu,\"psramClockHz\":%" PRIu32
        ",\"psramClockRegister\":%" PRIu32 ",\"psramCoreClockRegister\":%" PRIu32
        ",\"psramServiceBytes\":%zu,"
        "\"psramServiceCycles\":%" PRIu32 ",\"psramServiceCounters\":",
        sample.dirty_lines, sample.psram_clock_hz, sample.psram_clock_register,
        sample.psram_core_clock_register, kPsramServiceBytes, sample.psram_service_cycles);
    emit_cache_counters(sample.psram_service_counters);
  }
  if (sample.has_spi2_phases) {
    std::printf(",\"spiClockHz\":%" PRIu32 ",\"submissionCycles\":%" PRIu32
                ",\"completionCycles\":%" PRIu32,
                sample.spi2_clock_hz, sample.submission_cycles, sample.completion_cycles);
  }
}

void emit_sample(const Cell& cell, std::uint32_t ordinal, const Sample& sample) {
  if (!sample.ok) {
    std::printf("%s{\"protocolVersion\":%" PRIu32
                ",\"record\":\"refusal\",\"cell\":\"%s\",\"ordinal\":%" PRIu32
                ",\"reason\":\"%s\",\"tierCandidate\":\"%s\"",
                kPrefix, kProtocolVersion, cell.id, ordinal, sample.reason, sample.tier_candidate);
    emit_attribution(sample);
    std::printf("}\n");
    std::fflush(stdout);
    return;
  }
  std::printf("%s{\"protocolVersion\":%" PRIu32
              ",\"record\":\"sample\",\"cell\":\"%s\",\"ordinal\":%" PRIu32 ",\"cycles\":%" PRIu32
              ",\"bytes\":%zu,\"startCore\":0,\"endCore\":0,"
              "\"cacheCounters\":",
              kPrefix, kProtocolVersion, cell.id, ordinal, sample.cycles, sample.bytes);
  emit_cache_counters(sample.counters);
  if (sample.has_baseline) {
    std::printf(",\"baselineCycles\":%" PRIu32 ",\"baselineCacheCounters\":",
                sample.baseline_cycles);
    emit_cache_counters(sample.baseline_counters);
  }
  emit_attribution(sample);
  emit_control_evidence(sample);
  if (sample.note != nullptr) {
    std::printf(",\"note\":\"%s\"", sample.note);
  }
  std::printf("}\n");
  std::fflush(stdout);
}

bool invalidate_data(const std::uint8_t* data, std::size_t bytes);

std::uint32_t IRAM_ATTR aggressor_load(Context& context, Aggressor kind, std::size_t offset) {
  switch (kind) {
    case Aggressor::kInternal:
      return reinterpret_cast<volatile const std::uint8_t*>(
          context.internal)[offset & (kInternalBytes - 1)];
    case Aggressor::kFlash:
      return reinterpret_cast<volatile const std::uint32_t*>(
          g_flash_pool.data())[(offset >> 2) & (g_flash_pool.size() - 1)];
    case Aggressor::kPsram:
      return reinterpret_cast<volatile const std::uint8_t*>(
          context.psram)[(kPsramBytes / 2) + (offset & ((kPsramBytes / 2) - 1))];
  }
  return 0;
}

std::uint32_t expected_aggressor_value(Aggressor kind, std::size_t offset) {
  switch (kind) {
    case Aggressor::kInternal:
      return static_cast<std::uint8_t>((offset & (kInternalBytes - 1)) * 29U + 5U);
    case Aggressor::kFlash:
      return static_cast<std::uint32_t>(
          static_cast<std::uint32_t>((offset >> 2) & (g_flash_pool.size() - 1)) * 2'246'822'519U +
          31U);
    case Aggressor::kPsram:
      return static_cast<std::uint8_t>(
          ((kPsramBytes / 2) + (offset & ((kPsramBytes / 2) - 1))) * 17U + 3U);
  }
  return 0;
}

std::uint32_t expected_aggressor_checksum(Aggressor kind, std::uint32_t iterations) {
  std::uint32_t checksum = 0;
  for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
    checksum += expected_aggressor_value(kind, iteration * kDcacheLine);
  }
  return checksum;
}

bool prepare_attribution(Context& context, Aggressor kind) {
  constexpr std::size_t kAttributionBytes = kAttributionIterations * kDcacheLine;
  switch (kind) {
    case Aggressor::kInternal:
      return true;
    case Aggressor::kFlash:
      return invalidate_data(reinterpret_cast<const std::uint8_t*>(g_flash_pool.data()),
                             kAttributionBytes);
    case Aggressor::kPsram:
      return invalidate_data(context.psram + kPsramBytes / 2, kAttributionBytes);
  }
  return false;
}

const char* validate_attribution(const AttributionEvidence& evidence) {
  if (evidence.iterations != kAttributionIterations ||
      evidence.checksum != expected_aggressor_checksum(evidence.source, evidence.iterations)) {
    return "isolated aggressor attribution checksum mismatch";
  }
  switch (evidence.source) {
    case Aggressor::kInternal:
      if (evidence.counters.dbus_accesses != 0 || evidence.counters.dbus_flash_misses != 0 ||
          evidence.counters.dbus_psram_misses != 0) {
        return "isolated internal attribution observed external data-cache traffic";
      }
      break;
    case Aggressor::kFlash:
      if (evidence.counters.dbus_accesses == 0 || evidence.counters.dbus_flash_misses == 0 ||
          evidence.counters.dbus_psram_misses != 0) {
        return "isolated flash attribution lacks exclusive flash access or miss counters";
      }
      break;
    case Aggressor::kPsram:
      if (evidence.counters.dbus_accesses == 0 || evidence.counters.dbus_psram_misses == 0 ||
          evidence.counters.dbus_flash_misses != 0) {
        return "isolated PSRAM attribution lacks exclusive PSRAM access or miss counters";
      }
      break;
  }
  return nullptr;
}

void IRAM_ATTR aggressor_task(void*) {
  Context& context = *g_aggressor_context;
  g_aggressor_ready.store(true, std::memory_order_release);
  while (!g_aggressor_start.load(std::memory_order_acquire)) {
  }
  if (!prepare_attribution(context, g_aggressor_kind)) {
    g_aggressor_failure = "isolated aggressor attribution invalidation failed";
    g_aggressor_done.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
    return;
  }
  clear_cache_counters();
  std::uint32_t attribution_checksum = 0;
  for (std::uint32_t iteration = 0; iteration < kAttributionIterations; ++iteration) {
    attribution_checksum += aggressor_load(context, g_aggressor_kind, iteration * kDcacheLine);
  }
  g_aggressor_report.attribution = {
      .source = g_aggressor_kind,
      .counters = read_cache_counters(),
      .iterations = kAttributionIterations,
      .checksum = attribution_checksum,
  };
  g_aggressor_failure = validate_attribution(g_aggressor_report.attribution);
  if (g_aggressor_failure != nullptr) {
    g_aggressor_done.store(true, std::memory_order_release);
    vTaskDelete(nullptr);
    return;
  }
  clear_cache_counters();
  g_aggressor_active.store(true, std::memory_order_release);
  std::uint32_t sum = 0;
  std::size_t offset = 0;
  while (!g_aggressor_stop.load(std::memory_order_acquire)) {
    sum += aggressor_load(context, g_aggressor_kind, offset);
    offset += kDcacheLine;
  }
  g_aggressor_report.iterations = static_cast<std::uint32_t>(offset / kDcacheLine);
  g_aggressor_report.checksum = sum;
  g_aggressor_done.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

AggressorStart start_aggressor(Context& context, Aggressor kind) {
  g_aggressor_context = &context;
  g_aggressor_kind = kind;
  g_aggressor_ready.store(false, std::memory_order_relaxed);
  g_aggressor_start.store(false, std::memory_order_relaxed);
  g_aggressor_stop.store(false, std::memory_order_relaxed);
  g_aggressor_done.store(false, std::memory_order_relaxed);
  g_aggressor_active.store(false, std::memory_order_relaxed);
  g_aggressor_report = {};
  g_aggressor_failure = nullptr;
  if (xTaskCreatePinnedToCore(aggressor_task, "tier_b_aggress", 4096, nullptr,
                              configMAX_PRIORITIES - 2, nullptr, 1) != pdPASS) {
    return {};
  }
  while (!g_aggressor_ready.load(std::memory_order_acquire)) {
    taskYIELD();
  }
  g_aggressor_start.store(true, std::memory_order_release);
  while (!g_aggressor_active.load(std::memory_order_acquire)) {
    if (g_aggressor_done.load(std::memory_order_acquire)) {
      return {.failure = g_aggressor_failure, .report = g_aggressor_report};
    }
  }
  return {.active = true, .failure = nullptr};
}

AggressorReport stop_aggressor() {
  g_aggressor_stop.store(true, std::memory_order_release);
  while (!g_aggressor_done.load(std::memory_order_acquire)) {
    taskYIELD();
  }
  const AggressorReport report = g_aggressor_report;
  g_sink ^= report.checksum;
  return report;
}

Sample failed_aggressor_sample(const AggressorStart& start, const char* tier_candidate) {
  Sample sample{
      .reason = start.failure == nullptr ? "isolated aggressor attribution failed" : start.failure,
      .tier_candidate = tier_candidate,
  };
  if (start.report.attribution.iterations != 0) {
    sample.has_attribution = true;
    sample.attribution = start.report.attribution;
  }
  return sample;
}

Sample failed_runtime_sample(const AggressorReport& report, const char* reason,
                             const char* tier_candidate) {
  return {
      .has_attribution = true,
      .attribution = report.attribution,
      .aggressor_iterations = report.iterations,
      .aggressor_checksum = report.checksum,
      .reason = reason,
      .tier_candidate = tier_candidate,
  };
}

std::uint32_t read_stride(const std::uint8_t* data, std::size_t size) {
  std::uint32_t sum = 0;
  for (std::size_t offset = 0; offset < size; offset += kDcacheLine) {
    sum += data[offset];
  }
  return sum;
}

bool invalidate_data(const std::uint8_t* data, std::size_t bytes) {
  return esp_cache_msync(const_cast<std::uint8_t*>(data), bytes,
                         ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE) == ESP_OK;
}

bool has_expected_data_counters(const CacheCounters& counters, bool flash) {
  return counters.dbus_accesses != 0 &&
         (flash ? counters.dbus_flash_misses != 0 : counters.dbus_psram_misses != 0);
}

Sample measure_cold_read(const std::uint8_t* data, std::size_t bytes, bool flash) {
  if (!invalidate_data(data, bytes)) {
    return {.reason = "data-cache invalidation failed", .tier_candidate = "affine"};
  }
  clear_cache_counters();
  const std::uint32_t start = read_ccount();
  const std::uint32_t sum = read_stride(data, bytes);
  const std::uint32_t end = read_ccount();
  g_sink ^= sum;
  const CacheCounters counters = read_cache_counters();
  if (!has_expected_data_counters(counters, flash)) {
    return {.reason = "expected victim cache counters were not observed",
            .tier_candidate = "affine"};
  }
  return timed_result(start, end, bytes, counters);
}

Sample probe_arbitration(Context& context, const Cell& cell, std::uint32_t) {
  const auto kind = static_cast<Aggressor>(cell.parameter);
  const Sample baseline = measure_cold_read(context.psram, kBandwidthBytes, false);
  if (!baseline.ok) return baseline;
  if (!invalidate_data(context.psram, kBandwidthBytes)) {
    return {.reason = "contended victim invalidation failed", .tier_candidate = "affine"};
  }
  const AggressorStart startup = start_aggressor(context, kind);
  if (!startup.active) return failed_aggressor_sample(startup, "affine");
  const std::uint32_t start = read_ccount();
  const std::uint32_t sum = read_stride(context.psram, kBandwidthBytes);
  const std::uint32_t end = read_ccount();
  g_sink ^= sum;
  const CacheCounters counters = read_cache_counters();
  const AggressorReport aggressor = stop_aggressor();
  if (aggressor.iterations == 0 ||
      aggressor.checksum != expected_aggressor_checksum(kind, aggressor.iterations) ||
      !has_expected_data_counters(counters, false)) {
    return failed_runtime_sample(aggressor, "contended aggressor runtime evidence failed",
                                 "affine");
  }
  Sample result = timed_result(start, end, kBandwidthBytes, counters,
                               "core0 PSRAM victim with isolated source attribution");
  result.has_baseline = true;
  result.baseline_cycles = baseline.cycles;
  result.baseline_counters = baseline.counters;
  result.has_attribution = true;
  result.attribution = aggressor.attribution;
  result.aggressor_iterations = aggressor.iterations;
  result.aggressor_checksum = aggressor.checksum;
  return result;
}

Sample probe_store_hit(Context& context, const Cell&, std::uint32_t ordinal) {
  auto* psram_word = reinterpret_cast<volatile std::uint32_t*>(
      context.psram + (ordinal * kDcacheLine) % (kPsramBytes - kDcacheLine));
  auto* internal_word = reinterpret_cast<volatile std::uint32_t*>(
      context.internal + (ordinal * kDcacheLine) % (kInternalBytes - kDcacheLine));
  tier_b_store_issue_block(internal_word, ordinal);
  clear_cache_counters();
  const std::uint32_t baseline_start = read_ccount();
  tier_b_store_issue_block(internal_word, ordinal + 1);
  const std::uint32_t baseline_end = read_ccount();
  const CacheCounters baseline_counters = read_cache_counters();
  tier_b_store_issue_block(psram_word, ordinal);
  clear_cache_counters();
  const std::uint32_t start = read_ccount();
  tier_b_store_issue_block(psram_word, ordinal + 1);
  const std::uint32_t end = read_ccount();
  const CacheCounters counters = read_cache_counters();
  g_sink ^= *psram_word ^ *internal_word;
  if (counters.dbus_accesses == 0 || counters.dbus_psram_misses != 0) {
    return {.reason = "hot PSRAM store counters did not show hit-only accesses",
            .tier_candidate = "exact"};
  }
  Sample result = timed_result(start, end, kStoreIterations * sizeof(std::uint32_t), counters,
                               "PSRAM hot store issue block with internal-SRAM baseline");
  if (baseline_end == baseline_start) {
    return {.reason = "zero internal-SRAM store baseline", .tier_candidate = "exact"};
  }
  result.has_baseline = true;
  result.baseline_cycles = baseline_end - baseline_start;
  result.baseline_counters = baseline_counters;
  return result;
}

Sample probe_writeback(Context& context, const Cell& cell, std::uint32_t ordinal) {
  const std::size_t lines = cell.parameter & 0xffffU;
  const bool dirty = (cell.parameter & 0x10000U) != 0;
  std::uint8_t* data = context.psram + ((ordinal * 32 * kDcacheLine) % (kPsramBytes / 2));
  const std::size_t bytes = lines * kDcacheLine;
  if (esp_cache_msync(data, bytes,
                      ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK) {
    return {.reason = "writeback preparation failed", .tier_candidate = "affine"};
  }
  g_sink ^= read_stride(data, bytes);
  if (dirty) {
    for (std::size_t offset = 0; offset < bytes; offset += kDcacheLine) {
      data[offset] = static_cast<std::uint8_t>(ordinal + offset + 0x5aU);
    }
  }
  const std::uint32_t start = read_ccount();
  const esp_err_t error = esp_cache_msync(data, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  const std::uint32_t end = read_ccount();
  if (error != ESP_OK) {
    return {.reason = "esp_cache_msync writeback failed", .tier_candidate = "affine"};
  }
  if (esp_cache_msync(data, bytes,
                      ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK) {
    return {.reason = "writeback verification invalidation failed", .tier_candidate = "affine"};
  }
  clear_cache_counters();
  for (std::size_t offset = 0; offset < bytes; offset += kDcacheLine) {
    const std::uint8_t value = data[offset];
    g_sink ^= value;
    if (dirty && value != static_cast<std::uint8_t>(ordinal + offset + 0x5aU)) {
      return {.reason = "dirty writeback did not reach backing PSRAM", .tier_candidate = "affine"};
    }
  }
  if (read_cache_counters().dbus_psram_misses == 0) {
    return {.reason = "writeback verification reload did not miss", .tier_candidate = "affine"};
  }
  return timed_result(start, end, bytes, {},
                      dirty ? "dirty C2M writeback verified after invalidated reload"
                            : "clean C2M writeback verified after invalidated reload");
}

Sample restore_psram_clock(Sample sample) {
  std::uint32_t register_value = 0;
  std::uint32_t core_register_value = 0;
  if (!set_psram_service_clock(kPsramFastClockHz, register_value, core_register_value)) {
    return {.reason = "PSRAM service clock restore readback mismatch",
            .tier_candidate = "affine"};
  }
  return sample;
}

const char* measure_psram_service(Context& context, Sample& sample) {
  std::uint8_t* data = context.psram + (kPsramBytes * 3 / 4);
  if (!invalidate_data(data, kPsramServiceBytes)) {
    return "PSRAM service control invalidation failed";
  }
  clear_cache_counters();
  const std::uint32_t start = read_ccount();
  const std::uint32_t sum = read_stride(data, kPsramServiceBytes);
  const std::uint32_t end = read_ccount();
  g_sink ^= sum;
  sample.psram_service_cycles = end - start;
  sample.psram_service_counters = read_cache_counters();
  if (sample.psram_service_cycles == 0 || sample.psram_service_counters.dbus_accesses == 0 ||
      sample.psram_service_counters.dbus_psram_misses == 0 ||
      sample.psram_service_counters.dbus_flash_misses != 0) {
    return "PSRAM service control lacks exclusive PSRAM counter evidence";
  }
  return nullptr;
}

Sample probe_msync_decomposition(Context& context, const Cell& cell, std::uint32_t ordinal) {
  const std::size_t bytes = cell.parameter & 0xffffU;
  const std::size_t dirty_lines = (cell.parameter >> 16U) & 0x7fffU;
  const std::uint32_t clock_hz =
      (cell.parameter & 0x80000000U) != 0 ? kPsramSlowClockHz : kPsramFastClockHz;
  const std::size_t addressed_lines = bytes / kDcacheLine;
  if ((addressed_lines != 1 && addressed_lines != 16 && addressed_lines != 512) ||
      (dirty_lines != 0 && dirty_lines != addressed_lines)) {
    return {.reason = "unsupported cache-msync decomposition factors",
            .tier_candidate = "affine"};
  }

  Sample result{
      .bytes = bytes,
      .has_msync_factors = true,
      .dirty_lines = dirty_lines,
      .psram_clock_hz = clock_hz,
  };
  if (!set_psram_service_clock(clock_hz, result.psram_clock_register,
                               result.psram_core_clock_register)) {
    return restore_psram_clock(
        {.reason = "PSRAM service clock readback mismatch", .tier_candidate = "affine"});
  }
  if (const char* failure = measure_psram_service(context, result); failure != nullptr) {
    result.reason = failure;
    result.tier_candidate = "affine";
    return restore_psram_clock(result);
  }

  const std::size_t region_offset = (ordinal * 32 * kDcacheLine) % (kPsramBytes / 4);
  std::uint8_t* data = context.psram + region_offset;
  for (std::size_t offset = 0; offset < bytes; offset += kDcacheLine) {
    data[offset] = static_cast<std::uint8_t>((region_offset + offset) * 17U + 3U);
  }
  if (esp_cache_msync(data, bytes,
                      ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK) {
    result.reason = "cache-msync decomposition preparation failed";
    result.tier_candidate = "affine";
    return restore_psram_clock(result);
  }
  g_sink ^= read_stride(data, bytes);
  for (std::size_t line = 0; line < dirty_lines; ++line) {
    const std::size_t offset = line * kDcacheLine;
    data[offset] = static_cast<std::uint8_t>(ordinal + offset + 0x6dU);
  }

  const std::uint32_t start = read_ccount();
  const esp_err_t error = esp_cache_msync(data, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  const std::uint32_t end = read_ccount();
  if (error != ESP_OK) {
    result.reason = "cache-msync decomposition writeback failed";
    result.tier_candidate = "affine";
    return restore_psram_clock(result);
  }
  if (esp_cache_msync(data, bytes,
                      ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK) {
    result.reason = "cache-msync decomposition verification invalidation failed";
    result.tier_candidate = "affine";
    return restore_psram_clock(result);
  }
  clear_cache_counters();
  for (std::size_t offset = 0; offset < bytes; offset += kDcacheLine) {
    const std::uint8_t value = data[offset];
    const std::uint8_t expected = offset < dirty_lines * kDcacheLine
                                      ? static_cast<std::uint8_t>(ordinal + offset + 0x6dU)
                                      : static_cast<std::uint8_t>((region_offset + offset) * 17U +
                                                                  3U);
    g_sink ^= value;
    if (value != expected) {
      result.reason = "cache-msync decomposition backing value mismatch";
      result.tier_candidate = "affine";
      return restore_psram_clock(result);
    }
  }
  const CacheCounters verification = read_cache_counters();
  if (verification.dbus_accesses == 0 || verification.dbus_psram_misses == 0 ||
      verification.dbus_flash_misses != 0) {
    result.reason = "cache-msync decomposition reload lacks exclusive PSRAM evidence";
    result.tier_candidate = "affine";
    return restore_psram_clock(result);
  }

  result.ok = end != start;
  result.cycles = end - start;
  result.note = dirty_lines == 0 ? "matched clean no-op C2M control"
                                 : "crossed dirty-line and PSRAM-service C2M writeback";
  if (!result.ok) {
    result.reason = "zero cache-msync decomposition CCOUNT delta";
    result.tier_candidate = "affine";
  }
  return restore_psram_clock(result);
}

using InstructionFunction = void (*)(void);

struct InstructionRange {
  InstructionFunction function;
  const std::uint8_t* start;
  const std::uint8_t* end;
};

constexpr std::array<InstructionRange, kFirstLineSamples> kInstructionRanges{{
    {tier_b_instruction_1_lines, tier_b_instruction_1_lines_start, tier_b_instruction_1_lines_end},
    {tier_b_instruction_2_lines, tier_b_instruction_2_lines_start, tier_b_instruction_2_lines_end},
    {tier_b_instruction_4_lines, tier_b_instruction_4_lines_start, tier_b_instruction_4_lines_end},
    {tier_b_instruction_8_lines, tier_b_instruction_8_lines_start, tier_b_instruction_8_lines_end},
    {tier_b_instruction_16_lines, tier_b_instruction_16_lines_start,
     tier_b_instruction_16_lines_end},
}};

constexpr std::array<InstructionFunction, kFirstLineSamples> kFirstLineInstructionTargets{
    tier_b_first_line_i_0, tier_b_first_line_i_1, tier_b_first_line_i_2, tier_b_first_line_i_3,
    tier_b_first_line_i_4};

Sample call_instruction_range(const InstructionRange& range, bool cold) {
  const std::size_t bytes = static_cast<std::size_t>(range.end - range.start);
  if (cold && esp_cache_msync(const_cast<std::uint8_t*>(range.start), bytes,
                              ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_INST |
                                  ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK) {
    return {.reason = "instruction-cache invalidation failed", .tier_candidate = "exact"};
  }
  clear_cache_counters();
  const std::uint32_t start = read_ccount();
  range.function();
  const std::uint32_t end = read_ccount();
  const CacheCounters counters = read_cache_counters();
  if (!valid_instruction_counters(cold, counters.ibus_accesses, counters.ibus_misses)) {
    return {.reason = cold ? "cold instruction probe lacks I-cache access or miss evidence"
                           : "hot instruction probe reports an I-cache miss",
            .tier_candidate = "exact"};
  }
  return timed_result(start, end, bytes, counters);
}

Sample probe_instruction_psram(Context&, const Cell& cell, std::uint32_t) {
  const auto& range = kInstructionRanges[3];
  range.function();
  return call_instruction_range(range, cell.parameter != 0);
}

Sample probe_first_line_i_flash(Context&, const Cell&, std::uint32_t ordinal) {
  constexpr std::size_t kBytes = 32;
  auto function = kFirstLineInstructionTargets[ordinal];
  auto* start = reinterpret_cast<std::uint8_t*>(function);
  if (esp_cache_msync(start, kBytes,
                      ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_INST |
                          ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK) {
    return {.reason = "first-line instruction invalidation failed", .tier_candidate = "exact"};
  }
  clear_cache_counters();
  const std::uint32_t cycle_start = read_ccount();
  function();
  const std::uint32_t cycle_end = read_ccount();
  const CacheCounters counters = read_cache_counters();
  if (counters.ibus_accesses == 0 || counters.ibus_misses == 0) {
    return {.reason = "first-line instruction counters were not observed",
            .tier_candidate = "exact"};
  }
  return timed_result(cycle_start, cycle_end, kBytes, counters,
                      "fresh one-line instruction target");
}

Sample probe_first_line_data(Context& context, const Cell& cell, std::uint32_t ordinal) {
  const std::size_t bytes = kDcacheLine;
  const bool psram = cell.parameter != 0;
  const std::size_t offset = ordinal * kDcacheLine;
  const std::uint8_t* data =
      (psram ? context.psram : reinterpret_cast<const std::uint8_t*>(g_flash_pool.data())) + offset;
  Sample sample = measure_cold_read(data, bytes, !psram);
  if (!sample.ok) sample.tier_candidate = "exact";
  if (sample.ok) sample.note = "fresh one-line data target";
  return sample;
}

bool panel_transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* user) {
  auto* context = static_cast<Context*>(user);
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(context->panel_done, &woken);
  return woken == pdTRUE;
}

bool gdma_transfer_done(async_memcpy_handle_t, async_memcpy_event_t*, void* user) {
  auto* context = static_cast<Context*>(user);
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(context->gdma_done, &woken);
  return woken == pdTRUE;
}

esp_err_t init_i2c(Context& context) {
  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = GPIO_NUM_15;
  bus_config.scl_io_num = GPIO_NUM_14;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;
  esp_err_t error = i2c_new_master_bus(&bus_config, &context.i2c_bus);
  if (error != ESP_OK) return error;

  i2c_device_config_t device_config{};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = 0x20;
  device_config.scl_speed_hz = 400000;
  error = i2c_master_bus_add_device(context.i2c_bus, &device_config, &context.io_expander);
  if (error != ESP_OK) return error;
  device_config.device_address = 0x15;
  error = i2c_master_bus_add_device(context.i2c_bus, &device_config, &context.touch);
  if (error != ESP_OK) return error;

  const auto write_expander = [&](std::uint8_t address, std::uint8_t value) {
    const std::array payload{address, value};
    return i2c_master_transmit(context.io_expander, payload.data(), payload.size(), 100);
  };
  for (int attempt = 0; attempt < 3; ++attempt) {
    error = write_expander(0x03, static_cast<std::uint8_t>(~kExpanderOutputs));
    if (error == ESP_OK) error = write_expander(0x01, kExpanderPoweredDown);
    if (error == ESP_OK) {
      vTaskDelay(pdMS_TO_TICKS(20));
      error = write_expander(0x01, kExpanderOutputs);
    }
    if (error == ESP_OK) break;
    static_cast<void>(i2c_master_bus_reset(context.i2c_bus));
  }
  if (error != ESP_OK) return error;
  vTaskDelay(pdMS_TO_TICKS(150));

  constexpr std::uint8_t kIdentityRegister = 0xa7;
  std::array<std::uint8_t, 3> identity{};
  error = i2c_master_transmit_receive(context.touch, &kIdentityRegister, 1, identity.data(),
                                      identity.size(), 100);
  if (error != ESP_OK) return error;
  constexpr std::array<std::uint8_t, 3> kExpectedIdentity{0xb7, 0x41, 0x02};
  return identity == kExpectedIdentity ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t panel_command(Context& context, std::uint8_t command, const void* data,
                        std::size_t bytes) {
  constexpr std::uint32_t kWriteCommandOpcode = 0x02UL << 24U;
  return esp_lcd_panel_io_tx_param(
      context.panel_io, kWriteCommandOpcode | (std::uint32_t{command} << 8U), data, bytes);
}

esp_err_t init_spi_and_panel(Context& context) {
  spi_bus_config_t bus_config{};
  bus_config.sclk_io_num = GPIO_NUM_11;
  bus_config.data0_io_num = GPIO_NUM_4;
  bus_config.data1_io_num = GPIO_NUM_5;
  bus_config.data2_io_num = GPIO_NUM_6;
  bus_config.data3_io_num = GPIO_NUM_7;
  bus_config.max_transfer_sz = kDmaBytes;
  esp_err_t error = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
  if (error != ESP_OK) return error;

  esp_lcd_panel_io_spi_config_t io_config{};
  io_config.cs_gpio_num = GPIO_NUM_12;
  io_config.dc_gpio_num = GPIO_NUM_NC;
  io_config.spi_mode = 0;
  io_config.pclk_hz = 40 * 1000 * 1000;
  io_config.trans_queue_depth = 2;
  io_config.on_color_trans_done = panel_transfer_done;
  io_config.user_ctx = &context;
  io_config.lcd_cmd_bits = 32;
  io_config.lcd_param_bits = 8;
  io_config.flags.quad_mode = true;
  error = esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_config,
                                   &context.panel_io);
  if (error != ESP_OK) return error;

  spi_device_interface_config_t spi_config{};
  spi_config.clock_speed_hz = 40 * 1000 * 1000;
  spi_config.mode = 0;
  // This transport cell measures SPI2 and DMA without selecting the panel.
  spi_config.spics_io_num = GPIO_NUM_NC;
  spi_config.queue_size = 1;
  error = spi_bus_add_device(SPI2_HOST, &spi_config, &context.spi);
  if (error != ESP_OK) return error;

  context.phased_spi[1] = context.spi;
  spi_device_interface_config_t phased_config{};
  phased_config.clock_speed_hz = static_cast<int>(kSpi2SlowClockHz);
  phased_config.mode = 0;
  phased_config.spics_io_num = GPIO_NUM_NC;
  phased_config.queue_size = 1;
  error = spi_bus_add_device(SPI2_HOST, &phased_config, &context.phased_spi[0]);
  if (error != ESP_OK) return error;

  static constexpr std::array<std::uint8_t, 1> fe{0x00};
  static constexpr std::array<std::uint8_t, 1> c4{0x80};
  static constexpr std::array<std::uint8_t, 1> format{0x55};
  static constexpr std::array<std::uint8_t, 1> te{0x00};
  static constexpr std::array<std::uint8_t, 1> control{0x20};
  static constexpr std::array<std::uint8_t, 1> bright{0xff};
  static constexpr std::array<std::uint8_t, 4> columns{0x00, 0x00, 0x01, 0x6f};
  static constexpr std::array<std::uint8_t, 4> rows{0x00, 0x00, 0x01, 0xbf};
  struct Command {
    std::uint8_t command;
    const std::uint8_t* data;
    std::size_t bytes;
  };
  const std::array commands{
      Command{0xfe, fe.data(), fe.size()},           Command{0xc4, c4.data(), c4.size()},
      Command{0x3a, format.data(), format.size()},   Command{0x35, te.data(), te.size()},
      Command{0x53, control.data(), control.size()}, Command{0x51, bright.data(), bright.size()},
      Command{0x63, bright.data(), bright.size()},   Command{0x2a, columns.data(), columns.size()},
      Command{0x2b, rows.data(), rows.size()},
  };
  for (const auto& command : commands) {
    error = panel_command(context, command.command, command.data, command.bytes);
    if (error != ESP_OK) return error;
  }
  error = panel_command(context, 0x11, nullptr, 0);
  if (error != ESP_OK) return error;
  vTaskDelay(pdMS_TO_TICKS(100));
  return panel_command(context, 0x29, nullptr, 0);
}

esp_err_t init_context(Context& context) {
#if CONFIG_SPIRAM_FETCH_INSTRUCTIONS
  for (const auto& range : kInstructionRanges) {
    if (!mmu_psram_check_ptr_addr_in_xip_psram_instruction_region(
            reinterpret_cast<const void*>(range.function))) {
      return ESP_ERR_INVALID_STATE;
    }
  }
#endif
  context.psram = static_cast<std::uint8_t*>(
      heap_caps_aligned_alloc(kDcacheLine, kPsramBytes, MALLOC_CAP_SPIRAM));
  context.internal = static_cast<std::uint8_t*>(
      heap_caps_aligned_alloc(kDcacheLine, kInternalBytes, MALLOC_CAP_INTERNAL));
  context.dma_source = static_cast<std::uint8_t*>(
      heap_caps_aligned_alloc(kDcacheLine, kDmaBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
  context.dma_destination = static_cast<std::uint8_t*>(
      heap_caps_aligned_alloc(kDcacheLine, kDmaBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
  if (context.psram == nullptr || context.internal == nullptr || context.dma_source == nullptr ||
      context.dma_destination == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  for (std::size_t index = 0; index < kPsramBytes; ++index) {
    context.psram[index] = static_cast<std::uint8_t>(index * 17U + 3U);
  }
  for (std::size_t index = 0; index < kInternalBytes; ++index) {
    context.internal[index] = static_cast<std::uint8_t>(index * 29U + 5U);
  }
  for (std::size_t index = 0; index < kDmaBytes; ++index) {
    context.dma_source[index] = static_cast<std::uint8_t>(index * 31U + 7U);
  }

  context.panel_done = xSemaphoreCreateBinary();
  context.gdma_done = xSemaphoreCreateBinary();
  if (context.panel_done == nullptr || context.gdma_done == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  esp_err_t error = init_i2c(context);
  if (error != ESP_OK) return error;
  error = init_spi_and_panel(context);
  if (error != ESP_OK) return error;

  async_memcpy_config_t dma_config = ASYNC_MEMCPY_DEFAULT_CONFIG();
  dma_config.backlog = 1;
  error = esp_async_memcpy_install(&dma_config, &context.gdma);
  return error;
}

constexpr std::array<std::size_t, kSweepSamples> kSweepBytes{64, 256, 1024, 4096, 16384, 32768};

Sample probe_panel_qspi(Context& context, const Cell&, std::uint32_t ordinal) {
  constexpr std::uint32_t kWriteColorOpcode = 0x32UL << 24U;
  const std::size_t bytes = kSweepBytes[ordinal];
  while (xSemaphoreTake(context.panel_done, 0) == pdTRUE) {
  }
  const std::uint32_t start = read_ccount();
  const esp_err_t error = esp_lcd_panel_io_tx_color(
      context.panel_io, kWriteColorOpcode | (0x2cUL << 8U), context.dma_source, bytes);
  if (error != ESP_OK || xSemaphoreTake(context.panel_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return {.reason = "panel QSPI transfer failed", .tier_candidate = "affine"};
  }
  const std::uint32_t end = read_ccount();
  return timed_result(start, end, bytes);
}

Sample probe_gdma(Context& context, const Cell&, std::uint32_t ordinal) {
  const std::size_t bytes = kSweepBytes[ordinal];
  while (xSemaphoreTake(context.gdma_done, 0) == pdTRUE) {
  }
  const std::uint32_t start = read_ccount();
  const esp_err_t error = esp_async_memcpy(context.gdma, context.dma_destination,
                                           context.dma_source, bytes, gdma_transfer_done, &context);
  if (error != ESP_OK || xSemaphoreTake(context.gdma_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return {.reason = "GDMA transfer failed", .tier_candidate = "affine"};
  }
  const std::uint32_t end = read_ccount();
  g_sink ^= context.dma_destination[bytes - 1];
  return timed_result(start, end, bytes);
}

Sample probe_spi2(Context& context, const Cell&, std::uint32_t ordinal) {
  const std::size_t bytes = kSweepBytes[ordinal];
  spi_transaction_t transaction{};
  transaction.length = bytes * 8;
  transaction.tx_buffer = context.dma_source;
  const std::uint32_t start = read_ccount();
  const esp_err_t error = spi_device_transmit(context.spi, &transaction);
  const std::uint32_t end = read_ccount();
  if (error != ESP_OK) {
    return {.reason = "SPI2 transfer failed", .tier_candidate = "affine"};
  }
  return timed_result(start, end, bytes);
}

Sample probe_spi2_phased(Context& context, const Cell& cell, std::uint32_t ordinal) {
  static_cast<void>(ordinal);
  const std::size_t bytes = cell.parameter & 0xffffU;
  const std::uint32_t clock_hz =
      (cell.parameter & 0x80000000U) != 0 ? kSpi2SlowClockHz : kSpi2FastClockHz;
  if (bytes != 64 && bytes != 4096 && bytes != 32768) {
    return {.reason = "unsupported SPI2 decomposition payload",
            .tier_candidate = "affine"};
  }
  std::size_t handle_index = 0;
  if (clock_hz == kSpi2SlowClockHz) {
    handle_index = 0;
  } else if (clock_hz == kSpi2FastClockHz) {
    handle_index = 1;
  } else {
    return {.reason = "unsupported SPI2 decomposition clock",
            .tier_candidate = "affine"};
  }
  spi_device_handle_t handle = context.phased_spi[handle_index];
  int actual_khz = 0;
  if (spi_device_get_actual_freq(handle, &actual_khz) != ESP_OK || actual_khz <= 0 ||
      static_cast<std::uint32_t>(actual_khz) * 1000U != clock_hz) {
    return {.reason = "SPI2 decomposition clock readback mismatch",
            .tier_candidate = "affine"};
  }

  spi_transaction_t transaction{};
  transaction.length = bytes * 8;
  transaction.tx_buffer = context.dma_source;
  const std::uint32_t start = read_ccount();
  const esp_err_t queued = spi_device_queue_trans(handle, &transaction, portMAX_DELAY);
  const std::uint32_t submitted = read_ccount();
  if (queued != ESP_OK) {
    return {.reason = "SPI2 decomposition submission failed",
            .tier_candidate = "affine"};
  }
  spi_transaction_t* completed_transaction = nullptr;
  const esp_err_t completed =
      spi_device_get_trans_result(handle, &completed_transaction, portMAX_DELAY);
  const std::uint32_t end = read_ccount();
  if (completed != ESP_OK || completed_transaction != &transaction) {
    return {.reason = "SPI2 decomposition completion failed",
            .tier_candidate = "affine"};
  }
  Sample result = timed_result(start, end, bytes);
  result.has_spi2_phases = true;
  result.spi2_clock_hz = clock_hz;
  result.submission_cycles = submitted - start;
  result.completion_cycles = end - submitted;
  result.note = "CPU submission and DMA peripheral completion timed separately";
  if (result.submission_cycles == 0 || result.completion_cycles == 0 ||
      result.submission_cycles + result.completion_cycles != result.cycles) {
    return {.reason = "SPI2 decomposition phase timing invalid",
            .tier_candidate = "affine"};
  }
  return result;
}

Sample probe_touch_i2c(Context& context, const Cell&, std::uint32_t ordinal) {
  const std::uint8_t register_address = static_cast<std::uint8_t>(0x02U + (ordinal % 5U));
  std::array<std::uint8_t, 5> response{};
  const std::uint32_t start = read_ccount();
  const esp_err_t error = i2c_master_transmit_receive(context.touch, &register_address, 1,
                                                      response.data(), response.size(), 100);
  const std::uint32_t end = read_ccount();
  if (error != ESP_OK) {
    return {.reason = "CST820 I2C transaction failed", .tier_candidate = "distribution"};
  }
  g_sink ^= response[0];
  return timed_result(start, end, response.size() + 1);
}

Sample probe_gpio21_edge(Context&, const Cell&, std::uint32_t) {
  return {.reason = "GPIO21 electrical edge timestamp is unavailable; ISR latency remains open",
          .tier_candidate = "distribution"};
}

Sample probe_msync_sweep(Context& context, const Cell& cell, std::uint32_t ordinal) {
  const std::size_t bytes = kSweepBytes[ordinal];
  const bool clean_invalidate = cell.parameter != 0;
  if (clean_invalidate) {
    if (esp_cache_msync(context.psram, bytes,
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK) {
      return {.reason = "clean invalidation preparation failed", .tier_candidate = "affine"};
    }
    g_sink ^= read_stride(context.psram, bytes);
  } else {
    for (std::size_t offset = 0; offset < bytes; offset += kDcacheLine) {
      context.psram[offset] = static_cast<std::uint8_t>(ordinal + offset + 0xa5U);
    }
  }
  const int flags = clean_invalidate
                        ? ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE
                        : ESP_CACHE_MSYNC_FLAG_DIR_C2M;
  const std::uint32_t start = read_ccount();
  const esp_err_t error = esp_cache_msync(context.psram, bytes, flags);
  const std::uint32_t end = read_ccount();
  if (error != ESP_OK) {
    return {.reason = "esp_cache_msync sweep failed", .tier_candidate = "affine"};
  }
  if (!clean_invalidate &&
      esp_cache_msync(context.psram, bytes,
                      ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK) {
    return {.reason = "writeback sweep verification invalidation failed",
            .tier_candidate = "affine"};
  }
  clear_cache_counters();
  for (std::size_t offset = 0; offset < bytes; offset += kDcacheLine) {
    const std::uint8_t value = context.psram[offset];
    g_sink ^= value;
    if (!clean_invalidate && value != static_cast<std::uint8_t>(ordinal + offset + 0xa5U)) {
      return {.reason = "writeback sweep did not reach backing PSRAM", .tier_candidate = "affine"};
    }
  }
  if (read_cache_counters().dbus_psram_misses == 0) {
    return {.reason = "msync verification reload did not miss", .tier_candidate = "affine"};
  }
  return timed_result(start, end, bytes, {},
                      clean_invalidate ? "clean M2C invalidate verified by cold reload"
                                       : "dirty C2M writeback verified by cold reload");
}

Sample probe_cross_core_bandwidth(Context& context, const Cell& cell, std::uint32_t) {
  const bool flash_victim = cell.parameter != 0;
  const std::uint8_t* data =
      flash_victim ? reinterpret_cast<const std::uint8_t*>(g_flash_pool.data()) : context.psram;
  const Sample baseline = measure_cold_read(data, kBandwidthBytes, flash_victim);
  if (!baseline.ok) return baseline;
  if (!invalidate_data(data, kBandwidthBytes)) {
    return {.reason = "contended bandwidth invalidation failed", .tier_candidate = "distribution"};
  }
  const AggressorStart startup = start_aggressor(context, Aggressor::kPsram);
  if (!startup.active) return failed_aggressor_sample(startup, "distribution");
  const std::uint32_t start = read_ccount();
  const std::uint32_t sum = read_stride(data, kBandwidthBytes);
  const std::uint32_t end = read_ccount();
  g_sink ^= sum;
  const CacheCounters counters = read_cache_counters();
  const AggressorReport aggressor = stop_aggressor();
  if (aggressor.iterations == 0 ||
      aggressor.checksum != expected_aggressor_checksum(Aggressor::kPsram, aggressor.iterations) ||
      !has_expected_data_counters(counters, flash_victim)) {
    return failed_runtime_sample(aggressor, "cross-core PSRAM runtime evidence failed",
                                 "distribution");
  }
  Sample result = timed_result(start, end, kBandwidthBytes, counters,
                               "victim baseline plus isolated PSRAM source attribution");
  result.has_baseline = true;
  result.baseline_cycles = baseline.cycles;
  result.baseline_counters = baseline.counters;
  result.has_attribution = true;
  result.attribution = aggressor.attribution;
  result.aggressor_iterations = aggressor.iterations;
  result.aggressor_checksum = aggressor.checksum;
  return result;
}

#define CELL(id, probe, samples, parameter) \
  Cell { id, probe, samples, parameter }

constexpr std::uint32_t msync_decomposition_parameter(std::uint32_t bytes,
                                                       std::uint32_t dirty_lines,
                                                       std::uint32_t clock_hz) {
  return bytes | (dirty_lines << 16U) |
         (clock_hz == kPsramSlowClockHz ? 0x80000000U : 0U);
}

constexpr std::uint32_t spi2_decomposition_parameter(std::uint32_t bytes,
                                                      std::uint32_t clock_hz) {
  return bytes | (clock_hz == kSpi2SlowClockHz ? 0x80000000U : 0U);
}

constexpr std::array kCells{
    CELL("arbitration_psram_victim_internal_aggressor", probe_arbitration, kDefaultSamples,
         static_cast<std::uint32_t>(Aggressor::kInternal)),
    CELL("arbitration_psram_victim_flash_aggressor", probe_arbitration, kDefaultSamples,
         static_cast<std::uint32_t>(Aggressor::kFlash)),
    CELL("arbitration_psram_victim_psram_aggressor", probe_arbitration, kDefaultSamples,
         static_cast<std::uint32_t>(Aggressor::kPsram)),
    CELL("store_hit_psram", probe_store_hit, kDefaultSamples, 0),
    CELL("writeback_clean_1_lines", probe_writeback, kDefaultSamples, 1),
    CELL("writeback_clean_2_lines", probe_writeback, kDefaultSamples, 2),
    CELL("writeback_clean_4_lines", probe_writeback, kDefaultSamples, 4),
    CELL("writeback_clean_8_lines", probe_writeback, kDefaultSamples, 8),
    CELL("writeback_clean_16_lines", probe_writeback, kDefaultSamples, 16),
    CELL("writeback_dirty_1_lines", probe_writeback, kDefaultSamples, 0x10001),
    CELL("writeback_dirty_2_lines", probe_writeback, kDefaultSamples, 0x10002),
    CELL("writeback_dirty_4_lines", probe_writeback, kDefaultSamples, 0x10004),
    CELL("writeback_dirty_8_lines", probe_writeback, kDefaultSamples, 0x10008),
    CELL("writeback_dirty_16_lines", probe_writeback, kDefaultSamples, 0x10010),
    CELL("instruction_psram_hot", probe_instruction_psram, kDefaultSamples, 0),
    CELL("instruction_psram_cold", probe_instruction_psram, kDefaultSamples, 1),
    CELL("first_line_i_flash", probe_first_line_i_flash, kFirstLineSamples, 0),
    CELL("first_line_d_flash", probe_first_line_data, kFirstLineSamples, 0),
    CELL("first_line_d_psram", probe_first_line_data, kFirstLineSamples, 1),
    CELL("panel_qspi_flush_sweep", probe_panel_qspi, kSweepSamples, 0),
    CELL("gdma_transfer_sweep", probe_gdma, kSweepSamples, 0),
    CELL("spi2_transfer_sweep", probe_spi2, kSweepSamples, 0),
    CELL("touch_i2c_transaction", probe_touch_i2c, kDefaultSamples, 0),
    CELL("gpio21_edge", probe_gpio21_edge, 1, 0),
    CELL("cache_msync_writeback_sweep", probe_msync_sweep, kSweepSamples, 0),
    CELL("cache_msync_invalidate_clean_sweep", probe_msync_sweep, kSweepSamples, 1),
    CELL("psram_bandwidth_cross_core", probe_cross_core_bandwidth, kDefaultSamples, 0),
    CELL("flash_bandwidth_cross_core", probe_cross_core_bandwidth, kDefaultSamples, 1),
    CELL("msync_decompose_l1_d0_p40", probe_msync_decomposition, kDefaultSamples,
         msync_decomposition_parameter(64, 0, kPsramSlowClockHz)),
    CELL("msync_decompose_l1_d0_p80", probe_msync_decomposition, kDefaultSamples,
         msync_decomposition_parameter(64, 0, kPsramFastClockHz)),
    CELL("msync_decompose_l1_d1_p40", probe_msync_decomposition, kDefaultSamples,
         msync_decomposition_parameter(64, 1, kPsramSlowClockHz)),
    CELL("msync_decompose_l1_d1_p80", probe_msync_decomposition, kDefaultSamples,
         msync_decomposition_parameter(64, 1, kPsramFastClockHz)),
    CELL("msync_decompose_l16_d0_p40", probe_msync_decomposition, kDefaultSamples,
         msync_decomposition_parameter(1024, 0, kPsramSlowClockHz)),
    CELL("msync_decompose_l16_d0_p80", probe_msync_decomposition, kDefaultSamples,
         msync_decomposition_parameter(1024, 0, kPsramFastClockHz)),
    CELL("msync_decompose_l16_d16_p40", probe_msync_decomposition, kDefaultSamples,
         msync_decomposition_parameter(1024, 16, kPsramSlowClockHz)),
    CELL("msync_decompose_l16_d16_p80", probe_msync_decomposition, kDefaultSamples,
         msync_decomposition_parameter(1024, 16, kPsramFastClockHz)),
    CELL("msync_decompose_l512_d0_p40", probe_msync_decomposition, kDefaultSamples,
         msync_decomposition_parameter(32768, 0, kPsramSlowClockHz)),
    CELL("msync_decompose_l512_d0_p80", probe_msync_decomposition, kDefaultSamples,
         msync_decomposition_parameter(32768, 0, kPsramFastClockHz)),
    CELL("msync_decompose_l512_d512_p40", probe_msync_decomposition, kDefaultSamples,
         msync_decomposition_parameter(32768, 512, kPsramSlowClockHz)),
    CELL("msync_decompose_l512_d512_p80", probe_msync_decomposition, kDefaultSamples,
         msync_decomposition_parameter(32768, 512, kPsramFastClockHz)),
    CELL("spi2_phased_b64_c20", probe_spi2_phased, kDefaultSamples,
         spi2_decomposition_parameter(64, kSpi2SlowClockHz)),
    CELL("spi2_phased_b64_c40", probe_spi2_phased, kDefaultSamples,
         spi2_decomposition_parameter(64, kSpi2FastClockHz)),
    CELL("spi2_phased_b4096_c20", probe_spi2_phased, kDefaultSamples,
         spi2_decomposition_parameter(4096, kSpi2SlowClockHz)),
    CELL("spi2_phased_b4096_c40", probe_spi2_phased, kDefaultSamples,
         spi2_decomposition_parameter(4096, kSpi2FastClockHz)),
    CELL("spi2_phased_b32768_c20", probe_spi2_phased, kDefaultSamples,
         spi2_decomposition_parameter(32768, kSpi2SlowClockHz)),
    CELL("spi2_phased_b32768_c40", probe_spi2_phased, kDefaultSamples,
         spi2_decomposition_parameter(32768, kSpi2FastClockHz)),
};

#undef CELL

bool cell_available(const Cell& cell) {
  if (std::strcmp(cell.id, "gpio21_edge") == 0) return false;
#if CONFIG_SPIRAM_FETCH_INSTRUCTIONS
  return std::strcmp(cell.id, "first_line_i_flash") != 0;
#else
  return std::strncmp(cell.id, "instruction_psram_", 18) != 0;
#endif
}

void emit_string_array(const std::array<bool, kCells.size()>& selected, bool all) {
  bool first = true;
  for (std::size_t index = 0; index < kCells.size(); ++index) {
    if (!cell_available(kCells[index]) || (!all && !selected[index])) continue;
    std::printf("%s\"%s\"", first ? "" : ",", kCells[index].id);
    first = false;
  }
}

bool read_selection(std::array<bool, kCells.size()>& selected) {
  std::printf("TINYDRAW_TIER_B_SELECT_READY use: TIER_B_SELECT all|cell,cell\n");
  std::fflush(stdout);
  std::array<char, 1024> command{};
  constexpr std::uint32_t kSelectionPolls = 3000;
  for (std::uint32_t poll = 0; poll < kSelectionPolls; ++poll) {
    if (std::fgets(command.data(), command.size(), stdin) != nullptr) break;
    std::clearerr(stdin);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (command[0] == '\0') return false;
  constexpr char kCommand[] = "TIER_B_SELECT ";
  if (std::strncmp(command.data(), kCommand, sizeof(kCommand) - 1) != 0) return false;
  char* value = command.data() + sizeof(kCommand) - 1;
  value[std::strcspn(value, "\r\n")] = '\0';
  if (std::strcmp(value, "all") == 0) {
    for (std::size_t index = 0; index < kCells.size(); ++index) {
      selected[index] = cell_available(kCells[index]);
    }
    return true;
  }
  char* save = nullptr;
  for (char* token = strtok_r(value, ",", &save); token != nullptr;
       token = strtok_r(nullptr, ",", &save)) {
    bool found = false;
    for (std::size_t index = 0; index < kCells.size(); ++index) {
      if (std::strcmp(token, kCells[index].id) == 0) {
        if (selected[index] || !cell_available(kCells[index])) return false;
        selected[index] = true;
        found = true;
        break;
      }
    }
    if (!found) return false;
  }
  for (bool cell : selected) {
    if (cell) return true;
  }
  return false;
}

void emit_metadata(const std::array<bool, kCells.size()>& selected) {
  esp_chip_info_t chip_info{};
  esp_chip_info(&chip_info);
  const auto reset_reason = static_cast<unsigned>(esp_reset_reason());
  std::array<char, 40> boot_id{};
  std::snprintf(boot_id.data(), boot_id.size(), "%u-%08" PRIx32 "%08" PRIx32, reset_reason,
                esp_random(), esp_random());
  const DbusFlashClassifierRange classifier = read_dbus_flash_classifier_range();
  std::printf("%s{\"protocolVersion\":%" PRIu32
              ",\"record\":\"metadata\",\"suite\":\"tier-b\",\"harnessVersion\":\"%s\","
              "\"idfVersion\":\"%s\",\"gitCommit\":\"%s\",\"gitDirty\":%s,"
              "\"variant\":\"%s\",\"spiramRodata\":false,\"sdkconfigSha256\":\"%s\","
              "\"manifestSha256\":\"%s\",\"compilerVersion\":\"%s\",\"elfSha256\":\"%s\","
              "\"dbusFlashClassifier\":{\"start\":%" PRIu32 ",\"end\":%" PRIu32
              "},"
              "\"chipModel\":\"ESP32-S3\",\"chipRevision\":%u,\"resetReason\":%u,"
              "\"bootId\":\"%s\",\"availableCells\":[",
              kPrefix, kProtocolVersion, kHarnessVersion, esp_get_idf_version(),
              TINYDRAW_GIT_COMMIT, TINYDRAW_GIT_DIRTY ? "true" : "false", TINYDRAW_BUILD_VARIANT,
              TINYDRAW_SDKCONFIG_SHA256, TINYDRAW_MANIFEST_SHA256, __VERSION__,
              esp_app_get_elf_sha256_str(),
              classifier.start, classifier.end, static_cast<unsigned>(chip_info.revision),
              reset_reason, boot_id.data());
  emit_string_array(selected, true);
  std::printf("],\"selectedCells\":[");
  emit_string_array(selected, false);
  std::printf("]}\n");
  std::fflush(stdout);
}

}  // namespace

extern "C" void app_main() {
  if (std::strcmp(esp_get_idf_version(), kRequiredIdfVersion) != 0) {
    std::printf("TINYDRAW_TIER_B_FAILED ESP-IDF must be %s, got %s\n", kRequiredIdfVersion,
                esp_get_idf_version());
    return;
  }
  if (esp_cpu_get_core_id() != 0) {
    std::printf("TINYDRAW_TIER_B_FAILED app task is not pinned to core 0\n");
    return;
  }
  const DbusFlashClassifierRange expected_classifier = expected_dbus_flash_classifier_range();
  const DbusFlashClassifierRange actual_classifier = configure_dbus_flash_classifier();
  if (!ranges_equal(actual_classifier, expected_classifier)) {
    std::printf("TINYDRAW_TIER_B_FAILED DBUS flash classifier expected=%08" PRIx32 "-%08" PRIx32
                " actual=%08" PRIx32 "-%08" PRIx32 "\n",
                expected_classifier.start, expected_classifier.end, actual_classifier.start,
                actual_classifier.end);
    return;
  }
  Context context{};
  const esp_err_t initialized = init_context(context);
  if (initialized != ESP_OK) {
    std::printf("TINYDRAW_TIER_B_FAILED initialization=%s\n", esp_err_to_name(initialized));
    return;
  }

  std::array<bool, kCells.size()> selected{};
  if (!read_selection(selected)) {
    std::printf("TINYDRAW_TIER_B_FAILED invalid selection\n");
    return;
  }
  emit_metadata(selected);

  std::uint32_t completed_cells = 0;
  std::uint32_t total_samples = 0;
  std::uint32_t refusals = 0;
  for (std::size_t index = 0; index < kCells.size(); ++index) {
    if (!selected[index]) continue;
    const Cell& cell = kCells[index];
    std::printf("%s{\"protocolVersion\":%" PRIu32
                ",\"record\":\"cell-start\",\"cell\":\"%s\",\"expectedSamples\":%" PRIu32 "}\n",
                kPrefix, kProtocolVersion, cell.id, cell.samples);
    std::fflush(stdout);
    for (std::uint32_t ordinal = 0; ordinal < cell.samples; ++ordinal) {
      const Sample sample = cell.probe(context, cell, ordinal);
      emit_sample(cell, ordinal, sample);
      refusals += sample.ok ? 0U : 1U;
      ++total_samples;
      vTaskDelay(1);
    }
    std::printf("%s{\"protocolVersion\":%" PRIu32
                ",\"record\":\"cell-complete\",\"cell\":\"%s\",\"samples\":%" PRIu32 "}\n",
                kPrefix, kProtocolVersion, cell.id, cell.samples);
    std::fflush(stdout);
    ++completed_cells;
  }

  std::printf("%s{\"protocolVersion\":%" PRIu32
              ",\"record\":\"run-complete\",\"selectedCells\":%" PRIu32
              ",\"completedCells\":%" PRIu32 ",\"samples\":%" PRIu32 ",\"refusals\":%" PRIu32 "}\n",
              kPrefix, kProtocolVersion, completed_cells, completed_cells, total_samples, refusals);
  std::fflush(stdout);
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
