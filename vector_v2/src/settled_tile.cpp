#include "tinydraw/vector_v2/settled_tile.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "tinydraw/vector_v2/incremental_rasterizer.h"

namespace tinydraw::vector_v2 {
namespace {

bool rects_intersect(PixelRect left, PixelRect right) {
  return left.x0 < right.x1 && right.x0 < left.x1 && left.y0 < right.y1 && right.y0 < left.y1;
}

struct Channels {
  std::uint16_t red = 0;
  std::uint16_t green = 0;
  std::uint16_t blue = 0;
};

Channels expand_565(std::uint16_t rgb565) {
  // Bit replication, matching the frozen blend model.
  const auto r5 = static_cast<std::uint16_t>((rgb565 >> 11U) & 0x1FU);
  const auto g6 = static_cast<std::uint16_t>((rgb565 >> 5U) & 0x3FU);
  const auto b5 = static_cast<std::uint16_t>(rgb565 & 0x1FU);
  return {static_cast<std::uint16_t>((r5 << 3U) | (r5 >> 2U)),
          static_cast<std::uint16_t>((g6 << 2U) | (g6 >> 4U)),
          static_cast<std::uint16_t>((b5 << 3U) | (b5 >> 2U))};
}

}  // namespace

bool render_settled_window(const OperationLog& log, ZoomLevel zoom, PixelRect window_bounds,
                           const SettledTileWorkspace& workspace,
                           std::span<std::uint16_t> out_pixels, SettledTileStats* stats) {
  const PixelRect tile_bounds = window_bounds;
  const int tile_width = tile_bounds.x1 - tile_bounds.x0;
  const int tile_height = tile_bounds.y1 - tile_bounds.y0;
  if (!log.ready() || tile_width <= 0 || tile_height <= 0 ||
      tile_width > static_cast<int>(kTileWidth) || tile_height > static_cast<int>(kTileHeight)) {
    return false;
  }
  const std::size_t pixel_count =
      static_cast<std::size_t>(tile_width) * static_cast<std::size_t>(tile_height);
  if (out_pixels.size() < pixel_count || workspace.operation_alpha.size() < pixel_count ||
      workspace.accumulated_alpha.size() < pixel_count || workspace.red.size() < pixel_count ||
      workspace.green.size() < pixel_count || workspace.blue.size() < pixel_count) {
    return false;
  }
  // Window level bounds to conservative world bounds for operation culling.
  const int percent = zoom_percent(zoom);
  const PixelRect tile_world{tile_bounds.x0 * 100 / percent - 1, tile_bounds.y0 * 100 / percent - 1,
                             (tile_bounds.x1 * 100 + percent - 1) / percent + 1,
                             (tile_bounds.y1 * 100 + percent - 1) / percent + 1};
  const ZoomLevel key_zoom = zoom;

  std::memset(workspace.accumulated_alpha.data(), 0, pixel_count);
  std::memset(workspace.red.data(), 0, pixel_count * sizeof(std::uint16_t));
  std::memset(workspace.green.data(), 0, pixel_count * sizeof(std::uint16_t));
  std::memset(workspace.blue.data(), 0, pixel_count * sizeof(std::uint16_t));

  std::size_t saturated_pixels = 0;
  for (std::size_t operation_index = log.operation_count(); operation_index-- > 0U;) {
    const auto stored = log.operation(operation_index);
    if (!stored.has_value()) {
      return false;
    }
    if (stats != nullptr) {
      ++stats->operations_scanned;
    }
    if (!rects_intersect(stored->world_bounds, tile_world)) {
      continue;
    }
    std::memset(workspace.operation_alpha.data(), 0, pixel_count);
    bool touched = false;
    const auto samples = stored->samples;
    for (std::size_t endpoint = 1; endpoint < samples.size(); ++endpoint) {
      const auto unit = prepare_incremental_curve_unit(samples, endpoint, key_zoom);
      if (!unit.has_value()) {
        continue;
      }
      for (std::size_t step = 0; step < unit->step_count; ++step) {
        const auto& chord = unit->steps[step];
        const float ax = chord.first_x - static_cast<float>(tile_bounds.x0);
        const float ay = chord.first_y - static_cast<float>(tile_bounds.y0);
        const float bx = chord.second_x - static_cast<float>(tile_bounds.x0);
        const float by = chord.second_y - static_cast<float>(tile_bounds.y0);
        const float radius_max = std::max(chord.first_radius, chord.second_radius);
        const int px0 =
            std::max(0, static_cast<int>(std::floor(std::min(ax, bx) - radius_max - 1.5F)));
        const int py0 =
            std::max(0, static_cast<int>(std::floor(std::min(ay, by) - radius_max - 1.5F)));
        const int px1 =
            std::min(tile_width, static_cast<int>(std::ceil(std::max(ax, bx) + radius_max + 1.5F)));
        const int py1 = std::min(tile_height,
                                 static_cast<int>(std::ceil(std::max(ay, by) + radius_max + 1.5F)));
        const float delta_x = bx - ax;
        const float delta_y = by - ay;
        const float length_squared = delta_x * delta_x + delta_y * delta_y;
        const float inverse_length_squared = length_squared > 0.0F ? 1.0F / length_squared : 0.0F;
        for (int y = py0; y < py1; ++y) {
          const float sample_y = static_cast<float>(y) + 0.5F;
          std::uint8_t* row = workspace.operation_alpha.data() +
                              static_cast<std::size_t>(y) * static_cast<std::size_t>(tile_width);
          for (int x = px0; x < px1; ++x) {
            const float sample_x = static_cast<float>(x) + 0.5F;
            const float ap_x = sample_x - ax;
            const float ap_y = sample_y - ay;
            const float t =
                std::clamp((ap_x * delta_x + ap_y * delta_y) * inverse_length_squared, 0.0F, 1.0F);
            const float dx = ap_x - t * delta_x;
            const float dy = ap_y - t * delta_y;
            const float distance_squared = dx * dx + dy * dy;
            const float radius =
                chord.first_radius + (chord.second_radius - chord.first_radius) * t;
            // Squared-distance classification keeps sqrt off the interior
            // and exterior; only the one-pixel boundary annulus pays it.
            const float interior = radius - 0.5F;
            const float exterior = radius + 0.5F;
            if (distance_squared >= exterior * exterior) {
              continue;
            }
            std::uint8_t alpha_255 = 255U;
            if (interior <= 0.0F || distance_squared > interior * interior) {
              const float distance = std::sqrt(distance_squared);
              const float alpha = std::clamp(0.5F + (radius - distance), 0.0F, 1.0F);
              if (alpha <= 0.0F) {
                continue;
              }
              alpha_255 = static_cast<std::uint8_t>(alpha * 255.0F + 0.5F);
            }
            if (alpha_255 > row[x]) {
              row[x] = alpha_255;
              touched = true;
            }
          }
        }
      }
    }
    if (!touched) {
      continue;
    }
    if (stats != nullptr) {
      ++stats->operations_rendered;
    }
    const Channels color =
        expand_565(stored->tool == OperationTool::kEraser ? std::uint16_t{0xFFFFU} : stored->color);
    for (std::size_t at = 0; at < pixel_count; ++at) {
      const std::uint8_t alpha = workspace.operation_alpha[at];
      if (alpha == 0U) {
        continue;
      }
      const std::uint8_t accumulated = workspace.accumulated_alpha[at];
      const auto contribution = static_cast<std::uint16_t>(
          (static_cast<std::uint32_t>(alpha) * (255U - accumulated) + 127U) / 255U);
      if (contribution == 0U) {
        continue;
      }
      workspace.red[at] =
          static_cast<std::uint16_t>(workspace.red[at] + color.red * contribution / 255U);
      workspace.green[at] =
          static_cast<std::uint16_t>(workspace.green[at] + color.green * contribution / 255U);
      workspace.blue[at] =
          static_cast<std::uint16_t>(workspace.blue[at] + color.blue * contribution / 255U);
      const auto next_accumulated =
          static_cast<std::uint8_t>(std::min<std::uint32_t>(255U, accumulated + contribution));
      saturated_pixels += next_accumulated == 255U && accumulated != 255U ? 1U : 0U;
      workspace.accumulated_alpha[at] = next_accumulated;
    }
    if (saturated_pixels == pixel_count) {
      if (stats != nullptr) {
        stats->saturated_early = true;
      }
      break;
    }
  }
  for (std::size_t at = 0; at < pixel_count; ++at) {
    const std::uint32_t remaining = 255U - workspace.accumulated_alpha[at];
    const std::uint32_t r8 =
        std::min<std::uint32_t>(255U, workspace.red[at] + 255U * remaining / 255U);
    const std::uint32_t g8 =
        std::min<std::uint32_t>(255U, workspace.green[at] + 255U * remaining / 255U);
    const std::uint32_t b8 =
        std::min<std::uint32_t>(255U, workspace.blue[at] + 255U * remaining / 255U);
    out_pixels[at] =
        static_cast<std::uint16_t>(((r8 >> 3U) << 11U) | ((g8 >> 2U) << 5U) | (b8 >> 3U));
  }
  return true;
}

bool render_settled_tile(const OperationLog& log, TileKey key,
                         const SettledTileWorkspace& workspace, std::span<std::uint16_t> out_pixels,
                         SettledTileStats* stats) {
  if (!valid_tile_key(key)) {
    return false;
  }
  return render_settled_window(log, key.zoom, tile_pixel_bounds(key), workspace, out_pixels, stats);
}

}  // namespace tinydraw::vector_v2
