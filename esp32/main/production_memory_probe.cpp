#include "production_memory_probe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "esp_heap_caps.h"
#include "tinydraw/production/materialized_canvas.h"
#include "tinydraw/production/memory_layout.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::uint32_t kExternalCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

struct Allocation {
  const char* name;
  std::size_t bytes;
  void* memory = nullptr;
};

void release(std::span<Allocation> allocations) {
  for (auto& allocation : allocations) {
    heap_caps_free(allocation.memory);
    allocation.memory = nullptr;
  }
}

}  // namespace

void run_production_memory_probe() {
  using namespace tinydraw::production;

  std::array allocations{
      Allocation{"overview", kOverviewBytes},
      Allocation{"overview_publication", kOverviewPublicationBytes},
      Allocation{"tile_pool", kTilePoolBytes},
      Allocation{"tile_metadata", kTileMetadataBytes},
      Allocation{"operation_records", kOperationRecordBytes},
      Allocation{"operation_samples", kOperationSampleBytes},
      Allocation{"lod_samples", kLodSampleBytes},
      Allocation{"lod_spans", kLodSpanBytes},
      Allocation{"renderer_workspace", kRendererWorkspaceBytes},
      Allocation{"display_workspace", kDisplayWorkspaceBytes},
  };

  const std::size_t free_before = heap_caps_get_free_size(kExternalCaps);
  const std::size_t largest_before = heap_caps_get_largest_free_block(kExternalCaps);
  std::size_t allocated_bytes = 0;
  bool plan_allocated = true;
  for (auto& allocation : allocations) {
    allocation.memory = heap_caps_malloc(allocation.bytes, kExternalCaps);
    if (allocation.memory == nullptr) {
      plan_allocated = false;
      std::printf("TINYDRAW_PRODUCTION_MEMORY_ALLOCATION name=%s bytes=%lu ok=0\n", allocation.name,
                  static_cast<unsigned long>(allocation.bytes));
      break;
    }
    allocated_bytes += allocation.bytes;
    std::printf("TINYDRAW_PRODUCTION_MEMORY_ALLOCATION name=%s bytes=%lu ok=1\n", allocation.name,
                static_cast<unsigned long>(allocation.bytes));
  }

  const std::size_t free_after_plan = heap_caps_get_free_size(kExternalCaps);
  const std::size_t largest_after_plan = heap_caps_get_largest_free_block(kExternalCaps);
  void* reserve =
      plan_allocated ? heap_caps_malloc(kTargetContiguousReserveBytes, kExternalCaps) : nullptr;
  const bool reserve_allocated = reserve != nullptr;
  const std::size_t free_after_reserve = heap_caps_get_free_size(kExternalCaps);
  const std::size_t largest_after_reserve = heap_caps_get_largest_free_block(kExternalCaps);

  std::printf(
      "TINYDRAW_PRODUCTION_MEMORY_RECEIPT plan_bytes=%lu allocated_bytes=%lu "
      "free_before=%lu largest_before=%lu free_after_plan=%lu largest_after_plan=%lu "
      "reserve_target=%lu reserve_allocated=%u free_after_reserve=%lu "
      "largest_after_reserve=%lu plan_allocated=%u\n",
      static_cast<unsigned long>(kExternalPlanBytes), static_cast<unsigned long>(allocated_bytes),
      static_cast<unsigned long>(free_before), static_cast<unsigned long>(largest_before),
      static_cast<unsigned long>(free_after_plan), static_cast<unsigned long>(largest_after_plan),
      static_cast<unsigned long>(kTargetContiguousReserveBytes), reserve_allocated,
      static_cast<unsigned long>(free_after_reserve),
      static_cast<unsigned long>(largest_after_reserve), plan_allocated);
  std::printf("TINYDRAW_PRODUCTION_MEMORY_DONE pass=%u\n",
              plan_allocated && reserve_allocated && allocated_bytes == kExternalPlanBytes);
  std::fflush(stdout);

  heap_caps_free(reserve);
  release(allocations);
}

}  // namespace tinydraw::esp32
