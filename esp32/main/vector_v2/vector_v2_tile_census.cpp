#include "vector_v2_tile_census.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinydraw/vector_v2/tile_payload_analysis.h"

namespace tinydraw::esp32 {
namespace {

using vector_v2::MaterializedCanvas;
using vector_v2::TileKey;
using vector_v2::ZoomLevel;

const char* zoom_name(ZoomLevel zoom) {
  switch (zoom) {
    case ZoomLevel::k25Percent:
      return "25";
    case ZoomLevel::k50Percent:
      return "50";
    case ZoomLevel::k100Percent:
      return "100";
    case ZoomLevel::k200Percent:
      return "200";
    case ZoomLevel::k400Percent:
      return "400";
  }
  return "invalid";
}

struct TileClassCensus {
  std::size_t tiles = 0;
  std::size_t paper = 0;
  std::size_t uniform_color = 0;
  std::size_t raw = 0;
  std::size_t row_rle_smaller = 0;
  std::size_t raw_payload_bytes = 0;
  std::size_t row_rle_bytes = 0;
  std::size_t bounded_payload_bytes = 0;
  std::int64_t production_us = 0;
  std::int64_t classification_us = 0;
  std::int64_t maximum_classification_us = 0;
  std::size_t operations_scanned = 0;

  void include(const vector_v2::TilePayloadAnalysis& analysis, std::int64_t elapsed_us) {
    ++tiles;
    raw_payload_bytes += analysis.raw_bytes;
    classification_us += elapsed_us;
    maximum_classification_us = std::max(maximum_classification_us, elapsed_us);
    if (analysis.uniform) {
      paper += analysis.uniform_color == 0xFFFFU;
      uniform_color += analysis.uniform_color != 0xFFFFU;
      bounded_payload_bytes += sizeof(std::uint16_t);
      return;
    }
    ++raw;
    row_rle_bytes += analysis.estimated_row_rle_bytes;
    row_rle_smaller += analysis.estimated_row_rle_bytes < analysis.raw_bytes;
    bounded_payload_bytes += std::min(analysis.raw_bytes, analysis.estimated_row_rle_bytes);
  }

  void combine(const TileClassCensus& other) {
    tiles += other.tiles;
    paper += other.paper;
    uniform_color += other.uniform_color;
    raw += other.raw;
    row_rle_smaller += other.row_rle_smaller;
    raw_payload_bytes += other.raw_payload_bytes;
    row_rle_bytes += other.row_rle_bytes;
    bounded_payload_bytes += other.bounded_payload_bytes;
    production_us += other.production_us;
    classification_us += other.classification_us;
    maximum_classification_us =
        std::max(maximum_classification_us, other.maximum_classification_us);
    operations_scanned += other.operations_scanned;
  }
};

void print_zoom(ZoomLevel zoom, const TileClassCensus& stats, bool passed) {
  const std::size_t uniform = stats.paper + stats.uniform_color;
  std::printf(
      "TINYDRAW_TILE_CENSUS zoom=%s tiles=%lu paper=%lu uniform_color=%lu uniform_total=%lu "
      "raw=%lu uniform_permille=%lu raw_slot_bytes=%lu raw_payload_bytes=%lu "
      "row_rle_smaller=%lu row_rle_bytes=%lu bounded_payload_bytes=%lu "
      "operations_scanned=%lu production_us=%lld classification_us=%lld "
      "maximum_classification_us=%lld pass=%u\n",
      zoom_name(zoom), static_cast<unsigned long>(stats.tiles),
      static_cast<unsigned long>(stats.paper), static_cast<unsigned long>(stats.uniform_color),
      static_cast<unsigned long>(uniform), static_cast<unsigned long>(stats.raw),
      static_cast<unsigned long>(stats.tiles == 0U ? 0U : uniform * 1'000U / stats.tiles),
      static_cast<unsigned long>(stats.raw * vector_v2::kTileBytes),
      static_cast<unsigned long>(stats.raw_payload_bytes),
      static_cast<unsigned long>(stats.row_rle_smaller),
      static_cast<unsigned long>(stats.row_rle_bytes),
      static_cast<unsigned long>(stats.bounded_payload_bytes),
      static_cast<unsigned long>(stats.operations_scanned),
      static_cast<long long>(stats.production_us), static_cast<long long>(stats.classification_us),
      static_cast<long long>(stats.maximum_classification_us), passed);
}

}  // namespace

bool run_vector_v2_tile_census(vector_v2::TileProducer& producer, MaterializedCanvas& canvas,
                               std::span<std::uint16_t> packed_scratch) {
  constexpr std::array zooms{
      ZoomLevel::k50Percent,
      ZoomLevel::k100Percent,
      ZoomLevel::k200Percent,
      ZoomLevel::k400Percent,
  };
  TileClassCensus total{};
  std::size_t expected_tiles = 0;
  for (const ZoomLevel zoom : zooms) {
    const vector_v2::TileGrid grid = vector_v2::tile_grid(zoom);
    expected_tiles += static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows);
  }
  bool passed = packed_scratch.size() >= vector_v2::kTilePixels;
  for (const ZoomLevel zoom : zooms) {
    TileClassCensus stats{};
    passed = passed && canvas.discard_tiles();
    const vector_v2::TileGrid grid = vector_v2::tile_grid(zoom);
    for (int group_row = 0; group_row < grid.rows && passed;
         group_row += vector_v2::kTileProducerRows) {
      for (int group_column = 0; group_column < grid.columns && passed;
           group_column += vector_v2::kTileProducerColumns) {
        const int last_column =
            std::min(grid.columns, group_column + vector_v2::kTileProducerColumns);
        const int last_row = std::min(grid.rows, group_row + vector_v2::kTileProducerRows);
        const vector_v2::PixelRect first_bounds =
            vector_v2::tile_pixel_bounds({zoom, static_cast<std::uint16_t>(group_column),
                                          static_cast<std::uint16_t>(group_row)});
        const vector_v2::PixelRect last_bounds =
            vector_v2::tile_pixel_bounds({zoom, static_cast<std::uint16_t>(last_column - 1),
                                          static_cast<std::uint16_t>(last_row - 1)});
        const vector_v2::ViewRequest view{
            .zoom = zoom,
            .level_pixels = {first_bounds.x0, first_bounds.y0, last_bounds.x1, last_bounds.y1},
        };
        while (passed) {
          const std::int64_t started = esp_timer_get_time();
          const auto step = producer.produce_next(view);
          stats.production_us += esp_timer_get_time() - started;
          if (!step.has_value()) {
            passed = false;
            break;
          }
          stats.operations_scanned += step->operations_scanned;
          if (step->complete) {
            break;
          }
          vTaskDelay(1U);
        }
        for (int row = group_row; row < last_row && passed; ++row) {
          for (int column = group_column; column < last_column && passed; ++column) {
            const TileKey key{zoom, static_cast<std::uint16_t>(column),
                              static_cast<std::uint16_t>(row)};
            const vector_v2::PixelRect bounds = vector_v2::tile_pixel_bounds(key);
            const int width = bounds.x1 - bounds.x0;
            const int height = bounds.y1 - bounds.y0;
            const std::size_t pixel_count =
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            auto pixels = packed_scratch.first(pixel_count);
            if (!canvas.copy_resident_tile(key, pixels)) {
              passed = false;
              break;
            }
            const std::int64_t started = esp_timer_get_time();
            const auto analysis = vector_v2::analyze_tile_payload(pixels, width, height);
            const std::int64_t elapsed_us = esp_timer_get_time() - started;
            if (!analysis.has_value()) {
              passed = false;
              break;
            }
            stats.include(*analysis, elapsed_us);
          }
        }
        vTaskDelay(1U);
      }
    }
    print_zoom(zoom, stats, passed);
    total.combine(stats);
  }
  const std::size_t uniform = total.paper + total.uniform_color;
  const bool complete = total.tiles == expected_tiles;
  std::printf(
      "TINYDRAW_TILE_CENSUS_DONE tiles=%lu expected_tiles=%lu paper=%lu uniform_color=%lu "
      "uniform_total=%lu "
      "raw=%lu uniform_permille=%lu current_all_raw_slot_bytes=%lu nonuniform_slot_bytes=%lu "
      "row_rle_smaller=%lu row_rle_bytes=%lu bounded_payload_bytes=%lu "
      "operations_scanned=%lu production_us=%lld classification_us=%lld "
      "maximum_classification_us=%lld complete=%u pass=%u\n",
      static_cast<unsigned long>(total.tiles), static_cast<unsigned long>(expected_tiles),
      static_cast<unsigned long>(total.paper), static_cast<unsigned long>(total.uniform_color),
      static_cast<unsigned long>(uniform), static_cast<unsigned long>(total.raw),
      static_cast<unsigned long>(total.tiles == 0U ? 0U : uniform * 1'000U / total.tiles),
      static_cast<unsigned long>(total.tiles * vector_v2::kTileBytes),
      static_cast<unsigned long>(total.raw * vector_v2::kTileBytes),
      static_cast<unsigned long>(total.row_rle_smaller),
      static_cast<unsigned long>(total.row_rle_bytes),
      static_cast<unsigned long>(total.bounded_payload_bytes),
      static_cast<unsigned long>(total.operations_scanned),
      static_cast<long long>(total.production_us), static_cast<long long>(total.classification_us),
      static_cast<long long>(total.maximum_classification_us), complete, passed && complete);
  return passed && complete;
}

}  // namespace tinydraw::esp32
