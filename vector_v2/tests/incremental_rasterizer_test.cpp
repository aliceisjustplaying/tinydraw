#include "tinydraw/vector_v2/incremental_rasterizer.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include "tinydraw/graphics/coverage_tile.h"
#include "tinydraw/ink/ribbon_geometry.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

bool reference_covers(vector_v2::CompactOperationSample first,
                      vector_v2::CompactOperationSample second, float pixel_x, float pixel_y) {
  constexpr float scale = 4.0F;
  constexpr float minimum_radius = 0.75F;
  const float first_x = static_cast<float>(first.x_quarter) * 0.0625F * scale;
  const float first_y = static_cast<float>(first.y_quarter) * 0.0625F * scale;
  const float second_x = static_cast<float>(second.x_quarter) * 0.0625F * scale;
  const float second_y = static_cast<float>(second.y_quarter) * 0.0625F * scale;
  const float first_radius =
      std::max(static_cast<float>(first.radius_256) / 256.0F * scale, minimum_radius);
  const float second_radius =
      std::max(static_cast<float>(second.radius_256) / 256.0F * scale, minimum_radius);
  const float delta_x = second_x - first_x;
  const float delta_y = second_y - first_y;
  const float length_squared = delta_x * delta_x + delta_y * delta_y;
  const float projection =
      length_squared > 0.0F
          ? ((pixel_x - first_x) * delta_x + (pixel_y - first_y) * delta_y) / length_squared
          : 0.0F;
  const float amount = std::clamp(projection, 0.0F, 1.0F);
  const float center_x = first_x + amount * delta_x;
  const float center_y = first_y + amount * delta_y;
  const float radius = first_radius + amount * (second_radius - first_radius);
  const float distance_x = pixel_x - center_x;
  const float distance_y = pixel_y - center_y;
  return distance_x * distance_x + distance_y * distance_y <= radius * radius;
}

}  // namespace

TEST_CASE("incremental operation paints one stroke without clearing prior pixels") {
  std::array<std::uint16_t, 32U * 32U> pixels{};
  pixels.fill(0x1111U);
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 160, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 320, .y_quarter = 160, .radius_256 = 512},
  };
  const vector_v2::OperationAppend operation{
      .tool = vector_v2::OperationTool::kPen,
      .color = 0xF800U,
      .samples = samples,
  };
  REQUIRE(
      vector_v2::apply_incremental_operation(operation, {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                         .level_bounds = {0, 0, 32, 32},
                                                         .pixels = pixels,
                                                         .stride = 32}));
  CHECK(pixels[10U * 32U + 10U] == 0xF800U);
  CHECK(pixels[10U * 32U + 20U] == 0xF800U);
  CHECK(pixels.front() == 0x1111U);
}

TEST_CASE("committed sparse ink follows the curved midpoint path") {
  std::array<std::uint16_t, 64U * 48U> pixels{};
  pixels.fill(0xFFFFU);
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 480, .radius_256 = 384},
      vector_v2::CompactOperationSample{.x_quarter = 480, .y_quarter = 160, .radius_256 = 384},
      vector_v2::CompactOperationSample{.x_quarter = 800, .y_quarter = 480, .radius_256 = 384},
  };
  const vector_v2::OperationAppend operation{
      .tool = vector_v2::OperationTool::kPen,
      .color = 0x001FU,
      .samples = samples,
  };

  tinydraw::CoverageTile curved(0, 0);
  tinydraw::CurvedRibbonStream ribbon;
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto sample = samples[index];
    const tinydraw::InkPoint point{
        .position = {.x = static_cast<float>(sample.x_quarter) * 0.0625F,
                     .y = static_cast<float>(sample.y_quarter) * 0.0625F},
        .pressure = 0.0F,
        .radius = static_cast<float>(sample.radius_256) / 256.0F,
        .distance = 0.0F,
        .running_length = 0.0F,
        .timestamp_us = 0U,
    };
    const auto update =
        index + 1U == samples.size() ? ribbon.finish(point) : ribbon.append(point, false);
    for (const auto& primitive : update.committed) {
      if (primitive.kind == tinydraw::RibbonPrimitiveKind::kCircle) {
        curved.rasterize_circle(primitive.center, primitive.radius);
      } else {
        curved.rasterize_convex(std::span(primitive.points.data(), primitive.point_count));
      }
    }
  }
  REQUIRE(curved.coverage_at(27, 17) != 0U);

  REQUIRE(
      vector_v2::apply_incremental_operation(operation, {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                         .level_bounds = {0, 0, 64, 48},
                                                         .pixels = pixels,
                                                         .stride = 64}));

  // The production CurvedRibbonStream midpoint quadratic passes through the
  // centre of this pixel. Straight source-segment replay leaves it as paper,
  // producing the visible polygonal corner on sparse circles.
  CHECK(pixels[17U * 64U + 27U] == 0x001FU);
}

TEST_CASE("eraser applies after pen in painter order") {
  std::array<std::uint16_t, 24U * 24U> pixels{};
  pixels.fill(0xFFFFU);
  const std::array pen_samples{
      vector_v2::CompactOperationSample{.x_quarter = 64, .y_quarter = 160, .radius_256 = 768},
      vector_v2::CompactOperationSample{.x_quarter = 320, .y_quarter = 160, .radius_256 = 768},
  };
  const vector_v2::OperationAppend pen{
      .tool = vector_v2::OperationTool::kPen, .color = 0x001FU, .samples = pen_samples};
  REQUIRE(vector_v2::apply_incremental_operation(pen, {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                       .level_bounds = {0, 0, 24, 24},
                                                       .pixels = pixels,
                                                       .stride = 24}));
  CHECK(pixels[10U * 24U + 10U] == 0x001FU);

  const std::array eraser_samples{
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 64, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 256, .radius_256 = 512},
  };
  const vector_v2::OperationAppend eraser{.tool = vector_v2::OperationTool::kEraser,
                                          .samples = eraser_samples};
  REQUIRE(vector_v2::apply_incremental_operation(eraser, {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                          .level_bounds = {0, 0, 24, 24},
                                                          .pixels = pixels,
                                                          .stride = 24}));
  CHECK(pixels[10U * 24U + 10U] == 0xFFFFU);
  CHECK(pixels[10U * 24U + 18U] == 0x001FU);
}

TEST_CASE("overview and tile surfaces map the same world operation") {
  std::array<std::uint16_t, vector_v2::kOverviewPixels> overview{};
  overview.fill(0xFFFFU);
  std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
  tile.fill(0xFFFFU);
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 1088, .y_quarter = 64, .radius_256 = 1024},
  };
  const vector_v2::OperationAppend operation{.color = 0x07E0U, .samples = samples};
  REQUIRE(vector_v2::apply_incremental_operation(
      operation, {.zoom = vector_v2::ZoomLevel::k25Percent,
                  .level_bounds = {0, 0, vector_v2::kOverviewWidth, vector_v2::kOverviewHeight},
                  .pixels = overview,
                  .stride = vector_v2::kOverviewWidth}));
  REQUIRE(
      vector_v2::apply_incremental_operation(operation, {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                         .level_bounds = {64, 0, 128, 64},
                                                         .pixels = tile,
                                                         .stride = vector_v2::kTileWidth}));
  CHECK(overview[1U * vector_v2::kOverviewWidth + 17U] == 0x07E0U);
  CHECK(tile[4U * vector_v2::kTileWidth + 4U] == 0x07E0U);
}

TEST_CASE("shared operation bounds conservatively include radius and clip to the world") {
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 0, .y_quarter = 0, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = vector_v2::kWorldWidth * 16,
                                        .y_quarter = vector_v2::kWorldHeight * 16,
                                        .radius_256 = 512},
  };
  CHECK(vector_v2::operation_world_bounds(samples) ==
        vector_v2::PixelRect{0, 0, vector_v2::kWorldWidth, vector_v2::kWorldHeight});
  CHECK_FALSE(vector_v2::operation_world_bounds({}));
}

TEST_CASE("constant and tapered segments match a per-pixel oracle across clip boundaries") {
  constexpr int width = 128;
  constexpr int height = 128;
  constexpr std::size_t pixel_count = static_cast<std::size_t>(width * height);
  std::uint32_t state = 0xC0FFEEU;
  auto next = [&]() {
    state = state * 1'664'525U + 1'013'904'223U;
    return state;
  };

  for (int case_index = 0; case_index < 256; ++case_index) {
    vector_v2::CompactOperationSample first{
        .x_quarter = static_cast<std::uint16_t>((8U + next() % 112U) * 4U),
        .y_quarter = static_cast<std::uint16_t>((8U + next() % 112U) * 4U),
        .radius_256 = static_cast<std::uint16_t>(64U + next() % 4'096U),
    };
    vector_v2::CompactOperationSample second{
        .x_quarter = static_cast<std::uint16_t>((8U + next() % 112U) * 4U),
        .y_quarter = static_cast<std::uint16_t>((8U + next() % 112U) * 4U),
        .radius_256 = static_cast<std::uint16_t>(64U + next() % 4'096U),
    };
    if (case_index % 2 == 0) {
      second.radius_256 = first.radius_256;
    } else if (first.radius_256 == second.radius_256) {
      ++second.radius_256;
    }
    const std::array samples{first, second};
    const vector_v2::OperationAppend operation{.color = 0x001FU, .samples = samples};
    std::vector<std::uint16_t> rendered(pixel_count, 0xFFFFU);
    std::vector<std::uint16_t> split(pixel_count, 0xFFFFU);
    std::vector<std::uint16_t> reference(pixel_count, 0xFFFFU);

    REQUIRE(vector_v2::apply_incremental_operation(operation,
                                                   {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                    .level_bounds = {0, 0, width, height},
                                                    .pixels = rendered,
                                                    .stride = width}));
    REQUIRE(vector_v2::apply_incremental_operation(operation,
                                                   {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                    .level_bounds = {0, 0, width / 2, height},
                                                    .pixels = split,
                                                    .stride = width}));
    REQUIRE(vector_v2::apply_incremental_operation(operation,
                                                   {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                    .level_bounds = {width / 2, 0, width, height},
                                                    .pixels = std::span(split).subspan(width / 2),
                                                    .stride = width}));

    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if (reference_covers(first, second, static_cast<float>(x) + 0.5F,
                             static_cast<float>(y) + 0.5F)) {
          reference[static_cast<std::size_t>(y * width + x)] = operation.color;
        }
      }
    }
    // Bit-exact equality is intentional under the pinned toolchain: it locks
    // both the constant-radius span path and tapered fallback to one predicate.
    CHECK(rendered == reference);
    CHECK(split == reference);
  }
}

TEST_CASE("constructed two-span taper stays on the exact raster path") {
  constexpr int width = 128;
  constexpr int height = 128;
  constexpr std::size_t pixel_count = static_cast<std::size_t>(width * height);
  constexpr auto first =
      vector_v2::CompactOperationSample{.x_quarter = 192, .y_quarter = 192, .radius_256 = 4'096};
  constexpr auto second =
      vector_v2::CompactOperationSample{.x_quarter = 200, .y_quarter = 200, .radius_256 = 1'472};
  const std::array samples{first, second};
  const vector_v2::OperationAppend operation{.color = 0x001FU, .samples = samples};
  std::vector<std::uint16_t> rendered(pixel_count, 0xFFFFU);
  std::vector<std::uint16_t> reference(pixel_count, 0xFFFFU);

  REQUIRE(
      vector_v2::apply_incremental_operation(operation, {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                         .level_bounds = {0, 0, width, height},
                                                         .pixels = rendered,
                                                         .stride = width}));
  std::size_t maximum_spans = 0;
  for (int y = 0; y < height; ++y) {
    std::size_t spans = 0;
    bool prior_covered = false;
    for (int x = 0; x < width; ++x) {
      const bool covered = reference_covers(first, second, static_cast<float>(x) + 0.5F,
                                            static_cast<float>(y) + 0.5F);
      spans += covered && !prior_covered;
      prior_covered = covered;
      if (covered) {
        reference[static_cast<std::size_t>(y * width + x)] = operation.color;
      }
    }
    maximum_spans = std::max(maximum_spans, spans);
  }
  CHECK(maximum_spans >= 2U);
  CHECK(rendered == reference);
}

TEST_CASE("masked operation never repaints finalized pixels and finalizes covered ones") {
  constexpr int width = 32;
  constexpr int height = 32;
  constexpr auto first =
      vector_v2::CompactOperationSample{.x_quarter = 48, .y_quarter = 64, .radius_256 = 1'024};
  constexpr auto second =
      vector_v2::CompactOperationSample{.x_quarter = 400, .y_quarter = 384, .radius_256 = 2'048};
  const std::array samples{first, second};
  const vector_v2::OperationAppend operation{.color = 0x001FU, .samples = samples};
  const vector_v2::RasterSurface make_surface = {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                 .level_bounds = {0, 0, width, height},
                                                 .pixels = {},
                                                 .stride = width};

  std::vector<std::uint16_t> reference(static_cast<std::size_t>(width * height), 0xFFFFU);
  std::vector<std::uint8_t> reference_mask((reference.size() + 7U) / 8U, 0U);
  auto reference_surface = make_surface;
  reference_surface.pixels = reference;
  REQUIRE(
      vector_v2::apply_masked_incremental_operation(operation, reference_surface, reference_mask));

  // Pre-finalize a band; those pixels must keep their sentinel color while
  // every unfinalized pixel matches the empty-mask reference.
  constexpr std::uint16_t kSentinel = 0x1234U;
  std::vector<std::uint16_t> pixels(reference.size(), 0xFFFFU);
  std::vector<std::uint8_t> mask(reference_mask.size(), 0U);
  for (int y = 8; y < 14; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t pixel = static_cast<std::size_t>(y * width + x);
      pixels[pixel] = kSentinel;
      mask[pixel >> 3U] = static_cast<std::uint8_t>(mask[pixel >> 3U] | (1U << (pixel & 7U)));
    }
  }
  auto surface = make_surface;
  surface.pixels = pixels;
  REQUIRE(vector_v2::apply_masked_incremental_operation(operation, surface, mask));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t pixel = static_cast<std::size_t>(y * width + x);
      if (y >= 8 && y < 14) {
        CHECK(pixels[pixel] == kSentinel);
      } else {
        CHECK(pixels[pixel] == reference[pixel]);
        const bool finalized = (mask[pixel >> 3U] & (1U << (pixel & 7U))) != 0U;
        CHECK(finalized == (reference[pixel] == operation.color));
      }
    }
  }
}

TEST_CASE("masked painter rejects a mask that aliases its pixel surface") {
  std::array<std::uint16_t, 16U> pixels{};
  const vector_v2::RasterSurface surface{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_bounds = {0, 0, 4, 4},
      .pixels = pixels,
      .stride = 4,
  };
  auto* bytes = reinterpret_cast<std::uint8_t*>(pixels.data());
  const std::span<std::uint8_t> aliased_mask(bytes, (pixels.size() + 7U) / 8U);

  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 0, .y_quarter = 0, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 48, .y_quarter = 48, .radius_256 = 256},
  };
  CHECK_FALSE(vector_v2::apply_masked_incremental_operation({.color = 0x001FU, .samples = samples},
                                                            surface, aliased_mask));
}

TEST_CASE("masked painter honors edge bounds narrower than its stride") {
  constexpr int width = 48;
  constexpr int height = 32;
  constexpr int stride = 128;
  constexpr auto first =
      vector_v2::CompactOperationSample{.x_quarter = 600, .y_quarter = 120, .radius_256 = 2'560};
  constexpr auto second =
      vector_v2::CompactOperationSample{.x_quarter = 840, .y_quarter = 440, .radius_256 = 1'024};
  const std::array samples{first, second};
  const vector_v2::OperationAppend operation{.color = 0x07E0U, .samples = samples};

  constexpr std::uint16_t kPad = 0xDEADU;
  const auto at = [](int y, int x) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
           static_cast<std::size_t>(x);
  };
  const std::size_t footprint = at(height - 1, width);
  std::vector<std::uint16_t> masked(
      static_cast<std::size_t>(height) * static_cast<std::size_t>(stride), kPad);
  std::vector<std::uint16_t> direct(masked.size(), kPad);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      masked[at(y, x)] = 0xFFFFU;
      direct[at(y, x)] = 0xFFFFU;
    }
  }
  std::vector<std::uint8_t> mask((masked.size() + 7U) / 8U, 0U);
  REQUIRE(
      vector_v2::apply_masked_incremental_operation(operation,
                                                    {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                     .level_bounds = {0, 0, width, height},
                                                     .pixels = std::span(masked).first(footprint),
                                                     .stride = stride},
                                                    mask));
  REQUIRE(vector_v2::apply_incremental_operation(operation,
                                                 {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                  .level_bounds = {0, 0, width, height},
                                                  .pixels = std::span(direct).first(footprint),
                                                  .stride = stride}));
  CHECK(masked == direct);
  for (int y = 0; y < height; ++y) {
    for (int x = width; x < stride; ++x) {
      if (at(y, x) < footprint) {
        CHECK(masked[at(y, x)] == kPad);
      }
    }
  }
}

TEST_CASE("constructed two-span taper is identical through the masked path") {
  constexpr int width = 128;
  constexpr int height = 128;
  constexpr auto first =
      vector_v2::CompactOperationSample{.x_quarter = 192, .y_quarter = 192, .radius_256 = 4'096};
  constexpr auto second =
      vector_v2::CompactOperationSample{.x_quarter = 200, .y_quarter = 200, .radius_256 = 1'472};
  std::vector<std::uint16_t> masked(static_cast<std::size_t>(width * height), 0xFFFFU);
  std::vector<std::uint16_t> direct(masked.size(), 0xFFFFU);
  std::vector<std::uint8_t> mask((masked.size() + 7U) / 8U, 0U);
  const std::array samples{first, second};
  const vector_v2::OperationAppend operation{.color = 0x001FU, .samples = samples};
  REQUIRE(vector_v2::apply_masked_incremental_operation(operation,
                                                        {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                         .level_bounds = {0, 0, width, height},
                                                         .pixels = masked,
                                                         .stride = width},
                                                        mask));
  REQUIRE(
      vector_v2::apply_incremental_operation(operation, {.zoom = vector_v2::ZoomLevel::k400Percent,
                                                         .level_bounds = {0, 0, width, height},
                                                         .pixels = direct,
                                                         .stride = width}));
  CHECK(masked == direct);
}

TEST_CASE("thin stroke bounds include the coarsest tiled paint halo") {
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 1020, .y_quarter = 1024, .radius_256 = 1},
  };
  CHECK(vector_v2::operation_world_bounds(samples) == vector_v2::PixelRect{62, 62, 66, 66});
}

TEST_CASE("operation level bounds preserve exact legacy scaling at every world coordinate") {
  constexpr std::array zooms{
      vector_v2::ZoomLevel::k25Percent,  vector_v2::ZoomLevel::k50Percent,
      vector_v2::ZoomLevel::k100Percent, vector_v2::ZoomLevel::k200Percent,
      vector_v2::ZoomLevel::k400Percent,
  };
  const auto legacy_bounds = [](vector_v2::PixelRect bounds, vector_v2::ZoomLevel zoom) {
    const int percent = vector_v2::zoom_percent(zoom);
    return vector_v2::PixelRect{
        .x0 = bounds.x0 * percent / 100,
        .y0 = bounds.y0 * percent / 100,
        .x1 = std::min(vector_v2::kWorldWidth * percent / 100, (bounds.x1 * percent + 99) / 100),
        .y1 = std::min(vector_v2::kWorldHeight * percent / 100, (bounds.y1 * percent + 99) / 100),
    };
  };

  for (const vector_v2::ZoomLevel zoom : zooms) {
    for (int coordinate = 0; coordinate <= vector_v2::kWorldWidth; ++coordinate) {
      const vector_v2::PixelRect bounds{coordinate, 0, coordinate, vector_v2::kWorldHeight};
      CHECK(vector_v2::operation_level_bounds(bounds, zoom) == legacy_bounds(bounds, zoom));
    }
    for (int coordinate = 0; coordinate <= vector_v2::kWorldHeight; ++coordinate) {
      const vector_v2::PixelRect bounds{0, coordinate, vector_v2::kWorldWidth, coordinate};
      CHECK(vector_v2::operation_level_bounds(bounds, zoom) == legacy_bounds(bounds, zoom));
    }
  }

  CHECK(vector_v2::operation_level_bounds({0, 0, vector_v2::kWorldWidth, vector_v2::kWorldHeight},
                                          static_cast<vector_v2::ZoomLevel>(0xFFU)) ==
        vector_v2::PixelRect{});
}

TEST_CASE("all committed zooms paint the same world center") {
  constexpr std::array zooms{
      vector_v2::ZoomLevel::k25Percent,  vector_v2::ZoomLevel::k50Percent,
      vector_v2::ZoomLevel::k100Percent, vector_v2::ZoomLevel::k200Percent,
      vector_v2::ZoomLevel::k400Percent,
  };
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 1600, .y_quarter = 1920, .radius_256 = 256},
  };
  const vector_v2::OperationAppend operation{.color = 0xF800U, .samples = samples};
  for (const vector_v2::ZoomLevel zoom : zooms) {
    std::array<std::uint16_t, vector_v2::kTilePixels> tile{};
    tile.fill(0xFFFFU);
    const int percent = vector_v2::zoom_percent(zoom);
    const int center_x = 100 * percent / 100;
    const int center_y = 120 * percent / 100;
    const int tile_x = center_x / vector_v2::kTileWidth * vector_v2::kTileWidth;
    const int tile_y = center_y / vector_v2::kTileHeight * vector_v2::kTileHeight;
    REQUIRE(vector_v2::apply_incremental_operation(
        operation, {.zoom = zoom,
                    .level_bounds = {tile_x, tile_y, tile_x + vector_v2::kTileWidth,
                                     tile_y + vector_v2::kTileHeight},
                    .pixels = tile,
                    .stride = vector_v2::kTileWidth}));
    const std::size_t local_x = static_cast<std::size_t>(center_x - tile_x);
    const std::size_t local_y = static_cast<std::size_t>(center_y - tile_y);
    CHECK(tile[local_y * vector_v2::kTileWidth + local_x] == 0xF800U);
  }
}

TEST_CASE("raster surface honors a stride larger than its visible width") {
  std::array<std::uint16_t, 4U * 3U> pixels{};
  pixels.fill(0x1111U);
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  REQUIRE(vector_v2::apply_incremental_operation({.color = 0xF800U, .samples = samples},
                                                 {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                  .level_bounds = {0, 0, 2, 3},
                                                  .pixels = pixels,
                                                  .stride = 4}));
  CHECK(pixels[1U * 4U + 1U] == 0xF800U);
  CHECK(pixels[2] == 0x1111U);
  CHECK(pixels[3] == 0x1111U);
}

TEST_CASE("invalid surface and empty operation fail without changing pixels") {
  std::array<std::uint16_t, 4> pixels{};
  pixels.fill(0x1234U);
  CHECK_FALSE(vector_v2::apply_incremental_operation({}, {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                          .level_bounds = {0, 0, 2, 2},
                                                          .pixels = pixels,
                                                          .stride = 2}));
  CHECK_FALSE(vector_v2::apply_incremental_operation(
      {.samples = std::array{vector_v2::CompactOperationSample{}}},
      {.zoom = vector_v2::ZoomLevel::k100Percent,
       .level_bounds = {-1, 0, 1, 2},
       .pixels = pixels,
       .stride = 2}));
  CHECK(pixels == std::array<std::uint16_t, 4>{0x1234U, 0x1234U, 0x1234U, 0x1234U});
}

TEST_CASE("masked row summary tracks exact per-row saturation") {
  std::array<std::uint16_t, 8> counts{};
  std::array<std::uint32_t, 1> words{};
  vector_v2::MaskedRowSummary summary(counts, words);
  CHECK_FALSE(summary.ready(1));
  summary.reset(6, 4);
  CHECK(summary.ready(6));
  CHECK_FALSE(summary.ready(7));
  CHECK_FALSE(summary.all_saturated());
  CHECK_FALSE(summary.row_saturated(0));
  CHECK_FALSE(summary.rows_saturated(0, 5));

  summary.note_finalized(2, 3);
  CHECK_FALSE(summary.row_saturated(2));
  summary.note_finalized(2, 1);
  CHECK(summary.row_saturated(2));
  CHECK(summary.rows_saturated(2, 2));
  CHECK_FALSE(summary.rows_saturated(1, 2));
  CHECK_FALSE(summary.rows_saturated(2, 3));

  for (int row = 0; row < 6; ++row) {
    summary.note_finalized(row, 4);
  }
  CHECK(summary.all_saturated());
  CHECK(summary.rows_saturated(0, 5));
  // Out-of-range queries never report saturation.
  CHECK_FALSE(summary.rows_saturated(0, 6));
  CHECK_FALSE(summary.rows_saturated(-1, 2));

  // Reset rearms every row.
  summary.reset(6, 4);
  CHECK_FALSE(summary.rows_saturated(0, 0));
  CHECK_FALSE(summary.all_saturated());
}

TEST_CASE("masked row summary spans word boundaries exactly") {
  std::array<std::uint16_t, 70> counts{};
  std::array<std::uint32_t, 3> words{};
  vector_v2::MaskedRowSummary summary(counts, words);
  summary.reset(70, 1);
  for (int row = 10; row <= 40; ++row) {
    summary.note_finalized(row, 1);
  }
  CHECK(summary.rows_saturated(10, 40));
  CHECK(summary.rows_saturated(31, 33));
  CHECK_FALSE(summary.rows_saturated(9, 40));
  CHECK_FALSE(summary.rows_saturated(10, 41));
  CHECK_FALSE(summary.all_saturated());
  for (int row = 0; row < 70; ++row) {
    summary.note_finalized(row, 1);
  }
  CHECK(summary.all_saturated());
  CHECK(summary.rows_saturated(0, 69));
}

TEST_CASE("masked painter keeps a supplied row summary exact") {
  constexpr int kSize = 32;
  std::array<std::uint16_t, kSize * kSize> pixels{};
  pixels.fill(0xFFFFU);
  std::array<std::uint8_t, (kSize * kSize + 7) / 8> mask{};
  std::array<std::uint16_t, kSize> counts{};
  std::array<std::uint32_t, 1> words{};
  vector_v2::MaskedRowSummary summary(counts, words);
  summary.reset(kSize, kSize);
  const vector_v2::RasterSurface surface{.zoom = vector_v2::ZoomLevel::k100Percent,
                                         .level_bounds = {0, 0, kSize, kSize},
                                         .pixels = pixels,
                                         .stride = kSize};
  // A fat constant-radius segment across the middle saturates interior rows.
  const std::array pen_samples{
      vector_v2::CompactOperationSample{.x_quarter = 0, .y_quarter = 256, .radius_256 = 8 * 256},
      vector_v2::CompactOperationSample{.x_quarter = 512, .y_quarter = 256, .radius_256 = 8 * 256},
  };
  REQUIRE(vector_v2::apply_masked_incremental_operation({.color = 0x001FU, .samples = pen_samples},
                                                        surface, mask, &summary));
  // Verify the summary against the mask bit-for-bit.
  for (int row = 0; row < kSize; ++row) {
    bool full = true;
    for (int column = 0; column < kSize; ++column) {
      const std::size_t pixel =
          static_cast<std::size_t>(row) * kSize + static_cast<std::size_t>(column);
      full = full && ((mask[pixel >> 3U] >> (pixel & 7U)) & 1U) != 0U;
    }
    CHECK(summary.row_saturated(row) == full);
  }
  CHECK_FALSE(summary.all_saturated());
  // An eraser finalizes background pixels the same way; cover everything.
  const std::array eraser_samples{
      vector_v2::CompactOperationSample{.x_quarter = 256, .y_quarter = 256, .radius_256 = 64 * 256},
      vector_v2::CompactOperationSample{.x_quarter = 256, .y_quarter = 256, .radius_256 = 64 * 256},
  };
  REQUIRE(vector_v2::apply_masked_incremental_operation(
      {.tool = vector_v2::OperationTool::kEraser, .samples = eraser_samples}, surface, mask,
      &summary));
  CHECK(summary.all_saturated());
}

TEST_CASE("operation chord batch sweep matches the production operation painter bit for bit") {
  // The H7 union argument: all chords of one operation share a color, so an
  // op-level y-sorted sweep (including capacity batching and row-slice
  // resume) must write exactly the pixels and mask bits of the production
  // whole-operation painter.
  constexpr int kSize = 96;
  // A curved multi-sample operation with tapered and constant spans plus a
  // partially clipped tail, over a pre-finalized stripe to exercise windows.
  std::vector<vector_v2::CompactOperationSample> samples;
  for (int index = 0; index < 24; ++index) {
    samples.push_back({
        .x_quarter = static_cast<std::uint16_t>(40 + index * 14 + (index % 5) * 6),
        .y_quarter = static_cast<std::uint16_t>(60 + ((index * 37) % 190)),
        .radius_256 = static_cast<std::uint16_t>(256 + (index % 4) * 384),
    });
  }
  const auto run_reference = [&](std::span<std::uint8_t> mask, vector_v2::MaskedRowSummary& summary,
                                 const vector_v2::RasterSurface& surface) {
    REQUIRE(vector_v2::apply_masked_incremental_operation(
        {.tool = vector_v2::OperationTool::kPen, .color = 0xF800U, .samples = samples}, surface,
        mask, &summary));
  };
  const auto run_batches = [&](std::span<std::uint8_t> mask, vector_v2::MaskedRowSummary& summary,
                               const vector_v2::RasterSurface& surface) {
    std::vector<std::uint32_t> storage(vector_v2::kOperationChordStorageBytes / 4U);
    const auto bytes = std::as_writable_bytes(std::span(storage));
    std::size_t endpoint = samples.size() - 1U;
    while (true) {
      const auto batch = vector_v2::prepare_operation_chord_batch(
          samples, endpoint, vector_v2::ZoomLevel::k100Percent, surface.level_bounds, bytes);
      REQUIRE(batch.has_value());
      if (batch->chord_count != 0U) {
        // Deliberately tiny work slices so resume paths run.
        int row = batch->clipped_bounds.y0;
        while (row < batch->clipped_bounds.y1) {
          vector_v2::OperationSweepSlice slice{};
          REQUIRE(vector_v2::apply_masked_operation_chord_rows({
              .tool = vector_v2::OperationTool::kPen,
              .color = 0xF800U,
              .chord_storage = bytes,
              .batch = *batch,
              .first_row = row,
              .max_work_px = 25U,
              .surface = surface,
              .finalized_pixels = mask,
              .summary = &summary,
              .slice = slice,
          }));
          REQUIRE(slice.next_row > row);
          row = slice.next_row;
        }
      }
      if (batch->next_endpoint == 0U) {
        break;
      }
      endpoint = batch->next_endpoint;
    }
  };
  std::vector<std::uint16_t> reference_pixels(kSize * kSize, 0xFFFFU);
  std::vector<std::uint16_t> batch_pixels(kSize * kSize, 0xFFFFU);
  std::vector<std::uint8_t> reference_mask((kSize * kSize + 7) / 8, 0U);
  std::vector<std::uint8_t> batch_mask((kSize * kSize + 7) / 8, 0U);
  // Pre-finalize a horizontal stripe (a newer op already painted there).
  for (int y = 30; y < 34; ++y) {
    for (int x = 0; x < kSize; ++x) {
      const std::size_t pixel = static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x);
      reference_mask[pixel >> 3U] |= static_cast<std::uint8_t>(1U << (pixel & 7U));
      batch_mask[pixel >> 3U] |= static_cast<std::uint8_t>(1U << (pixel & 7U));
    }
  }
  std::vector<std::uint16_t> reference_rows(kSize);
  std::vector<std::uint32_t> reference_words((kSize + 31) / 32);
  std::vector<std::uint16_t> batch_rows(kSize);
  std::vector<std::uint32_t> batch_words((kSize + 31) / 32);
  vector_v2::MaskedRowSummary reference_summary{reference_rows, reference_words};
  vector_v2::MaskedRowSummary batch_summary{batch_rows, batch_words};
  reference_summary.reset(kSize, kSize);
  batch_summary.reset(kSize, kSize);
  const vector_v2::RasterSurface reference_surface{.zoom = vector_v2::ZoomLevel::k100Percent,
                                                   .level_bounds = {8, 8, 8 + kSize, 8 + kSize},
                                                   .pixels = reference_pixels,
                                                   .stride = kSize};
  const vector_v2::RasterSurface batch_surface{.zoom = vector_v2::ZoomLevel::k100Percent,
                                               .level_bounds = {8, 8, 8 + kSize, 8 + kSize},
                                               .pixels = batch_pixels,
                                               .stride = kSize};
  run_reference(reference_mask, reference_summary, reference_surface);
  run_batches(batch_mask, batch_summary, batch_surface);
  CHECK(reference_pixels == batch_pixels);
  CHECK(reference_mask == batch_mask);
  for (int row = 0; row < kSize; ++row) {
    CHECK(reference_summary.row_saturated(row) == batch_summary.row_saturated(row));
  }
}

TEST_CASE("unmasked operation replay yields inside a dense overview row") {
  constexpr int kWidth = vector_v2::kOverviewWidth;
  constexpr int kHeight = vector_v2::kOverviewHeight;
  constexpr std::size_t kWork = 256U;
  std::array<vector_v2::CompactOperationSample, 64> samples{};
  for (std::size_t index = 0; index < samples.size(); ++index) {
    samples[index] = {
        .x_quarter = static_cast<std::uint16_t>(index % 2U == 0U ? 1'024U : 14'336U),
        .y_quarter = 5'760U,
        .radius_256 = 5'120U,
    };
  }
  std::vector<std::uint16_t> reference(kWidth * kHeight, 0xFFFFU);
  std::vector<std::uint16_t> sliced(kWidth * kHeight, 0xFFFFU);
  const vector_v2::RasterSurface reference_surface{
      .zoom = vector_v2::ZoomLevel::k25Percent,
      .level_bounds = {0, 0, kWidth, kHeight},
      .pixels = reference,
      .stride = kWidth,
  };
  const vector_v2::RasterSurface sliced_surface{
      .zoom = vector_v2::ZoomLevel::k25Percent,
      .level_bounds = {0, 0, kWidth, kHeight},
      .pixels = sliced,
      .stride = kWidth,
  };
  REQUIRE(vector_v2::apply_incremental_operation({.color = 0x001FU, .samples = samples},
                                                 reference_surface));

  std::vector<std::uint32_t> storage(vector_v2::kOperationChordStorageBytes / 4U);
  const auto bytes = std::as_writable_bytes(std::span(storage));
  std::size_t endpoint = samples.size() - 1U;
  bool paused_inside_row = false;
  std::size_t slices = 0;
  while (true) {
    const auto batch = vector_v2::prepare_operation_chord_batch(
        samples, endpoint, vector_v2::ZoomLevel::k25Percent, sliced_surface.level_bounds, bytes);
    REQUIRE(batch.has_value());
    if (batch->chord_count != 0U) {
      vector_v2::OperationSweepCursor cursor{.next_row = batch->clipped_bounds.y0};
      while (cursor.next_row < batch->clipped_bounds.y1) {
        vector_v2::OperationSweepSlice slice{};
        REQUIRE(vector_v2::apply_operation_chord_slice({
            .tool = vector_v2::OperationTool::kPen,
            .color = 0x001FU,
            .chord_storage = bytes,
            .batch = *batch,
            .max_work_px = kWork,
            .surface = sliced_surface,
            .cursor = cursor,
            .slice = slice,
        }));
        CHECK(slice.work_px <= kWork + static_cast<std::size_t>(kWidth));
        paused_inside_row = paused_inside_row || cursor.next_chord != 0U;
        ++slices;
      }
    }
    if (batch->next_endpoint == 0U) {
      break;
    }
    endpoint = batch->next_endpoint;
  }
  CHECK(paused_inside_row);
  CHECK(slices > 100U);
  CHECK(sliced == reference);
}

TEST_CASE("operation chord sweep refreshes finalized windows between overlapping chords") {
  constexpr int kSize = 64;
  constexpr std::uint16_t kRadius = 20U * 256U;
  const std::array<vector_v2::CompactOperationSample, 8> samples{{
      {.x_quarter = 20U * 16U, .y_quarter = 30U * 16U, .radius_256 = kRadius},
      {.x_quarter = 44U * 16U, .y_quarter = 34U * 16U, .radius_256 = kRadius},
      {.x_quarter = 20U * 16U, .y_quarter = 32U * 16U, .radius_256 = kRadius},
      {.x_quarter = 44U * 16U, .y_quarter = 30U * 16U, .radius_256 = kRadius},
      {.x_quarter = 20U * 16U, .y_quarter = 34U * 16U, .radius_256 = kRadius},
      {.x_quarter = 44U * 16U, .y_quarter = 32U * 16U, .radius_256 = kRadius},
      {.x_quarter = 20U * 16U, .y_quarter = 30U * 16U, .radius_256 = kRadius},
      {.x_quarter = 44U * 16U, .y_quarter = 34U * 16U, .radius_256 = kRadius},
  }};
  std::vector<std::uint32_t> storage(vector_v2::kOperationChordStorageBytes / 4U);
  const auto bytes = std::as_writable_bytes(std::span(storage));
  const auto batch = vector_v2::prepare_operation_chord_batch(
      samples, samples.size() - 1U, vector_v2::ZoomLevel::k100Percent, {0, 0, kSize, kSize}, bytes);
  REQUIRE(batch.has_value());
  REQUIRE(batch->chord_count > 2U);

  std::array<std::uint16_t, kSize * kSize> pixels{};
  std::array<std::uint8_t, (kSize * kSize + 7U) / 8U> mask{};
  std::array<std::uint16_t, kSize> unset_rows{};
  std::array<std::uint32_t, (kSize + 31U) / 32U> saturated_rows{};
  vector_v2::MaskedRowSummary summary{unset_rows, saturated_rows};
  summary.reset(kSize, kSize);
  const vector_v2::RasterSurface surface{
      .zoom = vector_v2::ZoomLevel::k100Percent,
      .level_bounds = {0, 0, kSize, kSize},
      .pixels = pixels,
      .stride = kSize,
  };
  vector_v2::OperationSweepSlice slice{};
  REQUIRE(vector_v2::apply_masked_operation_chord_rows({
      .tool = vector_v2::OperationTool::kPen,
      .color = 0x001FU,
      .chord_storage = bytes,
      .batch = *batch,
      .first_row = batch->clipped_bounds.y0,
      .max_work_px = std::numeric_limits<std::size_t>::max(),
      .surface = surface,
      .finalized_pixels = mask,
      .summary = &summary,
      .slice = slice,
  }));
  CHECK(slice.next_row == batch->clipped_bounds.y1);
  CHECK(slice.work_px < 15'000U);
}
