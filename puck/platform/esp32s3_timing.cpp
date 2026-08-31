#include "esp32s3_timing.h"

#include <array>
#include <cstdint>
#include <limits>

#include "esp32s3_timing_profile.h"

namespace tinydraw::puck::timing {
namespace {

constexpr std::size_t kMaximumAllocations = 128U;
constexpr std::uint64_t kBitsPerByte = 8U;
static_assert(profile::kCpuHz % profile::kPanelPayloadBytesPerSecond == 0U);
static_assert(kBitsPerByte % profile::kPanelLanes == 0U);
static_assert(profile::kPanelPayloadBytesPerSecond ==
              profile::kPanelBusHz * profile::kPanelLanes / kBitsPerByte);
constexpr std::uint64_t kPanelPayloadCpuCyclesPerByte =
    profile::kCpuHz / profile::kPanelPayloadBytesPerSecond;
constexpr std::uint64_t kPanelWireClocksPerByte = kBitsPerByte / profile::kPanelLanes;

struct Allocation {
  std::uintptr_t begin = 0;
  std::uintptr_t end = 0;
  std::size_t size = 0;
  MemoryClass memory_class = MemoryClass::kUnclassified;
  bool occupied = false;
};

std::array<Allocation, kMaximumAllocations> g_allocations{};
SnapshotV1 g_snapshot{};

const char g_schema[] =
    "{"
    "\"schemaVersion\":1,"
    "\"byteOrder\":\"little\","
    "\"snapshotBytes\":144,"
    "\"claim\":\"accounted-events-only\","
    "\"cpuHz\":" TINYDRAW_TIMING_CPU_HZ_JSON
    ","
    "\"panelBusHz\":" TINYDRAW_TIMING_PANEL_BUS_HZ_JSON
    ","
    "\"panelLanes\":" TINYDRAW_TIMING_PANEL_LANES_JSON
    ","
    "\"panelPayloadBytesPerSecond\":" TINYDRAW_TIMING_PANEL_PAYLOAD_BPS_JSON
    ","
    "\"observationReset\":\"start-of-emu_tick\","
    "\"scopes\":{\"schema\":\"schema\",\"sequence\":\"instance-monotonic\","
    "\"live\":\"instance-live\",\"observation\":\"since-last-reset\","
    "\"lifetime\":\"instance-lifetime\"},"
    "\"fields\":["
    "[\"version\",0,\"u32\",\"schema\"],[\"size\",4,\"u32\",\"schema\"],"
    "[\"observation_sequence\",8,\"u64\",\"sequence\"],"
    "[\"internal_allocation_live_bytes\",16,\"u64\",\"live\"],"
    "[\"psram_allocation_live_bytes\",24,\"u64\",\"live\"],"
    "[\"unclassified_allocation_live_bytes\",32,\"u64\",\"live\"],"
    "[\"internal_read_bytes\",40,\"u64\",\"observation\"],"
    "[\"internal_write_bytes\",48,\"u64\",\"observation\"],"
    "[\"psram_read_bytes\",56,\"u64\",\"observation\"],"
    "[\"psram_write_bytes\",64,\"u64\",\"observation\"],"
    "[\"flash_read_bytes\",72,\"u64\",\"observation\"],"
    "[\"flash_write_bytes\",80,\"u64\",\"observation\"],"
    "[\"unclassified_read_bytes\",88,\"u64\",\"observation\"],"
    "[\"unclassified_write_bytes\",96,\"u64\",\"observation\"],"
    "[\"panel_write_bytes\",104,\"u64\",\"observation\"],"
    "[\"panel_submit_count\",112,\"u64\",\"observation\"],"
    "[\"panel_wire_clocks\",120,\"u64\",\"observation\"],"
    "[\"panel_payload_cpu_cycles\",128,\"u64\",\"observation\"],"
    "[\"allocation_registry_overflow_count\",136,\"u64\",\"lifetime\"]"
    "]}";

void add_live_bytes(MemoryClass memory_class, std::size_t bytes) {
  switch (memory_class) {
    case MemoryClass::kInternal:
      g_snapshot.internal_allocation_live_bytes += bytes;
      return;
    case MemoryClass::kPsram:
      g_snapshot.psram_allocation_live_bytes += bytes;
      return;
    case MemoryClass::kFlash:
    case MemoryClass::kUnclassified:
      g_snapshot.unclassified_allocation_live_bytes += bytes;
      return;
  }
}

void subtract_live_bytes(MemoryClass memory_class, std::size_t bytes) {
  std::uint64_t* target = nullptr;
  switch (memory_class) {
    case MemoryClass::kInternal:
      target = &g_snapshot.internal_allocation_live_bytes;
      break;
    case MemoryClass::kPsram:
      target = &g_snapshot.psram_allocation_live_bytes;
      break;
    case MemoryClass::kFlash:
    case MemoryClass::kUnclassified:
      target = &g_snapshot.unclassified_allocation_live_bytes;
      break;
  }
  const auto bounded = static_cast<std::uint64_t>(bytes);
  *target = *target >= bounded ? *target - bounded : 0U;
}

MemoryClass classify(const void* pointer, std::size_t bytes) {
  if (pointer == nullptr || bytes == 0U) return MemoryClass::kUnclassified;
  const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
  if (bytes > std::numeric_limits<std::uintptr_t>::max() - begin) {
    return MemoryClass::kUnclassified;
  }
  const auto end = begin + bytes;
  for (const Allocation& allocation : g_allocations) {
    if (allocation.occupied && begin >= allocation.begin && end <= allocation.end) {
      return allocation.memory_class;
    }
  }
  return MemoryClass::kUnclassified;
}

void record(MemoryClass memory_class, std::size_t bytes, bool write) {
  if (bytes == 0U) return;
  const auto amount = static_cast<std::uint64_t>(bytes);
  switch (memory_class) {
    case MemoryClass::kInternal:
      (write ? g_snapshot.internal_write_bytes : g_snapshot.internal_read_bytes) += amount;
      return;
    case MemoryClass::kPsram:
      (write ? g_snapshot.psram_write_bytes : g_snapshot.psram_read_bytes) += amount;
      return;
    case MemoryClass::kFlash:
      (write ? g_snapshot.flash_write_bytes : g_snapshot.flash_read_bytes) += amount;
      return;
    case MemoryClass::kUnclassified:
      (write ? g_snapshot.unclassified_write_bytes : g_snapshot.unclassified_read_bytes) += amount;
      return;
  }
}

}  // namespace

void reset_all() {
  g_allocations = {};
  g_snapshot = {};
}

void reset_observations() {
  const auto next_sequence = g_snapshot.observation_sequence + 1U;
  const auto internal_live = g_snapshot.internal_allocation_live_bytes;
  const auto psram_live = g_snapshot.psram_allocation_live_bytes;
  const auto unclassified_live = g_snapshot.unclassified_allocation_live_bytes;
  const auto registry_overflows = g_snapshot.allocation_registry_overflow_count;
  g_snapshot = {};
  g_snapshot.observation_sequence = next_sequence;
  g_snapshot.internal_allocation_live_bytes = internal_live;
  g_snapshot.psram_allocation_live_bytes = psram_live;
  g_snapshot.unclassified_allocation_live_bytes = unclassified_live;
  g_snapshot.allocation_registry_overflow_count = registry_overflows;
}

void register_allocation(void* pointer, std::size_t size, MemoryClass memory_class) {
  if (pointer == nullptr || size == 0U) return;
  const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
  if (size > std::numeric_limits<std::uintptr_t>::max() - begin) {
    ++g_snapshot.allocation_registry_overflow_count;
    return;
  }
  for (Allocation& allocation : g_allocations) {
    if (allocation.occupied) continue;
    allocation = {
        .begin = begin,
        .end = begin + size,
        .size = size,
        .memory_class = memory_class,
        .occupied = true,
    };
    add_live_bytes(memory_class, size);
    return;
  }
  ++g_snapshot.allocation_registry_overflow_count;
}

void unregister_allocation(void* pointer) {
  if (pointer == nullptr) return;
  const auto begin = reinterpret_cast<std::uintptr_t>(pointer);
  for (Allocation& allocation : g_allocations) {
    if (!allocation.occupied || allocation.begin != begin) continue;
    subtract_live_bytes(allocation.memory_class, allocation.size);
    allocation = {};
    return;
  }
}

void record_read(const void* pointer, std::size_t bytes) {
  record(classify(pointer, bytes), bytes, false);
}

void record_write(const void* pointer, std::size_t bytes) {
  record(classify(pointer, bytes), bytes, true);
}

void record_internal_read(std::size_t bytes) { record(MemoryClass::kInternal, bytes, false); }

void record_internal_write(std::size_t bytes) { record(MemoryClass::kInternal, bytes, true); }

void record_panel_write(std::size_t bytes) {
  const auto amount = static_cast<std::uint64_t>(bytes);
  g_snapshot.panel_write_bytes += amount;
  ++g_snapshot.panel_submit_count;
  g_snapshot.panel_wire_clocks += amount * kPanelWireClocksPerByte;
  g_snapshot.panel_payload_cpu_cycles += amount * kPanelPayloadCpuCyclesPerByte;
}

const SnapshotV1& snapshot() { return g_snapshot; }

const char* schema_json() { return g_schema; }

}  // namespace tinydraw::puck::timing
