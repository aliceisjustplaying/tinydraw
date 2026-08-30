#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "esp32s3_timing.h"

namespace timing = tinydraw::puck::timing;

int main() {
  std::array<std::byte, 64> internal{};
  std::array<std::byte, 96> psram{};

  timing::reset_all();
  timing::register_allocation(internal.data(), internal.size(), timing::MemoryClass::kInternal);
  timing::register_allocation(psram.data(), psram.size(), timing::MemoryClass::kPsram);
  timing::record_read(internal.data() + 8, 16);
  timing::record_write(psram.data() + 12, 24);
  timing::record_panel_write(10);

  const auto first = timing::snapshot();
  assert(first.version == 1U);
  assert(first.size == 144U);
  assert(first.observation_sequence == 0U);
  assert(first.internal_allocation_live_bytes == internal.size());
  assert(first.psram_allocation_live_bytes == psram.size());
  assert(first.internal_read_bytes == 16U);
  assert(first.psram_write_bytes == 24U);
  assert(first.panel_write_bytes == 10U);
  assert(first.panel_submit_count == 1U);
  assert(first.panel_wire_clocks == 20U);
  assert(first.panel_payload_cpu_cycles == 120U);

  timing::reset_observations();
  const auto reset = timing::snapshot();
  assert(reset.observation_sequence == 1U);
  assert(reset.internal_allocation_live_bytes == internal.size());
  assert(reset.psram_allocation_live_bytes == psram.size());
  assert(reset.internal_read_bytes == 0U);
  assert(reset.psram_write_bytes == 0U);
  assert(reset.panel_write_bytes == 0U);
  assert(reset.panel_submit_count == 0U);

  timing::record_read(internal.data() + 60, 8);
  assert(timing::snapshot().unclassified_read_bytes == 8U);
  timing::unregister_allocation(internal.data());
  timing::unregister_allocation(psram.data());
  assert(timing::snapshot().internal_allocation_live_bytes == 0U);
  assert(timing::snapshot().psram_allocation_live_bytes == 0U);

  timing::reset_all();
  std::array<std::array<std::byte, 1>, 129> allocations{};
  for (std::size_t index = 0; index < 128U; ++index) {
    timing::register_allocation(allocations[index].data(), 1U, timing::MemoryClass::kInternal);
  }
  timing::register_allocation(allocations[128].data(), 1U, timing::MemoryClass::kInternal);
  assert(timing::snapshot().internal_allocation_live_bytes == 128U);
  assert(timing::snapshot().unclassified_allocation_live_bytes == 0U);
  assert(timing::snapshot().allocation_registry_overflow_count == 1U);
  timing::reset_observations();
  assert(timing::snapshot().allocation_registry_overflow_count == 1U);
  for (std::size_t index = 0; index < 128U; ++index) {
    timing::unregister_allocation(allocations[index].data());
  }
  assert(timing::snapshot().internal_allocation_live_bytes == 0U);
  assert(timing::snapshot().allocation_registry_overflow_count == 1U);
}
