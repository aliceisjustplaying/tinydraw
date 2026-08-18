#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>

#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/settled_tile.h"
#include "tinydraw/vector_v2/tile_producer.h"

namespace v2 = tinydraw::vector_v2;

namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kCapacity = 1'200U;
constexpr std::size_t kRawSlots = 64U;
constexpr int kViewportWidth = v2::kOverviewWidth;
constexpr int kViewportHeight = v2::kOverviewHeight;

std::string_view zoom_name(v2::ZoomLevel zoom) {
  switch (zoom) {
    case v2::ZoomLevel::k25Percent:
      return "25";
    case v2::ZoomLevel::k50Percent:
      return "50";
    case v2::ZoomLevel::k100Percent:
      return "100";
    case v2::ZoomLevel::k200Percent:
      return "200";
    case v2::ZoomLevel::k400Percent:
      return "400";
  }
  return "?";
}

void mark_bounds(std::span<std::uint8_t> map, v2::PixelRect bounds) {
  const int first_column = bounds.x0 / v2::kOccupancyCellWorldSize;
  const int last_column = (bounds.x1 - 1) / v2::kOccupancyCellWorldSize;
  const int first_row = bounds.y0 / v2::kOccupancyCellWorldSize;
  const int last_row = (bounds.y1 - 1) / v2::kOccupancyCellWorldSize;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      const std::size_t bit =
          static_cast<std::size_t>(row) * v2::kOccupancyColumns + static_cast<std::size_t>(column);
      map[bit / 8U] |= static_cast<std::uint8_t>(1U << (bit % 8U));
    }
  }
}

std::size_t occupied_cells(std::span<const std::uint8_t> map) {
  std::size_t count = 0U;
  for (std::size_t bit = 0; bit < v2::kOccupancyCellCount; ++bit) {
    count += (map[bit / 8U] & static_cast<std::uint8_t>(1U << (bit % 8U))) != 0U ? 1U : 0U;
  }
  return count;
}

bool map_certainly_paper(std::span<const std::uint8_t> map, v2::TileKey key) {
  const v2::PixelRect level = v2::tile_pixel_bounds(key);
  const int percent = v2::zoom_percent(key.zoom);
  const v2::PixelRect world{level.x0 * 100 / percent, level.y0 * 100 / percent,
                            (level.x1 * 100 + percent - 1) / percent,
                            (level.y1 * 100 + percent - 1) / percent};
  for (int row = world.y0 / v2::kOccupancyCellWorldSize;
       row <= (world.y1 - 1) / v2::kOccupancyCellWorldSize; ++row) {
    for (int column = world.x0 / v2::kOccupancyCellWorldSize;
         column <= (world.x1 - 1) / v2::kOccupancyCellWorldSize; ++column) {
      const std::size_t bit =
          static_cast<std::size_t>(row) * v2::kOccupancyColumns + static_cast<std::size_t>(column);
      if ((map[bit / 8U] & static_cast<std::uint8_t>(1U << (bit % 8U))) != 0U) {
        return false;
      }
    }
  }
  return true;
}

v2::ViewRequest centered_view(v2::ZoomLevel zoom) {
  const int percent = v2::zoom_percent(zoom);
  const int level_width = v2::kWorldWidth * percent / 100;
  const int level_height = v2::kWorldHeight * percent / 100;
  const int center_x = v2::kWorldWidth * percent / 200;
  const int center_y = v2::kWorldHeight * percent / 200;
  const int x0 = std::clamp(center_x - kViewportWidth / 2, 0, level_width - kViewportWidth);
  const int y0 = std::clamp(center_y - kViewportHeight / 2, 0, level_height - kViewportHeight);
  return {.zoom = zoom, .level_pixels = {x0, y0, x0 + kViewportWidth, y0 + kViewportHeight}};
}

struct Document {
  std::vector<v2::OperationRecord> records = std::vector<v2::OperationRecord>(kCapacity);
  std::vector<v2::CompactOperationSample> samples =
      std::vector<v2::CompactOperationSample>(kCapacity);
  std::vector<std::uint64_t> spatial_cells =
      std::vector<std::uint64_t>(v2::operation_spatial_cell_word_count(kCapacity));
  std::vector<std::uint64_t> spatial_large =
      std::vector<std::uint64_t>(v2::operation_spatial_word_count(kCapacity));
  v2::OperationSpatialIndex spatial_index{kCapacity, spatial_cells, spatial_large};
  v2::OperationLog log{records, samples, &spatial_index};
  std::array<std::uint8_t, v2::kOccupancyBytes> historical{};

  bool append_point(std::size_t ordinal, int x, int y, int radius,
                    v2::OperationTool tool = v2::OperationTool::kPen) {
    const std::array point{v2::CompactOperationSample{
        .x_quarter = static_cast<std::uint16_t>(x * v2::kSampleUnitsPerWorldUnit),
        .y_quarter = static_cast<std::uint16_t>(y * v2::kSampleUnitsPerWorldUnit),
        .radius_256 = static_cast<std::uint16_t>(radius * 256)}};
    const auto identity = log.append({.tool = tool,
                                      .color = static_cast<std::uint16_t>(0x001FU + (ordinal & 1U)),
                                      .gesture_id = static_cast<std::uint16_t>(ordinal + 1U),
                                      .samples = point});
    if (!identity.has_value()) {
      return false;
    }
    const auto operation = log.operation(identity->operation_index);
    if (!operation.has_value()) {
      return false;
    }
    mark_bounds(historical, operation->world_bounds);
    return true;
  }

  bool undo(std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
      auto change = log.prepare_undo();
      if (!change.has_value()) {
        return false;
      }
      change->publish();
    }
    return true;
  }

  bool redo(std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
      auto change = log.prepare_redo();
      if (!change.has_value()) {
        return false;
      }
      change->publish();
    }
    return true;
  }
};

std::pair<int, int> distributed_point(std::size_t index) {
  return {24 + static_cast<int>((index * 541U) % 1'424U),
          24 + static_cast<int>((index * 887U) % 1'744U)};
}

std::unique_ptr<Document> make_document(std::string_view corpus) {
  auto document = std::make_unique<Document>();
  if (corpus == "erase-overlap") {
    for (std::size_t index = 0; index < 300U; ++index) {
      const auto [x, y] = distributed_point(index);
      if (!document->append_point(index * 2U, x, y, 4) ||
          !document->append_point(index * 2U + 1U, x, y, 4, v2::OperationTool::kEraser)) {
        return nullptr;
      }
    }
    return document;
  }
  if (corpus == "erase-drift") {
    for (std::size_t index = 0; index < 300U; ++index) {
      const auto [x, y] = distributed_point(index);
      if (!document->append_point(index, x, y, 3)) {
        return nullptr;
      }
    }
    for (std::size_t index = 0; index < 200U; ++index) {
      const auto [x, y] = distributed_point(index + 431U);
      if (!document->append_point(300U + index, x, y, 10, v2::OperationTool::kEraser)) {
        return nullptr;
      }
    }
    return document;
  }
  for (std::size_t index = 0; index < 1'000U; ++index) {
    const auto [x, y] = distributed_point(index);
    if (!document->append_point(index, x, y, corpus == "adversarial-undo" ? 12 : 3)) {
      return nullptr;
    }
  }
  if (corpus == "undo") {
    return document->undo(750U) ? std::move(document) : nullptr;
  }
  if (corpus == "redo") {
    return document->undo(750U) && document->redo(500U) ? std::move(document) : nullptr;
  }
  if (corpus == "branch") {
    if (!document->undo(750U)) {
      return nullptr;
    }
    for (std::size_t index = 0; index < 100U; ++index) {
      const auto [x, y] = distributed_point(index + 2'000U);
      if (!document->append_point(1'000U + index, x, y, 3)) {
        return nullptr;
      }
    }
    return document;
  }
  if (corpus == "adversarial-undo") {
    return document->undo(1'000U) ? std::move(document) : nullptr;
  }
  return nullptr;
}

void build_optical_map(std::span<const std::uint16_t> overview, std::span<std::uint8_t> output) {
  std::fill(output.begin(), output.end(), 0U);
  constexpr int kOverviewCell = v2::kOccupancyCellWorldSize / 4;
  for (int row = 0; row < v2::kOccupancyRows; ++row) {
    for (int column = 0; column < v2::kOccupancyColumns; ++column) {
      bool ink = false;
      for (int y = row * kOverviewCell; y < (row + 1) * kOverviewCell && !ink; ++y) {
        for (int x = column * kOverviewCell; x < (column + 1) * kOverviewCell; ++x) {
          ink = ink || overview[static_cast<std::size_t>(y) * v2::kOverviewWidth +
                                static_cast<std::size_t>(x)] != 0xFFFFU;
        }
      }
      if (ink) {
        const std::size_t bit = static_cast<std::size_t>(row) * v2::kOccupancyColumns +
                                static_cast<std::size_t>(column);
        output[bit / 8U] |= static_cast<std::uint8_t>(1U << (bit % 8U));
      }
    }
  }
}

struct ProducerTotals {
  std::size_t calls = 0U;
  std::size_t operations_scanned = 0U;
  std::size_t raster_work = 0U;
};

struct CanvasRun {
  std::vector<std::uint16_t> owned_overview = std::vector<std::uint16_t>(v2::kOverviewPixels);
  std::vector<v2::MaterializedUniformStorage> uniforms =
      std::vector<v2::MaterializedUniformStorage>(v2::kMaterializedTileIdentityCount);
  std::array<std::uint8_t, v2::kOccupancyBytes> occupancy{};
  std::vector<v2::MaterializedSlotStorage> slots =
      std::vector<v2::MaterializedSlotStorage>(kRawSlots);
  std::vector<std::uint16_t> tile_pixels = std::vector<std::uint16_t>(kRawSlots * v2::kTilePixels);
  std::vector<std::uint16_t> raw_directory =
      std::vector<std::uint16_t>(v2::kMaterializedTileIdentityCount);
  v2::MaterializedCanvas canvas{owned_overview, uniforms, occupancy,    slots,
                                tile_pixels,    {0},      raw_directory};
  std::array<std::uint16_t, v2::kTileProducerPixels> surface{};
  std::array<std::uint8_t, v2::kTileProducerMaskBytes> finalized{};
  std::array<std::uint16_t, v2::kTileProducerSummaryRows> summary_rows{};
  std::array<std::uint32_t, v2::kTileProducerSummaryWords> summary_words{};
  std::vector<std::uint32_t> chord_plans =
      std::vector<std::uint32_t>(v2::kOperationChordStorageBytes / 4U);
  std::vector<std::uint16_t> candidates = std::vector<std::uint16_t>(kCapacity);
  v2::TileProducer producer;

  CanvasRun(v2::OperationLog& log, std::span<const std::uint16_t> overview,
            std::span<const std::uint8_t> map)
      : producer(log, canvas,
                 {.supertask_pixels = surface,
                  .finalized_pixels = finalized,
                  .summary_row_unset = summary_rows,
                  .summary_saturated_words = summary_words,
                  .operation_chord_plans = std::as_writable_bytes(std::span(chord_plans)),
                  .candidate_indices = candidates}) {
    if (!canvas.restore_snapshot(log.current_revision(), overview, map)) {
      throw std::runtime_error("canvas restore failed");
    }
  }

  bool produce(const v2::ViewRequest& view, ProducerTotals& totals) {
    for (std::size_t guard = 0; guard < 100'000U; ++guard) {
      const auto step = producer.produce_next(view);
      if (!step.has_value()) {
        return false;
      }
      ++totals.calls;
      totals.operations_scanned += step->operations_scanned;
      totals.raster_work += step->raster_work;
      if (step->complete) {
        return true;
      }
    }
    return false;
  }
};

std::pair<std::uint64_t, std::uint64_t> median_produce_ns(v2::OperationLog& log,
                                                          std::span<const std::uint16_t> overview,
                                                          std::span<const std::uint8_t> current_map,
                                                          std::span<const std::uint8_t> safe_map,
                                                          const v2::ViewRequest& view) {
  std::array<std::uint64_t, 5> current_samples{};
  std::array<std::uint64_t, 5> safe_samples{};
  for (std::size_t index = 0; index < current_samples.size(); ++index) {
    CanvasRun current(log, overview, current_map);
    CanvasRun safe(log, overview, safe_map);
    ProducerTotals totals{};
    if ((index & 1U) == 0U) {
      auto started = Clock::now();
      if (!current.produce(view, totals)) {
        throw std::runtime_error("current production failed");
      }
      current_samples[index] = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
      totals = {};
      started = Clock::now();
      if (!safe.produce(view, totals)) {
        throw std::runtime_error("safe production failed");
      }
      safe_samples[index] = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
    } else {
      auto started = Clock::now();
      if (!safe.produce(view, totals)) {
        throw std::runtime_error("safe production failed");
      }
      safe_samples[index] = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
      totals = {};
      started = Clock::now();
      if (!current.produce(view, totals)) {
        throw std::runtime_error("current production failed");
      }
      current_samples[index] = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
    }
  }
  std::sort(current_samples.begin(), current_samples.end());
  std::sort(safe_samples.begin(), safe_samples.end());
  return {current_samples[current_samples.size() / 2U], safe_samples[safe_samples.size() / 2U]};
}

std::size_t false_positive_groups(std::span<const std::uint8_t> current,
                                  std::span<const std::uint8_t> safe, const v2::ViewRequest& view) {
  std::size_t groups = 0U;
  for (int row = view.level_pixels.y0 / v2::kTileHeight;
       row <= (view.level_pixels.y1 - 1) / v2::kTileHeight; row += 2) {
    for (int column = view.level_pixels.x0 / v2::kTileWidth;
         column <= (view.level_pixels.x1 - 1) / v2::kTileWidth; column += 2) {
      bool false_positive = false;
      for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
          const v2::TileKey key{view.zoom, static_cast<std::uint16_t>((column & ~1) + dx),
                                static_cast<std::uint16_t>((row & ~1) + dy)};
          if (v2::valid_tile_key(key)) {
            false_positive = false_positive ||
                             (!map_certainly_paper(current, key) && map_certainly_paper(safe, key));
          }
        }
      }
      groups += false_positive ? 1U : 0U;
    }
  }
  return groups;
}

struct SettleTotals {
  std::size_t tiles = 0U;
  std::size_t authority = 0U;
  std::size_t scans = 0U;
  std::size_t work = 0U;
};

bool measure_extra_settle(v2::OperationLog& log, const v2::ViewRequest& view,
                          const CanvasRun& current, const CanvasRun& safe, SettleTotals& totals) {
  std::array<std::uint8_t, v2::kTilePixels> operation_alpha{};
  std::array<std::uint8_t, v2::kTilePixels> accumulated{};
  std::array<std::uint16_t, v2::kTilePixels> red{};
  std::array<std::uint16_t, v2::kTilePixels> green{};
  std::array<std::uint16_t, v2::kTilePixels> blue{};
  std::vector<std::uint16_t> candidates(kCapacity);
  std::array<std::uint16_t, v2::kTilePixels> output{};
  const v2::SettledTileWorkspace workspace{operation_alpha, accumulated, red,
                                           green,           blue,        candidates};
  for (int row = view.level_pixels.y0 / v2::kTileHeight;
       row <= (view.level_pixels.y1 - 1) / v2::kTileHeight; ++row) {
    for (int column = view.level_pixels.x0 / v2::kTileWidth;
         column <= (view.level_pixels.x1 - 1) / v2::kTileWidth; ++column) {
      const v2::TileKey key{view.zoom, static_cast<std::uint16_t>(column),
                            static_cast<std::uint16_t>(row)};
      const auto current_source = current.canvas.lookup(key);
      const auto safe_source = safe.canvas.lookup(key);
      if (!current_source.has_value() || !safe_source.has_value() ||
          current_source->kind != v2::SourceKind::kTileSlot ||
          safe_source->kind != v2::SourceKind::kUniform) {
        continue;
      }
      v2::SettledTileStats stats{};
      if (!v2::render_settled_tile(log, key, workspace, output, &stats)) {
        return false;
      }
      ++totals.tiles;
      totals.authority += stats.operations_in_authority;
      totals.scans += stats.operations_scanned;
      totals.work += stats.initialize_pixels + stats.candidate_queries + stats.operations_scanned +
                     stats.operation_clear_pixels + stats.curve_units_prepared +
                     stats.raster_pixels + stats.composite_pixels + stats.fold_pixels;
    }
  }
  return true;
}

bool measure_corpus(std::string_view corpus) {
  auto document = make_document(corpus);
  if (!document) {
    return false;
  }
  std::array<std::uint16_t, v2::kOverviewPixels> overview{};
  std::array<std::uint8_t, v2::kOccupancyBytes> safe{};
  std::array<std::uint8_t, v2::kOccupancyBytes> optical{};
  if (!v2::replay_active_overview(document->log, overview) ||
      !v2::build_tiled_may_ink(document->log, safe)) {
    return false;
  }
  build_optical_map(overview, optical);

  std::array<std::uint64_t, 9> rebuild_samples{};
  std::array<std::uint8_t, v2::kOccupancyBytes> rebuild{};
  for (std::uint64_t& sample : rebuild_samples) {
    const auto started = Clock::now();
    if (!v2::build_tiled_may_ink(document->log, rebuild)) {
      return false;
    }
    sample = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - started).count());
  }
  std::sort(rebuild_samples.begin(), rebuild_samples.end());
  const std::uint64_t rebuild_ns = rebuild_samples[rebuild_samples.size() / 2U];
  const auto authority = document->log.read_view();

  constexpr std::array zooms{v2::ZoomLevel::k25Percent, v2::ZoomLevel::k50Percent,
                             v2::ZoomLevel::k100Percent, v2::ZoomLevel::k200Percent,
                             v2::ZoomLevel::k400Percent};
  for (const v2::ZoomLevel zoom : zooms) {
    ProducerTotals current_totals{};
    ProducerTotals safe_totals{};
    SettleTotals settle{};
    std::size_t extra_groups = 0U;
    std::uint64_t current_produce_ns = 0U;
    std::uint64_t safe_produce_ns = 0U;
    if (zoom != v2::ZoomLevel::k25Percent) {
      const v2::ViewRequest view = centered_view(zoom);
      CanvasRun current_run(document->log, overview, document->historical);
      CanvasRun safe_run(document->log, overview, safe);
      if (!current_run.produce(view, current_totals) || !safe_run.produce(view, safe_totals)) {
        return false;
      }
      std::vector<std::uint16_t> current_pixels(v2::kOverviewPixels);
      std::vector<std::uint16_t> safe_pixels(v2::kOverviewPixels);
      if (!current_run.canvas.compose_view(view, current_pixels).has_value() ||
          !safe_run.canvas.compose_view(view, safe_pixels).has_value() ||
          current_pixels != safe_pixels) {
        return false;
      }
      extra_groups = false_positive_groups(document->historical, safe, view);
      if (!measure_extra_settle(document->log, view, current_run, safe_run, settle)) {
        return false;
      }
      std::tie(current_produce_ns, safe_produce_ns) =
          median_produce_ns(document->log, overview, document->historical, safe, view);
    }
    std::cout << corpus << ',' << zoom_name(zoom) << ',' << authority.active_operation_count << ','
              << authority.retained_operation_count << ',' << occupied_cells(document->historical)
              << ',' << occupied_cells(safe) << ',' << occupied_cells(optical) << ','
              << extra_groups << ',' << current_totals.calls << ',' << safe_totals.calls << ','
              << current_totals.operations_scanned << ',' << safe_totals.operations_scanned << ','
              << current_totals.raster_work << ',' << safe_totals.raster_work << ',' << settle.tiles
              << ',' << settle.authority << ',' << settle.scans << ',' << settle.work << ','
              << static_cast<double>(current_produce_ns) / 1'000.0 << ','
              << static_cast<double>(safe_produce_ns) / 1'000.0 << ','
              << static_cast<double>(rebuild_ns) / 1'000.0 << '\n';
  }
  return true;
}

}  // namespace

int main() {
  std::cout << "corpus,zoom,active,retained,current_cells,safe_cells,optical_cells,extra_groups,"
               "current_calls,safe_calls,current_scans,safe_scans,current_raster_work,"
               "safe_raster_work,extra_settle_tiles,extra_settle_authority,extra_settle_scans,"
               "extra_settle_work,current_produce_us,safe_produce_us,rebuild_us\n";
  constexpr std::array corpora{"erase-overlap", "erase-drift", "undo",
                               "redo",          "branch",      "adversarial-undo"};
  for (const std::string_view corpus : corpora) {
    if (!measure_corpus(corpus)) {
      std::cerr << "failed corpus=" << corpus << '\n';
      return 1;
    }
  }
  return 0;
}
