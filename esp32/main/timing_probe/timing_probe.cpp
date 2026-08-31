#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <unistd.h>

#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_chip_info.h"
#include "esp_compiler.h"
#include "esp_cpu.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_psram.h"
#include "esp_random.h"
#include "esp_rom_regi2c.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/extmem_reg.h"
#include "soc/regi2c_brownout.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"
#include "soc/system_reg.h"
#include "timing_probe_build_metadata.h"

extern "C" {
extern const std::uint8_t tinydraw_flash_instruction_burst_1_lines_start[];
extern const std::uint8_t tinydraw_flash_instruction_burst_1_lines_end[];
extern const std::uint8_t tinydraw_flash_instruction_burst_2_lines_start[];
extern const std::uint8_t tinydraw_flash_instruction_burst_2_lines_end[];
extern const std::uint8_t tinydraw_flash_instruction_burst_4_lines_start[];
extern const std::uint8_t tinydraw_flash_instruction_burst_4_lines_end[];
extern const std::uint8_t tinydraw_flash_instruction_burst_8_lines_start[];
extern const std::uint8_t tinydraw_flash_instruction_burst_8_lines_end[];
extern const std::uint8_t tinydraw_iram_instruction_hit_8_lines_start[];
extern const std::uint8_t tinydraw_iram_instruction_hit_8_lines_end[];
}

namespace tinydraw::esp32::timing_probe {
namespace {

constexpr char kRecordPrefix[] = "TINYDRAW_TIMING_NDJSON ";
constexpr std::uint32_t kProtocolVersion = 1;
constexpr int kSamplesPerMeasurement = 100;
constexpr int kWarmupIterations = 8;
constexpr std::size_t kSramStreamBytes = 32U * 1024U;
constexpr std::size_t kDependentEntries = 4096U;
constexpr std::size_t kUnalignedStride = 5U;
constexpr std::size_t kPsramBytes = 1024U * 1024U;
constexpr std::size_t kPsramHotBytes = 4U * 1024U;
constexpr std::size_t kPsramColdBytes = 512U * 1024U;
constexpr std::size_t kFlashMapBytes = 256U * 1024U;
constexpr std::size_t kContentionBytes = 512U * 1024U;
constexpr std::uint32_t kDependentLoads = 4096U;
constexpr std::uint32_t kRandomHotLoads = 4096U;
constexpr std::uint32_t kRandomColdLoads = 16384U;
constexpr std::size_t kIcacheLineBytes = 32U;
constexpr std::size_t kDcacheLineBytes = 64U;
constexpr std::size_t kMatchedIcacheLines = 8U;
constexpr std::uint32_t kMatchedIcacheExecutedInstructions = 120U;
constexpr std::uint32_t kMatchedDcacheLoads = 16U;
constexpr std::uint32_t kConditionalBranchIterations = 4096U;
constexpr std::uint32_t kConditionalBranchChecksum = kConditionalBranchIterations;
constexpr std::uint32_t kMmioOperations = 4096U;
constexpr std::uint32_t kMmioHalfOperations = 2048U;
constexpr std::uint32_t kMmioStatePreservedChecksum = 0x7374'6174U;
constexpr std::size_t kRomMemsetBytes = 0x52e0U;
constexpr std::uint8_t kRomMemsetFill = 0xa5U;
constexpr std::uint32_t kRomCallbackStatePreservedChecksum = 0x524f'4d53U;
constexpr std::uint32_t kSramStoreCompletionChecksum = 0x5352'414dU;
constexpr std::size_t kRgb565StagePixels = 5U;
constexpr std::size_t kRgb565OracleCodeBytes = 41U;
constexpr std::array<std::uint16_t, kRgb565StagePixels> kRgb565StageInput = {
    0x1234U, 0xabcdU, 0x00ffU, 0xf81fU, 0x07e0U,
};
constexpr std::uint32_t kRgb565StageOutputChecksum = 0x471e'969fU;

DRAM_ATTR std::uint16_t g_rgb565_stage_source[kRgb565StagePixels] = {
    0x1234U, 0xabcdU, 0x00ffU, 0xf81fU, 0x07e0U,
};
DRAM_ATTR std::uint16_t g_rgb565_stage_destination[kRgb565StagePixels] = {};
DRAM_ATTR volatile std::uint32_t g_mmio_sram_peer = 0x6d6d'696fU;

struct ProbeContext {
  std::uint32_t* sram_dependent = nullptr;
  std::uint8_t* sram_unaligned_dependent = nullptr;
  std::uint8_t* sram_stream = nullptr;
  std::uint32_t* psram = nullptr;
  std::uint32_t* contention = nullptr;
  const std::uint32_t* flash = nullptr;
  std::uint32_t* sram_load_use = nullptr;
  std::uint32_t* psram_load_use = nullptr;
  volatile std::uint32_t* mmio_sram_peer = nullptr;
  volatile std::uint32_t* mmio_system_cpu_per_conf = nullptr;
  volatile std::uint32_t* mmio_rtc_store1 = nullptr;
  volatile std::uint32_t* mmio_extmem_cache_state = nullptr;
  volatile std::uint32_t* mmio_extmem_cache_counter_clear = nullptr;
  volatile std::uint32_t* mmio_system_sysclk_conf = nullptr;
  volatile std::uint32_t* mmio_extmem_dcache_ctrl1 = nullptr;
  volatile std::uint32_t* mmio_extmem_dcache_autoload_ctrl = nullptr;
  volatile std::uint32_t* mmio_extmem_icache_ctrl1 = nullptr;
  volatile std::uint32_t* mmio_extmem_icache_autoload_ctrl = nullptr;
  std::uint32_t mmio_same_value_sram = 0U;
  std::uint32_t mmio_same_value_system_sysclk_conf = 0U;
  std::uint32_t mmio_same_value_extmem_dcache_ctrl1 = 0U;
  std::uint32_t mmio_same_value_extmem_dcache_autoload_ctrl = 0U;
  std::uint32_t mmio_same_value_extmem_icache_ctrl1 = 0U;
  std::uint32_t mmio_same_value_extmem_icache_autoload_ctrl = 0U;
  volatile std::uint32_t* mmio_rtc_date = nullptr;
  esp_partition_mmap_handle_t flash_handle = 0;
  std::uint8_t* rom_memset_buffer = nullptr;
  std::uint32_t rom_cpu_ticks_per_us = 0U;
  std::uint32_t rom_reset_reason_core0 = 0U;
  std::uint32_t rom_reset_reason_core1 = 0U;
  std::uint32_t rom_i2c_bod_threshold = 0U;
};

static_assert(offsetof(ProbeContext, sram_dependent) == 0U);
static_assert(offsetof(ProbeContext, sram_stream) == 8U);
static_assert(offsetof(ProbeContext, psram) == 12U);
static_assert(offsetof(ProbeContext, flash) == 20U);
static_assert(offsetof(ProbeContext, sram_load_use) == 24U);
static_assert(offsetof(ProbeContext, psram_load_use) == 28U);
static_assert(offsetof(ProbeContext, mmio_sram_peer) == 32U);
static_assert(offsetof(ProbeContext, mmio_system_cpu_per_conf) == 36U);
static_assert(offsetof(ProbeContext, mmio_rtc_store1) == 40U);
static_assert(offsetof(ProbeContext, mmio_extmem_cache_state) == 44U);
static_assert(offsetof(ProbeContext, mmio_extmem_cache_counter_clear) == 48U);
static_assert(offsetof(ProbeContext, mmio_system_sysclk_conf) == 52U);
static_assert(offsetof(ProbeContext, mmio_extmem_dcache_ctrl1) == 56U);
static_assert(offsetof(ProbeContext, mmio_extmem_dcache_autoload_ctrl) == 60U);
static_assert(offsetof(ProbeContext, mmio_extmem_icache_ctrl1) == 64U);
static_assert(offsetof(ProbeContext, mmio_extmem_icache_autoload_ctrl) == 68U);
static_assert(offsetof(ProbeContext, mmio_same_value_sram) == 72U);
static_assert(offsetof(ProbeContext, mmio_same_value_system_sysclk_conf) == 76U);
static_assert(offsetof(ProbeContext, mmio_same_value_extmem_dcache_ctrl1) == 80U);
static_assert(offsetof(ProbeContext, mmio_same_value_extmem_dcache_autoload_ctrl) == 84U);
static_assert(offsetof(ProbeContext, mmio_same_value_extmem_icache_ctrl1) == 88U);
static_assert(offsetof(ProbeContext, mmio_same_value_extmem_icache_autoload_ctrl) == 92U);
static_assert(offsetof(ProbeContext, mmio_rtc_date) == 96U);
static_assert(offsetof(ProbeContext, flash_handle) == 100U);
static_assert(offsetof(ProbeContext, rom_memset_buffer) == 104U);
static_assert(offsetof(ProbeContext, rom_cpu_ticks_per_us) == 108U);
static_assert(offsetof(ProbeContext, rom_reset_reason_core0) == 112U);
static_assert(offsetof(ProbeContext, rom_reset_reason_core1) == 116U);
static_assert(offsetof(ProbeContext, rom_i2c_bod_threshold) == 120U);
static_assert(I2C_BOD == 0x61);
static_assert(I2C_BOD_HOSTID == 1);
static_assert(I2C_BOD_THRESHOLD == 0x5);
static_assert(SYSTEM_CPU_PER_CONF_REG == 0x600c'0010U);
static_assert(SYSTEM_SYSCLK_CONF_REG == 0x600c'0060U);
static_assert(RTC_CNTL_STORE1_REG == 0x6000'8054U);
static_assert(RTC_CNTL_DATE_REG == 0x6000'81fcU);
static_assert(EXTMEM_DCACHE_CTRL1_REG == 0x600c'4004U);
static_assert(EXTMEM_DCACHE_AUTOLOAD_CTRL_REG == 0x600c'404cU);
static_assert(EXTMEM_ICACHE_CTRL1_REG == 0x600c'4064U);
static_assert(EXTMEM_ICACHE_AUTOLOAD_CTRL_REG == 0x600c'40a0U);
static_assert(EXTMEM_CACHE_STATE_REG == 0x600c'4130U);
static_assert(EXTMEM_CACHE_ACS_CNT_CLR_REG == 0x600c'40c4U);
static_assert(CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE == kIcacheLineBytes);
static_assert(CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE == kDcacheLineBytes);

extern "C" std::uint32_t tinydraw_sram_instruction_issue(const ProbeContext& context,
                                                          std::uint32_t seed);
extern "C" std::uint32_t tinydraw_sram_l32_dependent(const ProbeContext& context,
                                                      std::uint32_t seed);
extern "C" std::uint32_t tinydraw_sram_l32_independent(const ProbeContext& context,
                                                        std::uint32_t seed);
extern "C" std::uint32_t tinydraw_sram_s32_store_complete(const ProbeContext& context,
                                                           std::uint32_t seed);
extern "C" std::uint32_t tinydraw_dependent_load_sram(const ProbeContext& context,
                                                       std::uint32_t seed);
extern "C" std::uint32_t tinydraw_dependent_load_psram(const ProbeContext& context,
                                                        std::uint32_t seed);
extern "C" std::uint32_t tinydraw_dependent_load_flash(const ProbeContext& context,
                                                        std::uint32_t seed);
extern "C" std::uint32_t tinydraw_branch_baseline(const ProbeContext& context,
                                                   std::uint32_t seed);
extern "C" std::uint32_t tinydraw_branch_not_taken(const ProbeContext& context,
                                                    std::uint32_t seed);
extern "C" std::uint32_t tinydraw_branch_taken(const ProbeContext& context,
                                                std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_read_sram(const ProbeContext& context,
                                                  std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_read_system_cpu_per_conf(const ProbeContext& context,
                                                                 std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_read_rtc_store1(const ProbeContext& context,
                                                        std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_read_extmem_cache_state(const ProbeContext& context,
                                                                std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_read_system_sysclk_conf(const ProbeContext& context,
                                                                std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_read_extmem_dcache_ctrl1(const ProbeContext& context,
                                                                 std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_read_extmem_dcache_autoload_ctrl(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_read_extmem_icache_ctrl1(const ProbeContext& context,
                                                                 std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_read_extmem_icache_autoload_ctrl(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_read_rtc_date(const ProbeContext& context,
                                                       std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_write_sram(const ProbeContext& context,
                                                   std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_write_extmem_cache_counter_clear(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_write_same_value_sram(const ProbeContext& context,
                                                              std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_write_same_value_system_sysclk_conf(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_write_same_value_extmem_dcache_ctrl1(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_write_same_value_extmem_icache_ctrl1(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_write_same_value_sram_2048(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_write_same_value_system_sysclk_conf_2048(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_write_same_value_extmem_dcache_ctrl1_2048(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_mmio_write_same_value_extmem_icache_ctrl1_2048(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_rom_baseline_reset_reason_core0(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_rom_reset_reason_core0(const ProbeContext& context,
                                                           std::uint32_t seed);
extern "C" std::uint32_t tinydraw_rom_baseline_reset_reason_core1(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_rom_reset_reason_core1(const ProbeContext& context,
                                                           std::uint32_t seed);
extern "C" std::uint32_t tinydraw_rom_baseline_memset_zero(const ProbeContext& context,
                                                             std::uint32_t seed);
extern "C" std::uint32_t tinydraw_rom_memset_zero(const ProbeContext& context,
                                                    std::uint32_t seed);
extern "C" std::uint32_t tinydraw_rom_baseline_memset_52e0(const ProbeContext& context,
                                                             std::uint32_t seed);
extern "C" std::uint32_t tinydraw_rom_memset_52e0(const ProbeContext& context,
                                                    std::uint32_t seed);
extern "C" std::uint32_t tinydraw_rom_baseline_set_cpu_ticks(const ProbeContext& context,
                                                               std::uint32_t seed);
extern "C" std::uint32_t tinydraw_rom_set_cpu_ticks(const ProbeContext& context,
                                                      std::uint32_t seed);
extern "C" std::uint32_t tinydraw_rom_i2c_baseline_write_same_bod_threshold(
    const ProbeContext& context, std::uint32_t seed);
extern "C" std::uint32_t tinydraw_rom_i2c_write_same_bod_threshold(
    const ProbeContext& context, std::uint32_t seed);

#define DECLARE_DCACHE_BURST_PROBE(path, lines)                                                  \
  extern "C" std::uint32_t tinydraw_dcache_##path##_##lines##_lines(const ProbeContext& context, \
                                                                    std::uint32_t seed)

DECLARE_DCACHE_BURST_PROBE(psram, 1);
DECLARE_DCACHE_BURST_PROBE(psram, 2);
DECLARE_DCACHE_BURST_PROBE(psram, 4);
DECLARE_DCACHE_BURST_PROBE(psram, 8);
DECLARE_DCACHE_BURST_PROBE(psram, 16);
DECLARE_DCACHE_BURST_PROBE(flash, 1);
DECLARE_DCACHE_BURST_PROBE(flash, 2);
DECLARE_DCACHE_BURST_PROBE(flash, 4);
DECLARE_DCACHE_BURST_PROBE(flash, 8);
DECLARE_DCACHE_BURST_PROBE(flash, 16);
DECLARE_DCACHE_BURST_PROBE(sram, 16);

#undef DECLARE_DCACHE_BURST_PROBE

struct CacheCounters {
  std::uint32_t ibus_accesses;
  std::uint32_t ibus_misses;
  std::uint32_t dbus_accesses;
  std::uint32_t dbus_flash_misses;
  std::uint32_t dbus_psram_misses;
};

constexpr CacheCounters kInternalCacheHitSignature{};
constexpr CacheCounters kFlashIcacheHitSignature{
    .ibus_accesses = 62U,
    .ibus_misses = 0U,
    .dbus_accesses = 0U,
    .dbus_flash_misses = 0U,
    .dbus_psram_misses = 0U,
};
constexpr CacheCounters kExternalDcacheHitSignature{
    .ibus_accesses = 0U,
    .ibus_misses = 0U,
    .dbus_accesses = kMatchedDcacheLoads,
    .dbus_flash_misses = 0U,
    .dbus_psram_misses = 0U,
};
constexpr CacheCounters kDependentExternalDcacheHitSignature{
    .ibus_accesses = 0U,
    .ibus_misses = 0U,
    .dbus_accesses = kDependentLoads,
    .dbus_flash_misses = 0U,
    .dbus_psram_misses = 0U,
};
constexpr CacheCounters kRtcMmioReadSignature{
    .ibus_accesses = 176U,
    .ibus_misses = 0U,
    .dbus_accesses = 0U,
    .dbus_flash_misses = 0U,
    .dbus_psram_misses = 0U,
};
static_assert(kMatchedIcacheExecutedInstructions ==
              (kMatchedIcacheLines - 1U) * 16U + 6U + 2U);
static_assert(kFlashIcacheHitSignature.ibus_accesses ==
              kMatchedIcacheExecutedInstructions / 2U + 2U);

struct Rgb565CallWindow {
  std::uint32_t start_ccount;
  std::uint32_t end_ccount;
};

struct FlashInstructionBurstWindow {
  std::uint32_t start_ccount;
  std::uint32_t end_ccount;
  std::uint32_t sentinel;
};

extern "C" void tinydraw_measure_rgb565_call_window(
    Rgb565CallWindow* endpoints, const std::uint16_t* source, std::uint16_t* destination,
    int width, void (*oracle)(const std::uint16_t*, std::uint16_t*, int));

extern "C" void tinydraw_measure_flash_instruction_burst_window(FlashInstructionBurstWindow* window,
                                                                const void* call0_target);

struct RawSample {
  std::uint32_t start_ccount;
  std::uint32_t end_ccount;
  std::uint32_t cycles;
  std::uint32_t checksum;
  int start_core;
  int end_core;
  CacheCounters cache_counters;
  bool has_cache_counters;
  esp_err_t preparation_error;
};

using Kernel = std::uint32_t (*)(const ProbeContext&, std::uint32_t);
using Prepare = esp_err_t (*)(const ProbeContext&, Kernel, std::uint32_t);
using Finalize = std::uint32_t (*)(const ProbeContext&, std::uint32_t, std::uint32_t);
using Sampler = RawSample (*)(const ProbeContext&, Kernel, Finalize, std::uint32_t, bool);

struct Measurement {
  const char* id;
  const char* memory_path;
  std::uint32_t bytes_per_iteration;
  std::uint32_t iterations_per_sample;
  std::uint32_t warmup_iterations;
  Kernel kernel;
  Prepare prepare;
  Finalize finalize = nullptr;
  std::uint32_t expected_checksum = 0U;
  Sampler sampler = nullptr;
};

std::atomic<bool> g_contention_run{false};
std::atomic<bool> g_contention_ready{false};
std::atomic<bool> g_contention_done{false};
std::atomic<std::uint32_t> g_contention_checksum{0};
DRAM_ATTR volatile std::uint32_t g_prepare_checksum = 0;

void flush_console() {
  std::fflush(stdout);
  static_cast<void>(::fsync(::fileno(stdout)));
}

const char* reset_reason_name(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external-pin";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
      return "interrupt-watchdog";
    case ESP_RST_TASK_WDT:
      return "task-watchdog";
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep-sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_SDIO:
      return "sdio";
    case ESP_RST_USB:
      return "usb";
    case ESP_RST_JTAG:
      return "jtag";
    case ESP_RST_EFUSE:
      return "efuse";
    case ESP_RST_PWR_GLITCH:
      return "power-glitch";
    case ESP_RST_CPU_LOCKUP:
      return "cpu-lockup";
    case ESP_RST_UNKNOWN:
    default:
      return "unknown";
  }
}

const char* psram_mode() {
#if CONFIG_SPIRAM_MODE_OCT
  return "octal";
#elif CONFIG_SPIRAM_MODE_QUAD
  return "quad";
#else
  return "unknown";
#endif
}

const char* flash_mode() {
#if CONFIG_ESPTOOLPY_FLASHMODE_QIO
  return "qio";
#elif CONFIG_ESPTOOLPY_FLASHMODE_QOUT
  return "qout";
#elif CONFIG_ESPTOOLPY_FLASHMODE_DIO
  return "dio";
#elif CONFIG_ESPTOOLPY_FLASHMODE_DOUT
  return "dout";
#else
  return "unknown";
#endif
}

constexpr std::uint32_t flash_bus_hz() {
#if CONFIG_ESPTOOLPY_FLASHFREQ_120M
  return 120'000'000U;
#elif CONFIG_ESPTOOLPY_FLASHFREQ_80M
  return 80'000'000U;
#elif CONFIG_ESPTOOLPY_FLASHFREQ_40M
  return 40'000'000U;
#elif CONFIG_ESPTOOLPY_FLASHFREQ_20M
  return 20'000'000U;
#else
  return 0U;
#endif
}

void print_json_string(const char* value) {
  std::putchar('"');
  for (const auto* cursor = reinterpret_cast<const unsigned char*>(value); *cursor != 0U;
       ++cursor) {
    switch (*cursor) {
      case '"':
        std::fputs("\\\"", stdout);
        break;
      case '\\':
        std::fputs("\\\\", stdout);
        break;
      case '\n':
        std::fputs("\\n", stdout);
        break;
      case '\r':
        std::fputs("\\r", stdout);
        break;
      case '\t':
        std::fputs("\\t", stdout);
        break;
      default:
        if (*cursor < 0x20U) {
          std::printf("\\u%04x", static_cast<unsigned>(*cursor));
        } else {
          std::putchar(static_cast<int>(*cursor));
        }
        break;
    }
  }
  std::putchar('"');
}

void print_error(const char* phase, const char* reason, esp_err_t error = ESP_OK) {
  std::printf("%s{\"protocolVersion\":%" PRIu32 ",\"record\":\"error\",\"phase\":",
              kRecordPrefix, kProtocolVersion);
  print_json_string(phase);
  std::fputs(",\"reason\":", stdout);
  print_json_string(reason);
  std::printf(",\"espError\":%d}\n", static_cast<int>(error));
  flush_console();
}

void print_metadata(const ProbeContext& context) {
  esp_chip_info_t chip{};
  esp_chip_info(&chip);
  std::uint32_t flash_bytes = 0;
  if (esp_flash_get_size(nullptr, &flash_bytes) != ESP_OK) {
    print_error("metadata", "flash-size");
    return;
  }
  std::uint8_t mac[6]{};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) {
    print_error("metadata", "efuse-mac");
    return;
  }
  char boot_id[64]{};
  std::snprintf(boot_id, sizeof(boot_id), "%02x%02x%02x%02x%02x%02x-%08" PRIx32 "-%08" PRIx32,
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], esp_random(),
                static_cast<std::uint32_t>(esp_timer_get_time()));

  std::printf("%s{\"protocolVersion\":%" PRIu32
              ",\"record\":\"metadata\",\"schemaVersion\":1,"
              "\"receiptKind\":\"esp32s3-hardware-calibration\",\"captureMode\":\"hardware\","
              "\"git\":{\"repository\":",
              kRecordPrefix, kProtocolVersion);
  print_json_string(build_metadata::kGitRepository);
  std::fputs(",\"commit\":", stdout);
  print_json_string(build_metadata::kGitCommit);
  std::printf(",\"dirty\":%s},\"toolchain\":{\"target\":\"esp32s3\",\"espIdfVersion\":",
              build_metadata::kGitDirty ? "true" : "false");
  print_json_string(esp_get_idf_version());
  std::fputs(",\"compiler\":", stdout);
  print_json_string(build_metadata::kCompiler);
  std::fputs(",\"compilerVersion\":", stdout);
  print_json_string(build_metadata::kCompilerVersion);
  std::fputs("},\"sdkconfig\":{\"path\":", stdout);
  print_json_string(build_metadata::kSdkconfigPath);
  std::fputs(",\"sha256\":", stdout);
  print_json_string(build_metadata::kSdkconfigSha256);
  std::printf(",\"cpuHz\":%u,\"psramMode\":", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1'000'000U);
  print_json_string(psram_mode());
  std::printf(",\"psramBusHz\":%u,\"flashMode\":", CONFIG_SPIRAM_SPEED * 1'000'000U);
  print_json_string(flash_mode());
  if (flash_bus_hz() == 0U) {
    std::fputs(",\"flashBusHz\":null},\"boot\":{\"bootId\":", stdout);
  } else {
    std::printf(",\"flashBusHz\":%" PRIu32 "},\"boot\":{\"bootId\":", flash_bus_hz());
  }
  print_json_string(boot_id);
  std::fputs(",\"resetReason\":", stdout);
  print_json_string(reset_reason_name(esp_reset_reason()));
  std::printf(
      ",\"chipModel\":\"ESP32-S3\",\"chipRevision\":%u,\"cpuCores\":%u,"
      "\"psramBytes\":%lu,\"flashBytes\":%" PRIu32 "},"
      "\"counter\":{\"source\":\"xtensa-ccount\",\"bits\":32,\"hz\":%u,\"core\":%d},"
      "\"workingSets\":{\"sramStreamBytes\":%lu,\"psramHotBytes\":%lu,"
      "\"psramColdBytes\":%lu,\"flashMapBytes\":%lu,\"contentionBytes\":%lu}}\n",
      static_cast<unsigned>(chip.revision), static_cast<unsigned>(chip.cores),
      static_cast<unsigned long>(esp_psram_get_size()), flash_bytes,
      CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1'000'000U, esp_cpu_get_core_id(),
      static_cast<unsigned long>(kSramStreamBytes), static_cast<unsigned long>(kPsramHotBytes),
      static_cast<unsigned long>(kPsramColdBytes), static_cast<unsigned long>(kFlashMapBytes),
      static_cast<unsigned long>(kContentionBytes));
  flush_console();
  static_cast<void>(context);
}

FORCE_INLINE_ATTR std::uint32_t read_unaligned_le(const volatile std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) |
         (static_cast<std::uint32_t>(bytes[1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[3]) << 24U);
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR sram_aligned_dependent(const ProbeContext& context,
                                                             std::uint32_t seed) {
  const volatile std::uint32_t* chain = context.sram_dependent;
  std::uint32_t index = seed & (kDependentEntries - 1U);
  for (std::uint32_t load = 0; load < kDependentLoads; ++load) {
    index = chain[index];
  }
  return index;
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR sram_unaligned_dependent(const ProbeContext& context,
                                                               std::uint32_t seed) {
  const volatile std::uint8_t* chain = context.sram_unaligned_dependent;
  std::uint32_t index = seed & (kDependentEntries - 1U);
  for (std::uint32_t load = 0; load < kDependentLoads; ++load) {
    index = read_unaligned_le(chain + index * kUnalignedStride + 1U);
  }
  return index;
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR sram_aligned_stream(const ProbeContext& context,
                                                          std::uint32_t seed) {
  constexpr std::size_t kWords = kSramStreamBytes / sizeof(std::uint32_t);
  const volatile std::uint32_t* words = reinterpret_cast<const std::uint32_t*>(context.sram_stream);
  std::uint32_t sum = seed;
  const std::size_t start = seed & (kWords - 1U);
  for (std::size_t index = 0; index < kWords; ++index) {
    sum += words[(start + index) & (kWords - 1U)];
  }
  return sum;
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR sram_unaligned_stream(const ProbeContext& context,
                                                            std::uint32_t seed) {
  constexpr std::size_t kWords = kSramStreamBytes / sizeof(std::uint32_t);
  const volatile std::uint8_t* bytes = context.sram_stream + 1U;
  std::uint32_t sum = seed;
  for (std::size_t index = 0; index < kWords; ++index) {
    sum += read_unaligned_le(bytes + index * sizeof(std::uint32_t));
  }
  return sum;
}

[[gnu::noipa, gnu::aligned(32)]] void stage_pixels_swapped_scalar_oracle(
    const std::uint16_t* source, std::uint16_t* destination, int width) {
  for (int column = 0; column < width; ++column) {
    const std::uint16_t value = source[column];
    destination[column] = static_cast<std::uint16_t>((value >> 8U) | (value << 8U));
  }
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR rgb565_stage_five_scalar_oracle(const ProbeContext&,
                                                                      std::uint32_t) {
  stage_pixels_swapped_scalar_oracle(g_rgb565_stage_source, g_rgb565_stage_destination,
                                     static_cast<int>(kRgb565StagePixels));
  return 0U;
}

std::uint32_t finalize_rgb565_stage(const ProbeContext&, std::uint32_t,
                                    std::uint32_t) {
  std::uint32_t checksum = 2'166'136'261U;
  for (const std::uint16_t pixel : g_rgb565_stage_destination) {
    checksum = (checksum ^ pixel) * 16'777'619U;
  }
  return checksum;
}

std::uint32_t finalize_sram_store_complete(const ProbeContext& context, std::uint32_t seed,
                                           std::uint32_t kernel_result) {
  bool valid = kernel_result == seed;
  for (std::size_t offset = 0; offset < kSramStreamBytes; offset += sizeof(std::uint32_t)) {
    std::uint32_t word = 0;
    std::memcpy(&word, context.sram_stream + offset, sizeof(word));
    valid = valid && word == seed;
  }
  for (std::size_t index = 0; index < kSramStreamBytes + 8U; ++index) {
    context.sram_stream[index] = static_cast<std::uint8_t>(index * 37U + 11U);
  }
  return valid ? kSramStoreCompletionChecksum : 0U;
}

std::uint32_t mmio_state_preserved(volatile std::uint32_t* address,
                                   std::uint32_t expected_value) {
  return *address == expected_value ? kMmioStatePreservedChecksum : 0U;
}

std::uint32_t finalize_mmio_same_value_sram(const ProbeContext& context, std::uint32_t,
                                            std::uint32_t) {
  return mmio_state_preserved(context.mmio_sram_peer, context.mmio_same_value_sram);
}

std::uint32_t finalize_mmio_same_value_system_sysclk_conf(const ProbeContext& context,
                                                          std::uint32_t, std::uint32_t) {
  return mmio_state_preserved(context.mmio_system_sysclk_conf,
                              context.mmio_same_value_system_sysclk_conf);
}

std::uint32_t finalize_mmio_same_value_extmem_dcache_ctrl1(const ProbeContext& context,
                                                           std::uint32_t, std::uint32_t) {
  return mmio_state_preserved(context.mmio_extmem_dcache_ctrl1,
                              context.mmio_same_value_extmem_dcache_ctrl1);
}

std::uint32_t finalize_mmio_same_value_extmem_icache_ctrl1(const ProbeContext& context,
                                                           std::uint32_t, std::uint32_t) {
  return mmio_state_preserved(context.mmio_extmem_icache_ctrl1,
                              context.mmio_same_value_extmem_icache_ctrl1);
}

std::uint32_t rom_callback_state(bool preserved) {
  return preserved ? kRomCallbackStatePreservedChecksum : 0U;
}

std::uint32_t finalize_rom_baseline_reset_reason_core0(const ProbeContext& context,
                                                        std::uint32_t, std::uint32_t result) {
  return rom_callback_state(result == 0U &&
                            esp_rom_get_cpu_ticks_per_us() == context.rom_cpu_ticks_per_us);
}

std::uint32_t finalize_rom_reset_reason_core0(const ProbeContext& context, std::uint32_t,
                                               std::uint32_t result) {
  return rom_callback_state(result == context.rom_reset_reason_core0);
}

std::uint32_t finalize_rom_baseline_reset_reason_core1(const ProbeContext& context,
                                                        std::uint32_t, std::uint32_t result) {
  return rom_callback_state(result == 1U &&
                            esp_rom_get_cpu_ticks_per_us() == context.rom_cpu_ticks_per_us);
}

std::uint32_t finalize_rom_reset_reason_core1(const ProbeContext& context, std::uint32_t,
                                               std::uint32_t result) {
  return rom_callback_state(result == context.rom_reset_reason_core1);
}

std::uint32_t finalize_rom_memset_fill(const ProbeContext& context, std::uint32_t,
                                       std::uint32_t result) {
  const bool pointer_matches = result == reinterpret_cast<std::uintptr_t>(context.rom_memset_buffer);
  const bool fill_matches = std::all_of(context.rom_memset_buffer,
                                        context.rom_memset_buffer + kRomMemsetBytes,
                                        [](std::uint8_t value) { return value == kRomMemsetFill; });
  return rom_callback_state(pointer_matches && fill_matches);
}

std::uint32_t finalize_rom_memset_zeroed(const ProbeContext& context, std::uint32_t,
                                         std::uint32_t result) {
  const bool pointer_matches = result == reinterpret_cast<std::uintptr_t>(context.rom_memset_buffer);
  const bool zeroed = std::all_of(context.rom_memset_buffer,
                                  context.rom_memset_buffer + kRomMemsetBytes,
                                  [](std::uint8_t value) { return value == 0U; });
  return rom_callback_state(pointer_matches && zeroed);
}

std::uint32_t finalize_rom_cpu_ticks(const ProbeContext& context, std::uint32_t,
                                     std::uint32_t result) {
  return rom_callback_state(result == 0U &&
                            esp_rom_get_cpu_ticks_per_us() == context.rom_cpu_ticks_per_us);
}

std::uint32_t finalize_rom_i2c_same_bod_threshold(const ProbeContext& context,
                                                   std::uint32_t, std::uint32_t result) {
  const std::uint8_t after =
      esp_rom_regi2c_read(I2C_BOD, I2C_BOD_HOSTID, I2C_BOD_THRESHOLD);
  return rom_callback_state(result == 0U && after == context.rom_i2c_bod_threshold);
}

FORCE_INLINE_ATTR std::uint32_t psram_sequential(const ProbeContext& context, std::uint32_t seed,
                                                 std::size_t bytes) {
  const volatile std::uint32_t* words = context.psram;
  const std::size_t count = bytes / sizeof(std::uint32_t);
  std::uint32_t sum = seed;
  for (std::size_t index = 0; index < count; ++index) {
    sum += words[index];
  }
  return sum;
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR psram_hot_sequential(const ProbeContext& context,
                                                           std::uint32_t seed) {
  return psram_sequential(context, seed, kPsramHotBytes);
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR psram_cold_sequential(const ProbeContext& context,
                                                            std::uint32_t seed) {
  return psram_sequential(context, seed, kPsramColdBytes);
}

FORCE_INLINE_ATTR std::uint32_t random_reads(const volatile std::uint32_t* words,
                                             std::size_t working_set_words, std::uint32_t loads,
                                             std::uint32_t seed) {
  std::uint32_t state = seed | 1U;
  std::uint32_t sum = seed;
  const std::size_t mask = working_set_words - 1U;
  for (std::uint32_t load = 0; load < loads; ++load) {
    state = state * 1'664'525U + 1'013'904'223U;
    sum += words[(state >> 2U) & mask];
  }
  return sum;
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR psram_hot_random(const ProbeContext& context,
                                                       std::uint32_t seed) {
  return random_reads(context.psram, kPsramHotBytes / sizeof(std::uint32_t), kRandomHotLoads, seed);
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR psram_cold_random(const ProbeContext& context,
                                                        std::uint32_t seed) {
  return random_reads(context.psram, kPsramColdBytes / sizeof(std::uint32_t), kRandomColdLoads,
                      seed);
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR flash_hot_sequential(const ProbeContext& context,
                                                           std::uint32_t seed) {
  const volatile std::uint32_t* words = context.flash;
  std::uint32_t sum = seed;
  for (std::size_t index = 0; index < kPsramHotBytes / sizeof(std::uint32_t); ++index) {
    sum += words[index];
  }
  return sum;
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR flash_cold_sequential(const ProbeContext& context,
                                                            std::uint32_t seed) {
  const volatile std::uint32_t* words = context.flash;
  std::uint32_t sum = seed;
  for (std::size_t index = 0; index < kFlashMapBytes / sizeof(std::uint32_t); ++index) {
    sum += words[index];
  }
  return sum;
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR flash_hot_random(const ProbeContext& context,
                                                       std::uint32_t seed) {
  return random_reads(context.flash, kPsramHotBytes / sizeof(std::uint32_t), kRandomHotLoads, seed);
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR flash_cold_random(const ProbeContext& context,
                                                        std::uint32_t seed) {
  return random_reads(context.flash, kFlashMapBytes / sizeof(std::uint32_t), kRandomColdLoads,
                      seed);
}

std::uint32_t NOINLINE_ATTR flash_instruction_body(std::uint32_t seed) {
  std::uint32_t value = seed;
  for (std::uint32_t index = 0; index < 256U; ++index) {
    asm volatile("nop; nop; nop; nop" ::: "memory");
    value = (value << 5U) ^ (value >> 2U) ^ index;
  }
  return value;
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR flash_instruction_probe(const ProbeContext&,
                                                              std::uint32_t seed) {
  return flash_instruction_body(seed);
}

esp_err_t prepare_none(const ProbeContext&, Kernel, std::uint32_t) { return ESP_OK; }

esp_err_t prepare_rom_memset_fill(const ProbeContext& context, Kernel, std::uint32_t) {
  std::fill(context.rom_memset_buffer, context.rom_memset_buffer + kRomMemsetBytes,
            kRomMemsetFill);
  return ESP_OK;
}

esp_err_t prepare_hot(const ProbeContext& context, Kernel kernel, std::uint32_t seed) {
  g_prepare_checksum = kernel(context, seed ^ 0xa5a5'a5a5U);
  return ESP_OK;
}

esp_err_t prepare_psram_hot(const ProbeContext& context, Kernel kernel, std::uint32_t seed) {
  g_prepare_checksum = psram_hot_sequential(context, seed);
  g_prepare_checksum ^= kernel(context, seed ^ 0x55aa'55aaU);
  return ESP_OK;
}

esp_err_t prepare_psram_cold(const ProbeContext& context, Kernel, std::uint32_t) {
  return esp_cache_msync(context.psram, kPsramColdBytes,
                         ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}

esp_err_t prepare_flash_hot(const ProbeContext& context, Kernel kernel, std::uint32_t seed) {
  g_prepare_checksum = flash_hot_sequential(context, seed);
  g_prepare_checksum ^= kernel(context, seed ^ 0xc3c3'c3c3U);
  return ESP_OK;
}

esp_err_t prepare_flash_cold(const ProbeContext& context, Kernel, std::uint32_t) {
  return esp_cache_msync(const_cast<std::uint32_t*>(context.flash), kFlashMapBytes,
                         ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}

esp_err_t prepare_dcache_burst_hot(const ProbeContext& context, Kernel kernel, std::uint32_t seed) {
  g_prepare_checksum = kernel(context, seed ^ 0xdca0'0000U);
  return ESP_OK;
}

esp_err_t prepare_dependent_load_hot(const ProbeContext& context, Kernel kernel,
                                     std::uint32_t seed) {
  // Warm the exact seed-dependent address chain that the following sample repeats.
  g_prepare_checksum = kernel(context, seed);
  return ESP_OK;
}

template <std::size_t Lines>
esp_err_t prepare_psram_dcache_burst_cold(const ProbeContext& context, Kernel, std::uint32_t) {
  static_assert(Lines >= 1U && Lines <= 16U);
  return esp_cache_msync(context.psram, Lines * kDcacheLineBytes,
                         ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}

template <std::size_t Lines>
esp_err_t prepare_flash_dcache_burst_cold(const ProbeContext& context, Kernel, std::uint32_t) {
  static_assert(Lines >= 1U && Lines <= 16U);
  return esp_cache_msync(const_cast<std::uint32_t*>(context.flash), Lines * kDcacheLineBytes,
                         ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
}

esp_err_t prepare_flash_instruction_cold(const ProbeContext&, Kernel, std::uint32_t) {
  const auto address = reinterpret_cast<std::uintptr_t>(&flash_instruction_body);
  constexpr std::size_t line_size = CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE;
  static_assert(line_size > 0U && (line_size & (line_size - 1U)) == 0U);
  const auto aligned_start = address & ~(static_cast<std::uintptr_t>(line_size) - 1U);
  const auto aligned_end =
      (address + 64U + line_size - 1U) & ~(static_cast<std::uintptr_t>(line_size) - 1U);
  return esp_cache_msync(reinterpret_cast<void*>(aligned_start), aligned_end - aligned_start,
                         ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_INST);
}

struct FlashInstructionBurstRange {
  const void* start;
  const void* end;
};

template <std::size_t Lines>
struct FlashInstructionBurstSymbols;

FORCE_INLINE_ATTR FlashInstructionBurstRange matched_iram_instruction_range() {
  return {tinydraw_iram_instruction_hit_8_lines_start,
          tinydraw_iram_instruction_hit_8_lines_end};
}

FORCE_INLINE_ATTR FlashInstructionBurstRange matched_flash_instruction_range() {
  return {tinydraw_flash_instruction_burst_8_lines_start,
          tinydraw_flash_instruction_burst_8_lines_end};
}

#define DEFINE_FLASH_INSTRUCTION_BURST_RANGE(lines)                   \
  template <>                                                         \
  struct FlashInstructionBurstSymbols<lines> {                        \
    FORCE_INLINE_ATTR FlashInstructionBurstRange range() {            \
      return {tinydraw_flash_instruction_burst_##lines##_lines_start, \
              tinydraw_flash_instruction_burst_##lines##_lines_end};  \
    }                                                                 \
  };

DEFINE_FLASH_INSTRUCTION_BURST_RANGE(1)
DEFINE_FLASH_INSTRUCTION_BURST_RANGE(2)
DEFINE_FLASH_INSTRUCTION_BURST_RANGE(4)
DEFINE_FLASH_INSTRUCTION_BURST_RANGE(8)

#undef DEFINE_FLASH_INSTRUCTION_BURST_RANGE

template <std::size_t Lines>
std::uint32_t IRAM_ATTR NOINLINE_ATTR flash_instruction_burst_kernel(const ProbeContext&,
                                                                     std::uint32_t) {
  const FlashInstructionBurstRange range = FlashInstructionBurstSymbols<Lines>::range();
  FlashInstructionBurstWindow window{};
  tinydraw_measure_flash_instruction_burst_window(&window, range.start);
  return window.sentinel;
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR matched_iram_instruction_kernel(const ProbeContext&,
                                                                      std::uint32_t) {
  FlashInstructionBurstWindow window{};
  tinydraw_measure_flash_instruction_burst_window(&window,
                                                   matched_iram_instruction_range().start);
  return window.sentinel;
}

std::uint32_t IRAM_ATTR NOINLINE_ATTR matched_flash_instruction_kernel(const ProbeContext&,
                                                                       std::uint32_t) {
  FlashInstructionBurstWindow window{};
  tinydraw_measure_flash_instruction_burst_window(&window,
                                                   matched_flash_instruction_range().start);
  return window.sentinel;
}

void reset_rgb565_stage_data() {
  std::copy(kRgb565StageInput.begin(), kRgb565StageInput.end(),
            std::begin(g_rgb565_stage_source));
  std::fill(std::begin(g_rgb565_stage_destination), std::end(g_rgb565_stage_destination), 0U);
}

esp_err_t prepare_rgb565_stage_hot(const ProbeContext&, Kernel, std::uint32_t) {
  reset_rgb565_stage_data();
  return ESP_OK;
}

esp_err_t prepare_rgb565_stage_cold(const ProbeContext&, Kernel, std::uint32_t) {
  reset_rgb565_stage_data();
  const auto address = reinterpret_cast<std::uintptr_t>(&stage_pixels_swapped_scalar_oracle);
  constexpr std::size_t line_size = CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE;
  static_assert(line_size == 32U);
  static_assert(kRgb565OracleCodeBytes > line_size && kRgb565OracleCodeBytes <= line_size * 2U);
  const auto aligned_start = address & ~(static_cast<std::uintptr_t>(line_size) - 1U);
  const auto aligned_end =
      (address + kRgb565OracleCodeBytes + line_size - 1U) &
      ~(static_cast<std::uintptr_t>(line_size) - 1U);
  return esp_cache_msync(
      reinterpret_cast<void*>(aligned_start), aligned_end - aligned_start,
      ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE |
          ESP_CACHE_MSYNC_FLAG_TYPE_INST);
}

FORCE_INLINE_ATTR void clear_cache_counters() {
  REG_WRITE(EXTMEM_CACHE_ACS_CNT_CLR_REG,
            EXTMEM_ICACHE_ACS_CNT_CLR | EXTMEM_DCACHE_ACS_CNT_CLR);
  asm volatile("memw" ::: "memory");
}

FORCE_INLINE_ATTR CacheCounters read_cache_counters() {
  asm volatile("memw" ::: "memory");
  return {
      .ibus_accesses = REG_READ(EXTMEM_IBUS_ACS_CNT_REG),
      .ibus_misses = REG_READ(EXTMEM_IBUS_ACS_MISS_CNT_REG),
      .dbus_accesses = REG_READ(EXTMEM_DBUS_ACS_CNT_REG),
      .dbus_flash_misses = REG_READ(EXTMEM_DBUS_ACS_FLASH_MISS_CNT_REG),
      .dbus_psram_misses = REG_READ(EXTMEM_DBUS_ACS_SPIRAM_MISS_CNT_REG),
  };
}

FORCE_INLINE_ATTR bool cache_counters_equal(const CacheCounters& actual,
                                            const CacheCounters& expected) {
  return actual.ibus_accesses == expected.ibus_accesses &&
         actual.ibus_misses == expected.ibus_misses &&
         actual.dbus_accesses == expected.dbus_accesses &&
         actual.dbus_flash_misses == expected.dbus_flash_misses &&
         actual.dbus_psram_misses == expected.dbus_psram_misses;
}

FORCE_INLINE_ATTR void require_cache_counter_signature(RawSample& sample,
                                                       const CacheCounters& expected,
                                                       bool collect_cache_counters) {
  if (collect_cache_counters && !cache_counters_equal(sample.cache_counters, expected)) {
    sample.preparation_error = ESP_ERR_INVALID_RESPONSE;
  }
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_once(const ProbeContext& context, Kernel kernel,
                                               Finalize finalize, std::uint32_t seed,
                                               bool collect_cache_counters) {
  RawSample sample{};
  if (collect_cache_counters) clear_cache_counters();
  sample.start_core = esp_cpu_get_core_id();
  sample.start_ccount = esp_cpu_get_cycle_count();
  const std::uint32_t kernel_result = kernel(context, seed);
  sample.end_ccount = esp_cpu_get_cycle_count();
  sample.end_core = esp_cpu_get_core_id();
  if (collect_cache_counters) sample.cache_counters = read_cache_counters();
  sample.cycles = sample.end_ccount - sample.start_ccount;
  sample.checksum = finalize == nullptr ? kernel_result : finalize(context, seed, kernel_result);
  sample.has_cache_counters = collect_cache_counters;
  return sample;
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_matched_dcache_internal_once(
    const ProbeContext& context, Kernel kernel, Finalize finalize, std::uint32_t seed,
    bool collect_cache_counters) {
  RawSample sample =
      measure_once(context, kernel, finalize, seed, collect_cache_counters);
  require_cache_counter_signature(sample, kInternalCacheHitSignature, collect_cache_counters);
  return sample;
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_matched_dcache_external_once(
    const ProbeContext& context, Kernel kernel, Finalize finalize, std::uint32_t seed,
    bool collect_cache_counters) {
  RawSample sample =
      measure_once(context, kernel, finalize, seed, collect_cache_counters);
  require_cache_counter_signature(sample, kExternalDcacheHitSignature,
                                  collect_cache_counters);
  return sample;
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_dependent_load_internal_once(
    const ProbeContext& context, Kernel kernel, Finalize finalize, std::uint32_t seed,
    bool collect_cache_counters) {
  RawSample sample =
      measure_once(context, kernel, finalize, seed, collect_cache_counters);
  require_cache_counter_signature(sample, kInternalCacheHitSignature, collect_cache_counters);
  return sample;
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_dependent_load_external_once(
    const ProbeContext& context, Kernel kernel, Finalize finalize, std::uint32_t seed,
    bool collect_cache_counters) {
  RawSample sample =
      measure_once(context, kernel, finalize, seed, collect_cache_counters);
  require_cache_counter_signature(sample, kDependentExternalDcacheHitSignature,
                                  collect_cache_counters);
  return sample;
}

FORCE_INLINE_ATTR RawSample measure_mmio_with_signature(
    const ProbeContext& context, Kernel kernel, Finalize finalize, std::uint32_t seed,
    bool collect_cache_counters, const CacheCounters* expected_counters) {
  RawSample sample{};
  if (collect_cache_counters) clear_cache_counters();
  std::uint32_t saved_ps = 0;
  asm volatile("rsil %0, 15" : "=a"(saved_ps) : : "memory");
  sample.start_core = esp_cpu_get_core_id();
  sample.start_ccount = esp_cpu_get_cycle_count();
  const std::uint32_t kernel_result = kernel(context, seed);
  sample.end_ccount = esp_cpu_get_cycle_count();
  sample.end_core = esp_cpu_get_core_id();
  if (collect_cache_counters) sample.cache_counters = read_cache_counters();
  asm volatile("wsr %0, ps\nrsync" : : "a"(saved_ps) : "memory");
  sample.cycles = sample.end_ccount - sample.start_ccount;
  sample.checksum = finalize == nullptr ? kernel_result : finalize(context, seed, kernel_result);
  sample.has_cache_counters = collect_cache_counters;
  if (expected_counters != nullptr) {
    require_cache_counter_signature(sample, *expected_counters, collect_cache_counters);
  }
  return sample;
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_mmio_once(
    const ProbeContext& context, Kernel kernel, Finalize finalize, std::uint32_t seed,
    bool collect_cache_counters) {
  return measure_mmio_with_signature(context, kernel, finalize, seed, collect_cache_counters,
                                     &kInternalCacheHitSignature);
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_rtc_mmio_once(
    const ProbeContext& context, Kernel kernel, Finalize finalize, std::uint32_t seed,
    bool collect_cache_counters) {
  return measure_mmio_with_signature(context, kernel, finalize, seed, collect_cache_counters,
                                     &kRtcMmioReadSignature);
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_observed_rtc_mmio_once(
    const ProbeContext& context, Kernel kernel, Finalize finalize, std::uint32_t seed,
    bool collect_cache_counters) {
  return measure_mmio_with_signature(context, kernel, finalize, seed, collect_cache_counters,
                                     nullptr);
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_rom_callback_once(
    const ProbeContext& context, Kernel kernel, Finalize finalize, std::uint32_t seed,
    bool collect_cache_counters) {
  return measure_mmio_with_signature(context, kernel, finalize, seed, collect_cache_counters,
                                     nullptr);
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_rom_i2c_callback_once(
    const ProbeContext& context, Kernel kernel, Finalize finalize, std::uint32_t seed,
    bool collect_cache_counters) {
  RawSample sample{};
  if (collect_cache_counters) clear_cache_counters();
  std::uint32_t saved_ps = 0U;
  // The pinned full-boot callback arrives at interrupt level 3. Retain that
  // exact caller state while excluding ordinary task and peripheral IRQs.
  asm volatile("rsil %0, 3" : "=a"(saved_ps) : : "memory");
  sample.start_core = esp_cpu_get_core_id();
  sample.start_ccount = esp_cpu_get_cycle_count();
  const std::uint32_t kernel_result = kernel(context, seed);
  sample.end_ccount = esp_cpu_get_cycle_count();
  sample.end_core = esp_cpu_get_core_id();
  if (collect_cache_counters) sample.cache_counters = read_cache_counters();
  asm volatile("wsr %0, ps\nrsync" : : "a"(saved_ps) : "memory");
  sample.cycles = sample.end_ccount - sample.start_ccount;
  sample.checksum = finalize == nullptr ? kernel_result : finalize(context, seed, kernel_result);
  sample.has_cache_counters = collect_cache_counters;
  require_cache_counter_signature(sample, kInternalCacheHitSignature, collect_cache_counters);
  return sample;
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_conditional_branch_once(
    const ProbeContext& context, Kernel kernel, Finalize finalize, std::uint32_t seed,
    bool collect_cache_counters) {
  RawSample sample =
      measure_once(context, kernel, finalize, seed, collect_cache_counters);
  require_cache_counter_signature(sample, kInternalCacheHitSignature, collect_cache_counters);
  return sample;
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_rgb565_stage_once(const ProbeContext& context, Kernel,
                                                            Finalize finalize, std::uint32_t seed,
                                                            bool collect_cache_counters) {
  RawSample sample{};
  Rgb565CallWindow call_window{};
  const std::uint16_t* const source = g_rgb565_stage_source;
  std::uint16_t* const destination = g_rgb565_stage_destination;
  constexpr int width = static_cast<int>(kRgb565StagePixels);
  if (collect_cache_counters) clear_cache_counters();

  // The assembly boundary materializes these arguments before reading CCOUNT, keeps the start in
  // its caller register window, and stores both endpoints only after the oracle returns.
  sample.start_core = esp_cpu_get_core_id();
  tinydraw_measure_rgb565_call_window(&call_window, source, destination, width,
                                      &stage_pixels_swapped_scalar_oracle);
  sample.end_core = esp_cpu_get_core_id();
  sample.start_ccount = call_window.start_ccount;
  sample.end_ccount = call_window.end_ccount;

  if (collect_cache_counters) sample.cache_counters = read_cache_counters();
  sample.cycles = sample.end_ccount - sample.start_ccount;
  sample.checksum = finalize(context, seed, 0U);
  sample.has_cache_counters = collect_cache_counters;
  return sample;
}

template <std::size_t Lines, bool Cold>
RawSample IRAM_ATTR NOINLINE_ATTR measure_flash_instruction_burst_once(
    const ProbeContext&, Kernel, Finalize, std::uint32_t, bool collect_cache_counters) {
  static_assert(Lines == 1U || Lines == 2U || Lines == 4U || Lines == 8U);
  RawSample sample{};
  const FlashInstructionBurstRange range = FlashInstructionBurstSymbols<Lines>::range();
  const auto start = reinterpret_cast<std::uintptr_t>(range.start);
  const auto end = reinterpret_cast<std::uintptr_t>(range.end);
  if ((start & (kIcacheLineBytes - 1U)) != 0U || end - start != Lines * kIcacheLineBytes) {
    sample.preparation_error = ESP_ERR_INVALID_SIZE;
    return sample;
  }

  if constexpr (Cold) {
    sample.preparation_error =
        esp_cache_msync(reinterpret_cast<void*>(start), end - start,
                        ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE |
                            ESP_CACHE_MSYNC_FLAG_TYPE_INST);
    if (sample.preparation_error != ESP_OK) return sample;
  } else {
    FlashInstructionBurstWindow warm_window{};
    tinydraw_measure_flash_instruction_burst_window(&warm_window, range.start);
    if (warm_window.sentinel != Lines) {
      sample.preparation_error = ESP_ERR_INVALID_RESPONSE;
      return sample;
    }
  }

  // The cold invalidation or hot helper warmup is complete before this clear. The only external
  // instruction fetch in the CCOUNT interval is the helper's one call0 target invocation.
  sample.start_core = esp_cpu_get_core_id();
  if (collect_cache_counters) clear_cache_counters();
  FlashInstructionBurstWindow window{};
  tinydraw_measure_flash_instruction_burst_window(&window, range.start);
  sample.end_core = esp_cpu_get_core_id();
  if (collect_cache_counters) sample.cache_counters = read_cache_counters();

  sample.start_ccount = window.start_ccount;
  sample.end_ccount = window.end_ccount;
  sample.cycles = sample.end_ccount - sample.start_ccount;
  sample.checksum = window.sentinel;
  sample.has_cache_counters = collect_cache_counters;
  return sample;
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_matched_icache_target_once(
    const FlashInstructionBurstRange& range, const CacheCounters& expected_counters,
    bool collect_cache_counters) {
  RawSample sample{};
  const auto start = reinterpret_cast<std::uintptr_t>(range.start);
  const auto end = reinterpret_cast<std::uintptr_t>(range.end);
  if ((start & (kIcacheLineBytes - 1U)) != 0U ||
      end - start != kMatchedIcacheLines * kIcacheLineBytes) {
    sample.preparation_error = ESP_ERR_INVALID_SIZE;
    return sample;
  }

  FlashInstructionBurstWindow warm_window{};
  tinydraw_measure_flash_instruction_burst_window(&warm_window, range.start);
  if (warm_window.sentinel != kMatchedIcacheLines) {
    sample.preparation_error = ESP_ERR_INVALID_RESPONSE;
    return sample;
  }

  sample.start_core = esp_cpu_get_core_id();
  if (collect_cache_counters) clear_cache_counters();
  FlashInstructionBurstWindow window{};
  tinydraw_measure_flash_instruction_burst_window(&window, range.start);
  sample.end_core = esp_cpu_get_core_id();
  if (collect_cache_counters) sample.cache_counters = read_cache_counters();

  sample.start_ccount = window.start_ccount;
  sample.end_ccount = window.end_ccount;
  sample.cycles = sample.end_ccount - sample.start_ccount;
  sample.checksum = window.sentinel;
  sample.has_cache_counters = collect_cache_counters;
  require_cache_counter_signature(sample, expected_counters, collect_cache_counters);
  return sample;
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_matched_icache_iram_once(
    const ProbeContext&, Kernel, Finalize, std::uint32_t, bool collect_cache_counters) {
  return measure_matched_icache_target_once(matched_iram_instruction_range(),
                                            kInternalCacheHitSignature,
                                            collect_cache_counters);
}

RawSample IRAM_ATTR NOINLINE_ATTR measure_matched_icache_flash_once(
    const ProbeContext&, Kernel, Finalize, std::uint32_t, bool collect_cache_counters) {
  return measure_matched_icache_target_once(matched_flash_instruction_range(),
                                            kFlashIcacheHitSignature,
                                            collect_cache_counters);
}

void print_measurement_start(const Measurement& measurement, const char* measurement_id) {
  std::printf("%s{\"protocolVersion\":%" PRIu32
              ",\"record\":\"measurement-start\",\"measurementId\":",
              kRecordPrefix, kProtocolVersion);
  print_json_string(measurement_id);
  std::fputs(",\"measurement\":{\"kind\":\"ccount-kernel\",\"kernel\":", stdout);
  print_json_string(measurement_id);
  std::fputs(",\"memoryPath\":", stdout);
  print_json_string(measurement.memory_path);
  std::printf(",\"bytesPerIteration\":%" PRIu32 ",\"iterationsPerSample\":%" PRIu32
              ",\"warmupIterations\":%" PRIu32 "}}\n",
              measurement.bytes_per_iteration, measurement.iterations_per_sample,
              measurement.warmup_iterations);
  flush_console();
}

bool run_measurement(const ProbeContext& context, const Measurement& measurement,
                     const char* contention_mode) {
  char measurement_id[96]{};
  std::snprintf(measurement_id, sizeof(measurement_id), "%s_%s", measurement.id, contention_mode);
  for (std::uint32_t warmup = 0; warmup < measurement.warmup_iterations; ++warmup) {
    g_prepare_checksum = measurement.kernel(context, warmup + 1U);
  }
  print_measurement_start(measurement, measurement_id);
  for (int ordinal = 0; ordinal < kSamplesPerMeasurement; ++ordinal) {
    const std::uint32_t seed = 0x9e37'79b9U * static_cast<std::uint32_t>(ordinal + 1);
    const esp_err_t prepared = measurement.prepare(context, measurement.kernel, seed);
    if (prepared != ESP_OK) {
      print_error(measurement_id, "cache-prepare", prepared);
      return false;
    }
    const bool collect_cache_counters = std::strcmp(contention_mode, "single_core") == 0;
    const Sampler sampler = measurement.sampler == nullptr ? measure_once : measurement.sampler;
    const RawSample sample = sampler(context, measurement.kernel, measurement.finalize, seed,
                                     collect_cache_counters);
    if (sample.preparation_error != ESP_OK) {
      print_error(measurement_id, "cache-prepare", sample.preparation_error);
      return false;
    }
    if (measurement.expected_checksum != 0U && sample.checksum != measurement.expected_checksum) {
      print_error(measurement_id, "checksum");
      return false;
    }
    std::printf(
        "%s{\"protocolVersion\":%" PRIu32
        ",\"record\":\"sample\",\"measurementId\":",
        kRecordPrefix, kProtocolVersion);
    print_json_string(measurement_id);
    std::printf(
        ",\"sample\":{\"ordinal\":%d,\"startCore\":%d,\"endCore\":%d,"
        "\"startCcount\":%" PRIu32 ",\"endCcount\":%" PRIu32 ",\"cycles\":%" PRIu32,
        ordinal, sample.start_core, sample.end_core, sample.start_ccount, sample.end_ccount,
        sample.cycles);
    if (sample.has_cache_counters) {
      std::printf(
          ",\"cacheCounters\":{\"ibus\":{\"accesses\":%" PRIu32 ",\"misses\":%" PRIu32
          "},\"dbus\":{\"accesses\":%" PRIu32 ",\"flashMisses\":%" PRIu32
          ",\"psramMisses\":%" PRIu32 "}}",
          sample.cache_counters.ibus_accesses, sample.cache_counters.ibus_misses,
          sample.cache_counters.dbus_accesses, sample.cache_counters.dbus_flash_misses,
          sample.cache_counters.dbus_psram_misses);
    }
    std::printf("},\"checksum\":%" PRIu32 "}\n", sample.checksum);
    // USB Serial/JTAG has a small transmit FIFO. Drain each complete evidence record before the
    // next sample so sustained probe output cannot silently lose bytes on the host boundary.
    flush_console();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  std::printf("%s{\"protocolVersion\":%" PRIu32
              ",\"record\":\"measurement-complete\",\"measurementId\":",
              kRecordPrefix, kProtocolVersion);
  print_json_string(measurement_id);
  std::printf(",\"samples\":%d}\n", kSamplesPerMeasurement);
  flush_console();
  vTaskDelay(1);
  return true;
}

void IRAM_ATTR contention_task(void* argument) {
  auto* words = static_cast<volatile std::uint32_t*>(argument);
  constexpr std::size_t kWords = kContentionBytes / sizeof(std::uint32_t);
  std::uint32_t sum = 0;
  g_contention_ready.store(true, std::memory_order_release);
  while (g_contention_run.load(std::memory_order_acquire)) {
    for (std::size_t index = 0; index < kWords; index += 4U) {
      sum += words[index];
    }
    g_contention_checksum.store(sum, std::memory_order_relaxed);
  }
  g_contention_done.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

bool start_contention(ProbeContext& context) {
  g_contention_done.store(false, std::memory_order_relaxed);
  g_contention_ready.store(false, std::memory_order_relaxed);
  g_contention_run.store(true, std::memory_order_release);
  if (xTaskCreatePinnedToCore(contention_task, "timing_contend", 4096U, context.contention,
                              configMAX_PRIORITIES - 2U, nullptr, 1) != pdPASS) {
    g_contention_run.store(false, std::memory_order_release);
    print_error("contention", "task-create");
    return false;
  }
  while (!g_contention_ready.load(std::memory_order_acquire)) {
    vTaskDelay(1);
  }
  return true;
}

bool initialize_context(ProbeContext& context) {
  context.mmio_sram_peer = &g_mmio_sram_peer;
  context.mmio_system_cpu_per_conf =
      reinterpret_cast<volatile std::uint32_t*>(SYSTEM_CPU_PER_CONF_REG);
  context.mmio_rtc_store1 =
      reinterpret_cast<volatile std::uint32_t*>(RTC_CNTL_STORE1_REG);
  context.mmio_extmem_cache_state =
      reinterpret_cast<volatile std::uint32_t*>(EXTMEM_CACHE_STATE_REG);
  context.mmio_extmem_cache_counter_clear =
      reinterpret_cast<volatile std::uint32_t*>(EXTMEM_CACHE_ACS_CNT_CLR_REG);
  context.mmio_system_sysclk_conf =
      reinterpret_cast<volatile std::uint32_t*>(SYSTEM_SYSCLK_CONF_REG);
  context.mmio_extmem_dcache_ctrl1 =
      reinterpret_cast<volatile std::uint32_t*>(EXTMEM_DCACHE_CTRL1_REG);
  context.mmio_extmem_dcache_autoload_ctrl =
      reinterpret_cast<volatile std::uint32_t*>(EXTMEM_DCACHE_AUTOLOAD_CTRL_REG);
  context.mmio_extmem_icache_ctrl1 =
      reinterpret_cast<volatile std::uint32_t*>(EXTMEM_ICACHE_CTRL1_REG);
  context.mmio_extmem_icache_autoload_ctrl =
      reinterpret_cast<volatile std::uint32_t*>(EXTMEM_ICACHE_AUTOLOAD_CTRL_REG);
  context.mmio_rtc_date = reinterpret_cast<volatile std::uint32_t*>(RTC_CNTL_DATE_REG);
  context.mmio_same_value_sram = g_mmio_sram_peer;
  context.mmio_same_value_system_sysclk_conf = *context.mmio_system_sysclk_conf;
  context.mmio_same_value_extmem_dcache_ctrl1 = *context.mmio_extmem_dcache_ctrl1;
  context.mmio_same_value_extmem_dcache_autoload_ctrl =
      *context.mmio_extmem_dcache_autoload_ctrl;
  context.mmio_same_value_extmem_icache_ctrl1 = *context.mmio_extmem_icache_ctrl1;
  context.mmio_same_value_extmem_icache_autoload_ctrl =
      *context.mmio_extmem_icache_autoload_ctrl;
  if ((context.mmio_same_value_extmem_dcache_autoload_ctrl &
       EXTMEM_DCACHE_AUTOLOAD_BUFFER_CLEAR_M) != 0U ||
      (context.mmio_same_value_extmem_icache_autoload_ctrl &
       EXTMEM_ICACHE_AUTOLOAD_BUFFER_CLEAR_M) != 0U) {
    print_error("initialize", "autoload-clear-active");
    return false;
  }
  std::printf(
      "TINYDRAW_MMIO_BOOT_VALUES system_sysclk_conf=0x%08" PRIx32
      " extmem_dcache_ctrl1=0x%08" PRIx32
      " extmem_dcache_autoload_ctrl=0x%08" PRIx32
      " extmem_icache_ctrl1=0x%08" PRIx32
      " extmem_icache_autoload_ctrl=0x%08" PRIx32 "\n",
      context.mmio_same_value_system_sysclk_conf,
      context.mmio_same_value_extmem_dcache_ctrl1,
      context.mmio_same_value_extmem_dcache_autoload_ctrl,
      context.mmio_same_value_extmem_icache_ctrl1,
      context.mmio_same_value_extmem_icache_autoload_ctrl);
  context.rom_cpu_ticks_per_us = esp_rom_get_cpu_ticks_per_us();
  context.rom_reset_reason_core0 = esp_rom_get_reset_reason(0);
  context.rom_reset_reason_core1 = esp_rom_get_reset_reason(1);
  std::printf("TINYDRAW_ROM_CALLBACK_VALUES reset_core0=%" PRIu32
              " reset_core1=%" PRIu32 " cpu_ticks_per_us=%" PRIu32 "\n",
              context.rom_reset_reason_core0, context.rom_reset_reason_core1,
              context.rom_cpu_ticks_per_us);
  context.rom_i2c_bod_threshold =
      esp_rom_regi2c_read(I2C_BOD, I2C_BOD_HOSTID, I2C_BOD_THRESHOLD);
  std::printf("TINYDRAW_ROM_I2C_VALUES bod_threshold_register=0x%02" PRIx32 "\n",
              context.rom_i2c_bod_threshold);
  context.sram_dependent = static_cast<std::uint32_t*>(heap_caps_malloc(
      kDependentEntries * sizeof(std::uint32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  context.sram_unaligned_dependent = static_cast<std::uint8_t*>(heap_caps_malloc(
      kDependentEntries * kUnalignedStride + 8U, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  context.sram_stream = static_cast<std::uint8_t*>(
      heap_caps_malloc(kSramStreamBytes + 8U, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  context.psram = static_cast<std::uint32_t*>(
      heap_caps_aligned_alloc(64U, kPsramBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  context.contention = static_cast<std::uint32_t*>(
      heap_caps_aligned_alloc(64U, kContentionBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  context.sram_load_use = static_cast<std::uint32_t*>(heap_caps_aligned_alloc(
      64U, kDependentEntries * sizeof(std::uint32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  context.psram_load_use = static_cast<std::uint32_t*>(heap_caps_aligned_alloc(
      64U, kDependentEntries * sizeof(std::uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  context.rom_memset_buffer = static_cast<std::uint8_t*>(
      heap_caps_aligned_alloc(16U, kRomMemsetBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (context.sram_dependent == nullptr || context.sram_unaligned_dependent == nullptr ||
      context.sram_stream == nullptr || context.psram == nullptr || context.contention == nullptr ||
      context.sram_load_use == nullptr || context.psram_load_use == nullptr ||
      context.rom_memset_buffer == nullptr) {
    print_error("initialize", "allocation");
    return false;
  }

  for (std::size_t index = 0; index < kDependentEntries; ++index) {
    const std::uint32_t next = static_cast<std::uint32_t>((index + 257U) & (kDependentEntries - 1U));
    context.sram_dependent[index] = next;
    auto* destination = context.sram_unaligned_dependent + index * kUnalignedStride + 1U;
    destination[0] = static_cast<std::uint8_t>(next);
    destination[1] = static_cast<std::uint8_t>(next >> 8U);
    destination[2] = static_cast<std::uint8_t>(next >> 16U);
    destination[3] = static_cast<std::uint8_t>(next >> 24U);
  }
  for (std::size_t index = 0; index < kSramStreamBytes + 8U; ++index) {
    context.sram_stream[index] = static_cast<std::uint8_t>(index * 37U + 11U);
  }
  for (std::size_t index = 0; index < kPsramBytes / sizeof(std::uint32_t); ++index) {
    context.psram[index] = static_cast<std::uint32_t>(index * 2'654'435'761U + 17U);
  }
  for (std::size_t index = 0; index < kContentionBytes / sizeof(std::uint32_t); ++index) {
    context.contention[index] = static_cast<std::uint32_t>(index * 2'246'822'519U + 31U);
  }
  const esp_err_t psram_sync =
      esp_cache_msync(context.psram, kPsramBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  const esp_err_t contention_sync =
      esp_cache_msync(context.contention, kContentionBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  if (psram_sync != ESP_OK || contention_sync != ESP_OK) {
    print_error("initialize", "psram-cache-sync",
                psram_sync != ESP_OK ? psram_sync : contention_sync);
    return false;
  }

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr || running->size < kFlashMapBytes) {
    print_error("initialize", "running-partition");
    return false;
  }
  const void* mapped = nullptr;
  const esp_err_t mapped_result = esp_partition_mmap(running, 0, kFlashMapBytes,
                                                     ESP_PARTITION_MMAP_DATA, &mapped,
                                                     &context.flash_handle);
  if (mapped_result != ESP_OK || mapped == nullptr) {
    print_error("initialize", "flash-mmap", mapped_result);
    return false;
  }
  context.flash = static_cast<const std::uint32_t*>(mapped);

  if ((reinterpret_cast<std::uintptr_t>(context.sram_load_use) & (kDcacheLineBytes - 1U)) != 0U ||
      (reinterpret_cast<std::uintptr_t>(context.psram_load_use) &
       (kDcacheLineBytes - 1U)) != 0U ||
      (reinterpret_cast<std::uintptr_t>(context.psram) & (kDcacheLineBytes - 1U)) != 0U ||
      (reinterpret_cast<std::uintptr_t>(context.flash) & (kDcacheLineBytes - 1U)) != 0U) {
    print_error("initialize", "dcache-burst-alignment");
    return false;
  }

  // All three dependent probes traverse identical values and therefore identical indices.
  // The flash-mapped firmware bytes are the immutable source; every load masks to 12 bits.
  for (std::size_t index = 0; index < kDependentEntries; ++index) {
    const std::uint32_t value = context.flash[index];
    context.sram_load_use[index] = value;
    context.psram_load_use[index] = value;
  }
  const esp_err_t load_use_sync = esp_cache_msync(
      context.psram_load_use, kDependentEntries * sizeof(std::uint32_t),
      ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  if (load_use_sync != ESP_OK) {
    print_error("initialize", "load-use-cache-sync", load_use_sync);
    return false;
  }

  // ESP32-S3 resets the DBUS flash-classifier range to 0..0. Flash and PSRAM share the
  // external-data virtual window, so the miss counters only distinguish them after software
  // constrains the mapped flash interval explicitly.
  const auto flash_start = reinterpret_cast<std::uintptr_t>(context.flash);
  const auto flash_end = flash_start + kFlashMapBytes - 1U;
  REG_WRITE(EXTMEM_DBUS_TO_FLASH_START_VADDR_REG, flash_start);
  REG_WRITE(EXTMEM_DBUS_TO_FLASH_END_VADDR_REG, flash_end);
  if (REG_READ(EXTMEM_DBUS_TO_FLASH_START_VADDR_REG) != flash_start ||
      REG_READ(EXTMEM_DBUS_TO_FLASH_END_VADDR_REG) != flash_end) {
    print_error("initialize", "dbus-flash-counter-range");
    return false;
  }
  std::printf("TINYDRAW_TIMING_COUNTER_RANGE flashStart=0x%08" PRIxPTR
              " flashEnd=0x%08" PRIxPTR " psramStart=0x%08" PRIxPTR "\n",
              flash_start, flash_end, reinterpret_cast<std::uintptr_t>(context.psram));
  flush_console();
  return true;
}

#define DCACHE_BURST_MEASUREMENTS(path, memory_path, lines)                                 \
  {"dcache_" #path "_burst_" #lines "_lines_hot",                                           \
   memory_path,                                                                             \
   lines * 4U,                                                                              \
   1U,                                                                                      \
   kWarmupIterations,                                                                       \
   tinydraw_dcache_##path##_##lines##_lines,                                                \
   prepare_dcache_burst_hot},                                                               \
  {                                                                                         \
    "dcache_" #path "_burst_" #lines "_lines_cold", memory_path, lines * 4U, 1U, 0U,        \
        tinydraw_dcache_##path##_##lines##_lines, prepare_##path##_dcache_burst_cold<lines> \
  }

#define ICACHE_BURST_MEASUREMENTS(lines)                                     \
  {"icache_flash_burst_" #lines "_lines_hot",                                \
   "other",                                                                  \
   0U,                                                                       \
   1U,                                                                       \
   kWarmupIterations,                                                        \
   flash_instruction_burst_kernel<lines>,                                    \
   prepare_none,                                                             \
   nullptr,                                                                  \
   lines,                                                                    \
   measure_flash_instruction_burst_once<lines, false>},                      \
  {                                                                          \
    "icache_flash_burst_" #lines "_lines_cold", "other", 0U, 1U, 0U,         \
        flash_instruction_burst_kernel<lines>, prepare_none, nullptr, lines, \
        measure_flash_instruction_burst_once<lines, true>                    \
  }

constexpr Measurement kMeasurements[] = {
    {"rom_baseline_reset_reason_core0", "internal-to-internal", 0U, 1U, 0U,
     tinydraw_rom_baseline_reset_reason_core0, prepare_none,
     finalize_rom_baseline_reset_reason_core0, kRomCallbackStatePreservedChecksum,
     measure_rom_callback_once},
    {"rom_reset_reason_core0", "other", 0U, 1U, 0U, tinydraw_rom_reset_reason_core0,
     prepare_none, finalize_rom_reset_reason_core0, kRomCallbackStatePreservedChecksum,
     measure_rom_callback_once},
    {"rom_baseline_reset_reason_core1", "internal-to-internal", 0U, 1U, 0U,
     tinydraw_rom_baseline_reset_reason_core1, prepare_none,
     finalize_rom_baseline_reset_reason_core1, kRomCallbackStatePreservedChecksum,
     measure_rom_callback_once},
    {"rom_reset_reason_core1", "other", 0U, 1U, 0U, tinydraw_rom_reset_reason_core1,
     prepare_none, finalize_rom_reset_reason_core1, kRomCallbackStatePreservedChecksum,
     measure_rom_callback_once},
    {"rom_baseline_memset_zero_length", "internal-to-internal", 0U, 1U, 0U,
     tinydraw_rom_baseline_memset_zero, prepare_rom_memset_fill, finalize_rom_memset_fill,
     kRomCallbackStatePreservedChecksum, measure_rom_callback_once},
    {"rom_memset_zero_length", "internal-to-internal", 0U, 1U, 0U,
     tinydraw_rom_memset_zero, prepare_rom_memset_fill, finalize_rom_memset_fill,
     kRomCallbackStatePreservedChecksum, measure_rom_callback_once},
    {"rom_baseline_memset_0x52e0", "internal-to-internal", kRomMemsetBytes, 1U, 0U,
     tinydraw_rom_baseline_memset_52e0, prepare_rom_memset_fill, finalize_rom_memset_fill,
     kRomCallbackStatePreservedChecksum, measure_rom_callback_once},
    {"rom_memset_0x52e0", "internal-to-internal", kRomMemsetBytes, 1U, 0U,
     tinydraw_rom_memset_52e0, prepare_rom_memset_fill, finalize_rom_memset_zeroed,
     kRomCallbackStatePreservedChecksum, measure_rom_callback_once},
    {"rom_baseline_set_cpu_ticks_per_us", "internal-to-internal", 0U, 1U, 0U,
     tinydraw_rom_baseline_set_cpu_ticks, prepare_none, finalize_rom_cpu_ticks,
     kRomCallbackStatePreservedChecksum, measure_rom_callback_once},
    {"rom_set_cpu_ticks_per_us_same_value", "internal-to-internal", 0U, 1U, 0U,
     tinydraw_rom_set_cpu_ticks, prepare_none, finalize_rom_cpu_ticks,
     kRomCallbackStatePreservedChecksum, measure_rom_callback_once},
    {"rom_i2c_baseline_write_same_bod_threshold", "other", 0U, 1U, 0U,
     tinydraw_rom_i2c_baseline_write_same_bod_threshold, prepare_none,
     finalize_rom_i2c_same_bod_threshold, kRomCallbackStatePreservedChecksum,
     measure_rom_i2c_callback_once},
    {"rom_i2c_write_same_bod_threshold", "other", 0U, 1U, 0U,
     tinydraw_rom_i2c_write_same_bod_threshold, prepare_none,
     finalize_rom_i2c_same_bod_threshold, kRomCallbackStatePreservedChecksum,
     measure_rom_i2c_callback_once},
    {"mmio_read_sram_4096_aligned", "internal-to-internal",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations, tinydraw_mmio_read_sram,
     prepare_none, nullptr, 0U, measure_mmio_once},
    {"mmio_read_system_cpu_per_conf_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_read_system_cpu_per_conf, prepare_none, nullptr, 0U, measure_mmio_once},
    {"mmio_read_rtc_store1_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_read_rtc_store1, prepare_none, nullptr, 0U, measure_rtc_mmio_once},
    {"mmio_read_extmem_cache_state_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_read_extmem_cache_state, prepare_none, nullptr, 0U, measure_mmio_once},
    {"mmio_write_sram_4096_aligned", "internal-to-internal",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations, tinydraw_mmio_write_sram,
     prepare_none, nullptr, 0U, measure_mmio_once},
    {"mmio_read_system_sysclk_conf_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_read_system_sysclk_conf, prepare_none, nullptr, 0U, measure_mmio_once},
    {"mmio_read_extmem_dcache_ctrl1_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_read_extmem_dcache_ctrl1, prepare_none, nullptr, 0U, measure_mmio_once},
    {"mmio_read_extmem_dcache_autoload_ctrl_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_read_extmem_dcache_autoload_ctrl, prepare_none, nullptr, 0U,
     measure_mmio_once},
    {"mmio_read_extmem_icache_ctrl1_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_read_extmem_icache_ctrl1, prepare_none, nullptr, 0U, measure_mmio_once},
    {"mmio_read_extmem_icache_autoload_ctrl_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_read_extmem_icache_autoload_ctrl, prepare_none, nullptr, 0U,
     measure_mmio_once},
    {"mmio_write_same_value_sram_4096_aligned", "internal-to-internal",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_write_same_value_sram, prepare_none, finalize_mmio_same_value_sram,
     kMmioStatePreservedChecksum, measure_mmio_once},
    {"mmio_write_same_value_system_sysclk_conf_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_write_same_value_system_sysclk_conf, prepare_none,
     finalize_mmio_same_value_system_sysclk_conf, kMmioStatePreservedChecksum,
     measure_mmio_once},
    {"mmio_write_same_value_extmem_dcache_ctrl1_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_write_same_value_extmem_dcache_ctrl1, prepare_none,
     finalize_mmio_same_value_extmem_dcache_ctrl1, kMmioStatePreservedChecksum,
     measure_mmio_once},
    {"mmio_write_same_value_extmem_icache_ctrl1_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_write_same_value_extmem_icache_ctrl1, prepare_none,
     finalize_mmio_same_value_extmem_icache_ctrl1, kMmioStatePreservedChecksum,
     measure_mmio_once},
    {"conditional_branch_baseline_4096_iterations", "internal-to-internal", 0U, 1U,
     kWarmupIterations, tinydraw_branch_baseline, prepare_none, nullptr,
     kConditionalBranchChecksum, measure_conditional_branch_once},
    {"conditional_branch_not_taken_4096_iterations", "internal-to-internal", 0U, 1U,
     kWarmupIterations, tinydraw_branch_not_taken, prepare_none, nullptr,
     kConditionalBranchChecksum, measure_conditional_branch_once},
    {"conditional_branch_taken_4096_iterations", "internal-to-internal", 0U, 1U,
     kWarmupIterations, tinydraw_branch_taken, prepare_none, nullptr,
     kConditionalBranchChecksum, measure_conditional_branch_once},
    {"dependent_load_sram_4096_steps", "internal-to-internal", kDependentLoads * 4U, 1U,
     kWarmupIterations, tinydraw_dependent_load_sram, prepare_dependent_load_hot, nullptr,
     0U, measure_dependent_load_internal_once},
    {"dependent_load_psram_hot_4096_steps", "psram-to-internal", kDependentLoads * 4U, 1U,
     kWarmupIterations, tinydraw_dependent_load_psram, prepare_dependent_load_hot, nullptr,
     0U, measure_dependent_load_external_once},
    {"dependent_load_flash_hot_4096_steps", "flash-to-internal", kDependentLoads * 4U, 1U,
     kWarmupIterations, tinydraw_dependent_load_flash, prepare_dependent_load_hot, nullptr,
     0U, measure_dependent_load_external_once},
    {"sram_aligned_dependent", "internal-to-internal", kDependentLoads * 4U, 1U,
     kWarmupIterations, sram_aligned_dependent, prepare_none},
    {"sram_unaligned_dependent", "internal-to-internal", kDependentLoads * 4U, 1U,
     kWarmupIterations, sram_unaligned_dependent, prepare_none},
    {"sram_aligned_stream", "internal-to-internal", kSramStreamBytes, 1U, kWarmupIterations,
     sram_aligned_stream, prepare_none},
    {"sram_unaligned_stream", "internal-to-internal", kSramStreamBytes, 1U, kWarmupIterations,
     sram_unaligned_stream, prepare_none},
    {"sram_instruction_issue", "internal-to-internal", 0U, 1U, kWarmupIterations,
     tinydraw_sram_instruction_issue, prepare_none},
    {"sram_l32_dependent", "internal-to-internal", kDependentLoads * 4U, 1U,
     kWarmupIterations, tinydraw_sram_l32_dependent, prepare_none},
    {"sram_l32_independent", "internal-to-internal", kSramStreamBytes, 1U,
     kWarmupIterations, tinydraw_sram_l32_independent, prepare_none},
    {"sram_s32_store_complete", "internal-to-internal", kSramStreamBytes, 1U,
     kWarmupIterations, tinydraw_sram_s32_store_complete, prepare_none,
     finalize_sram_store_complete, kSramStoreCompletionChecksum},
    {"rgb565_stage_five_scalar_oracle_hot", "internal-to-internal", kRgb565StagePixels * 4U, 1U,
     kWarmupIterations, rgb565_stage_five_scalar_oracle, prepare_rgb565_stage_hot,
     finalize_rgb565_stage, kRgb565StageOutputChecksum, measure_rgb565_stage_once},
    {"rgb565_stage_five_scalar_oracle_cold", "internal-to-internal", kRgb565StagePixels * 4U, 1U,
     0U, rgb565_stage_five_scalar_oracle, prepare_rgb565_stage_cold, finalize_rgb565_stage,
     kRgb565StageOutputChecksum, measure_rgb565_stage_once},
    {"psram_hot_sequential", "psram-to-internal", kPsramHotBytes, 1U, kWarmupIterations,
     psram_hot_sequential, prepare_psram_hot},
    {"psram_cold_sequential", "psram-to-internal", kPsramColdBytes, 1U, 0U,
     psram_cold_sequential, prepare_psram_cold},
    {"psram_hot_random", "psram-to-internal", kRandomHotLoads * 4U, 1U, kWarmupIterations,
     psram_hot_random, prepare_psram_hot},
    {"psram_cold_random", "psram-to-internal", kRandomColdLoads * 4U, 1U, 0U,
     psram_cold_random, prepare_psram_cold},
    {"flash_mmap_hot_sequential", "flash-to-internal", kPsramHotBytes, 1U, kWarmupIterations,
     flash_hot_sequential, prepare_flash_hot},
    {"flash_mmap_cold_sequential", "flash-to-internal", kFlashMapBytes, 1U, 0U,
     flash_cold_sequential, prepare_flash_cold},
    {"flash_mmap_hot_random", "flash-to-internal", kRandomHotLoads * 4U, 1U,
     kWarmupIterations, flash_hot_random, prepare_flash_hot},
    {"flash_mmap_cold_random", "flash-to-internal", kRandomColdLoads * 4U, 1U, 0U,
     flash_cold_random, prepare_flash_cold},
    {"icache_hit_iram_120_instructions", "internal-to-internal", 0U, 1U,
     kWarmupIterations, matched_iram_instruction_kernel, prepare_none, nullptr,
     kMatchedIcacheLines, measure_matched_icache_iram_once},
    {"icache_hit_flash_120_instructions", "other", 0U, 1U, kWarmupIterations,
     matched_flash_instruction_kernel, prepare_none, nullptr, kMatchedIcacheLines,
     measure_matched_icache_flash_once},
    {"dcache_hit_sram_16_loads", "internal-to-internal", kMatchedDcacheLoads * 4U, 1U,
     kWarmupIterations, tinydraw_dcache_sram_16_lines, prepare_dcache_burst_hot, nullptr,
     0U, measure_matched_dcache_internal_once},
    {"dcache_hit_psram_16_loads", "psram-to-internal", kMatchedDcacheLoads * 4U, 1U,
     kWarmupIterations, tinydraw_dcache_psram_16_lines, prepare_dcache_burst_hot, nullptr,
     0U, measure_matched_dcache_external_once},
    {"dcache_hit_flash_16_loads", "flash-to-internal", kMatchedDcacheLoads * 4U, 1U,
     kWarmupIterations, tinydraw_dcache_flash_16_lines, prepare_dcache_burst_hot, nullptr,
     0U, measure_matched_dcache_external_once},
    DCACHE_BURST_MEASUREMENTS(psram, "psram-to-internal", 1),
    DCACHE_BURST_MEASUREMENTS(psram, "psram-to-internal", 2),
    DCACHE_BURST_MEASUREMENTS(psram, "psram-to-internal", 4),
    DCACHE_BURST_MEASUREMENTS(psram, "psram-to-internal", 8),
    DCACHE_BURST_MEASUREMENTS(psram, "psram-to-internal", 16),
    DCACHE_BURST_MEASUREMENTS(flash, "flash-to-internal", 1),
    DCACHE_BURST_MEASUREMENTS(flash, "flash-to-internal", 2),
    DCACHE_BURST_MEASUREMENTS(flash, "flash-to-internal", 4),
    DCACHE_BURST_MEASUREMENTS(flash, "flash-to-internal", 8),
    DCACHE_BURST_MEASUREMENTS(flash, "flash-to-internal", 16),
    ICACHE_BURST_MEASUREMENTS(1),
    ICACHE_BURST_MEASUREMENTS(2),
    ICACHE_BURST_MEASUREMENTS(4),
    ICACHE_BURST_MEASUREMENTS(8),
    {"flash_instruction_cold", "other", 0U, 1U, 0U, flash_instruction_probe,
     prepare_flash_instruction_cold},
    {"flash_instruction_hot", "other", 0U, 1U, kWarmupIterations, flash_instruction_probe,
     prepare_hot},
    // These are the new variables in this calibration. Keep them after every
    // cache-sensitive measurement so an observed counter-state change cannot
    // invalidate an earlier strict receipt.
    {"mmio_read_rtc_date_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_read_rtc_date, prepare_none, nullptr, 0U,
     measure_observed_rtc_mmio_once},
    {"mmio_write_same_value_sram_2048_aligned", "internal-to-internal",
     kMmioHalfOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_write_same_value_sram_2048, prepare_none, finalize_mmio_same_value_sram,
     kMmioStatePreservedChecksum, measure_mmio_once},
    {"mmio_write_same_value_system_sysclk_conf_2048_aligned", "other",
     kMmioHalfOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_write_same_value_system_sysclk_conf_2048, prepare_none,
     finalize_mmio_same_value_system_sysclk_conf, kMmioStatePreservedChecksum,
     measure_mmio_once},
    {"mmio_write_same_value_extmem_dcache_ctrl1_2048_aligned", "other",
     kMmioHalfOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_write_same_value_extmem_dcache_ctrl1_2048, prepare_none,
     finalize_mmio_same_value_extmem_dcache_ctrl1, kMmioStatePreservedChecksum,
     measure_mmio_once},
    {"mmio_write_same_value_extmem_icache_ctrl1_2048_aligned", "other",
     kMmioHalfOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_write_same_value_extmem_icache_ctrl1_2048, prepare_none,
     finalize_mmio_same_value_extmem_icache_ctrl1, kMmioStatePreservedChecksum,
     measure_mmio_once},
    // Counter-clear is a documented write-only strobe. Keep it last so no
    // later strict counter signature observes the controller's clear pulse.
    {"mmio_write_extmem_cache_counter_clear_4096_aligned", "other",
     kMmioOperations * sizeof(std::uint32_t), 1U, kWarmupIterations,
     tinydraw_mmio_write_extmem_cache_counter_clear, prepare_none, nullptr, 0U,
     measure_mmio_once},
};

#undef DCACHE_BURST_MEASUREMENTS
#undef ICACHE_BURST_MEASUREMENTS

// Capture-mode provenance commits bind one bounded cohort to one ELF. Evidence
// commits restore the aggregate suite after two complete boots.
constexpr bool kMmioSlopeCaptureMode = false;
constexpr std::size_t kMmioSlopeMeasurementCount = 10U;
constexpr bool kRomCallbackCaptureMode = false;
constexpr std::size_t kRomCallbackMeasurementCount = 10U;
constexpr bool kRomI2cWriteCaptureMode = false;
constexpr std::size_t kRomI2cWriteMeasurementCount = 2U;
static_assert(static_cast<unsigned>(kMmioSlopeCaptureMode) +
                  static_cast<unsigned>(kRomCallbackCaptureMode) +
                  static_cast<unsigned>(kRomI2cWriteCaptureMode) <=
              1U);

bool is_mmio_slope_measurement(const Measurement& measurement) {
  constexpr const char* kIds[] = {
      "mmio_read_sram_4096_aligned",
      "mmio_read_rtc_date_4096_aligned",
      "mmio_write_same_value_sram_4096_aligned",
      "mmio_write_same_value_system_sysclk_conf_4096_aligned",
      "mmio_write_same_value_extmem_dcache_ctrl1_4096_aligned",
      "mmio_write_same_value_extmem_icache_ctrl1_4096_aligned",
      "mmio_write_same_value_sram_2048_aligned",
      "mmio_write_same_value_system_sysclk_conf_2048_aligned",
      "mmio_write_same_value_extmem_dcache_ctrl1_2048_aligned",
      "mmio_write_same_value_extmem_icache_ctrl1_2048_aligned",
  };
  static_assert(std::size(kIds) == kMmioSlopeMeasurementCount);
  for (const char* id : kIds) {
    if (std::strcmp(measurement.id, id) == 0) return true;
  }
  return false;
}

bool is_rom_callback_measurement(const Measurement& measurement) {
  constexpr const char* kIds[] = {
      "rom_baseline_reset_reason_core0",
      "rom_reset_reason_core0",
      "rom_baseline_reset_reason_core1",
      "rom_reset_reason_core1",
      "rom_baseline_memset_zero_length",
      "rom_memset_zero_length",
      "rom_baseline_memset_0x52e0",
      "rom_memset_0x52e0",
      "rom_baseline_set_cpu_ticks_per_us",
      "rom_set_cpu_ticks_per_us_same_value",
  };
  static_assert(std::size(kIds) == kRomCallbackMeasurementCount);
  for (const char* id : kIds) {
    if (std::strcmp(measurement.id, id) == 0) return true;
  }
  return false;
}

bool is_rom_i2c_write_measurement(const Measurement& measurement) {
  constexpr const char* kIds[] = {
      "rom_i2c_baseline_write_same_bod_threshold",
      "rom_i2c_write_same_bod_threshold",
  };
  static_assert(std::size(kIds) == kRomI2cWriteMeasurementCount);
  for (const char* id : kIds) {
    if (std::strcmp(measurement.id, id) == 0) return true;
  }
  return false;
}

bool run_suite(const ProbeContext& context, const char* contention_mode) {
  for (const auto& measurement : kMeasurements) {
    if (kRomI2cWriteCaptureMode && !is_rom_i2c_write_measurement(measurement)) continue;
    if (kRomCallbackCaptureMode && !is_rom_callback_measurement(measurement)) continue;
    if (kMmioSlopeCaptureMode && !is_mmio_slope_measurement(measurement)) continue;
    if (!run_measurement(context, measurement, contention_mode)) {
      return false;
    }
  }
  return true;
}

void probe_task(void*) {
  ProbeContext context;
  if (!initialize_context(context)) {
    vTaskDelete(nullptr);
    return;
  }
  print_metadata(context);
  bool passed = run_suite(context, "single_core");
  if (passed && start_contention(context)) {
    passed = run_suite(context, "core1_contended");
  } else if (passed) {
    passed = false;
  }
  std::printf("%s{\"protocolVersion\":%" PRIu32
              ",\"record\":\"run-complete\",\"measurements\":%lu,\"samplesPerMeasurement\":%d,"
              "\"pass\":%s}\n",
              kRecordPrefix, kProtocolVersion,
              static_cast<unsigned long>((kRomI2cWriteCaptureMode
                                              ? kRomI2cWriteMeasurementCount
                                              : (kRomCallbackCaptureMode
                                                     ? kRomCallbackMeasurementCount
                                                     : (kMmioSlopeCaptureMode
                                                            ? kMmioSlopeMeasurementCount
                                                            : std::size(kMeasurements)))) *
                                         2U),
              kSamplesPerMeasurement,
              passed ? "true" : "false");
  flush_console();
  // The console driver drains asynchronously after stdio has flushed. Keep the
  // producer task alive long enough for the one-shot completion record to leave.
  vTaskDelay(pdMS_TO_TICKS(100));
  // This one-shot firmware keeps its probe buffers alive until the next reset.
  // The core-1 contention task may still be leaving its final PSRAM pass here.
  vTaskDelete(nullptr);
}

}  // namespace
}  // namespace tinydraw::esp32::timing_probe

extern "C" void app_main() {
  if (xTaskCreatePinnedToCore(tinydraw::esp32::timing_probe::probe_task, "timing_probe", 12'288U,
                              nullptr, configMAX_PRIORITIES - 3U, nullptr, 0) != pdPASS) {
    std::printf("TINYDRAW_TIMING_NDJSON {\"protocolVersion\":1,\"record\":\"error\","
                "\"phase\":\"startup\",\"reason\":\"task-create\",\"espError\":0}\n");
  }
}
