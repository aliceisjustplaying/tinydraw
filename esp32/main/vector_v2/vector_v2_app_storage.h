#ifndef TINYDRAW_ESP32_VECTOR_V2_APP_STORAGE_H
#define TINYDRAW_ESP32_VECTOR_V2_APP_STORAGE_H

#include <cstddef>
#include <cstdint>

#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/operation.h"
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
#include "tinydraw/vector_v2/rerender_ledger.h"
#endif
#include "tinydraw/vector_v2/touch_event_buffer.h"

namespace tinydraw::esp32 {

// One slot beyond the advertised 4,096 input points retains a distinct lift
// sample without splitting the Stroke into multiple authority records.
inline constexpr std::size_t kInputPointCapacity = 4'096;
inline constexpr std::size_t kInputSampleCapacity = kInputPointCapacity + 1U;

struct AppStorage {
  std::uint16_t* overview = nullptr;
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  std::uint16_t* snapshot = nullptr;
#endif
  std::uint16_t* frame = nullptr;
  std::uint16_t* tile_pixels = nullptr;
  std::uint16_t* overview_scratch = nullptr;
  std::uint16_t* region_scratch = nullptr;
  std::uint16_t* chrome_cache = nullptr;
  std::uint16_t* producer_supertask = nullptr;
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  // Harness-only compose/census scratch for one 64x64 tile. The producer
  // publishes straight from the supertask surface and no longer stages here.
  std::uint16_t* harness_tile_scratch = nullptr;
#endif
  bool supertask_internal = false;
  bool settle_internal = false;
  std::uint8_t* producer_mask = nullptr;
  std::uint16_t* producer_summary_rows = nullptr;
  std::uint32_t* producer_summary_words = nullptr;
  std::uint32_t* producer_chord_plans = nullptr;
  std::uint8_t* chunk_mask = nullptr;
  // Settled-AA workspace (40 KiB PSRAM — exactly five 8 KiB dcache ways, so
  // downstream allocations keep their measured cache sets; internal SRAM is
  // off-limits here per the panel-init DMA razor note below): per-op union
  // alpha, accumulated alpha, exact 16-bit channel accumulators, and the
  // output staging tile. Settling is idle-budget work; PSRAM latency is
  // acceptable.
  std::uint8_t* settle_op_alpha = nullptr;
  std::uint8_t* settle_accumulated = nullptr;
  std::uint16_t* settle_red = nullptr;
  std::uint16_t* settle_green = nullptr;
  std::uint16_t* settle_blue = nullptr;
  std::uint16_t* settle_pixels = nullptr;
  vector_v2::MaterializedUniformStorage* uniforms = nullptr;
  std::uint8_t* occupancy = nullptr;
  vector_v2::MaterializedSlotStorage* slots = nullptr;
  std::uint16_t* eviction_links = nullptr;
  std::uint16_t* raw_slot_directory = nullptr;
  vector_v2::OperationRecord* records = nullptr;
  vector_v2::CompactOperationSample* samples = nullptr;
  vector_v2::CompactOperationSample* input_samples = nullptr;
  std::uint64_t* operation_spatial_cells = nullptr;
  std::uint64_t* operation_spatial_large = nullptr;
  std::uint16_t* operation_candidates = nullptr;
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  vector_v2::RerenderLedgerEntry* rerender_entries = nullptr;
#endif
  vector_v2::TouchEvent* touch_events = nullptr;
  vector_v2::TileKey* affected_keys = nullptr;

  [[nodiscard]] bool allocate();
};

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_APP_STORAGE_H
