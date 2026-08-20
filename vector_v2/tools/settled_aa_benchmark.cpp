#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/settled_tile.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

constexpr std::size_t kOperationCapacity = 1'000U;
constexpr std::size_t kSampleCapacity = kOperationCapacity * 2U;
constexpr int kViewportWidth = vector_v2::kOverviewWidth;
constexpr int kViewportHeight = vector_v2::kOverviewHeight;
constexpr std::size_t kViewportPixels = static_cast<std::size_t>(kViewportWidth) * kViewportHeight;
constexpr std::size_t kIterations = 5U;

using Clock = std::chrono::steady_clock;

struct Rng {
  std::uint32_t state = 0x7A31'9D4BU;

  std::uint32_t next() {
    state = state * 1'664'525U + 1'013'904'223U;
    return state;
  }

  int bounded(int limit) { return static_cast<int>(next() % static_cast<std::uint32_t>(limit)); }
};

struct Document {
  std::vector<vector_v2::OperationRecord> records{kOperationCapacity};
  std::vector<vector_v2::CompactOperationSample> samples{kSampleCapacity};
  std::vector<std::uint64_t> spatial_cells =
      std::vector<std::uint64_t>(vector_v2::operation_spatial_cell_word_count(kOperationCapacity));
  std::vector<std::uint64_t> spatial_large =
      std::vector<std::uint64_t>(vector_v2::operation_spatial_word_count(kOperationCapacity));
  vector_v2::OperationSpatialIndex spatial_index{kOperationCapacity, spatial_cells, spatial_large};
  vector_v2::OperationLog log{records, samples, &spatial_index};
};

struct Workspace {
  std::array<std::uint8_t, vector_v2::kTilePixels> operation_alpha{};
  std::array<std::uint8_t, vector_v2::kTilePixels> accumulated{};
  std::array<std::uint16_t, vector_v2::kTilePixels> red{};
  std::array<std::uint16_t, vector_v2::kTilePixels> green{};
  std::array<std::uint16_t, vector_v2::kTilePixels> blue{};
  std::array<std::uint16_t, kOperationCapacity> candidates{};

  vector_v2::SettledTileWorkspace spans() {
    return {.operation_alpha = operation_alpha,
            .accumulated_alpha = accumulated,
            .red = red,
            .green = green,
            .blue = blue,
            .candidate_indices = candidates};
  }
};

struct View {
  int level_x = 0;
  int level_y = 0;
  int world_x = 0;
  int world_y = 0;
  int world_width = 0;
  int world_height = 0;
};

struct Measurement {
  std::uint64_t render_ns = 0;
  std::uint64_t publish_ns = 0;
  std::uint64_t checksum = 0;
  vector_v2::SettledTileStats stats{};
  std::size_t legacy_initialize_pixels = 0;
  std::size_t legacy_clear_pixels = 0;
  std::size_t legacy_composite_pixels = 0;
  std::size_t legacy_fold_pixels = 0;
};

struct OutputCounters {
  std::size_t queries = 0;
  std::size_t initialize = 0;
  std::size_t clear = 0;
  std::size_t curve = 0;
  std::size_t raster = 0;
  std::size_t composite = 0;
  std::size_t fold = 0;
};

View centered_view(vector_v2::ZoomLevel zoom) {
  const int percent = vector_v2::zoom_percent(zoom);
  const int world_width = (kViewportWidth * 100 + percent - 1) / percent;
  const int world_height = (kViewportHeight * 100 + percent - 1) / percent;
  const int world_x = std::max(0, (vector_v2::kWorldWidth - world_width) / 2);
  const int world_y = std::max(0, (vector_v2::kWorldHeight - world_height) / 2);
  return {.level_x = world_x * percent / 100,
          .level_y = world_y * percent / 100,
          .world_x = world_x,
          .world_y = world_y,
          .world_width = std::min(world_width, vector_v2::kWorldWidth),
          .world_height = std::min(world_height, vector_v2::kWorldHeight)};
}

std::uint16_t sample_coordinate(int world) {
  return static_cast<std::uint16_t>(
      std::clamp(world * vector_v2::kSampleUnitsPerWorldUnit, 0, static_cast<int>(UINT16_MAX)));
}

bool append_line(Document& document, int x0, int y0, int x1, int y1, int radius_256,
                 vector_v2::OperationTool tool, std::uint16_t color) {
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = sample_coordinate(x0),
                                        .y_quarter = sample_coordinate(y0),
                                        .radius_256 = static_cast<std::uint16_t>(radius_256)},
      vector_v2::CompactOperationSample{.x_quarter = sample_coordinate(x1),
                                        .y_quarter = sample_coordinate(y1),
                                        .radius_256 = static_cast<std::uint16_t>(radius_256),
                                        .elapsed_ms = 8U}};
  return document.log.append({.tool = tool, .color = color, .samples = samples}).has_value();
}

bool build_corpus(Document& document, std::string_view corpus, const View& view) {
  Rng rng{};
  const auto local_x = [&](int margin) {
    return view.world_x + margin + rng.bounded(std::max(1, view.world_width - 2 * margin));
  };
  const auto local_y = [&](int margin) {
    return view.world_y + margin + rng.bounded(std::max(1, view.world_height - 2 * margin));
  };

  for (std::size_t index = 0; index < kOperationCapacity; ++index) {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    int radius = 256;
    vector_v2::OperationTool tool = vector_v2::OperationTool::kPen;
    if (corpus == "distributed") {
      x0 = rng.bounded(vector_v2::kWorldWidth - 12) + 6;
      y0 = rng.bounded(vector_v2::kWorldHeight - 12) + 6;
      x1 = std::clamp(x0 + rng.bounded(25) - 12, 1, vector_v2::kWorldWidth - 2);
      y1 = std::clamp(y0 + rng.bounded(25) - 12, 1, vector_v2::kWorldHeight - 2);
      radius = (1 + rng.bounded(4)) * 256;
    } else if (corpus == "sparse") {
      const bool local = index < 40U;
      x0 = local ? local_x(8) : 8 + rng.bounded(96);
      y0 = local ? local_y(8) : 8 + rng.bounded(96);
      x1 = std::clamp(x0 + rng.bounded(17) - 8, 1, vector_v2::kWorldWidth - 2);
      y1 = std::clamp(y0 + rng.bounded(17) - 8, 1, vector_v2::kWorldHeight - 2);
      radius = (1 + rng.bounded(3)) * 256;
    } else if (corpus == "dense") {
      x0 = local_x(8);
      y0 = local_y(8);
      x1 = std::clamp(x0 + rng.bounded(33) - 16, 1, vector_v2::kWorldWidth - 2);
      y1 = std::clamp(y0 + rng.bounded(33) - 16, 1, vector_v2::kWorldHeight - 2);
      radius = (2 + rng.bounded(7)) * 256;
      tool = index % 9U == 0U ? vector_v2::OperationTool::kEraser : vector_v2::OperationTool::kPen;
    } else if (corpus == "long-crossing") {
      if (index >= 256U) {
        break;
      }
      const int inset = 2 + static_cast<int>(index % 7U);
      x0 = view.world_x + inset;
      x1 = view.world_x + view.world_width - inset - 1;
      y0 = view.world_y + rng.bounded(std::max(1, view.world_height));
      y1 = view.world_y + rng.bounded(std::max(1, view.world_height));
      radius = (1 + static_cast<int>(index % 4U)) * 256;
    } else if (corpus == "hairline-eraser") {
      if (index >= 600U) {
        break;
      }
      const bool eraser = index >= 400U;
      x0 = view.world_x + rng.bounded(std::max(1, view.world_width));
      y0 = view.world_y + rng.bounded(std::max(1, view.world_height));
      x1 = view.world_x + rng.bounded(std::max(1, view.world_width));
      y1 = view.world_y + rng.bounded(std::max(1, view.world_height));
      radius = eraser ? 384 : 128;
      tool = eraser ? vector_v2::OperationTool::kEraser : vector_v2::OperationTool::kPen;
    } else {
      return false;
    }
    if (!append_line(document, x0, y0, x1, y1, radius, tool,
                     static_cast<std::uint16_t>(0x001FU + index * 977U))) {
      return false;
    }
  }
  return true;
}

void add_stats(vector_v2::SettledTileStats& destination,
               const vector_v2::SettledTileStats& source) {
  destination.operations_scanned += source.operations_scanned;
  destination.operations_in_authority += source.operations_in_authority;
  destination.index_candidates += source.index_candidates;
  destination.deduplicated_candidates += source.deduplicated_candidates;
  destination.operations_intersecting += source.operations_intersecting;
  destination.strokes_intersecting += source.strokes_intersecting;
  destination.strokes_rendered += source.strokes_rendered;
#ifndef TINYDRAW_AA_BASELINE
  destination.candidate_queries += source.candidate_queries;
  destination.initialize_pixels += source.initialize_pixels;
  destination.operation_clear_pixels += source.operation_clear_pixels;
  destination.curve_units_prepared += source.curve_units_prepared;
  destination.raster_pixels += source.raster_pixels;
  destination.saturated_skip_pixels += source.saturated_skip_pixels;
  destination.composite_pixels += source.composite_pixels;
  destination.fold_pixels += source.fold_pixels;
#endif
}

std::uint64_t checksum(std::span<const std::uint16_t> pixels) {
  std::uint64_t hash = 1'469'598'103'934'665'603ULL;
  for (const std::uint16_t pixel : pixels) {
    hash ^= pixel;
    hash *= 1'099'511'628'211ULL;
  }
  return hash;
}

Measurement measure(Document& document, vector_v2::ZoomLevel zoom, const View& view) {
  Workspace workspace;
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  std::vector<std::uint16_t> viewport(kViewportPixels, 0xFFFFU);
  Measurement measurement{};
  for (int local_y = 0; local_y < kViewportHeight; local_y += vector_v2::kTileHeight) {
    for (int local_x = 0; local_x < kViewportWidth; local_x += vector_v2::kTileWidth) {
      const int width = std::min(vector_v2::kTileWidth, kViewportWidth - local_x);
      const int height = std::min(vector_v2::kTileHeight, kViewportHeight - local_y);
      const vector_v2::PixelRect bounds{view.level_x + local_x, view.level_y + local_y,
                                        view.level_x + local_x + width,
                                        view.level_y + local_y + height};
      vector_v2::SettledTileStats tile_stats{};
      const auto render_start = Clock::now();
      const bool rendered = vector_v2::render_settled_window(
          document.log, zoom, bounds, workspace.spans(),
          std::span<std::uint16_t>{tile}.first(static_cast<std::size_t>(width * height)),
          &tile_stats);
      measurement.render_ns += static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - render_start)
              .count());
      if (!rendered) {
        return {};
      }
      const std::size_t pixel_count = static_cast<std::size_t>(width * height);
      measurement.legacy_initialize_pixels += pixel_count;
      measurement.legacy_clear_pixels += tile_stats.strokes_intersecting * pixel_count;
      measurement.legacy_composite_pixels += tile_stats.strokes_rendered * pixel_count;
      measurement.legacy_fold_pixels += pixel_count;
      add_stats(measurement.stats, tile_stats);
      const auto publish_start = Clock::now();
      for (int row = 0; row < height; ++row) {
        std::memcpy(viewport.data() + static_cast<std::size_t>(local_y + row) * kViewportWidth +
                        static_cast<std::size_t>(local_x),
                    tile.data() + static_cast<std::size_t>(row * width),
                    static_cast<std::size_t>(width) * sizeof(std::uint16_t));
      }
      measurement.publish_ns += static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - publish_start)
              .count());
    }
  }
  measurement.checksum = checksum(viewport);
  return measurement;
}

OutputCounters counters(const Measurement& measurement) {
#ifdef TINYDRAW_AA_BASELINE
  return {.queries = 42U,
          .initialize = measurement.legacy_initialize_pixels,
          .clear = measurement.legacy_clear_pixels,
          .curve = 0U,
          .raster = 0U,
          .composite = measurement.legacy_composite_pixels,
          .fold = measurement.legacy_fold_pixels};
#else
  return {.queries = measurement.stats.candidate_queries,
          .initialize = measurement.stats.initialize_pixels,
          .clear = measurement.stats.operation_clear_pixels,
          .curve = measurement.stats.curve_units_prepared,
          .raster = measurement.stats.raster_pixels,
          .composite = measurement.stats.composite_pixels,
          .fold = measurement.stats.fold_pixels};
#endif
}

std::string_view zoom_name(vector_v2::ZoomLevel zoom) {
  switch (zoom) {
    case vector_v2::ZoomLevel::k25Percent:
      return "25";
    case vector_v2::ZoomLevel::k50Percent:
      return "50";
    case vector_v2::ZoomLevel::k100Percent:
      return "100";
    case vector_v2::ZoomLevel::k200Percent:
      return "200";
    case vector_v2::ZoomLevel::k400Percent:
      return "400";
  }
  return "?";
}

std::uint64_t oracle_checksum(std::string_view corpus, vector_v2::ZoomLevel zoom) {
  struct Oracle {
    std::string_view corpus;
    std::string_view zoom;
    std::uint64_t checksum;
  };
  constexpr std::array<Oracle, 25> oracles{{
      {"distributed", "25", 0xf21f680af58f9866ULL},
      {"distributed", "50", 0xc0a5646cb2bf177eULL},
      {"distributed", "100", 0x9e05d1cfad3f1b68ULL},
      {"distributed", "200", 0xc9d6359183976277ULL},
      {"distributed", "400", 0xe62a2cda4a6d82fULL},
      {"long-crossing", "25", 0xe68b1dca95a4368eULL},
      {"long-crossing", "50", 0x4ba18dcf10560193ULL},
      {"long-crossing", "100", 0x839e971cb464fc2fULL},
      {"long-crossing", "200", 0x22d9ba6266911eebULL},
      {"long-crossing", "400", 0x654834c2b30445d6ULL},
      {"hairline-eraser", "25", 0x2629a0ec1603ec71ULL},
      {"hairline-eraser", "50", 0x2f2523d338fb5623ULL},
      {"hairline-eraser", "100", 0x590faa31099211ecULL},
      {"hairline-eraser", "200", 0x3005fdd5293de995ULL},
      {"hairline-eraser", "400", 0x2b012a039f81af12ULL},
      {"sparse", "25", 0x17c763d40a50b0aULL},
      {"sparse", "50", 0x153365cdb94c0194ULL},
      {"sparse", "100", 0x06dfebfcdfccfe42ULL},
      {"sparse", "200", 0xe085e7385c3d3a77ULL},
      {"sparse", "400", 0x4ccd7bc508f117d6ULL},
      {"dense", "25", 0xc04fee06e4076d41ULL},
      {"dense", "50", 0x369dc227b8dff53cULL},
      {"dense", "100", 0xf7c4c0bdec22d429ULL},
      {"dense", "200", 0xbc8eb6fcdb5bbf20ULL},
      {"dense", "400", 0xfa935a0f96569284ULL},
  }};
  const auto match = std::find_if(oracles.begin(), oracles.end(), [&](const Oracle& oracle) {
    return oracle.corpus == corpus && oracle.zoom == zoom_name(zoom);
  });
  return match == oracles.end() ? 0U : match->checksum;
}

}  // namespace

int main() {
  constexpr std::array zooms{vector_v2::ZoomLevel::k25Percent, vector_v2::ZoomLevel::k50Percent,
                             vector_v2::ZoomLevel::k100Percent, vector_v2::ZoomLevel::k200Percent,
                             vector_v2::ZoomLevel::k400Percent};
  constexpr std::array<std::string_view, 5> corpora{"distributed", "long-crossing",
                                                    "hairline-eraser", "sparse", "dense"};
  std::cout << "corpus,zoom,render_ms,publish_ms,checksum,authority_operations,candidates,"
               "operations_scanned,operations_intersecting,strokes_rendered,queries,initialize_px,"
               "clear_px,curve_units,raster_px,composite_px,fold_px\n";
  for (const std::string_view corpus : corpora) {
    for (const vector_v2::ZoomLevel zoom : zooms) {
      const View view = centered_view(zoom);
      Document document;
      if (!document.log.ready() || !build_corpus(document, corpus, view)) {
        std::cerr << "failed corpus=" << corpus << " zoom=" << zoom_name(zoom) << '\n';
        return 1;
      }
      const Measurement warmup = measure(document, zoom, view);
      const std::uint64_t oracle = oracle_checksum(corpus, zoom);
      if (oracle == 0U || warmup.checksum != oracle) {
        std::cerr << "pixel oracle mismatch corpus=" << corpus << " zoom=" << zoom_name(zoom)
                  << " expected=0x" << std::hex << oracle << " actual=0x" << warmup.checksum
                  << std::dec << '\n';
        return 1;
      }
      std::array<Measurement, kIterations> measurements{};
      for (Measurement& measurement : measurements) {
        measurement = measure(document, zoom, view);
        if (measurement.checksum != warmup.checksum) {
          std::cerr << "nondeterministic pixels corpus=" << corpus << " zoom=" << zoom_name(zoom)
                    << '\n';
          return 1;
        }
      }
      std::sort(measurements.begin(), measurements.end(), [](const auto& left, const auto& right) {
        return left.render_ns < right.render_ns;
      });
      const Measurement& median = measurements[kIterations / 2U];
      const OutputCounters work = counters(median);
      std::cout << corpus << ',' << zoom_name(zoom) << ',' << std::fixed << std::setprecision(3)
                << static_cast<double>(median.render_ns) / 1'000'000.0 << ','
                << static_cast<double>(median.publish_ns) / 1'000'000.0 << ",0x" << std::hex
                << median.checksum << std::dec << ',' << median.stats.operations_in_authority << ','
                << median.stats.index_candidates << ',' << median.stats.operations_scanned << ','
                << median.stats.operations_intersecting << ',' << median.stats.strokes_rendered
                << ',' << work.queries << ',' << work.initialize << ',' << work.clear << ','
                << work.curve << ',' << work.raster << ',' << work.composite << ',' << work.fold
                << '\n';
    }
  }
  return 0;
}
