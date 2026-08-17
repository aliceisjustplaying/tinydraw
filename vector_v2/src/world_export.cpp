#include "tinydraw/vector_v2/world_export.h"

#include <algorithm>

#include "tinydraw/vector_v2/incremental_rasterizer.h"

namespace tinydraw::vector_v2 {

WorldBandRenderer::WorldBandRenderer(const OperationLog& log, std::span<std::uint16_t> band_pixels)
    : log_(log),
      band_pixels_(band_pixels),
      band_rows_(
          static_cast<int>(std::min(band_pixels.size() / static_cast<std::size_t>(kWorldWidth),
                                    static_cast<std::size_t>(kWorldHeight)))) {}

bool WorldBandRenderer::ready() const { return log_.ready() && band_rows_ > 0; }

int WorldBandRenderer::band_rows() const { return band_rows_; }

bool WorldBandRenderer::render_band(int first_row) {
  const int height = std::min(band_rows_, kWorldHeight - first_row);
  const PixelRect bounds{0, first_row, kWorldWidth, first_row + height};
  const auto pixels =
      band_pixels_.first(static_cast<std::size_t>(kWorldWidth) * static_cast<std::size_t>(height));
  std::fill(pixels.begin(), pixels.end(), 0xFFFFU);
  const RasterSurface surface{
      .zoom = ZoomLevel::k100Percent,
      .level_bounds = bounds,
      .pixels = pixels,
      .stride = kWorldWidth,
  };
  for (std::size_t index = 0; index < log_.operation_count(); ++index) {
    const auto stored = log_.operation(index);
    if (!stored.has_value()) {
      return false;
    }
    // World units are 100% level pixels; the conservative operation bounds
    // reject whole out-of-band operations before per-segment work.
    if (stored->world_bounds.y1 <= bounds.y0 || stored->world_bounds.y0 >= bounds.y1) {
      continue;
    }
    if (!apply_incremental_operation(
            {.tool = stored->tool, .color = stored->color, .samples = stored->samples}, surface)) {
      return false;
    }
  }
  band_first_ = first_row;
  band_height_ = height;
  band_valid_ = true;
  return true;
}

bool WorldBandRenderer::render_row(int y, std::span<std::uint16_t> destination) {
  if (!ready() || y < 0 || y >= kWorldHeight ||
      destination.size() < static_cast<std::size_t>(kWorldWidth)) {
    return false;
  }
  if (!band_valid_ || y < band_first_ || y >= band_first_ + band_height_) {
    if (!render_band(y)) {
      band_valid_ = false;
      return false;
    }
  }
  const auto source = band_pixels_.subspan(
      static_cast<std::size_t>(y - band_first_) * static_cast<std::size_t>(kWorldWidth),
      static_cast<std::size_t>(kWorldWidth));
  std::copy(source.begin(), source.end(), destination.begin());
  return true;
}

SettledWorldBandRenderer::SettledWorldBandRenderer(const OperationLog& log,
                                                   std::span<std::uint16_t> band_pixels,
                                                   std::span<std::uint16_t> window_pixels,
                                                   SettledTileWorkspace workspace,
                                                   WorldExportProgress progress,
                                                   void* progress_context)
    : log_(log),
      band_pixels_(band_pixels),
      window_pixels_(window_pixels),
      workspace_(workspace),
      progress_(progress),
      progress_context_(progress_context),
      epoch_(log.epoch()),
      revision_(log.current_revision()),
      operation_count_(log.operation_count()),
      band_rows_(static_cast<int>(std::min(
          {band_pixels.size() / static_cast<std::size_t>(kWorldWidth),
           static_cast<std::size_t>(kTileHeight), static_cast<std::size_t>(kWorldHeight)}))) {}

bool SettledWorldBandRenderer::authority_matches() const {
  return log_.epoch() == epoch_ && log_.current_revision() == revision_ &&
         log_.operation_count() == operation_count_;
}

bool SettledWorldBandRenderer::ready() const {
  return log_.ready() && authority_matches() && band_rows_ > 0 &&
         window_pixels_.size() >= kTilePixels && workspace_.operation_alpha.size() >= kTilePixels &&
         workspace_.accumulated_alpha.size() >= kTilePixels &&
         workspace_.red.size() >= kTilePixels && workspace_.green.size() >= kTilePixels &&
         workspace_.blue.size() >= kTilePixels;
}

int SettledWorldBandRenderer::band_rows() const { return band_rows_; }

bool SettledWorldBandRenderer::render_band(int first_row) {
  if (!authority_matches()) {
    return false;
  }
  const int height = std::min(band_rows_, kWorldHeight - first_row);
  for (int first_column = 0; first_column < kWorldWidth; first_column += kTileWidth) {
    const int width = std::min(kTileWidth, kWorldWidth - first_column);
    const std::size_t window_size =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const auto window = window_pixels_.first(window_size);
    if (!render_settled_window(log_, ZoomLevel::k100Percent,
                               {first_column, first_row, first_column + width, first_row + height},
                               workspace_, window)) {
      return false;
    }
    for (int row = 0; row < height; ++row) {
      const auto source =
          window.subspan(static_cast<std::size_t>(row) * static_cast<std::size_t>(width),
                         static_cast<std::size_t>(width));
      auto destination = band_pixels_.subspan(
          static_cast<std::size_t>(row) * static_cast<std::size_t>(kWorldWidth) +
              static_cast<std::size_t>(first_column),
          static_cast<std::size_t>(width));
      std::copy(source.begin(), source.end(), destination.begin());
    }
    if (progress_ != nullptr) {
      progress_(progress_context_);
    }
  }
  if (!authority_matches()) {
    return false;
  }
  band_first_ = first_row;
  band_height_ = height;
  band_valid_ = true;
  return true;
}

bool SettledWorldBandRenderer::render_row(int y, std::span<std::uint16_t> destination) {
  if (!ready() || y < 0 || y >= kWorldHeight ||
      destination.size() < static_cast<std::size_t>(kWorldWidth)) {
    return false;
  }
  if (!band_valid_ || y < band_first_ || y >= band_first_ + band_height_) {
    if (!render_band(y)) {
      band_valid_ = false;
      return false;
    }
  }
  const auto source = band_pixels_.subspan(
      static_cast<std::size_t>(y - band_first_) * static_cast<std::size_t>(kWorldWidth),
      static_cast<std::size_t>(kWorldWidth));
  std::copy(source.begin(), source.end(), destination.begin());
  return authority_matches();
}

}  // namespace tinydraw::vector_v2
