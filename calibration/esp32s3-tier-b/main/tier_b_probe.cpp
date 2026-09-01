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
#include "esp_async_memcpy.h"
#include "esp_attr.h"
#include "esp_cache.h"
#include "esp_chip_info.h"
#include "esp_cpu.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_psram.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/extmem_reg.h"
#include "soc/soc.h"

extern "C" {
void tier_b_instruction_1_lines(void);
void tier_b_instruction_2_lines(void);
void tier_b_instruction_4_lines(void);
void tier_b_instruction_8_lines(void);
void tier_b_instruction_16_lines(void);
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
}

namespace {

constexpr char kPrefix[] = "TINYDRAW_TIER_B_NDJSON ";
constexpr char kHarnessVersion[] = "0.1.0-draft";
constexpr std::uint32_t kProtocolVersion = 1;
constexpr std::size_t kIcacheLine = 32;
constexpr std::size_t kDcacheLine = 64;
constexpr std::size_t kPsramBytes = 1024 * 1024;
constexpr std::size_t kInternalBytes = 64 * 1024;
constexpr std::size_t kDmaBytes = 32 * 1024;
constexpr std::uint32_t kDefaultSamples = 9;
constexpr std::uint32_t kSweepSamples = 6;
constexpr std::uint32_t kFirstLineSamples = 5;
constexpr std::uint32_t kStoreIterations = 4096;
constexpr std::uint32_t kBandwidthBytes = 256 * 1024;

static_assert(CONFIG_ESP32S3_INSTRUCTION_CACHE_LINE_SIZE == kIcacheLine);
static_assert(CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE == kDcacheLine);

alignas(kDcacheLine) const std::array<std::uint32_t, 64 * 1024> g_flash_pool = [] {
  std::array<std::uint32_t, 64 * 1024> values{};
  for (std::size_t index = 0; index < values.size(); ++index) {
    values[index] = static_cast<std::uint32_t>(index * 2'246'822'519U + 31U);
  }
  return values;
}();

volatile std::uint32_t g_sink;

struct CacheCounters {
  std::uint32_t ibus_accesses = 0;
  std::uint32_t ibus_misses = 0;
  std::uint32_t dbus_accesses = 0;
  std::uint32_t dbus_flash_misses = 0;
  std::uint32_t dbus_psram_misses = 0;
};

struct Sample {
  bool ok = false;
  std::uint32_t cycles = 0;
  std::size_t bytes = 0;
  CacheCounters counters{};
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
  async_memcpy_handle_t gdma = nullptr;
  SemaphoreHandle_t panel_done = nullptr;
  SemaphoreHandle_t gdma_done = nullptr;
  SemaphoreHandle_t gpio_edge = nullptr;
  volatile std::uint32_t gpio_start = 0;
  volatile std::uint32_t gpio_edge_ccount = 0;
};

enum class Aggressor : std::uint32_t { kInternal, kFlash, kPsram };

std::atomic<bool> g_aggressor_ready{false};
std::atomic<bool> g_aggressor_start{false};
std::atomic<bool> g_aggressor_stop{false};
std::atomic<bool> g_aggressor_done{false};
std::atomic<std::uint32_t> g_aggressor_checksum{0};
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

Sample timed_result(std::uint32_t start, std::uint32_t end, std::size_t bytes,
                    CacheCounters counters = {}, const char* note = nullptr) {
  if (end == start) {
    return {.reason = "zero CCOUNT delta", .tier_candidate = "exact"};
  }
  return {.ok = true, .cycles = end - start, .bytes = bytes, .counters = counters, .note = note};
}

void emit_sample(const Cell& cell, std::uint32_t ordinal, const Sample& sample) {
  if (!sample.ok) {
    std::printf("%s{\"protocolVersion\":%" PRIu32
                ",\"record\":\"refusal\",\"cell\":\"%s\",\"ordinal\":%" PRIu32
                ",\"reason\":\"%s\",\"tierCandidate\":\"%s\"}\n",
                kPrefix, kProtocolVersion, cell.id, ordinal, sample.reason, sample.tier_candidate);
    std::fflush(stdout);
    return;
  }
  std::printf("%s{\"protocolVersion\":%" PRIu32
              ",\"record\":\"sample\",\"cell\":\"%s\",\"ordinal\":%" PRIu32 ",\"cycles\":%" PRIu32
              ",\"bytes\":%zu,\"startCore\":0,\"endCore\":0,"
              "\"cacheCounters\":{\"ibusAccesses\":%" PRIu32 ",\"ibusMisses\":%" PRIu32
              ",\"dbusAccesses\":%" PRIu32 ",\"dbusFlashMisses\":%" PRIu32
              ",\"dbusPsramMisses\":%" PRIu32 "}%s%s%s}\n",
              kPrefix, kProtocolVersion, cell.id, ordinal, sample.cycles, sample.bytes,
              sample.counters.ibus_accesses, sample.counters.ibus_misses,
              sample.counters.dbus_accesses, sample.counters.dbus_flash_misses,
              sample.counters.dbus_psram_misses, sample.note == nullptr ? "" : ",\"note\":\"",
              sample.note == nullptr ? "" : sample.note, sample.note == nullptr ? "" : "\"");
  std::fflush(stdout);
}

void IRAM_ATTR aggressor_task(void*) {
  Context& context = *g_aggressor_context;
  g_aggressor_ready.store(true, std::memory_order_release);
  while (!g_aggressor_start.load(std::memory_order_acquire)) {
  }
  std::uint32_t sum = 0;
  std::size_t offset = 0;
  while (!g_aggressor_stop.load(std::memory_order_acquire)) {
    switch (g_aggressor_kind) {
      case Aggressor::kInternal:
        sum += context.internal[offset & (kInternalBytes - 1)];
        break;
      case Aggressor::kFlash:
        sum += g_flash_pool[(offset >> 2) & (g_flash_pool.size() - 1)];
        break;
      case Aggressor::kPsram:
        sum += context.psram[offset & (kPsramBytes - 1)];
        break;
    }
    offset += kDcacheLine;
  }
  g_aggressor_checksum.store(sum, std::memory_order_release);
  g_aggressor_done.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}

bool start_aggressor(Context& context, Aggressor kind) {
  g_aggressor_context = &context;
  g_aggressor_kind = kind;
  g_aggressor_ready.store(false, std::memory_order_relaxed);
  g_aggressor_start.store(false, std::memory_order_relaxed);
  g_aggressor_stop.store(false, std::memory_order_relaxed);
  g_aggressor_done.store(false, std::memory_order_relaxed);
  if (xTaskCreatePinnedToCore(aggressor_task, "tier_b_aggress", 4096, nullptr,
                              configMAX_PRIORITIES - 2, nullptr, 1) != pdPASS) {
    return false;
  }
  while (!g_aggressor_ready.load(std::memory_order_acquire)) {
    taskYIELD();
  }
  return true;
}

void stop_aggressor() {
  g_aggressor_stop.store(true, std::memory_order_release);
  while (!g_aggressor_done.load(std::memory_order_acquire)) {
    taskYIELD();
  }
  g_sink ^= g_aggressor_checksum.load(std::memory_order_acquire);
}

std::uint32_t read_stride(const std::uint8_t* data, std::size_t size) {
  std::uint32_t sum = 0;
  for (std::size_t offset = 0; offset < size; offset += kDcacheLine) {
    sum += data[offset];
  }
  return sum;
}

Sample probe_arbitration(Context& context, const Cell& cell, std::uint32_t) {
  const auto kind = static_cast<Aggressor>(cell.parameter);
  if (!start_aggressor(context, kind)) {
    return {.reason = "core-1 aggressor task creation failed", .tier_candidate = "affine"};
  }
  clear_cache_counters();
  const std::uint32_t start = read_ccount();
  g_aggressor_start.store(true, std::memory_order_release);
  const std::uint32_t sum = read_stride(context.psram, kBandwidthBytes);
  const std::uint32_t end = read_ccount();
  stop_aggressor();
  g_sink ^= sum;
  return timed_result(start, end, kBandwidthBytes, read_cache_counters(),
                      "core0 PSRAM victim with core1 start barrier");
}

Sample probe_store_hit(Context& context, const Cell&, std::uint32_t ordinal) {
  auto* word = reinterpret_cast<volatile std::uint32_t*>(
      context.psram + (ordinal * kDcacheLine) % (kPsramBytes - kDcacheLine));
  *word = ordinal;
  g_sink ^= *word;
  const std::uint32_t start = read_ccount();
  for (std::uint32_t index = 0; index < kStoreIterations; ++index) {
    *word = index;
  }
  const std::uint32_t end = read_ccount();
  g_sink ^= *word;
  return timed_result(start, end, kStoreIterations * sizeof(std::uint32_t));
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
      data[offset] ^= static_cast<std::uint8_t>(ordinal + offset);
    }
  }
  const std::uint32_t start = read_ccount();
  const esp_err_t error = esp_cache_msync(data, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  const std::uint32_t end = read_ccount();
  if (error != ESP_OK) {
    return {.reason = "esp_cache_msync writeback failed", .tier_candidate = "affine"};
  }
  return timed_result(start, end, bytes);
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
  return timed_result(start, end, bytes, read_cache_counters());
}

Sample probe_instruction_psram(Context&, const Cell& cell, std::uint32_t) {
  const auto& range = kInstructionRanges[3];
  range.function();
  return call_instruction_range(range, cell.parameter != 0);
}

Sample probe_first_line_i_flash(Context&, const Cell&, std::uint32_t ordinal) {
  return call_instruction_range(kInstructionRanges[ordinal], true);
}

Sample probe_first_line_data(Context& context, const Cell& cell, std::uint32_t ordinal) {
  const std::size_t lines = std::size_t{1} << ordinal;
  const std::size_t bytes = lines * kDcacheLine;
  const bool psram = cell.parameter != 0;
  const std::uint8_t* data =
      psram ? context.psram : reinterpret_cast<const std::uint8_t*>(g_flash_pool.data());
  if (esp_cache_msync(const_cast<std::uint8_t*>(data), bytes,
                      ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE) != ESP_OK) {
    return {.reason = "data-cache invalidation failed", .tier_candidate = "exact"};
  }
  clear_cache_counters();
  const std::uint32_t start = read_ccount();
  const std::uint32_t sum = read_stride(data, bytes);
  const std::uint32_t end = read_ccount();
  g_sink ^= sum;
  return timed_result(start, end, bytes, read_cache_counters());
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

void IRAM_ATTR gpio_edge_handler(void* user) {
  auto* context = static_cast<Context*>(user);
  context->gpio_edge_ccount = read_ccount();
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(context->gpio_edge, &woken);
  if (woken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
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

  const std::array<std::uint8_t, 2> configure{0x03, 0x78};
  const std::array<std::uint8_t, 2> power{0x01, 0x87};
  error = i2c_master_transmit(context.io_expander, configure.data(), configure.size(), 100);
  if (error != ESP_OK) return error;
  return i2c_master_transmit(context.io_expander, power.data(), power.size(), 100);
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
  spi_config.spics_io_num = GPIO_NUM_12;
  spi_config.queue_size = 1;
  error = spi_bus_add_device(SPI2_HOST, &spi_config, &context.spi);
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
  context.gpio_edge = xSemaphoreCreateBinary();
  if (context.panel_done == nullptr || context.gdma_done == nullptr ||
      context.gpio_edge == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  esp_err_t error = init_i2c(context);
  if (error != ESP_OK) return error;
  error = init_spi_and_panel(context);
  if (error != ESP_OK) return error;

  async_memcpy_config_t dma_config = ASYNC_MEMCPY_DEFAULT_CONFIG();
  dma_config.backlog = 1;
  error = esp_async_memcpy_install(&dma_config, &context.gdma);
  if (error != ESP_OK) return error;

  gpio_config_t edge_config{};
  edge_config.pin_bit_mask = 1ULL << GPIO_NUM_21;
  edge_config.mode = GPIO_MODE_INPUT;
  edge_config.pull_up_en = GPIO_PULLUP_ENABLE;
  edge_config.intr_type = GPIO_INTR_ANYEDGE;
  error = gpio_config(&edge_config);
  if (error != ESP_OK) return error;
  error = gpio_install_isr_service(0);
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) return error;
  return gpio_isr_handler_add(GPIO_NUM_21, gpio_edge_handler, &context);
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

Sample probe_gpio21_edge(Context& context, const Cell&, std::uint32_t) {
  while (xSemaphoreTake(context.gpio_edge, 0) == pdTRUE) {
  }
  context.gpio_start = read_ccount();
  if (xSemaphoreTake(context.gpio_edge, pdMS_TO_TICKS(5000)) != pdTRUE) {
    return {.reason = "no GPIO 21 edge observed within 5 seconds",
            .tier_candidate = "distribution"};
  }
  return timed_result(context.gpio_start, context.gpio_edge_ccount, 0, {},
                      "task armed to GPIO21 ISR entry");
}

Sample probe_msync_sweep(Context& context, const Cell& cell, std::uint32_t ordinal) {
  const std::size_t bytes = kSweepBytes[ordinal];
  for (std::size_t offset = 0; offset < bytes; offset += kDcacheLine) {
    context.psram[offset] ^= static_cast<std::uint8_t>(ordinal + offset);
  }
  const int flags =
      ESP_CACHE_MSYNC_FLAG_DIR_C2M | (cell.parameter != 0 ? ESP_CACHE_MSYNC_FLAG_INVALIDATE : 0);
  const std::uint32_t start = read_ccount();
  const esp_err_t error = esp_cache_msync(context.psram, bytes, flags);
  const std::uint32_t end = read_ccount();
  if (error != ESP_OK) {
    return {.reason = "esp_cache_msync sweep failed", .tier_candidate = "affine"};
  }
  return timed_result(start, end, bytes);
}

Sample probe_cross_core_bandwidth(Context& context, const Cell& cell, std::uint32_t) {
  const bool flash_victim = cell.parameter != 0;
  if (!start_aggressor(context, Aggressor::kPsram)) {
    return {.reason = "core-1 PSRAM aggressor task creation failed",
            .tier_candidate = "distribution"};
  }
  clear_cache_counters();
  g_aggressor_start.store(true, std::memory_order_release);
  const std::uint32_t start = read_ccount();
  const std::uint32_t sum = read_stride(
      flash_victim ? reinterpret_cast<const std::uint8_t*>(g_flash_pool.data()) : context.psram,
      kBandwidthBytes);
  const std::uint32_t end = read_ccount();
  stop_aggressor();
  g_sink ^= sum;
  return timed_result(start, end, kBandwidthBytes, read_cache_counters());
}

#define CELL(id, probe, samples, parameter) \
  Cell { id, probe, samples, parameter }

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
    CELL("cache_msync_invalidate_sweep", probe_msync_sweep, kSweepSamples, 1),
    CELL("psram_bandwidth_cross_core", probe_cross_core_bandwidth, kDefaultSamples, 0),
    CELL("flash_bandwidth_cross_core", probe_cross_core_bandwidth, kDefaultSamples, 1),
};

#undef CELL

bool cell_available(const Cell& cell) {
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
  if (std::fgets(command.data(), command.size(), stdin) == nullptr) return false;
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
  std::printf("%s{\"protocolVersion\":%" PRIu32
              ",\"record\":\"metadata\",\"suite\":\"tier-b\",\"harnessVersion\":\"%s\","
              "\"idfVersion\":\"%s\",\"availableCells\":[",
              kPrefix, kProtocolVersion, kHarnessVersion, esp_get_idf_version());
  emit_string_array(selected, true);
  std::printf("],\"selectedCells\":[");
  emit_string_array(selected, false);
  std::printf("]}\n");
  std::fflush(stdout);
}

}  // namespace

extern "C" void app_main() {
  if (esp_cpu_get_core_id() != 0) {
    std::printf("TINYDRAW_TIER_B_FAILED app task is not pinned to core 0\n");
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
