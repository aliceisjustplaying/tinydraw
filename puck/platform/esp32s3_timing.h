#ifndef TINYDRAW_PUCK_ESP32S3_TIMING_H
#define TINYDRAW_PUCK_ESP32S3_TIMING_H

#include <cstddef>
#include <cstdint>

namespace tinydraw::puck::timing {

enum class MemoryClass : std::uint8_t {
  kInternal,
  kPsram,
  kFlash,
  kUnclassified,
};

// Versioned, little-endian telemetry copied directly out of wasm memory.
// It is an accounting ledger, not a total-cycle claim. Ordinary C++ loads,
// stores and Xtensa instruction fetches remain invisible to this build.
struct SnapshotV1 {
  std::uint32_t version = 1;
  std::uint32_t size = 144U;
  std::uint64_t observation_sequence = 0;
  std::uint64_t internal_allocation_live_bytes = 0;
  std::uint64_t psram_allocation_live_bytes = 0;
  std::uint64_t unclassified_allocation_live_bytes = 0;
  std::uint64_t internal_read_bytes = 0;
  std::uint64_t internal_write_bytes = 0;
  std::uint64_t psram_read_bytes = 0;
  std::uint64_t psram_write_bytes = 0;
  std::uint64_t flash_read_bytes = 0;
  std::uint64_t flash_write_bytes = 0;
  std::uint64_t unclassified_read_bytes = 0;
  std::uint64_t unclassified_write_bytes = 0;
  std::uint64_t panel_write_bytes = 0;
  std::uint64_t panel_submit_count = 0;
  std::uint64_t panel_wire_clocks = 0;
  std::uint64_t panel_payload_cpu_cycles = 0;
  std::uint64_t allocation_registry_overflow_count = 0;
};

static_assert(sizeof(SnapshotV1) == 144U);

void reset_all();
void reset_observations();

void register_allocation(void* pointer, std::size_t size, MemoryClass memory_class);
void unregister_allocation(void* pointer);

void record_read(const void* pointer, std::size_t bytes);
void record_write(const void* pointer, std::size_t bytes);
void record_internal_read(std::size_t bytes);
void record_internal_write(std::size_t bytes);
void record_panel_write(std::size_t bytes);

[[nodiscard]] const SnapshotV1& snapshot();
[[nodiscard]] const char* schema_json();

}  // namespace tinydraw::puck::timing

#endif  // TINYDRAW_PUCK_ESP32S3_TIMING_H
