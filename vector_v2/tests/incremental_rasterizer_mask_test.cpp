#include <doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "../src/incremental_rasterizer_internal.h"

namespace raster = tinydraw::vector_v2::raster_internal;

namespace {

std::optional<raster::UnsetWindow> reference_unset_window(std::span<const std::uint8_t> mask,
                                                          std::size_t first, std::size_t last) {
  std::optional<std::size_t> first_unset;
  std::size_t last_unset = 0;
  for (std::size_t pixel = first; pixel <= last; ++pixel) {
    if ((mask[pixel >> 3U] & static_cast<std::uint8_t>(1U << (pixel & 7U))) == 0U) {
      if (!first_unset.has_value()) {
        first_unset = pixel;
      }
      last_unset = pixel;
    }
  }
  if (!first_unset.has_value()) {
    return std::nullopt;
  }
  return raster::UnsetWindow{.first = *first_unset, .last = last_unset};
}

bool exact_span_matches_reference(unsigned surface_phase, unsigned mask_phase, int first, int last,
                                  std::span<const std::uint8_t> initial_mask, std::uint16_t color) {
  constexpr int kWidth = 24;
  constexpr int kStride = 29;
  constexpr std::size_t kRow = kStride;
  constexpr std::size_t kFootprint = kRow + kWidth;
  constexpr std::size_t kMaskBytes = (kFootprint + 7U) / 8U;
  constexpr std::uint16_t kSurfaceGuard = 0xA55AU;
  constexpr std::uint8_t kMaskGuard = 0x69U;
  alignas(16) std::array<std::uint16_t, kFootprint + 32U> actual_pixels{};
  alignas(16) std::array<std::uint8_t, kMaskBytes + 16U> actual_mask{};
  const std::size_t pixel_offset = 8U + surface_phase;
  std::size_t mask_offset = 4U;
  while ((reinterpret_cast<std::uintptr_t>(actual_mask.data() + mask_offset) & 3U) != mask_phase) {
    ++mask_offset;
  }

  actual_pixels.fill(kSurfaceGuard);
  actual_mask.fill(kMaskGuard);
  auto pixels = std::span(actual_pixels).subspan(pixel_offset, kFootprint);
  auto mask = std::span(actual_mask).subspan(mask_offset, kMaskBytes);
  for (std::size_t pixel = 0; pixel < pixels.size(); ++pixel) {
    pixels[pixel] = static_cast<std::uint16_t>(0x1000U + pixel * 37U);
  }
  std::copy(initial_mask.begin(), initial_mask.end(), mask.begin());
  const auto expected_pixels = actual_pixels;
  const auto expected_mask = actual_mask;
  auto reference_pixels = expected_pixels;
  auto reference_mask = expected_mask;

  int expected_count = 0;
  for (int x = first; x <= last; ++x) {
    const std::size_t pixel = kRow + static_cast<std::size_t>(x);
    auto& byte = reference_mask[mask_offset + (pixel >> 3U)];
    const std::uint8_t bit = static_cast<std::uint8_t>(1U << (pixel & 7U));
    if ((byte & bit) != 0U) {
      continue;
    }
    reference_pixels[pixel_offset + pixel] = color;
    byte = static_cast<std::uint8_t>(byte | bit);
    ++expected_count;
  }

  const tinydraw::vector_v2::RasterSurface surface{
      .zoom = tinydraw::vector_v2::ZoomLevel::k100Percent,
      .level_bounds = {0, 0, kWidth, 2},
      .pixels = pixels,
      .stride = kStride,
  };
  const int actual_count = raster::paint_masked_exact_span(first, last, kRow, color, surface, mask);
  return actual_count == expected_count && actual_pixels == reference_pixels &&
         actual_mask == reference_mask;
}

void check_range(std::span<const std::uint8_t> mask, std::size_t first, std::size_t last) {
  const auto expected = reference_unset_window(mask, first, last);
  const auto actual = raster::mask_unset_window(mask, first, last);
  REQUIRE(actual.has_value() == expected.has_value());
  if (expected.has_value()) {
    CHECK(actual->first == expected->first);
    CHECK(actual->last == expected->last);
  }
  CHECK(raster::mask_range_all_set(mask, first, last) == !expected.has_value());
}

void check_sample_bits(const raster::Sample& actual, const raster::Sample& expected) {
  CHECK(std::bit_cast<std::uint32_t>(actual.x) == std::bit_cast<std::uint32_t>(expected.x));
  CHECK(std::bit_cast<std::uint32_t>(actual.y) == std::bit_cast<std::uint32_t>(expected.y));
  CHECK(std::bit_cast<std::uint32_t>(actual.radius) ==
        std::bit_cast<std::uint32_t>(expected.radius));
}

void check_curve_bits(const raster::CurveUnit& actual, const raster::CurveUnit& expected) {
  REQUIRE(actual.count == expected.count);
  for (std::size_t step = 0; step < expected.count; ++step) {
    const raster::Segment& actual_segment = actual.segments[step];
    const raster::Segment& expected_segment = expected.segments[step];
    check_sample_bits(actual_segment.first, expected_segment.first);
    check_sample_bits(actual_segment.second, expected_segment.second);
    CHECK(std::bit_cast<std::uint32_t>(actual_segment.delta_x) ==
          std::bit_cast<std::uint32_t>(expected_segment.delta_x));
    CHECK(std::bit_cast<std::uint32_t>(actual_segment.delta_y) ==
          std::bit_cast<std::uint32_t>(expected_segment.delta_y));
    CHECK(std::bit_cast<std::uint32_t>(actual_segment.inverse_length_squared) ==
          std::bit_cast<std::uint32_t>(expected_segment.inverse_length_squared));
  }
}

raster::Sample reference_midpoint(const raster::Sample& first, const raster::Sample& second) {
  return {
      .x = (first.x + second.x) * 0.5F,
      .y = (first.y + second.y) * 0.5F,
      .radius = (first.radius + second.radius) * 0.5F,
  };
}

raster::Segment reference_segment(const raster::Sample& first, const raster::Sample& second) {
  const float delta_x = second.x - first.x;
  const float delta_y = second.y - first.y;
  const float length_squared = delta_x * delta_x + delta_y * delta_y;
  return {
      .first = first,
      .second = second,
      .delta_x = delta_x,
      .delta_y = delta_y,
      .inverse_length_squared = length_squared > 0.0F ? 1.0F / length_squared : 0.0F,
  };
}

raster::CurveUnit reference_curve_unit(
    std::span<const tinydraw::vector_v2::CompactOperationSample> samples, std::size_t endpoint,
    tinydraw::vector_v2::ZoomLevel zoom) {
  const raster::Sample prior = raster::scaled_sample(samples[endpoint - 2U], zoom);
  const raster::Sample control = raster::scaled_sample(samples[endpoint - 1U], zoom);
  const raster::Sample current = raster::scaled_sample(samples[endpoint], zoom);
  const raster::Sample start = endpoint == 2U ? prior : reference_midpoint(prior, control);
  const raster::Sample end = reference_midpoint(control, current);
  const raster::Sample curve_midpoint =
      reference_midpoint(reference_midpoint(start, control), reference_midpoint(control, end));
  raster::CurveUnit unit;
  unit.segments[0] = reference_segment(start, curve_midpoint);
  unit.segments[1] = reference_segment(curve_midpoint, end);
  unit.count = 2U;
  if (endpoint + 1U == samples.size()) {
    unit.segments[2] = reference_segment(end, current);
    unit.count = 3U;
  }
  return unit;
}

[[gnu::noinline]] bool reference_covers_pixel(const raster::Segment& segment, float pixel_x,
                                              float pixel_y) {
  const float projection = ((pixel_x - segment.first.x) * segment.delta_x +
                            (pixel_y - segment.first.y) * segment.delta_y) *
                           segment.inverse_length_squared;
  const float amount = std::clamp(projection, 0.0F, 1.0F);
  const float center_x = segment.first.x + amount * segment.delta_x;
  const float center_y = segment.first.y + amount * segment.delta_y;
  const float radius =
      segment.first.radius + amount * (segment.second.radius - segment.first.radius);
  const float distance_x = pixel_x - center_x;
  const float distance_y = pixel_y - center_y;
  return distance_x * distance_x + distance_y * distance_y <= radius * radius;
}

}  // namespace

TEST_CASE("fixed-row coverage preserves the original predicate exactly") {
  std::uint32_t random = 0xC0A6E5A1U;
  const auto next = [&random]() {
    random = random * 1'664'525U + 1'013'904'223U;
    return random;
  };

  for (int segment_index = 0; segment_index < 512; ++segment_index) {
    const raster::Sample first{
        .x = static_cast<float>(next() % 23'553U) * 0.0625F,
        .y = static_cast<float>(next() % 16'385U) * 0.0625F,
        .radius = std::max(static_cast<float>(next() & UINT16_MAX) / 256.0F, 0.75F),
    };
    raster::Sample second{
        .x = static_cast<float>(next() % 23'553U) * 0.0625F,
        .y = static_cast<float>(next() % 16'385U) * 0.0625F,
        .radius = std::max(static_cast<float>(next() & UINT16_MAX) / 256.0F, 0.75F),
    };
    if ((segment_index & 15) == 0) {
      second = first;
    }
    const float delta_x = second.x - first.x;
    const float delta_y = second.y - first.y;
    const float length_squared = delta_x * delta_x + delta_y * delta_y;
    const raster::Segment segment{
        .first = first,
        .second = second,
        .delta_x = delta_x,
        .delta_y = delta_y,
        .inverse_length_squared = length_squared > 0.0F ? 1.0F / length_squared : 0.0F,
    };

    for (int probe = 0; probe < 64; ++probe) {
      const float pixel_y = static_cast<float>(next() % 16'385U) + 0.5F;
      const raster::PixelCoverageRow row = raster::make_pixel_coverage_row(segment, pixel_y);
      const float pixel_x = static_cast<float>(next() % 23'553U) + 0.5F;
      CAPTURE(segment_index);
      CAPTURE(probe);
      CHECK(raster::covers_pixel(row, pixel_x) ==
            reference_covers_pixel(segment, pixel_x, pixel_y));
    }
  }
}

TEST_CASE("word mask scans are exact for every phase length and pixel range") {
  constexpr std::uint8_t kGuard = 0x69U;
  constexpr std::size_t kMaximumBytes = 40U;
  std::array<std::uint8_t, kMaximumBytes + 16U> storage{};

  for (std::uintptr_t phase = 0U; phase < 4U; ++phase) {
    std::size_t offset = 4U;
    while ((reinterpret_cast<std::uintptr_t>(storage.data() + offset) & 3U) != phase) {
      ++offset;
    }
    REQUIRE(offset <= 7U);

    for (std::size_t byte_count = 1U; byte_count <= kMaximumBytes; ++byte_count) {
      CAPTURE(phase);
      CAPTURE(byte_count);
      storage.fill(kGuard);
      auto mask = std::span(storage).subspan(offset, byte_count);
      for (std::size_t byte = 0; byte < byte_count; ++byte) {
        // Includes saturated words, alternating bytes, and unset bits at
        // every position as byte_count and address phase advance.
        constexpr std::array pattern{0xFFU, 0x00U, 0xAAU, 0x55U, 0x7FU, 0xFEU, 0xF7U, 0xEFU};
        mask[byte] =
            static_cast<std::uint8_t>(pattern[(byte + byte_count + phase) % pattern.size()]);
      }

      const std::size_t bit_count = byte_count * 8U;
      for (std::size_t first = 0; first < bit_count; ++first) {
        for (std::size_t last = first; last < bit_count; ++last) {
          check_range(mask, first, last);
        }
      }
      CHECK(std::all_of(storage.begin(), storage.begin() + static_cast<std::ptrdiff_t>(offset),
                        [](std::uint8_t value) { return value == kGuard; }));
      CHECK(std::all_of(storage.begin() + static_cast<std::ptrdiff_t>(offset + byte_count),
                        storage.end(), [](std::uint8_t value) { return value == kGuard; }));
    }
  }
}

TEST_CASE("word mask scans find every isolated unset bit without touching guards") {
  constexpr std::uint8_t kGuard = 0x96U;
  constexpr std::size_t kBytes = 37U;
  std::array<std::uint8_t, kBytes + 16U> storage{};

  for (std::uintptr_t phase = 0U; phase < 4U; ++phase) {
    std::size_t offset = 4U;
    while ((reinterpret_cast<std::uintptr_t>(storage.data() + offset) & 3U) != phase) {
      ++offset;
    }
    for (std::size_t unset = 0; unset < kBytes * 8U; ++unset) {
      CAPTURE(phase);
      CAPTURE(unset);
      storage.fill(kGuard);
      auto mask = std::span(storage).subspan(offset, kBytes);
      std::fill(mask.begin(), mask.end(), 0xFFU);
      mask[unset >> 3U] &= static_cast<std::uint8_t>(~(1U << (unset & 7U)));
      check_range(mask, 0U, kBytes * 8U - 1U);
      CHECK(std::all_of(storage.begin(), storage.begin() + static_cast<std::ptrdiff_t>(offset),
                        [](std::uint8_t value) { return value == kGuard; }));
      CHECK(std::all_of(storage.begin() + static_cast<std::ptrdiff_t>(offset + kBytes),
                        storage.end(), [](std::uint8_t value) { return value == kGuard; }));
    }
  }
}

TEST_CASE("masked exact spans match the bit oracle for every pointer phase and mask pair") {
  constexpr std::size_t kFootprint = 29U + 24U;
  constexpr std::size_t kMaskBytes = (kFootprint + 7U) / 8U;
  constexpr std::array<std::uint8_t, 8> kPatterns{
      0x00U, 0xFFU, 0xAAU, 0x55U, 0x81U, 0x7EU, 0x18U, 0xE7U,
  };
  std::array<std::uint8_t, kMaskBytes> mask{};
  bool matched = true;
  std::uint64_t cases = 0;

  // Every clipped head/tail, full-byte interior, pointer phase, and a set of
  // mixed neighboring bytes. These cases exercise both the bit fallback and
  // the proven-free eight-pixel path.
  for (unsigned surface_phase = 0; surface_phase < 8U; ++surface_phase) {
    for (unsigned mask_phase = 0; mask_phase < 4U; ++mask_phase) {
      for (int first = 0; first < 24; ++first) {
        for (int last = first; last < 24; ++last) {
          for (std::size_t pattern = 0; pattern < kPatterns.size(); ++pattern) {
            for (std::size_t byte = 0; byte < mask.size(); ++byte) {
              mask[byte] = kPatterns[(pattern + byte * 3U) % kPatterns.size()];
            }
            matched = matched && exact_span_matches_reference(
                                     surface_phase, mask_phase, first, last, mask,
                                     static_cast<std::uint16_t>(0x001FU + pattern * 0x1111U));
            ++cases;
          }
        }
      }

      // Row one begins at pixel 29, so x=3 starts on a mask-byte boundary.
      // Exhaust all 65,536 combinations across its two full bytes. This
      // includes two free bytes, either free byte beside every mixed byte,
      // fully finalized bytes, and every per-bit fallback arrangement.
      for (std::uint32_t pair = 0; pair <= UINT16_MAX; ++pair) {
        mask.fill(0xA5U);
        mask[4] = static_cast<std::uint8_t>(pair);
        mask[5] = static_cast<std::uint8_t>(pair >> 8U);
        matched =
            matched && exact_span_matches_reference(surface_phase, mask_phase, 3, 18, mask,
                                                    static_cast<std::uint16_t>(0xF800U ^ pair));
        ++cases;
      }
    }
  }

  CHECK(cases == 2'173'952U);
  CHECK(matched);
}

TEST_CASE("rolling curve sample windows are bit exact across directions and chunk boundaries") {
  constexpr std::array kZooms{
      tinydraw::vector_v2::ZoomLevel::k25Percent,  tinydraw::vector_v2::ZoomLevel::k50Percent,
      tinydraw::vector_v2::ZoomLevel::k100Percent, tinydraw::vector_v2::ZoomLevel::k200Percent,
      tinydraw::vector_v2::ZoomLevel::k400Percent,
  };
  constexpr std::array<std::size_t, 7> kSizes{3U, 4U, 5U, 7U, 16U, 31U, 65U};
  constexpr std::array<std::size_t, 6> kChunkSizes{1U, 2U, 3U, 7U, 13U, 29U};
  std::array<tinydraw::vector_v2::CompactOperationSample, kSizes.back()> samples{};
  std::uint32_t random = 0x5CA1ED5AU;
  const auto next = [&random]() {
    random = random * 1'664'525U + 1'013'904'223U;
    return random;
  };

  for (int iteration = 0; iteration < 32; ++iteration) {
    for (std::size_t index = 0; index < samples.size(); ++index) {
      samples[index] = {
          .x_quarter = static_cast<std::uint16_t>(next()),
          .y_quarter = static_cast<std::uint16_t>(next()),
          .radius_256 = static_cast<std::uint16_t>(next()),
          .elapsed_ms = static_cast<std::uint16_t>(next()),
      };
    }
    // Exercise minimum-radius clamping, degenerate segments, and the largest
    // representable coordinates/radius at the rolling-window boundaries.
    samples.front() = {};
    samples[1] = samples.front();
    samples[samples.size() - 2U] = {
        .x_quarter = UINT16_MAX,
        .y_quarter = UINT16_MAX,
        .radius_256 = UINT16_MAX,
        .elapsed_ms = UINT16_MAX,
    };
    samples.back() = samples[samples.size() - 2U];

    for (const auto zoom : kZooms) {
      for (const std::size_t size : kSizes) {
        const auto operation = std::span(samples).first(size);
        CAPTURE(iteration);
        CAPTURE(static_cast<int>(zoom));
        CAPTURE(size);

        raster::CurveSampleWindow forward_window;
        raster::CurveUnit streamed;
        raster::initialize_curve_sample_window(operation, 2U, zoom, forward_window, streamed);
        for (std::size_t endpoint = 2U;;) {
          const raster::CurveUnit expected = reference_curve_unit(operation, endpoint, zoom);
          raster::CurveUnit direct;
          REQUIRE(raster::curved_unit(operation, endpoint, zoom, direct));
          check_curve_bits(direct, expected);
          check_curve_bits(streamed, expected);
          ++endpoint;
          if (endpoint == size) {
            break;
          }
          raster::advance_curve_sample_window(forward_window, operation[endpoint],
                                              endpoint + 1U == size, streamed);
        }

        for (const std::size_t chunk_size : kChunkSizes) {
          std::size_t endpoint = size - 1U;
          bool complete = false;
          while (!complete) {
            raster::CurveSampleWindow reverse_window;
            raster::initialize_curve_sample_window(operation, endpoint, zoom, reverse_window,
                                                   streamed);
            std::size_t chunk_count = 0U;
            while (true) {
              const raster::CurveUnit expected = reference_curve_unit(operation, endpoint, zoom);
              raster::CurveUnit direct;
              REQUIRE(raster::curved_unit(operation, endpoint, zoom, direct));
              check_curve_bits(direct, expected);
              check_curve_bits(streamed, expected);
              ++chunk_count;
              if (endpoint == 2U) {
                complete = true;
                break;
              }
              --endpoint;
              if (chunk_count == chunk_size) {
                break;
              }
              raster::retreat_curve_sample_window(reverse_window, operation[endpoint - 2U],
                                                  endpoint == 2U, streamed);
            }
          }
        }
      }
    }
  }
}
