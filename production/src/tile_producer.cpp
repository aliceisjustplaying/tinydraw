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

bool TileProducer::valid_view(const ViewRequest& view) {
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

bool TileProducer::tile_satisfies(TileKey key, MaterializationQuality quality) const {
  const auto source = canvas_.lookup(key);
  return source.has_value() && source->kind == SourceKind::kTileSlot &&
         source->identity.revision == canvas_.current_revision() &&
         static_cast<int>(source->identity.quality) >= static_cast<int>(quality);
}

std::optional<std::size_t> TileProducer::visible_tiles_remaining(const ViewRequest& view) const {
  return visible_tiles_remaining(view, MaterializationQuality::kImmediate);
}

std::optional<std::size_t> TileProducer::visible_tiles_remaining(
    const ViewRequest& view, MaterializationQuality quality) const {
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
          {view.zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)},
          quality);
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
      if (tile_satisfies(key, MaterializationQuality::kImmediate)) {
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

std::optional<TileKey> TileProducer::choose_missing_tile(const ViewRequest& view,
                                                         MaterializationQuality quality) const {
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
      if (tile_satisfies(key, quality)) {
        continue;
      }
      const std::size_t candidate_distance =
          distance_squared(column * kTileWidth + kTileWidth / 2,
                           row * kTileHeight + kTileHeight / 2, center_x, center_y);
      if (!selected.has_value() || candidate_distance < best_distance) {
        selected = key;
        best_distance = candidate_distance;
      }
    }
  }
  return selected;
}

std::optional<TileProductionStep> TileProducer::produce_next(const ViewRequest& view) {
  const auto remaining = visible_tiles_remaining(view);
  if (!remaining.has_value()) {
    active_group_ = {};
    return std::nullopt;
  }
  if (*remaining == 0U) {
    active_group_ = {};
    return TileProductionStep{.complete = true};
  }
  if (!active_group_.active || !(active_group_.view.zoom == view.zoom &&
                                 active_group_.view.level_pixels == view.level_pixels)) {
    const auto group = choose_missing_group(view);
    if (!group.has_value() || !start_group(view, *group)) {
      active_group_ = {};
      return std::nullopt;
    }
  }
  return render_active_batch();
}

std::optional<TileProductionStep> TileProducer::produce_next_2x_aa_100(const ViewRequest& view) {
  if (view.zoom != ZoomLevel::k100Percent) {
    return std::nullopt;
  }
  const auto remaining = visible_tiles_remaining(view, MaterializationQuality::kSettled);
  if (!remaining.has_value()) {
    return std::nullopt;
  }
  if (*remaining == 0U) {
    return TileProductionStep{.complete = true};
  }
  const auto tile = choose_missing_tile(view, MaterializationQuality::kSettled);
  if (!tile.has_value()) {
    return std::nullopt;
  }
  return render_2x_aa_tile(view, *tile);
}

bool TileProducer::reset_uniform_baseline(DocumentRevision revision, std::uint16_t color) {
  if (log_.operation_count() != 0U || log_.current_revision() != revision ||
      canvas_.current_revision() != revision) {
    return false;
  }
  baseline_revision_ = revision;
  baseline_color_ = color;
  active_group_ = {};
  return true;
}

bool TileProducer::start_group(const ViewRequest& view, TileKey group_origin) {
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
  const OperationLogEpoch epoch = log_.epoch();
  const DocumentRevision revision = log_.current_revision();
  const auto replay = log_.replay_range(epoch, baseline_revision_, revision);
  if (bounds.x1 <= bounds.x0 || bounds.y1 <= bounds.y0 || !replay.has_value() ||
      revision != canvas_.current_revision()) {
    return false;
  }
  auto surface = workspace_.supertask_pixels.first(kTileProducerPixels);
  std::fill(surface.begin(), surface.end(), baseline_color_);
  active_group_ = {.view = view,
                   .origin = group_origin,
                   .bounds = bounds,
                   .epoch = epoch,
                   .revision = revision,
                   .first_operation = replay->first_operation,
                   .operation_count = replay->operation_count,
                   .next_operation = 0,
                   .next_sample = 1,
                   .active = true};
  return true;
}

std::optional<TileProductionStep> TileProducer::render_active_batch() {
  if (!active_group_.active || log_.epoch() != active_group_.epoch ||
      log_.current_revision() != active_group_.revision ||
      canvas_.current_revision() != active_group_.revision) {
    active_group_ = {};
    return std::nullopt;
  }
  TileProductionStep result{.level_bounds = active_group_.bounds};
  auto surface = workspace_.supertask_pixels.first(kTileProducerPixels);
  std::size_t operations_consumed = 0;
  std::size_t samples_consumed = 0;
  while (active_group_.next_operation < active_group_.operation_count &&
         operations_consumed < kTileProducerOperationBatch &&
         samples_consumed < kTileProducerSampleBatch) {
    const auto stored =
        log_.operation(active_group_.first_operation + active_group_.next_operation);
    if (!stored.has_value()) {
      active_group_ = {};
      return std::nullopt;
    }
    const bool visible =
        intersects(operation_level_bounds(stored->world_bounds, active_group_.view.zoom),
                   active_group_.bounds);
    const std::size_t segment_count = std::max<std::size_t>(1U, stored->samples.size() - 1U);
    if (!visible) {
      ++active_group_.next_operation;
      active_group_.next_sample = 1U;
      ++operations_consumed;
      ++result.operations_scanned;
      continue;
    }
    const std::size_t available_segments = segment_count - (active_group_.next_sample - 1U);
    const std::size_t segment_batch =
        std::min(available_segments, kTileProducerSampleBatch - samples_consumed);
    const std::size_t first_sample =
        stored->samples.size() == 1U ? 0U : active_group_.next_sample - 1U;
    const std::size_t sample_count = stored->samples.size() == 1U ? 1U : segment_batch + 1U;
    if (!apply_incremental_operation(
            {.tool = stored->tool,
             .color = stored->color,
             .samples = stored->samples.subspan(first_sample, sample_count)},
            {.zoom = active_group_.view.zoom,
             .level_bounds = active_group_.bounds,
             .pixels = surface,
             .stride = kTileProducerWidth})) {
      active_group_ = {};
      return std::nullopt;
    }
    samples_consumed += segment_batch;
    ++result.operations_rendered;
    if (segment_batch == available_segments) {
      ++active_group_.next_operation;
      active_group_.next_sample = 1U;
      ++operations_consumed;
      ++result.operations_scanned;
    } else {
      active_group_.next_sample += segment_batch;
    }
  }
  if (active_group_.next_operation < active_group_.operation_count) {
    result.visible_tiles_remaining =
        visible_tiles_remaining(active_group_.view).value_or(active_group_.operation_count);
    return result;
  }
  const TileGrid grid = tile_grid(active_group_.view.zoom);
  const auto published =
      publish_group(active_group_.bounds, active_group_.origin, grid, active_group_.revision);
  const ViewRequest view = active_group_.view;
  active_group_ = {};
  if (!published.has_value()) {
    return std::nullopt;
  }
  result.tiles_published = *published;
  const auto remaining = visible_tiles_remaining(view);
  if (!remaining.has_value()) {
    return std::nullopt;
  }
  result.visible_tiles_remaining = *remaining;
  result.complete = *remaining == 0U;
  return result;
}

std::optional<TileProducer::ReplayRender> TileProducer::render_replay(
    ZoomLevel zoom, PixelRect bounds, std::span<std::uint16_t> surface, int stride) {
  if (log_.current_revision() != canvas_.current_revision()) {
    return std::nullopt;
  }
  const OperationLogEpoch epoch = log_.epoch();
  const DocumentRevision revision = log_.current_revision();
  const auto replay = log_.replay_range(epoch, baseline_revision_, revision);
  if (!replay.has_value()) {
    return std::nullopt;
  }
  std::fill(surface.begin(), surface.end(), baseline_color_);
  ReplayRender result{
      .epoch = epoch, .revision = revision, .operations_scanned = replay->operation_count};
  for (std::size_t offset = 0; offset < replay->operation_count; ++offset) {
    const auto stored = log_.operation(replay->first_operation + offset);
    if (!stored.has_value()) {
      return std::nullopt;
    }
    if (!intersects(operation_level_bounds(stored->world_bounds, zoom), bounds)) {
      continue;
    }
    if (!apply_incremental_operation(
            {.tool = stored->tool, .color = stored->color, .samples = stored->samples},
            {.zoom = zoom, .level_bounds = bounds, .pixels = surface, .stride = stride})) {
      return std::nullopt;
    }
    ++result.operations_rendered;
  }
  if (log_.epoch() != epoch || log_.current_revision() != revision ||
      canvas_.current_revision() != revision) {
    return std::nullopt;
  }
  return result;
}

std::optional<std::size_t> TileProducer::publish_group(PixelRect bounds, TileKey group_origin,
                                                       TileGrid grid, DocumentRevision revision) {
  const int first_column = group_origin.column;
  const int first_row = group_origin.row;
  const int last_column = std::min(grid.columns, first_column + kTileProducerColumns);
  const int last_row = std::min(grid.rows, first_row + kTileProducerRows);
  const auto surface = workspace_.supertask_pixels.first(kTileProducerPixels);
  std::size_t published = 0;
  for (int row = first_row; row < last_row; ++row) {
    for (int column = first_column; column < last_column; ++column) {
      const TileKey key{group_origin.zoom, static_cast<std::uint16_t>(column),
                        static_cast<std::uint16_t>(row)};
      if (tile_satisfies(key, MaterializationQuality::kImmediate)) {
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
        const auto destination_offset =
            static_cast<std::size_t>(tile_row) * static_cast<std::size_t>(tile_width);
        std::copy_n(surface.begin() + static_cast<std::ptrdiff_t>(source_offset), tile_width,
                    packed.begin() + static_cast<std::ptrdiff_t>(destination_offset));
      }
      if (!canvas_.publish_tile(key, revision, MaterializationQuality::kImmediate, packed)
               .has_value()) {
        return std::nullopt;
      }
      ++published;
    }
  }
  return published;
}

std::optional<TileProductionStep> TileProducer::render_2x_aa_tile(const ViewRequest& view,
                                                                  TileKey key) {
  const PixelRect output_bounds = tile_pixel_bounds(key);
  const int output_width = output_bounds.x1 - output_bounds.x0;
  const int output_height = output_bounds.y1 - output_bounds.y0;
  if (output_width != kTileWidth || output_height != kTileHeight ||
      log_.current_revision() != canvas_.current_revision()) {
    return std::nullopt;
  }
  const PixelRect sample_bounds{
      .x0 = output_bounds.x0 * 2,
      .y0 = output_bounds.y0 * 2,
      .x1 = output_bounds.x1 * 2,
      .y1 = output_bounds.y1 * 2,
  };
  auto surface = workspace_.supertask_pixels.first(kTileProducerPixels);
  const auto replay =
      render_replay(ZoomLevel::k200Percent, sample_bounds, surface, kTileProducerWidth);
  if (!replay.has_value()) {
    return std::nullopt;
  }
  TileProductionStep result{.level_bounds = output_bounds,
                            .operations_scanned = replay->operations_scanned,
                            .operations_rendered = replay->operations_rendered};
  auto packed = workspace_.packed_tile_pixels.first(kTilePixels);
  for (int y = 0; y < kTileHeight; ++y) {
    for (int x = 0; x < kTileWidth; ++x) {
      const std::size_t top_left =
          static_cast<std::size_t>(y * 2) * kTileProducerWidth + static_cast<std::size_t>(x * 2);
      const std::array<std::uint16_t, 4> samples{surface[top_left], surface[top_left + 1U],
                                                 surface[top_left + kTileProducerWidth],
                                                 surface[top_left + kTileProducerWidth + 1U]};
      unsigned red = 0;
      unsigned green = 0;
      unsigned blue = 0;
      for (const std::uint16_t sample : samples) {
        red += (sample >> 11U) & 0x1FU;
        green += (sample >> 5U) & 0x3FU;
        blue += sample & 0x1FU;
      }
      packed[static_cast<std::size_t>(y) * kTileWidth + static_cast<std::size_t>(x)] =
          static_cast<std::uint16_t>((((red + 2U) / 4U) << 11U) | (((green + 2U) / 4U) << 5U) |
                                     ((blue + 2U) / 4U));
    }
  }
  if (!canvas_.publish_tile(key, replay->revision, MaterializationQuality::kSettled, packed)
           .has_value()) {
    return std::nullopt;
  }
  result.tiles_published = 1U;
  const auto remaining = visible_tiles_remaining(view, MaterializationQuality::kSettled);
  if (!remaining.has_value()) {
    return std::nullopt;
  }
  result.visible_tiles_remaining = *remaining;
  result.complete = *remaining == 0U;
  return result;
}

}  // namespace tinydraw::production
