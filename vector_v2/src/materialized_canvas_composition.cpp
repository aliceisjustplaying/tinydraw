#include <algorithm>

#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/pixel_memory.h"
#include "tinydraw/vector_v2/storage_overlap.h"

namespace tinydraw::vector_v2 {

bool MaterializedCanvas::has_complete_source(const ViewRequest& request) const {
  if (overview_valid_ && overview_revision_ == current_revision_) {
    return true;
  }
  const PixelRect rect = request.level_pixels;
  const int first_column = rect.x0 / kTileWidth;
  const int last_column = (rect.x1 - 1) / kTileWidth;
  const int first_row = rect.y0 / kTileHeight;
  const int last_row = (rect.y1 - 1) / kTileHeight;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      const TileKey key{request.zoom, static_cast<std::uint16_t>(column),
                        static_cast<std::uint16_t>(row)};
      const auto index = find_tile(key);
      const bool raw_current = index.has_value() && raw_slot_is_current(slots_[*index]);
      if (!raw_current && !find_uniform(key).has_value()) {
        return false;
      }
    }
  }
  return true;
}

void MaterializedCanvas::compose_overview_pixels(PixelRect bounds,
                                                 CompositionContext& context) const {
  const PixelRect view = context.request.level_pixels;
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const auto source_offset =
        static_cast<std::size_t>(y) * kOverviewWidth + static_cast<std::size_t>(bounds.x0);
    const auto destination_offset =
        static_cast<std::size_t>(y - view.y0) * static_cast<std::size_t>(context.view_width);
    std::copy_n(overview_pixels_.begin() + static_cast<std::ptrdiff_t>(source_offset),
                bounds.x1 - bounds.x0,
                context.destination.begin() + static_cast<std::ptrdiff_t>(destination_offset));
  }
  context.stats.overview_pixels += static_cast<std::size_t>(bounds.x1 - bounds.x0) *
                                   static_cast<std::size_t>(bounds.y1 - bounds.y0);
}

void MaterializedCanvas::include_quality(MaterializationQuality quality,
                                         ViewCompositionStats& stats) {
  stats.immediate_tiles += quality == MaterializationQuality::kImmediate;
  stats.settled_tiles += quality == MaterializationQuality::kSettled;
}

void MaterializedCanvas::compose_raw_pixels(std::size_t slot_index, PixelRect bounds,
                                            CompositionContext& context) {
  const PixelRect view = context.request.level_pixels;
  const std::size_t tile_base = slot_index * kTilePixels;
  const int local_x = bounds.x0 % kTileWidth;
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  const std::size_t source = tile_base +
                             static_cast<std::size_t>(bounds.y0 % kTileHeight) * kTileWidth +
                             static_cast<std::size_t>(local_x);
  const std::size_t destination =
      static_cast<std::size_t>(bounds.y0 - view.y0) * static_cast<std::size_t>(context.view_width) +
      static_cast<std::size_t>(bounds.x0 - view.x0);
  copy_pixel_rows_disjoint(tile_pixels_.data() + static_cast<std::ptrdiff_t>(source), kTileWidth,
                           context.destination.data() + static_cast<std::ptrdiff_t>(destination),
                           context.view_width, width, height);
  context.stats.tile_pixels += static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

void MaterializedCanvas::compose_uniform_pixels(std::uint16_t color, PixelRect bounds,
                                                CompositionContext& context) {
  const PixelRect view = context.request.level_pixels;
  const int width = bounds.x1 - bounds.x0;
  const int height = bounds.y1 - bounds.y0;
  const std::size_t offset =
      static_cast<std::size_t>(bounds.y0 - view.y0) * static_cast<std::size_t>(context.view_width) +
      static_cast<std::size_t>(bounds.x0 - view.x0);
  fill_pixel_rows_unchecked(context.destination.data() + static_cast<std::ptrdiff_t>(offset),
                            context.view_width, width, height, color);
  context.stats.uniform_pixels +=
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

void MaterializedCanvas::compose_fallback_pixels(PixelRect bounds, CompositionContext& context) {
  const PixelRect view = context.request.level_pixels;
  const unsigned overview_shift = static_cast<unsigned>(context.request.zoom);
  for (int y = bounds.y0; y < bounds.y1; ++y) {
    const std::size_t source_row = static_cast<std::size_t>(y >> overview_shift) * kOverviewWidth;
    for (int x = bounds.x0; x < bounds.x1;) {
      const int source_x = x >> overview_shift;
      const int run_end = std::min(bounds.x1, static_cast<int>((source_x + 1) << overview_shift));
      const int run_width = run_end - x;
      const std::size_t destination =
          static_cast<std::size_t>(y - view.y0) * static_cast<std::size_t>(context.view_width) +
          static_cast<std::size_t>(x - view.x0);
      std::fill_n(context.destination.begin() + static_cast<std::ptrdiff_t>(destination), run_width,
                  overview_pixels_[source_row + static_cast<std::size_t>(source_x)]);
      x = run_end;
    }
  }
  context.stats.fallback_pixels += static_cast<std::size_t>(bounds.x1 - bounds.x0) *
                                   static_cast<std::size_t>(bounds.y1 - bounds.y0);
}

void MaterializedCanvas::compose_tile(TileKey key, PixelRect band, CompositionContext& context) {
  const PixelRect tile = tile_pixel_bounds(key);
  const PixelRect view = context.request.level_pixels;
  const PixelRect bounds{.x0 = std::max(view.x0, tile.x0),
                         .y0 = std::max(band.y0, tile.y0),
                         .x1 = std::min(view.x1, tile.x1),
                         .y1 = std::min(band.y1, tile.y1)};
  const bool first_slice = bounds.y0 == std::max(view.y0, tile.y0);
  const auto raw = find_tile(key);
  const bool raw_current = raw.has_value() && raw_slot_is_current(slots_[*raw]);
  if (raw_current) {
    if (first_slice) {
      touch(slots_[*raw]);
      include_quality(slots_[*raw].quality_, context.stats);
    }
    compose_raw_pixels(*raw, bounds, context);
    return;
  }
  const auto uniform = find_uniform(key);
  if (uniform.has_value()) {
    if (first_slice) {
      include_quality(uniform_catalog_[*uniform].quality_, context.stats);
    }
    compose_uniform_pixels(uniform_catalog_[*uniform].color_, bounds, context);
    return;
  }
  context.stats.fallback_tiles += first_slice;
  compose_fallback_pixels(bounds, context);
}

std::optional<ViewCompositionStats> MaterializedCanvas::compose_view(
    const ViewRequest& request, std::span<std::uint16_t> destination) {
  const bool destination_aliases_source =
      storage_overlaps(std::as_bytes(destination), std::as_bytes(std::span(overview_pixels_))) ||
      storage_overlaps(std::as_bytes(destination), std::as_bytes(std::span(tile_pixels_))) ||
      storage_overlaps(std::as_bytes(destination), std::as_bytes(std::span(slots_)));
  if (!valid_view(request, destination.size()) || destination_aliases_source ||
      !has_complete_source(request)) {
    return std::nullopt;
  }
  CompositionContext context{
      .request = request,
      .destination = destination,
      .stats = {.revision = current_revision_},
      .view_width = request.level_pixels.x1 - request.level_pixels.x0,
  };
  const PixelRect rect = request.level_pixels;
  if (request.zoom == ZoomLevel::k25Percent) {
    compose_overview_pixels(rect, context);
  } else {
    const int first_column = rect.x0 / kTileWidth;
    const int last_column = (rect.x1 - 1) / kTileWidth;
    const int first_row = rect.y0 / kTileHeight;
    const int last_row = (rect.y1 - 1) / kTileHeight;
    for (int row = first_row; row <= last_row; ++row) {
      for (int column = first_column; column <= last_column; ++column) {
        compose_tile(
            {request.zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)},
            rect, context);
      }
    }
  }
  return context.stats;
}

ViewCompositionSliceResult MaterializedCanvas::compose_view_slice(
    const ViewRequest& request, std::span<std::uint16_t> destination,
    ViewCompositionCursor& cursor) {
  if (cursor.active_) {
    const bool same_transaction = cursor.canvas_ == this && cursor.request_ == request &&
                                  cursor.destination_ == destination.data() &&
                                  cursor.destination_size_ == destination.size() &&
                                  cursor.revision_ == current_revision_ &&
                                  cursor.canvas_epoch_ == composition_epoch_;
    if (!same_transaction) {
      cursor.cancel();
      return {};
    }
  }

  if (!cursor.active_) {
    const bool destination_aliases_source =
        storage_overlaps(std::as_bytes(destination), std::as_bytes(std::span(overview_pixels_))) ||
        storage_overlaps(std::as_bytes(destination), std::as_bytes(std::span(tile_pixels_))) ||
        storage_overlaps(std::as_bytes(destination), std::as_bytes(std::span(slots_)));
    if (!valid_view(request, destination.size()) || destination_aliases_source ||
        !has_complete_source(request)) {
      cursor.cancel();
      return {};
    }
    cursor.canvas_ = this;
    cursor.request_ = request;
    cursor.destination_ = destination.data();
    cursor.destination_size_ = destination.size();
    cursor.revision_ = current_revision_;
    cursor.canvas_epoch_ = composition_epoch_;
    cursor.stats_ = {.revision = current_revision_};
    cursor.next_y_ = request.level_pixels.y0;
    cursor.active_ = true;
  }

  CompositionContext context{
      .request = request,
      .destination = destination,
      .stats = cursor.stats_,
      .view_width = request.level_pixels.x1 - request.level_pixels.x0,
  };
  const PixelRect rect = request.level_pixels;
  const int band_y1 = std::min(cursor.next_y_ + kViewCompositionRowsPerSlice, rect.y1);
  const PixelRect band{rect.x0, cursor.next_y_, rect.x1, band_y1};
  if (request.zoom == ZoomLevel::k25Percent) {
    compose_overview_pixels(band, context);
  } else {
    const int first_column = rect.x0 / kTileWidth;
    const int last_column = (rect.x1 - 1) / kTileWidth;
    const int first_row = band.y0 / kTileHeight;
    const int last_row = (band.y1 - 1) / kTileHeight;
    for (int row = first_row; row <= last_row; ++row) {
      for (int column = first_column; column <= last_column; ++column) {
        compose_tile(
            {request.zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)},
            band, context);
      }
    }
  }

  cursor.stats_ = context.stats;
  cursor.next_y_ = band_y1;
  const bool complete = band_y1 == rect.y1;
  const ViewCompositionSliceResult result{
      .status = complete ? ViewCompositionStatus::kComplete : ViewCompositionStatus::kInProgress,
      .stats = context.stats,
  };
  if (complete) {
    cursor.cancel();
  }
  return result;
}

}  // namespace tinydraw::vector_v2
