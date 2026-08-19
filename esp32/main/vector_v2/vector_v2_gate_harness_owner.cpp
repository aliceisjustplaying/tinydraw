#include <cstring>

#include "vector_v2_gate_harness_internal.h"

extern "C" {
extern const char _binary_captured_drawing_2026_08_19_tdoc_start[];
extern const char _binary_captured_drawing_2026_08_19_tdoc_end[];
}

namespace tinydraw::esp32::gate_harness {
namespace {

constexpr std::size_t kHeaderBytes = 12U;
constexpr std::size_t kOperationHeaderBytes = 5U;
constexpr std::uint32_t kExpectedOperations = 102U;
constexpr std::uint32_t kExpectedSamples = 2'706U;

std::uint16_t read_u16(const std::uint8_t* bytes) {
  return static_cast<std::uint16_t>(bytes[0]) |
         static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) | static_cast<std::uint32_t>(bytes[1]) << 8U |
         static_cast<std::uint32_t>(bytes[2]) << 16U | static_cast<std::uint32_t>(bytes[3]) << 24U;
}

struct OwnerDocumentPosition {
  int world_x = 0;
  int world_y = 0;
};

std::optional<OwnerDocumentPosition> load_owner_document(
    OperationLog& log, MaterializedCanvas& canvas, const InPlaceAppendWorkspace& workspace,
    std::span<CompactOperationSample> conversion_storage) {
  const auto* begin =
      reinterpret_cast<const std::uint8_t*>(_binary_captured_drawing_2026_08_19_tdoc_start);
  const auto* end =
      reinterpret_cast<const std::uint8_t*>(_binary_captured_drawing_2026_08_19_tdoc_end);
  const std::size_t size = static_cast<std::size_t>(end - begin);
  if (size < kHeaderBytes || std::memcmp(begin, "TDOC", 4U) != 0) {
    return std::nullopt;
  }
  const std::uint32_t operation_count = read_u32(begin + 4U);
  const std::uint32_t sample_count = read_u32(begin + 8U);
  const std::size_t metadata_bytes =
      static_cast<std::size_t>(operation_count) * kOperationHeaderBytes;
  const std::size_t sample_bytes =
      static_cast<std::size_t>(sample_count) * sizeof(CompactOperationSample);
  if (operation_count != kExpectedOperations || sample_count != kExpectedSamples ||
      kHeaderBytes + metadata_bytes > size ||
      sample_bytes != size - kHeaderBytes - metadata_bytes) {
    return std::nullopt;
  }

  const std::uint8_t* operation_at = begin + kHeaderBytes;
  const std::uint8_t* sample_at = operation_at + metadata_bytes;
  std::uint64_t sum_x_quarter = 0U;
  std::uint64_t sum_y_quarter = 0U;
  std::size_t loaded_samples = 0U;
  const std::int64_t started = esp_timer_get_time();
  for (std::uint32_t index = 0U; index < operation_count; ++index) {
    const std::uint8_t tool = operation_at[0];
    const std::uint16_t color = read_u16(operation_at + 1U);
    const std::uint16_t count = read_u16(operation_at + 3U);
    operation_at += kOperationHeaderBytes;
    if (tool > static_cast<std::uint8_t>(OperationTool::kEraser) || count == 0U ||
        count > conversion_storage.size() ||
        static_cast<std::size_t>(count) > sample_count - loaded_samples) {
      return std::nullopt;
    }
    for (std::size_t sample = 0U; sample < count; ++sample) {
      CompactOperationSample value{
          .x_quarter = read_u16(sample_at),
          .y_quarter = read_u16(sample_at + 2U),
          .radius_256 = read_u16(sample_at + 4U),
          .elapsed_ms = read_u16(sample_at + 6U),
      };
      sample_at += sizeof(CompactOperationSample);
      conversion_storage[sample] = value;
      sum_x_quarter += value.x_quarter;
      sum_y_quarter += value.y_quarter;
    }
    loaded_samples += count;
    if (!append_and_absorb(log, canvas,
                           vector_v2::OperationAppend{
                               .tool = static_cast<OperationTool>(tool),
                               .color = color,
                               .gesture_id = static_cast<std::uint16_t>(index + 1U),
                               .samples = conversion_storage.first(count),
                           },
                           workspace)
             .has_value()) {
      return std::nullopt;
    }
  }
  if (loaded_samples != sample_count || sample_at != end) {
    return std::nullopt;
  }
  const OwnerDocumentPosition center{
      .world_x = static_cast<int>(sum_x_quarter / sample_count / 16U),
      .world_y = static_cast<int>(sum_y_quarter / sample_count / 16U),
  };
  std::printf(
      "TINYDRAW_OWNER_DOCUMENT operations=%lu samples=%lu center_x=%d center_y=%d load_us=%lld "
      "pass=1\n",
      static_cast<unsigned long>(operation_count), static_cast<unsigned long>(sample_count),
      center.world_x, center.world_y, static_cast<long long>(esp_timer_get_time() - started));
  std::fflush(stdout);
  return center;
}

}  // namespace

bool run_owner_document_cold_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                                  OperationLog& log, MaterializedCanvas& canvas,
                                  VectorV2TouchSampler& touch, const vector_v2::ChromeState& chrome,
                                  const InPlaceAppendWorkspace& workspace,
                                  std::span<const std::uint16_t> blank_snapshot,
                                  std::span<CompactOperationSample> conversion_storage) {
  const DocumentRevision baseline{canvas.current_revision().value + 1U};
  if (!vector_v2::restore_document_snapshot(log, canvas, baseline, blank_snapshot) ||
      !producer.reset_uniform_baseline(baseline)) {
    return false;
  }
  const auto center = load_owner_document(log, canvas, workspace, conversion_storage);
  if (!center.has_value()) {
    std::printf("TINYDRAW_OWNER_DOCUMENT pass=0\n");
    return false;
  }

  constexpr std::array zooms{ZoomLevel::k50Percent, ZoomLevel::k100Percent, ZoomLevel::k200Percent,
                             ZoomLevel::k400Percent};
  bool passed = true;
  for (const ZoomLevel zoom : zooms) {
    const int percent = vector_v2::zoom_percent(zoom);
    const int level_width = vector_v2::kWorldWidth * percent / 100;
    const int level_height = vector_v2::kWorldHeight * percent / 100;
    const int center_x = center->world_x * percent / 100;
    const int center_y = center->world_y * percent / 100;
    const int x = std::clamp(center_x - vector_v2::kOverviewWidth / 2, 0,
                             level_width - vector_v2::kOverviewWidth);
    const int y = std::clamp(center_y - vector_v2::kOverviewHeight / 2, 0,
                             level_height - vector_v2::kOverviewHeight);
    passed = run_paced_cold_gate(presenter, producer, canvas, touch, chrome, zoom, x, y,
                                 "owner_torture", contract::kColdViewportRequiredUs) &&
             passed;
  }
  return passed;
}

}  // namespace tinydraw::esp32::gate_harness
