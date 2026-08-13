#include "tinydraw/production/tile_producer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "tinydraw/production/storage_overlap.h"

namespace tinydraw::production {
namespace {

bool intersects(PixelRect left, PixelRect right) {
  return left.x0 < right.x1 && right.x0 < left.x1 && left.y0 < right.y1 && right.y0 < left.y1;
}

PixelRect operation_level_bounds(PixelRect world, ZoomLevel zoom) {
  const int percent = zoom_percent(zoom);
  return {
      .x0 = world.x0 * percent / 100,
      .y0 = world.y0 * percent / 100,
      .x1 = std::min(kWorldWidth * percent / 100, (world.x1 * percent + 99) / 100),
      .y1 = std::min(kWorldHeight * percent / 100, (world.y1 * percent + 99) / 100),
  };
}

std::size_t distance_squared(int x, int y, int center_x, int center_y) {
  const auto delta_x = static_cast<std::int64_t>(x) - center_x;
  const auto delta_y = static_cast<std::int64_t>(y) - center_y;
  return static_cast<std::size_t>(delta_x * delta_x + delta_y * delta_y);
}

}  // namespace

TileProducer::TileProducer(OperationLog& log, MaterializedCanvas& canvas,
                           TileProducerWorkspace workspace,
                           DocumentRevision uniform_baseline_revision, std::uint16_t baseline_color)
    : log_(log),
      canvas_(canvas),
      workspace_(workspace),
      baseline_revision_(uniform_baseline_revision),
      baseline_color_(baseline_color) {}

bool TileProducer::ready() const {
  const auto supertask_bytes = std::as_bytes(workspace_.supertask_pixels);
  const auto packed_bytes = std::as_bytes(workspace_.packed_tile_pixels);
  return log_.ready() && canvas_.ready() &&
         workspace_.supertask_pixels.size() >= kTileProducerPixels &&
         workspace_.packed_tile_pixels.size() >= kTilePixels &&
         !storage_overlaps(supertask_bytes, packed_bytes) &&
         canvas_.accepts_external_workspace(supertask_bytes) &&
         canvas_.accepts_external_workspace(packed_bytes) &&
         !log_.workspace_overlaps_storage(supertask_bytes) &&
         !log_.workspace_overlaps_storage(packed_bytes);
}

bool TileProducer::valid_view(const ViewRequest& view) const {
  if (view.zoom == ZoomLevel::k25Percent) {
    return false;
  }
  const int level_width = kWorldWidth * zoom_percent(view.zoom) / 100;
  const int level_height = kWorldHeight * zoom_percent(view.zoom) / 100;
  return view.level_pixels.x0 >= 0 && view.level_pixels.y0 >= 0 &&
         view.level_pixels.x1 > view.level_pixels.x0 &&
         view.level_pixels.y1 > view.level_pixels.y0 && view.level_pixels.x1 <= level_width &&
         view.level_pixels.y1 <= level_height;
}

bool TileProducer::tile_satisfies(TileKey key) const {
  const auto source = canvas_.lookup(key);
  return source.has_value() && source->kind == SourceKind::kTileSlot &&
         source->identity.revision == canvas_.current_revision() &&
         static_cast<int>(source->identity.quality) >=
             static_cast<int>(MaterializationQuality::kImmediate);
}

std::optional<std::size_t> TileProducer::visible_tiles_remaining(const ViewRequest& view) const {
  if (!ready() || !valid_view(view)) {
    return std::nullopt;
  }
  const PixelRect rect = view.level_pixels;
  const int first_column = rect.x0 / kTileWidth;
  const int last_column = (rect.x1 - 1) / kTileWidth;
  const int first_row = rect.y0 / kTileHeight;
  const int last_row = (rect.y1 - 1) / kTileHeight;
  std::size_t remaining = 0;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      remaining += !tile_satisfies(
          {view.zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
    }
  }
  return remaining;
}

std::optional<TileKey> TileProducer::choose_missing_group(const ViewRequest& view) const {
  const PixelRect rect = view.level_pixels;
  const int first_column = rect.x0 / kTileWidth;
  const int last_column = (rect.x1 - 1) / kTileWidth;
  const int first_row = rect.y0 / kTileHeight;
  const int last_row = (rect.y1 - 1) / kTileHeight;
  const int center_x = (rect.x0 + rect.x1) / 2;
  const int center_y = (rect.y0 + rect.y1) / 2;
  std::optional<TileKey> selected;
  std::size_t best_distance = std::numeric_limits<std::size_t>::max();
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      const TileKey key{view.zoom, static_cast<std::uint16_t>(column),
                        static_cast<std::uint16_t>(row)};
      if (tile_satisfies(key)) {
        continue;
      }
      const int group_column = column & ~1;
      const int group_row = row & ~1;
      const std::size_t candidate_distance =
          distance_squared(group_column * kTileWidth + kTileProducerWidth / 2,
                           group_row * kTileHeight + kTileProducerHeight / 2, center_x, center_y);
      if (!selected.has_value() || candidate_distance < best_distance) {
        selected = {view.zoom, static_cast<std::uint16_t>(group_column),
                    static_cast<std::uint16_t>(group_row)};
        best_distance = candidate_distance;
      }
    }
  }
  return selected;
}

std::optional<TileProductionStep> TileProducer::produce_next(const ViewRequest& view) {
  const auto remaining = visible_tiles_remaining(view);
  if (!remaining.has_value()) {
    return std::nullopt;
  }
  if (*remaining == 0U) {
    return TileProductionStep{.complete = true};
  }
  const auto group = choose_missing_group(view);
  if (!group.has_value()) {
    return std::nullopt;
  }
  return render_group(view, *group);
}

std::optional<TileProductionStep> TileProducer::render_group(const ViewRequest& view,
                                                             TileKey group_origin) {
  const TileGrid grid = tile_grid(view.zoom);
  const int first_column = group_origin.column;
  const int first_row = group_origin.row;
  const int last_column = std::min(grid.columns, first_column + kTileProducerColumns);
  const int last_row = std::min(grid.rows, first_row + kTileProducerRows);
  const int level_width = kWorldWidth * zoom_percent(view.zoom) / 100;
  const int level_height = kWorldHeight * zoom_percent(view.zoom) / 100;
  const PixelRect bounds{
      .x0 = first_column * kTileWidth,
      .y0 = first_row * kTileHeight,
      .x1 = std::min(level_width, last_column * kTileWidth),
      .y1 = std::min(level_height, last_row * kTileHeight),
  };
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  if (width <= 0 || height <= 0 || log_.current_revision() != canvas_.current_revision()) {
    return std::nullopt;
  }
  const OperationLogEpoch epoch = log_.epoch();
  const DocumentRevision revision = log_.current_revision();
  const auto replay = log_.replay_range(epoch, baseline_revision_, revision);
  if (!replay.has_value()) {
    return std::nullopt;
  }

  auto surface = workspace_.supertask_pixels.first(kTileProducerPixels);
  std::fill(surface.begin(), surface.end(), baseline_color_);
  TileProductionStep result{.level_bounds = bounds, .operations_scanned = replay->operation_count};
  for (std::size_t offset = 0; offset < replay->operation_count; ++offset) {
    const auto stored = log_.operation(replay->first_operation + offset);
    if (!stored.has_value()) {
      return std::nullopt;
    }
    if (!intersects(operation_level_bounds(stored->world_bounds, view.zoom), bounds)) {
      continue;
    }
    if (!apply_incremental_operation(
            {.tool = stored->tool, .color = stored->color, .samples = stored->samples},
            {.zoom = view.zoom,
             .level_bounds = bounds,
             .pixels = surface,
             .stride = kTileProducerWidth})) {
      return std::nullopt;
    }
    ++result.operations_rendered;
  }
  if (log_.epoch() != epoch || log_.current_revision() != revision ||
      canvas_.current_revision() != revision) {
    return std::nullopt;
  }

  for (int row = first_row; row < last_row; ++row) {
    for (int column = first_column; column < last_column; ++column) {
      const TileKey key{view.zoom, static_cast<std::uint16_t>(column),
                        static_cast<std::uint16_t>(row)};
      if (tile_satisfies(key)) {
        continue;
      }
      const PixelRect tile_bounds = tile_pixel_bounds(key);
      const int tile_width = tile_bounds.x1 - tile_bounds.x0;
      const int tile_height = tile_bounds.y1 - tile_bounds.y0;
      const std::size_t packed_count =
          static_cast<std::size_t>(tile_width) * static_cast<std::size_t>(tile_height);
      auto packed = workspace_.packed_tile_pixels.first(packed_count);
      for (int tile_row = 0; tile_row < tile_height; ++tile_row) {
        const auto source_offset =
            static_cast<std::size_t>(tile_bounds.y0 - bounds.y0 + tile_row) * kTileProducerWidth +
            static_cast<std::size_t>(tile_bounds.x0 - bounds.x0);
        const auto destination_offset = static_cast<std::size_t>(tile_row * tile_width);
        std::copy_n(surface.begin() + static_cast<std::ptrdiff_t>(source_offset), tile_width,
                    packed.begin() + static_cast<std::ptrdiff_t>(destination_offset));
      }
      if (!canvas_.publish_tile(key, revision, MaterializationQuality::kImmediate, packed)
               .has_value()) {
        return std::nullopt;
      }
      ++result.tiles_published;
    }
  }
  const auto remaining = visible_tiles_remaining(view);
  if (!remaining.has_value()) {
    return std::nullopt;
  }
  result.visible_tiles_remaining = *remaining;
  result.complete = *remaining == 0U;
  return result;
}

}  // namespace tinydraw::production
