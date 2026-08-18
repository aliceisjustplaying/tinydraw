#include "vector_v2_app_storage.h"

#include <cstddef>
#include <memory>

#include "esp_heap_caps.h"
#include "tinydraw/vector_v2/chrome.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/tile_producer.h"
#include "vector_v2_presenter.h"
#include "vector_v2_touch_sampler.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

template <typename Type>
[[nodiscard]] Type* allocate_array(std::size_t count) {
  return static_cast<Type*>(heap_caps_malloc(count * sizeof(Type), kExternalCaps));
}

template <typename Type>
[[nodiscard]] Type* allocate_internal(std::size_t count) {
  return static_cast<Type*>(
      heap_caps_malloc(count * sizeof(Type), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

}  // namespace

bool AppStorage::allocate() {
  overview = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  snapshot = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
#endif
  frame = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
  tile_pixels = allocate_array<std::uint16_t>(vector_v2::kTileSlotCount * vector_v2::kTilePixels);
  overview_scratch = allocate_array<std::uint16_t>(vector_v2::kOverviewPixels);
  region_scratch = allocate_array<std::uint16_t>(kLiveRegionScratchPixels);
  chrome_cache = allocate_array<std::uint16_t>(vector_v2::kChromeStagingCachePixels);
  // The producer paint scratch is the hottest pixel memory in cold replay:
  // every group starts with a 32 KiB fill and ends with a 32 KiB publish
  // read, with masked span writes in between. Internal SRAM removes the
  // PSRAM round-trips; the PSRAM fallback keeps allocation infallible.
  producer_supertask = allocate_internal<std::uint16_t>(vector_v2::kTileProducerPixels);
  supertask_internal = producer_supertask != nullptr;
  if (producer_supertask == nullptr) {
    producer_supertask = allocate_array<std::uint16_t>(vector_v2::kTileProducerPixels);
  }
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  harness_tile_scratch = allocate_internal<std::uint16_t>(vector_v2::kTilePixels);
  if (harness_tile_scratch == nullptr) {
    harness_tile_scratch = allocate_array<std::uint16_t>(vector_v2::kTilePixels);
  }
#endif
  producer_mask = allocate_internal<std::uint8_t>(vector_v2::kTileProducerMaskBytes);
  producer_summary_rows = allocate_internal<std::uint16_t>(vector_v2::kTileProducerSummaryRows);
  producer_summary_words = allocate_internal<std::uint32_t>(vector_v2::kTileProducerSummaryWords);
  // One operation's prepared chord batch (H7 sweep), read once per chord
  // per swept row: internal SRAM keeps it off the PSRAM dcache path.
  producer_chord_plans =
      allocate_internal<std::uint32_t>(vector_v2::kOperationChordStorageBytes / 4U);
  chunk_mask = allocate_internal<std::uint8_t>(vector_v2::kInPlaceTileMaskBytes);
  uniforms = allocate_array<vector_v2::MaterializedUniformStorage>(
      vector_v2::kMaterializedTileIdentityCount);
  occupancy = allocate_array<std::uint8_t>(vector_v2::kOccupancyBytes);
  slots = allocate_array<vector_v2::MaterializedSlotStorage>(vector_v2::kTileSlotCount);
  records = allocate_array<vector_v2::OperationRecord>(vector_v2::kOperationCapacity);
  samples = allocate_array<vector_v2::CompactOperationSample>(vector_v2::kOperationSampleCapacity);
  input_samples = allocate_array<vector_v2::CompactOperationSample>(kInputSampleCapacity);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  rerender_entries =
      allocate_array<vector_v2::RerenderLedgerEntry>(vector_v2::kRerenderLedgerEntryCount);
#endif
  touch_events = allocate_internal<vector_v2::TouchEvent>(kVectorV2TouchEventCapacity);
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  const bool variant_storage_ready =
      snapshot != nullptr && harness_tile_scratch != nullptr && rerender_entries != nullptr;
#else
  constexpr bool variant_storage_ready = true;
#endif
  affected_keys = allocate_array<vector_v2::TileKey>(vector_v2::kTileSlotCount +
                                                     vector_v2::kMaximumVisibleTiles);
  // O(1) find_tile metadata (27,384 B used), allocated LAST and padded to
  // 32 KiB. Placement receipts (2026-08-16): internal SRAM shifted the
  // panel-init DMA staging buffers (+50 us pan strip staging, pan_seq
  // red); an unpadded PSRAM allocation shifted every later heap address by
  // 27.4 KB onto presentation-hostile dcache sets (+40 us strip staging in
  // seven straight build variants). Padding to a multiple of the 8 KiB
  // dcache way size keeps downstream allocations on the same cache sets
  // that every pan optical receipt was measured on.
  constexpr std::size_t kDirectoryPaddedEntries = (32U * 1024U) / sizeof(std::uint16_t);
  static_assert(kDirectoryPaddedEntries >= vector_v2::kMaterializedTileIdentityCount);
  raw_slot_directory = allocate_array<std::uint16_t>(kDirectoryPaddedEntries);
  // Settled-AA workspace, allocated dead LAST so it shifts no other
  // allocation's cache sets (the first settle-build battery measured the
  // 400% cold wall +9 ms with these placed mid-heap).
  // Settled-AA planes are per-pixel hot in every phase (clear, composite,
  // fold): internal SRAM removes the external-RAM round trips that the
  // 2026-08-18 host/device divergence receipts implicated. The PSRAM
  // fallback keeps allocation infallible; removing all six from PSRAM
  // shifts later allocations by exactly 40,960 B = five 8 KiB dcache ways,
  // preserving the measured cache-set placement.
  settle_op_alpha = allocate_internal<std::uint8_t>(vector_v2::kTilePixels);
  settle_accumulated = allocate_internal<std::uint8_t>(vector_v2::kTilePixels);
  settle_red = allocate_internal<std::uint16_t>(vector_v2::kTilePixels);
  settle_green = allocate_internal<std::uint16_t>(vector_v2::kTilePixels);
  settle_blue = allocate_internal<std::uint16_t>(vector_v2::kTilePixels);
  settle_pixels = allocate_internal<std::uint16_t>(vector_v2::kTilePixels);
  settle_internal = settle_op_alpha != nullptr && settle_accumulated != nullptr &&
                    settle_red != nullptr && settle_green != nullptr && settle_blue != nullptr &&
                    settle_pixels != nullptr;
  if (settle_op_alpha == nullptr) {
    settle_op_alpha = allocate_array<std::uint8_t>(vector_v2::kTilePixels);
  }
  if (settle_accumulated == nullptr) {
    settle_accumulated = allocate_array<std::uint8_t>(vector_v2::kTilePixels);
  }
  if (settle_red == nullptr) {
    settle_red = allocate_array<std::uint16_t>(vector_v2::kTilePixels);
  }
  if (settle_green == nullptr) {
    settle_green = allocate_array<std::uint16_t>(vector_v2::kTilePixels);
  }
  if (settle_blue == nullptr) {
    settle_blue = allocate_array<std::uint16_t>(vector_v2::kTilePixels);
  }
  if (settle_pixels == nullptr) {
    settle_pixels = allocate_array<std::uint16_t>(vector_v2::kTilePixels);
  }
  // Dense authority index and shared query output are allocated dead-last so
  // they do not move the cache-set placement of the measured render path.
  operation_spatial_cells = allocate_array<std::uint64_t>(
      vector_v2::operation_spatial_cell_word_count(vector_v2::kOperationCapacity));
  operation_spatial_large = allocate_array<std::uint64_t>(
      vector_v2::operation_spatial_word_count(vector_v2::kOperationCapacity));
  operation_candidates = allocate_array<std::uint16_t>(vector_v2::kOperationCapacity);
  if (overview == nullptr || !variant_storage_ready || frame == nullptr || tile_pixels == nullptr ||
      overview_scratch == nullptr || region_scratch == nullptr || chrome_cache == nullptr ||
      producer_supertask == nullptr || producer_mask == nullptr ||
      producer_summary_rows == nullptr || producer_summary_words == nullptr ||
      producer_chord_plans == nullptr || chunk_mask == nullptr || uniforms == nullptr ||
      occupancy == nullptr || slots == nullptr || raw_slot_directory == nullptr ||
      records == nullptr || samples == nullptr || input_samples == nullptr ||
      touch_events == nullptr || affected_keys == nullptr || settle_op_alpha == nullptr ||
      settle_accumulated == nullptr || settle_red == nullptr || settle_green == nullptr ||
      settle_blue == nullptr || settle_pixels == nullptr || operation_spatial_cells == nullptr ||
      operation_spatial_large == nullptr || operation_candidates == nullptr) {
    return false;
  }
  for (std::size_t index = 0; index < vector_v2::kMaterializedTileIdentityCount; ++index) {
    std::construct_at(uniforms + index);
  }
  for (std::size_t index = 0; index < vector_v2::kTileSlotCount; ++index) {
    std::construct_at(slots + index);
  }
  return true;
}

}  // namespace tinydraw::esp32
