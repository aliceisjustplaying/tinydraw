#include "vector_v2_frame_trace.h"

#ifdef TINYDRAW_FRAME_TRACE

#include <cinttypes>
#include <cstdio>

#include "esp_cpu.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "soc/extmem_reg.h"
#include "soc/soc.h"
#include "soc/spi_mem_reg.h"

#ifndef TINYDRAW_FRAME_TRACE_BUNDLE_ID
#error "TINYDRAW_FRAME_TRACE_BUNDLE_ID must identify the frozen build bundle"
#endif

#ifndef TINYDRAW_FRAME_TRACE_SOURCE
#error "TINYDRAW_FRAME_TRACE_SOURCE must identify the source commit"
#endif

namespace tinydraw::esp32 {
namespace {

constexpr std::uint32_t kMspiCoreClockHz = 160'000'000U;
constexpr const char* kWorkloadId = "tinydraw-v2-fixed-stroke-v1";
std::uint32_t frame_sequence = 0U;

struct CacheCounters {
  std::uint32_t ibus_accesses = 0U;
  std::uint32_t ibus_misses = 0U;
  std::uint32_t dbus_accesses = 0U;
  std::uint32_t dbus_flash_misses = 0U;
  std::uint32_t dbus_psram_misses = 0U;
};

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
  return ((divider - 1U) << SPI_MEM_SCLKCNT_N_S) | ((divider / 2U - 1U) << SPI_MEM_SCLKCNT_H_S) |
         ((divider - 1U) << SPI_MEM_SCLKCNT_L_S);
}

extern "C" __attribute__((noinline, used)) std::uint32_t tinydraw_frame_trace_begin() {
  asm volatile("" ::: "memory");
  return esp_cpu_get_cycle_count();
}

extern "C" __attribute__((noinline, used)) std::uint32_t tinydraw_frame_trace_end() {
  const std::uint32_t value = esp_cpu_get_cycle_count();
  asm volatile("" ::: "memory");
  return value;
}

}  // namespace

bool start_frame_trace() {
  constexpr std::uint32_t psram_clock_hz = CONFIG_SPIRAM_SPEED * 1'000'000U;
  static_assert(psram_clock_hz == 40'000'000U || psram_clock_hz == 80'000'000U);
  const std::uint32_t clock_register = REG_READ(SPI_MEM_SRAM_CLK_REG(0));
  const std::uint32_t core_clock_register =
      REG_GET_FIELD(SPI_MEM_CORE_CLK_SEL_REG(0), SPI_MEM_CORE_CLK_SEL);
  const bool verified =
      clock_register == expected_psram_clock_register(psram_clock_hz) && core_clock_register == 2U;
  std::printf(
      "TINYDRAW_TRACE_V1 {\"schema\":\"tinydraw-trace-v1\","
      "\"bundle_id\":\"%s\",\"source_commit\":\"%s\","
      "\"board\":\"waveshare-esp32-s3-touch-amoled-1.8-v2\","
      "\"idf\":\"%s\",\"workload_id\":\"%s\","
      "\"cpu_hz\":%u,\"counter_source\":\"esp32s3_ccount32\","
      "\"psram_clock_hz\":%" PRIu32 ",\"psram_clock_register\":%" PRIu32
      ",\"psram_core_clock_register\":%" PRIu32
      ",\"psram_clock_verified\":%s,"
      "\"comparison_tier_candidate\":\"distribution_or_affine\"}\n",
      TINYDRAW_FRAME_TRACE_BUNDLE_ID, TINYDRAW_FRAME_TRACE_SOURCE, esp_get_idf_version(),
      kWorkloadId, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1'000'000U, psram_clock_hz, clock_register,
      core_clock_register, verified ? "true" : "false");
  std::fflush(stdout);
  return verified;
}

bool load_frame_trace_workload(VectorV2DemoController& demo) {
  struct Input {
    vector_v2::TouchEventKind kind;
    float x;
    float y;
    std::uint32_t offset_us;
  };
  constexpr Input inputs[] = {
      {vector_v2::TouchEventKind::kDown, 48.0F, 116.0F, 100'000U},
      {vector_v2::TouchEventKind::kMove, 56.0F, 121.0F, 116'000U},
      {vector_v2::TouchEventKind::kMove, 65.0F, 128.0F, 132'000U},
      {vector_v2::TouchEventKind::kMove, 75.0F, 137.0F, 148'000U},
      {vector_v2::TouchEventKind::kMove, 86.0F, 148.0F, 164'000U},
      {vector_v2::TouchEventKind::kMove, 98.0F, 160.0F, 180'000U},
      {vector_v2::TouchEventKind::kMove, 111.0F, 174.0F, 196'000U},
      {vector_v2::TouchEventKind::kMove, 125.0F, 188.0F, 212'000U},
      {vector_v2::TouchEventKind::kMove, 140.0F, 201.0F, 228'000U},
      {vector_v2::TouchEventKind::kMove, 156.0F, 212.0F, 244'000U},
      {vector_v2::TouchEventKind::kMove, 173.0F, 219.0F, 260'000U},
      {vector_v2::TouchEventKind::kMove, 190.0F, 221.0F, 276'000U},
      {vector_v2::TouchEventKind::kMove, 207.0F, 217.0F, 292'000U},
      {vector_v2::TouchEventKind::kMove, 223.0F, 207.0F, 308'000U},
      {vector_v2::TouchEventKind::kMove, 238.0F, 193.0F, 324'000U},
      {vector_v2::TouchEventKind::kMove, 251.0F, 176.0F, 340'000U},
      {vector_v2::TouchEventKind::kMove, 262.0F, 158.0F, 356'000U},
      {vector_v2::TouchEventKind::kMove, 271.0F, 141.0F, 372'000U},
      {vector_v2::TouchEventKind::kMove, 279.0F, 128.0F, 388'000U},
      {vector_v2::TouchEventKind::kUp, 285.0F, 121.0F, 404'000U},
  };
  if (!demo.ready()) {
    return false;
  }
  demo.begin_recording(0U);
  std::uint32_t sequence = 0U;
  for (const Input& input : inputs) {
    if (!demo.record_touch({
            .point = {.x = input.x, .y = input.y},
            .timestamp_us = input.offset_us,
            .sequence = ++sequence,
            .kind = input.kind,
        })) {
      demo.stop_recording();
      return false;
    }
  }
  demo.stop_recording();
  const bool ready = demo.tape_ready() && demo.sample_count() == std::size(inputs);
  std::printf(
      "TINYDRAW_FRAME_WORKLOAD_V1 {\"schema\":\"tinydraw-frame-workload-v1\","
      "\"bundle_id\":\"%s\",\"workload_id\":\"%s\",\"events\":%u,"
      "\"duration_us\":%" PRIu32 ",\"ready\":%s}\n",
      TINYDRAW_FRAME_TRACE_BUNDLE_ID, kWorkloadId, static_cast<unsigned>(std::size(inputs)),
      inputs[std::size(inputs) - 1U].offset_us, ready ? "true" : "false");
  std::fflush(stdout);
  return ready;
}

void trace_input(std::uint32_t event_sequence, const char* kind, float x, float y,
                 std::uint32_t event_us, bool replay) {
  std::printf(
      "TINYDRAW_INPUT_V1 {\"schema\":\"tinydraw-input-v1\","
      "\"bundle_id\":\"%s\",\"event_seq\":%" PRIu32
      ",\"kind\":\"%s\","
      "\"x_milli\":%ld,\"y_milli\":%ld,\"event_us\":%" PRIu32 ",\"replay\":%s}\n",
      TINYDRAW_FRAME_TRACE_BUNDLE_ID, event_sequence, kind, static_cast<long>(x * 1000.0F),
      static_cast<long>(y * 1000.0F), event_us, replay ? "true" : "false");
}

FrameTraceScope::FrameTraceScope(const char* kind, std::uint32_t event_sequence,
                                 std::uint32_t event_us)
    : kind_(kind),
      started_us_(static_cast<std::uint64_t>(esp_timer_get_time())),
      event_sequence_(event_sequence),
      event_us_(event_us) {
  clear_cache_counters();
  started_ccount_ = tinydraw_frame_trace_begin();
}

void FrameTraceScope::finish(const LivePresentationTiming& timing) {
  if (finished_) {
    return;
  }
  const std::uint32_t ended_ccount = tinydraw_frame_trace_end();
  const CacheCounters counters = read_cache_counters();
  const std::uint64_t ended_us = static_cast<std::uint64_t>(esp_timer_get_time());
  finished_ = true;
  const std::uint32_t sequence = ++frame_sequence;
  const std::uint32_t total_cycles = ended_ccount - started_ccount_;
  std::printf(
      "TINYDRAW_FRAME_V1 {\"schema\":\"tinydraw-frame-v1\","
      "\"bundle_id\":\"%s\",\"seq\":%" PRIu32
      ",\"kind\":\"%s\","
      "\"event_seq\":%" PRIu32 ",\"total_cycles\":%" PRIu32
      ",\"non_psram_cycles\":null,\"psram_cycles\":null,"
      "\"unknown_components\":[\"psram\"],\"ibus_accesses\":%" PRIu32 ",\"ibus_misses\":%" PRIu32
      ",\"dbus_accesses\":%" PRIu32 ",\"dbus_flash_misses\":%" PRIu32
      ",\"dbus_psram_misses\":%" PRIu32 "}\n",
      TINYDRAW_FRAME_TRACE_BUNDLE_ID, sequence, kind_, event_sequence_, total_cycles,
      counters.ibus_accesses, counters.ibus_misses, counters.dbus_accesses,
      counters.dbus_flash_misses, counters.dbus_psram_misses);
  std::printf(
      "TINYDRAW_FRAME_TELEMETRY_V1 {\"schema\":\"tinydraw-frame-telemetry-v1\","
      "\"bundle_id\":\"%s\",\"seq\":%" PRIu32 ",\"event_us\":%" PRIu32
      ",\"core\":%u,\"start_ccount\":%" PRIu32 ",\"end_ccount\":%" PRIu32 ",\"wall_us\":%" PRIu64
      ",\"compose_us\":%" PRId64 ",\"transfer_wait_us\":%" PRId64
      ",\"submitted_pixels\":%u,"
      "\"tile_pixels\":%u,\"fallback_pixels\":%u,\"pushes\":%" PRIu32
      ",\"passed\":%s,\"compose_pending\":%s}\n",
      TINYDRAW_FRAME_TRACE_BUNDLE_ID, sequence, event_us_, xPortGetCoreID(), started_ccount_,
      ended_ccount, ended_us - started_us_, timing.compose_us, timing.complete_us,
      static_cast<unsigned>(timing.submitted_pixels), static_cast<unsigned>(timing.tile_pixels),
      static_cast<unsigned>(timing.fallback_pixels), timing.pushes,
      timing.passed ? "true" : "false", timing.compose_pending ? "true" : "false");
  std::fflush(stdout);
}

}  // namespace tinydraw::esp32

#endif
