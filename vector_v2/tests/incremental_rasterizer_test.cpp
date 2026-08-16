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
  const float first_x = static_cast<float>(first.x_quarter) * 0.25F * scale;
  const float first_y = static_cast<float>(first.y_quarter) * 0.25F * scale;
  const float second_x = static_cast<float>(second.x_quarter) * 0.25F * scale;
  const float second_y = static_cast<float>(second.y_quarter) * 0.25F * scale;
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
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 40, .radius_256 = 512},
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
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 120, .radius_256 = 384},
      vector_v2::CompactOperationSample{.x_quarter = 120, .y_quarter = 40, .radius_256 = 384},
      vector_v2::CompactOperationSample{.x_quarter = 200, .y_quarter = 120, .radius_256 = 384},
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
        .position = {.x = static_cast<float>(sample.x_quarter) * 0.25F,
                     .y = static_cast<float>(sample.y_quarter) * 0.25F},
        .radius = static_cast<float>(sample.radius_256) / 256.0F,
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
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 40, .radius_256 = 768},
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 40, .radius_256 = 768},
  };
  const vector_v2::OperationAppend pen{
      .tool = vector_v2::OperationTool::kPen, .color = 0x001FU, .samples = pen_samples};
  REQUIRE(vector_v2::apply_incremental_operation(pen, {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                       .level_bounds = {0, 0, 24, 24},
                                                       .pixels = pixels,
                                                       .stride = 24}));
  CHECK(pixels[10U * 24U + 10U] == 0x001FU);

  const std::array eraser_samples{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 16, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 64, .radius_256 = 512},
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
      vector_v2::CompactOperationSample{.x_quarter = 272, .y_quarter = 16, .radius_256 = 1024},
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
      vector_v2::CompactOperationSample{.x_quarter = vector_v2::kWorldWidth * 4,
                                        .y_quarter = vector_v2::kWorldHeight * 4,
                                        .radius_256 = 512},
  };
  CHECK(vector_v2::operation_world_bounds(samples) ==
        vector_v2::PixelRect{0, 0, vector_v2::kWorldWidth, vector_v2::kWorldHeight});
  CHECK_FALSE(vector_v2::operation_world_bounds({}));
}

TEST_CASE("a clipped source segment is one bounded raster unit") {
  constexpr vector_v2::PixelRect bounds{0, 0, 128, 128};
  constexpr auto first =
      vector_v2::CompactOperationSample{.x_quarter = 20, .y_quarter = 20, .radius_256 = 5'120};
  constexpr auto second =
      vector_v2::CompactOperationSample{.x_quarter = 420, .y_quarter = 360, .radius_256 = 3'328};
  std::array<std::uint16_t, 128U * 128U> complete{};
  std::array<std::uint16_t, 128U * 128U> sliced{};
  complete.fill(0xFFFFU);
  sliced.fill(0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(
      {.color = 0x001FU, .samples = std::array{first, second}},
      {.zoom = vector_v2::ZoomLevel::k400Percent,
       .level_bounds = bounds,
       .pixels = complete,
       .stride = 128}));

  const std::size_t steps =
      vector_v2::incremental_segment_step_count(first, second, vector_v2::ZoomLevel::k400Percent);
  REQUIRE(steps == 1U);
  for (std::size_t step = 0; step < steps; ++step) {
    CHECK(vector_v2::incremental_segment_step_work(first, second, vector_v2::ZoomLevel::k400Percent,
                                                   bounds, step) <= 128U * 128U);
    REQUIRE(vector_v2::apply_incremental_segment_steps(
        {.color = 0x001FU, .first = first, .second = second},
        {.zoom = vector_v2::ZoomLevel::k400Percent,
         .level_bounds = bounds,
         .pixels = sliced,
         .stride = 128},
        step, 1U));
  }
  CHECK(sliced == complete);
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
        .x_quarter = static_cast<std::uint16_t>(8U + next() % 112U),
        .y_quarter = static_cast<std::uint16_t>(8U + next() % 112U),
        .radius_256 = static_cast<std::uint16_t>(64U + next() % 4'096U),
    };
    vector_v2::CompactOperationSample second{
        .x_quarter = static_cast<std::uint16_t>(8U + next() % 112U),
        .y_quarter = static_cast<std::uint16_t>(8U + next() % 112U),
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
      vector_v2::CompactOperationSample{.x_quarter = 48, .y_quarter = 48, .radius_256 = 4'096};
  constexpr auto second =
      vector_v2::CompactOperationSample{.x_quarter = 50, .y_quarter = 50, .radius_256 = 1'472};
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

TEST_CASE("masked segment never repaints finalized pixels and finalizes covered ones") {
  constexpr int width = 32;
  constexpr int height = 32;
  constexpr auto first =
      vector_v2::CompactOperationSample{.x_quarter = 12, .y_quarter = 16, .radius_256 = 1'024};
  constexpr auto second =
      vector_v2::CompactOperationSample{.x_quarter = 100, .y_quarter = 96, .radius_256 = 2'048};
  const vector_v2::IncrementalSegment segment{.color = 0x001FU, .first = first, .second = second};
  const vector_v2::RasterSurface make_surface = {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                 .level_bounds = {0, 0, width, height},
                                                 .pixels = {},
                                                 .stride = width};

  std::vector<std::uint16_t> reference(static_cast<std::size_t>(width * height), 0xFFFFU);
  std::vector<std::uint8_t> reference_mask((reference.size() + 7U) / 8U, 0U);
  auto reference_surface = make_surface;
  reference_surface.pixels = reference;
  REQUIRE(vector_v2::apply_masked_incremental_segment(segment, reference_surface, reference_mask));

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
  REQUIRE(vector_v2::apply_masked_incremental_segment(segment, surface, mask));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const std::size_t pixel = static_cast<std::size_t>(y * width + x);
      if (y >= 8 && y < 14) {
        CHECK(pixels[pixel] == kSentinel);
      } else {
        CHECK(pixels[pixel] == reference[pixel]);
        const bool finalized = (mask[pixel >> 3U] & (1U << (pixel & 7U))) != 0U;
        CHECK(finalized == (reference[pixel] == segment.color));
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

  CHECK_FALSE(vector_v2::apply_masked_incremental_segment(
      {.color = 0x001FU,
       .first = {.x_quarter = 0, .y_quarter = 0, .radius_256 = 256},
       .second = {.x_quarter = 12, .y_quarter = 12, .radius_256 = 256}},
      surface, aliased_mask));
}

TEST_CASE("masked painter honors edge bounds narrower than its stride") {
  constexpr int width = 48;
  constexpr int height = 32;
  constexpr int stride = 128;
  constexpr auto first =
      vector_v2::CompactOperationSample{.x_quarter = 150, .y_quarter = 30, .radius_256 = 2'560};
  constexpr auto second =
      vector_v2::CompactOperationSample{.x_quarter = 210, .y_quarter = 110, .radius_256 = 1'024};
  const vector_v2::IncrementalSegment segment{.color = 0x07E0U, .first = first, .second = second};

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
  REQUIRE(vector_v2::apply_masked_incremental_segment(segment,
                                                      {.zoom = vector_v2::ZoomLevel::k100Percent,
                                                       .level_bounds = {0, 0, width, height},
                                                       .pixels = std::span(masked).first(footprint),
                                                       .stride = stride},
                                                      mask));
  REQUIRE(vector_v2::apply_incremental_operation(
      {.color = segment.color, .samples = std::array{first, second}},
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
      vector_v2::CompactOperationSample{.x_quarter = 48, .y_quarter = 48, .radius_256 = 4'096};
  constexpr auto second =
      vector_v2::CompactOperationSample{.x_quarter = 50, .y_quarter = 50, .radius_256 = 1'472};
  std::vector<std::uint16_t> masked(static_cast<std::size_t>(width * height), 0xFFFFU);
  std::vector<std::uint16_t> direct(masked.size(), 0xFFFFU);
  std::vector<std::uint8_t> mask((masked.size() + 7U) / 8U, 0U);
  REQUIRE(vector_v2::apply_masked_incremental_segment(
      {.color = 0x001FU, .first = first, .second = second},
      {.zoom = vector_v2::ZoomLevel::k400Percent,
       .level_bounds = {0, 0, width, height},
       .pixels = masked,
       .stride = width},
      mask));
  REQUIRE(vector_v2::apply_incremental_operation(
      {.color = 0x001FU, .samples = std::array{first, second}},
      {.zoom = vector_v2::ZoomLevel::k400Percent,
       .level_bounds = {0, 0, width, height},
       .pixels = direct,
       .stride = width}));
  CHECK(masked == direct);
}

TEST_CASE("affected tile enumeration clips to the bounded world") {
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 0, .y_quarter = 0, .radius_256 = 2048},
      vector_v2::CompactOperationSample{.x_quarter = 272, .y_quarter = 272, .radius_256 = 2048},
  };
  const vector_v2::OperationAppend operation{.samples = samples};
  std::array<vector_v2::TileKey, 4> keys{};
  const auto count = vector_v2::affected_tiles(operation, vector_v2::ZoomLevel::k100Percent, keys);
  REQUIRE(count.has_value());
  CHECK(count->required == 4U);
  CHECK(count->written == 4U);
  CHECK(count->complete());
  CHECK(keys[0] == vector_v2::TileKey{vector_v2::ZoomLevel::k100Percent, 0, 0});
  CHECK(keys[3] == vector_v2::TileKey{vector_v2::ZoomLevel::k100Percent, 1, 1});

  std::array<vector_v2::TileKey, 3> too_small{};
  const auto partial =
      vector_v2::affected_tiles(operation, vector_v2::ZoomLevel::k100Percent, too_small);
  REQUIRE(partial.has_value());
  CHECK(partial->required == 4U);
  CHECK(partial->written == 3U);
  CHECK_FALSE(partial->complete());
  CHECK_FALSE(vector_v2::affected_tiles(operation, vector_v2::ZoomLevel::k25Percent, keys));
}

TEST_CASE("thin stroke bounds include the coarsest tiled paint halo") {
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 255, .y_quarter = 256, .radius_256 = 1},
  };
  CHECK(vector_v2::operation_world_bounds(samples) == vector_v2::PixelRect{62, 62, 66, 66});
  const vector_v2::OperationAppend operation{.samples = samples};
  std::array<vector_v2::TileKey, 4> keys{};
  const auto at_100 = vector_v2::affected_tiles(operation, vector_v2::ZoomLevel::k100Percent, keys);
  REQUIRE(at_100.has_value());
  REQUIRE(at_100->complete());
  CHECK(at_100->written == 4U);
  CHECK(keys[0] == vector_v2::TileKey{vector_v2::ZoomLevel::k100Percent, 0, 0});
  CHECK(keys[1] == vector_v2::TileKey{vector_v2::ZoomLevel::k100Percent, 1, 0});
}

TEST_CASE("affected tiles cover partial edge grids and high zooms") {
  const std::array edge_sample{
      vector_v2::CompactOperationSample{.x_quarter = vector_v2::kWorldWidth * 4,
                                        .y_quarter = vector_v2::kWorldHeight * 4,
                                        .radius_256 = 256}};
  const vector_v2::OperationAppend operation{.samples = edge_sample};
  std::array<vector_v2::TileKey, 4> keys{};
  const auto at_50 = vector_v2::affected_tiles(operation, vector_v2::ZoomLevel::k50Percent, keys);
  REQUIRE(at_50.has_value());
  REQUIRE(at_50->complete());
  CHECK(keys[0] == vector_v2::TileKey{vector_v2::ZoomLevel::k50Percent, 11, 13});

  const auto at_400 = vector_v2::affected_tiles(operation, vector_v2::ZoomLevel::k400Percent, keys);
  REQUIRE(at_400.has_value());
  REQUIRE(at_400->complete());
  CHECK(keys[0] == vector_v2::TileKey{vector_v2::ZoomLevel::k400Percent, 91, 111});
}

TEST_CASE("all committed zooms paint the same world center and enumerate its tile") {
  constexpr std::array zooms{
      vector_v2::ZoomLevel::k25Percent,  vector_v2::ZoomLevel::k50Percent,
      vector_v2::ZoomLevel::k100Percent, vector_v2::ZoomLevel::k200Percent,
      vector_v2::ZoomLevel::k400Percent,
  };
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 400, .y_quarter = 480, .radius_256 = 256},
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

    std::array<vector_v2::TileKey, 4> affected{};
    const auto result = vector_v2::affected_tiles(operation, zoom, affected);
    if (zoom == vector_v2::ZoomLevel::k25Percent) {
      CHECK_FALSE(result.has_value());
    } else {
      REQUIRE(result.has_value());
      CHECK(result->complete());
      CHECK(affected[0].column == static_cast<std::uint16_t>(tile_x / vector_v2::kTileWidth));
      CHECK(affected[0].row == static_cast<std::uint16_t>(tile_y / vector_v2::kTileHeight));
    }
  }
}

TEST_CASE("raster surface honors a stride larger than its visible width") {
  std::array<std::uint16_t, 4U * 3U> pixels{};
  pixels.fill(0x1111U);
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 4, .y_quarter = 4, .radius_256 = 256}};
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
  REQUIRE(vector_v2::apply_masked_incremental_segment(
      {.color = 0x001FU,
       .first = {.x_quarter = 0, .y_quarter = 64, .radius_256 = 8 * 256},
       .second = {.x_quarter = 128, .y_quarter = 64, .radius_256 = 8 * 256}},
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
  REQUIRE(vector_v2::apply_masked_incremental_segment(
      {.tool = vector_v2::OperationTool::kEraser,
       .first = {.x_quarter = 64, .y_quarter = 64, .radius_256 = 64 * 256},
       .second = {.x_quarter = 64, .y_quarter = 64, .radius_256 = 64 * 256}},
      surface, mask, &summary));
  CHECK(summary.all_saturated());
}
