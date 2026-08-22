#include "esp_attr.h"
#include "tinydraw/vector_v2/panel_staging.h"
#include "tinydraw/vector_v2/pixel_memory.h"
#include "tinydraw/vector_v2/settled_tile.h"
#include "tinydraw/vector_v2/tile_uniform.h"
#include "vector_v2_gate_harness_internal.h"

extern "C" void tinydraw_gate_initialize_producer_buffers(std::uint16_t* pixels,
                                                          std::size_t pixel_count,
                                                          std::uint8_t* mask,
                                                          std::size_t mask_count,
                                                          std::uint16_t color);
extern "C" void tinydraw_gate_copy_publication_rows(const std::uint16_t* source,
                                                    std::size_t source_stride,
                                                    std::uint16_t* destination, int width,
                                                    int height);
extern "C" std::uint32_t tinydraw_settled_composite_esp32s3(
    const tinydraw::vector_v2::SettledTileWorkspace* workspace, std::size_t first_at,
    std::size_t count, std::uint32_t packed_color);
namespace tinydraw::vector_v2::raster_internal {
int paint_masked_exact_span(int first_covered, int last_covered, std::size_t row,
                            std::uint16_t color, const RasterSurface& surface,
                            std::span<std::uint8_t> finalized);
}

namespace tinydraw::esp32::gate_harness {
namespace {

constexpr std::uint16_t byte_swapped(std::uint16_t value) {
  return static_cast<std::uint16_t>((value >> 8U) | (value << 8U));
}

[[gnu::noinline]] void stage_pixels_swapped_scalar_oracle(const std::uint16_t* source,
                                                          std::uint16_t* destination, int width) {
  for (int column = 0; column < width; ++column) {
    destination[column] = byte_swapped(source[column]);
  }
}

[[gnu::noinline]] void stage_ring_rows_swapped_scalar_oracle(const std::uint16_t* source,
                                                             int area_width, int first_row,
                                                             int rows, int ring_height, int shift_x,
                                                             std::uint16_t* destination,
                                                             int destination_stride) {
  int source_row = first_row;
  for (int row = 0; row < rows; ++row) {
    const std::uint16_t* const source_pixels =
        source + static_cast<std::ptrdiff_t>(source_row) * area_width;
    std::uint16_t* const destination_pixels =
        destination + static_cast<std::ptrdiff_t>(row) * destination_stride;
    const int tail = area_width - shift_x;
    for (int column = 0; column < tail; ++column) {
      destination_pixels[column] = byte_swapped(source_pixels[shift_x + column]);
    }
    for (int column = 0; column < shift_x; ++column) {
      destination_pixels[tail + column] = byte_swapped(source_pixels[column]);
    }
    if (++source_row == ring_height) {
      source_row = 0;
    }
  }
}

std::uint32_t staging_checksum(std::span<const std::uint16_t> pixels) {
  std::uint32_t checksum = 2166136261U;
  for (const std::uint16_t pixel : pixels) {
    checksum = (checksum ^ pixel) * 16777619U;
  }
  return checksum;
}

[[gnu::noinline]] bool run_panel_staging_ab_benchmark() {
  constexpr int kWidth = 368;
  constexpr int kHeight = 372;
  constexpr int kFramePixels = kWidth * kHeight;
  // Keep the benchmark's internal DMA allocation below the fragmented free
  // block left by the fully constructed application. The row kernel is the
  // same one used by production's 44-row transfers.
  constexpr int kRowsPerTransfer = 8;
  constexpr int kStagingPixels = kRowsPerTransfer * kWidth;
  constexpr int kGuardPixels = 8;
  constexpr std::uint16_t kGuard = 0xDEADU;
  constexpr int kLinearIterations = 2'048;
  constexpr int kRingIterations = 8;
  struct LinearCase {
    int source_phase;
    int destination_phase;
    int width;
  };
  constexpr std::array<LinearCase, 4> kLinearCases{{
      {1, 0, 368},
      {3, 1, 351},
      {7, 2, 255},
      {2, 5, 127},
  }};
  constexpr std::array<int, 6> kRingShifts{1, 2, 7, 9, 183, 367};

  auto source = std::unique_ptr<std::uint16_t, decltype(&heap_caps_free)>{
      static_cast<std::uint16_t*>(heap_caps_aligned_alloc(
          16U, static_cast<std::size_t>(kFramePixels) * sizeof(std::uint16_t), kExternalCaps)),
      &heap_caps_free};
  auto destination = std::unique_ptr<std::uint16_t, decltype(&heap_caps_free)>{
      static_cast<std::uint16_t*>(heap_caps_aligned_alloc(
          16U, static_cast<std::size_t>(kStagingPixels + 2 * kGuardPixels) * sizeof(std::uint16_t),
          MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)),
      &heap_caps_free};
  if (source == nullptr || destination == nullptr) {
    std::printf("TINYDRAW_GATE1_PANEL_STAGE_AB allocation=0 pass=0\n");
    return false;
  }
  for (int index = 0; index < kFramePixels; ++index) {
    source.get()[index] = static_cast<std::uint16_t>(index * 251U + 0x4A31U);
  }
  std::uint16_t* const output = destination.get() + kGuardPixels;
  volatile std::uint32_t timing_sink = 0U;

  const auto run_linear = [&](bool pie) {
    std::fill_n(destination.get(), kStagingPixels + 2 * kGuardPixels, kGuard);
    const std::int64_t started = esp_timer_get_time();
    for (int iteration = 0; iteration < kLinearIterations; ++iteration) {
      for (const LinearCase& test : kLinearCases) {
        if (pie) {
          vector_v2::stage_pixels_swapped(source.get() + test.source_phase,
                                          output + test.destination_phase, test.width);
        } else {
          stage_pixels_swapped_scalar_oracle(source.get() + test.source_phase,
                                             output + test.destination_phase, test.width);
        }
        timing_sink = timing_sink ^ output[test.destination_phase + test.width - 1];
      }
    }
    const std::int64_t elapsed = esp_timer_get_time() - started;
    const std::uint32_t checksum =
        staging_checksum({destination.get(), kStagingPixels + 2 * kGuardPixels});
    const bool guards =
        destination.get()[kGuardPixels - 1] == kGuard && output[16 + kWidth] == kGuard;
    return std::array<std::uint64_t, 3>{static_cast<std::uint64_t>(elapsed), checksum, guards};
  };

  const auto run_ring = [&](bool pie) {
    std::fill_n(destination.get(), kStagingPixels + 2 * kGuardPixels, kGuard);
    const std::int64_t started = esp_timer_get_time();
    for (int iteration = 0; iteration < kRingIterations; ++iteration) {
      for (std::size_t shift_index = 0; shift_index < kRingShifts.size(); ++shift_index) {
        const int shift_x = kRingShifts[shift_index];
        const int shift_y = static_cast<int>((shift_index * 61U + 371U) % kHeight);
        for (int first_output_row = 0; first_output_row < kHeight;
             first_output_row += kRowsPerTransfer) {
          const int rows = std::min(kRowsPerTransfer, kHeight - first_output_row);
          const int first_source_row = (first_output_row + shift_y) % kHeight;
          if (pie) {
            const bool staged = vector_v2::stage_full_ring_rows_swapped(
                source.get(), kWidth, first_source_row, rows, kHeight, shift_x, output, kWidth);
            timing_sink = timing_sink ^ static_cast<std::uint32_t>(staged);
          } else {
            stage_ring_rows_swapped_scalar_oracle(source.get(), kWidth, first_source_row, rows,
                                                  kHeight, shift_x, output, kWidth);
          }
          timing_sink = timing_sink ^ output[(rows - 1) * kWidth + kWidth - 1];
        }
      }
    }
    const std::int64_t elapsed = esp_timer_get_time() - started;
    const std::uint32_t checksum =
        staging_checksum({destination.get(), kStagingPixels + 2 * kGuardPixels});
    const bool guards = destination.get()[kGuardPixels - 1] == kGuard &&
                        output[kRowsPerTransfer * kWidth] == kGuard;
    return std::array<std::uint64_t, 3>{static_cast<std::uint64_t>(elapsed), checksum, guards};
  };

  // Warm the source/destination cache paths before the timed A/B runs.
  vector_v2::stage_pixels_swapped(source.get() + 1, output, kWidth);
  stage_pixels_swapped_scalar_oracle(source.get() + 1, output, kWidth);
  const auto linear_pie = run_linear(true);
  const auto linear_scalar = run_linear(false);
  const auto ring_pie = run_ring(true);
  const auto ring_scalar = run_ring(false);
  const bool passed = linear_pie[1] == linear_scalar[1] && ring_pie[1] == ring_scalar[1] &&
                      linear_pie[2] != 0U && linear_scalar[2] != 0U && ring_pie[2] != 0U &&
                      ring_scalar[2] != 0U;
  const std::uint64_t linear_speedup_x1000 =
      linear_pie[0] == 0U ? 0U : linear_scalar[0] * 1'000U / linear_pie[0];
  const std::uint64_t ring_speedup_x1000 =
      ring_pie[0] == 0U ? 0U : ring_scalar[0] * 1'000U / ring_pie[0];
  std::printf(
      "TINYDRAW_GATE1_PANEL_STAGE_AB allocation=1 linear_iterations=%d linear_calls=%d "
      "linear_pie_us=%llu linear_scalar_us=%llu linear_speedup_x1000=%llu "
      "linear_pie_checksum=%08lx linear_scalar_checksum=%08lx linear_guards=%u "
      "ring_iterations=%d ring_sweeps=%d "
      "ring_pie_us=%llu ring_scalar_us=%llu ring_speedup_x1000=%llu "
      "ring_pie_checksum=%08lx ring_scalar_checksum=%08lx ring_guards=%u pass=%u\n",
      kLinearIterations, kLinearIterations * static_cast<int>(kLinearCases.size()),
      static_cast<unsigned long long>(linear_pie[0]),
      static_cast<unsigned long long>(linear_scalar[0]),
      static_cast<unsigned long long>(linear_speedup_x1000),
      static_cast<unsigned long>(linear_pie[1]), static_cast<unsigned long>(linear_scalar[1]),
      linear_pie[2] != 0U && linear_scalar[2] != 0U, kRingIterations,
      kRingIterations * static_cast<int>(kRingShifts.size()),
      static_cast<unsigned long long>(ring_pie[0]), static_cast<unsigned long long>(ring_scalar[0]),
      static_cast<unsigned long long>(ring_speedup_x1000), static_cast<unsigned long>(ring_pie[1]),
      static_cast<unsigned long>(ring_scalar[1]), ring_pie[2] != 0U && ring_scalar[2] != 0U,
      passed);
  return passed;
}

[[gnu::noinline]] bool run_tile_uniform_kernel_gate(std::size_t& cases) {
  constexpr std::size_t kGuard = 8U;
  constexpr std::size_t kMaximumExtent = static_cast<std::size_t>(vector_v2::kTileHeight - 1) *
                                             static_cast<std::size_t>(vector_v2::kTileWidth + 7) +
                                         static_cast<std::size_t>(vector_v2::kTileWidth);
  constexpr std::size_t kStorage = kGuard + 7U + kMaximumExtent + kGuard;
  constexpr std::uint16_t kGuardColor = 0xDEADU;
  constexpr std::array<std::uint16_t, 4> kColors{0x0000U, 0xFFFFU, 0x39E7U, 0xA55AU};
  auto storage = std::unique_ptr<std::uint16_t, decltype(&heap_caps_free)>{
      static_cast<std::uint16_t*>(heap_caps_aligned_alloc(16U, kStorage * sizeof(std::uint16_t),
                                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      &heap_caps_free};
  if (storage == nullptr) {
    return false;
  }

  bool passed = (reinterpret_cast<std::uintptr_t>(storage.get()) & 0x0FU) == 0U;
  for (const std::uint16_t color : kColors) {
    for (std::size_t phase = 0; phase < 8U; ++phase) {
      std::fill_n(storage.get(), kStorage, kGuardColor);
      std::uint16_t* const pixels = storage.get() + kGuard + phase;
      std::fill_n(pixels, vector_v2::kTilePixels, color);
      const std::span<const std::uint16_t> tile(pixels, vector_v2::kTilePixels);
      passed = passed &&
               vector_v2::tile_uniform_color(tile, vector_v2::kTileWidth, vector_v2::kTileHeight,
                                             vector_v2::kTileWidth) == color;
      ++cases;
      for (std::size_t mismatch = 0; mismatch < vector_v2::kTilePixels; ++mismatch) {
        pixels[mismatch] = static_cast<std::uint16_t>(color ^ 0x0001U);
        passed =
            passed && !vector_v2::tile_uniform_color(tile, vector_v2::kTileWidth,
                                                     vector_v2::kTileHeight, vector_v2::kTileWidth)
                           .has_value();
        pixels[mismatch] = color;
        ++cases;
        if ((cases & 0x3FFU) == 0U) {
          vTaskDelay(1);
        }
      }
      passed = passed && storage.get()[kGuard + phase - 1U] == kGuardColor &&
               storage.get()[kGuard + phase + vector_v2::kTilePixels] == kGuardColor;
    }
  }

  constexpr std::array<int, 10> kWidths{1, 7, 8, 9, 15, 16, 17, 31, 63, 64};
  constexpr std::array<int, 4> kHeights{1, 2, 7, vector_v2::kTileHeight};
  constexpr std::array<std::size_t, 3> kPaddings{0U, 1U, 7U};
  constexpr std::uint16_t kColor = 0x7BEFU;
  for (const int width : kWidths) {
    for (const int height : kHeights) {
      for (const std::size_t padding : kPaddings) {
        const std::size_t stride = static_cast<std::size_t>(width) + padding;
        const std::size_t extent =
            static_cast<std::size_t>(height - 1) * stride + static_cast<std::size_t>(width);
        for (std::size_t phase = 0; phase < 8U; ++phase) {
          std::fill_n(storage.get(), kStorage, kGuardColor);
          std::uint16_t* const pixels = storage.get() + kGuard + phase;
          for (int row = 0; row < height; ++row) {
            std::fill_n(
                pixels + static_cast<std::ptrdiff_t>(row) * static_cast<std::ptrdiff_t>(stride),
                width, kColor);
          }
          const std::span<const std::uint16_t> window(pixels, extent);
          passed = passed && vector_v2::tile_uniform_color(window, width, height, stride) == kColor;
          ++cases;
          if (width * height > 1) {
            pixels[static_cast<std::ptrdiff_t>(height - 1) * static_cast<std::ptrdiff_t>(stride) +
                   width - 1] ^= 0x8000U;
            passed =
                passed && !vector_v2::tile_uniform_color(window, width, height, stride).has_value();
            ++cases;
          }
          passed = passed && storage.get()[kGuard + phase - 1U] == kGuardColor &&
                   storage.get()[kGuard + phase + extent] == kGuardColor;
          if ((cases & 0x3FFU) == 0U) {
            vTaskDelay(1);
          }
        }
      }
    }
  }
  return passed;
}

[[gnu::noinline]] bool run_settled_composite_kernel_gate(std::size_t& cases) {
  constexpr std::size_t kChunk = 256U;
  constexpr std::size_t kGuard = 8U;
  constexpr std::size_t kStorage = kChunk + kGuard * 2U;
  constexpr std::uint8_t kByteGuard = 0xA5U;
  constexpr std::uint16_t kPlaneGuard = 0xA55AU;
  constexpr std::array<std::uint32_t, 8> kColors{
      0x000000U, 0xFFFFFFU, 0x0000FFU, 0x00FF00U, 0xFF0000U, 0x31A5E7U, 0xE73918U, 0x5AC67BU,
  };
  auto operation = std::unique_ptr<std::uint8_t, decltype(&heap_caps_free)>{
      static_cast<std::uint8_t*>(heap_caps_malloc(kStorage, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      &heap_caps_free};
  auto accumulated = std::unique_ptr<std::uint8_t, decltype(&heap_caps_free)>{
      static_cast<std::uint8_t*>(heap_caps_malloc(kStorage, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      &heap_caps_free};
  const auto allocate_plane = [] {
    return std::unique_ptr<std::uint16_t, decltype(&heap_caps_free)>{
        static_cast<std::uint16_t*>(heap_caps_malloc(kStorage * sizeof(std::uint16_t),
                                                     MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        &heap_caps_free};
  };
  auto red = allocate_plane();
  auto green = allocate_plane();
  auto blue = allocate_plane();
  if (operation == nullptr || accumulated == nullptr || red == nullptr || green == nullptr ||
      blue == nullptr) {
    return false;
  }
  const vector_v2::SettledTileWorkspace workspace{
      .operation_alpha = {operation.get(), kStorage},
      .accumulated_alpha = {accumulated.get(), kStorage},
      .red = {red.get(), kStorage},
      .green = {green.get(), kStorage},
      .blue = {blue.get(), kStorage},
  };
  bool passed = tinydraw_settled_composite_esp32s3(&workspace, 0U, 0U, 0xFFFFFFU) == 0U;
  for (const std::uint32_t color : kColors) {
    const std::uint32_t source_red = color & 0xFFU;
    const std::uint32_t source_green = (color >> 8U) & 0xFFU;
    const std::uint32_t source_blue = (color >> 16U) & 0xFFU;
    for (std::uint32_t old_alpha = 0U; old_alpha < 256U; ++old_alpha) {
      const std::size_t first_at = kGuard + (old_alpha & 3U);
      std::fill_n(operation.get(), kStorage, kByteGuard);
      std::fill_n(accumulated.get(), kStorage, kByteGuard);
      std::fill_n(red.get(), kStorage, kPlaneGuard);
      std::fill_n(green.get(), kStorage, kPlaneGuard);
      std::fill_n(blue.get(), kStorage, kPlaneGuard);
      for (std::uint32_t source_alpha = 0U; source_alpha < 256U; ++source_alpha) {
        const std::size_t at = first_at + source_alpha;
        operation.get()[at] = static_cast<std::uint8_t>(source_alpha);
        accumulated.get()[at] = static_cast<std::uint8_t>(old_alpha);
        red.get()[at] = static_cast<std::uint16_t>((source_alpha * 13U + color) % (old_alpha + 1U));
        green.get()[at] =
            static_cast<std::uint16_t>((source_alpha * 29U + (color >> 8U)) % (old_alpha + 1U));
        blue.get()[at] =
            static_cast<std::uint16_t>((source_alpha * 47U + (color >> 16U)) % (old_alpha + 1U));
      }
      std::uint32_t expected_saturated = 0U;
      const std::uint32_t actual_saturated =
          tinydraw_settled_composite_esp32s3(&workspace, first_at, kChunk, color);
      for (std::uint32_t source_alpha = 0U; source_alpha < 256U; ++source_alpha) {
        const std::size_t at = first_at + source_alpha;
        const std::uint32_t initial_red = (source_alpha * 13U + color) % (old_alpha + 1U);
        const std::uint32_t initial_green = (source_alpha * 29U + (color >> 8U)) % (old_alpha + 1U);
        const std::uint32_t initial_blue = (source_alpha * 47U + (color >> 16U)) % (old_alpha + 1U);
        const std::uint32_t contribution = (source_alpha * (255U - old_alpha) + 127U) / 255U;
        const bool opaque = source_alpha == 255U && old_alpha == 0U;
        const std::uint32_t expected_alpha =
            opaque ? 255U : (contribution == 0U ? old_alpha : old_alpha + contribution);
        const std::uint32_t expected_red =
            opaque ? source_red : initial_red + source_red * contribution / 255U;
        const std::uint32_t expected_green =
            opaque ? source_green : initial_green + source_green * contribution / 255U;
        const std::uint32_t expected_blue =
            opaque ? source_blue : initial_blue + source_blue * contribution / 255U;
        expected_saturated += expected_alpha == 255U && old_alpha != 255U ? 1U : 0U;
        passed = passed && accumulated.get()[at] == expected_alpha &&
                 red.get()[at] == expected_red && green.get()[at] == expected_green &&
                 blue.get()[at] == expected_blue;
      }
      passed = passed && actual_saturated == expected_saturated &&
               operation.get()[first_at - 1U] == kByteGuard &&
               accumulated.get()[first_at - 1U] == kByteGuard &&
               red.get()[first_at - 1U] == kPlaneGuard &&
               green.get()[first_at - 1U] == kPlaneGuard &&
               blue.get()[first_at - 1U] == kPlaneGuard &&
               operation.get()[first_at + kChunk] == kByteGuard &&
               accumulated.get()[first_at + kChunk] == kByteGuard &&
               red.get()[first_at + kChunk] == kPlaneGuard &&
               green.get()[first_at + kChunk] == kPlaneGuard &&
               blue.get()[first_at + kChunk] == kPlaneGuard;
      ++cases;
    }
  }
  return passed;
}

// Frozen copy of the exact span implementation immediately before the
// free-mask-byte word-store specialization. Keeping this in IRAM gives the
// gate an instruction-placement-equivalent halfword-store reference.
IRAM_ATTR [[gnu::noinline, gnu::noipa]] int paint_masked_exact_span_halfword_reference(
    int first_covered, int last_covered, std::size_t row, std::uint16_t color,
    const vector_v2::RasterSurface& surface, std::span<std::uint8_t> finalized) {
  int newly_finalized = 0;
  int x = first_covered;
  std::size_t pixel = row + static_cast<std::size_t>(x - surface.level_bounds.x0);
  while (x <= last_covered) {
    const std::size_t byte = pixel >> 3U;
    const unsigned bit = static_cast<unsigned>(pixel & 7U);
    const int in_byte = std::min(static_cast<int>(8U - bit), last_covered - x + 1);
    const std::uint8_t chunk_mask = static_cast<std::uint8_t>(
        in_byte == 8 ? 0xFFU : ((1U << static_cast<unsigned>(in_byte)) - 1U) << bit);
    const std::uint8_t have = finalized[byte];
    if ((have & chunk_mask) == chunk_mask) {
      // Already finalized.
    } else if ((have & chunk_mask) == 0U) {
      std::fill_n(surface.pixels.data() + pixel, static_cast<std::size_t>(in_byte), color);
      finalized[byte] = static_cast<std::uint8_t>(have | chunk_mask);
      newly_finalized += in_byte;
    } else {
      for (int offset = 0; offset < in_byte; ++offset) {
        const std::size_t candidate = pixel + static_cast<std::size_t>(offset);
        const std::uint8_t candidate_bit = static_cast<std::uint8_t>(1U << (candidate & 7U));
        if ((finalized[candidate >> 3U] & candidate_bit) != 0U) {
          continue;
        }
        surface.pixels[candidate] = color;
        finalized[candidate >> 3U] =
            static_cast<std::uint8_t>(finalized[candidate >> 3U] | candidate_bit);
        ++newly_finalized;
      }
    }
    x += in_byte;
    pixel += static_cast<std::size_t>(in_byte);
  }
  return newly_finalized;
}

std::uint32_t masked_span_checksum(std::uint32_t checksum, std::span<const std::uint16_t> pixels) {
  for (const std::uint16_t pixel : pixels) {
    checksum = (checksum ^ pixel) * 16777619U;
  }
  return checksum;
}

std::uint32_t masked_span_checksum(std::uint32_t checksum, std::span<const std::uint8_t> bytes) {
  for (const std::uint8_t byte : bytes) {
    checksum = (checksum ^ byte) * 16777619U;
  }
  return checksum;
}

[[gnu::noinline]] bool run_masked_exact_span_ab_benchmark() {
  constexpr int kWidth = 64;
  constexpr std::size_t kPixelGuard = 8U;
  constexpr std::size_t kMaximumPixelPhase = 7U;
  constexpr std::size_t kPixelStorage =
      kPixelGuard + kMaximumPixelPhase + static_cast<std::size_t>(kWidth) + kPixelGuard;
  constexpr std::size_t kMaskBytes = static_cast<std::size_t>(kWidth) / 8U;
  constexpr std::size_t kMaskStride = 16U;
  constexpr std::size_t kSlots = 256U;
  constexpr std::size_t kMaskStorage = kMaskStride * kSlots;
  constexpr std::uint16_t kPixelGuardValue = 0xA55AU;
  constexpr std::uint8_t kMaskGuardValue = 0x69U;
  constexpr std::uint16_t kColor = 0x07E0U;
  constexpr std::uint64_t kExpectedFinalizedPerSlot = 226U;
  struct Workload {
    int first;
    int last;
    std::array<std::uint8_t, kMaskBytes> mask;
  };
  constexpr std::array<Workload, 6> kWorkloads{{
      {.first = 0, .last = 7, .mask = {0x00U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU}},
      {.first = 0, .last = 15, .mask = {0x00U, 0x00U, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU}},
      {.first = 0, .last = 63, .mask = {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U}},
      {.first = 0, .last = 63, .mask = {0x00U, 0xFFU, 0x00U, 0xFFU, 0x00U, 0xFFU, 0x00U, 0xFFU}},
      {.first = 0, .last = 63, .mask = {0x81U, 0x00U, 0x7EU, 0x00U, 0xAAU, 0x00U, 0x55U, 0x00U}},
      {.first = 3, .last = 60, .mask = {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U}},
  }};

  const auto allocate_pixels = []() {
    return std::unique_ptr<std::uint16_t, decltype(&heap_caps_free)>{
        static_cast<std::uint16_t*>(heap_caps_aligned_alloc(
            16U, kPixelStorage * sizeof(std::uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        &heap_caps_free};
  };
  const auto allocate_masks = []() {
    return std::unique_ptr<std::uint8_t, decltype(&heap_caps_free)>{
        static_cast<std::uint8_t*>(
            heap_caps_aligned_alloc(16U, kMaskStorage, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        &heap_caps_free};
  };
  auto candidate_pixels = allocate_pixels();
  auto reference_pixels = allocate_pixels();
  auto candidate_masks = allocate_masks();
  auto reference_masks = allocate_masks();
  if (candidate_pixels == nullptr || reference_pixels == nullptr || candidate_masks == nullptr ||
      reference_masks == nullptr) {
    std::printf("TINYDRAW_GATE1_MASKED_SPAN_AB allocation=0 pass=0\n");
    return false;
  }

  std::uint64_t candidate_us = 0U;
  std::uint64_t reference_us = 0U;
  std::uint64_t candidate_count = 0U;
  std::uint64_t reference_count = 0U;
  std::uint32_t candidate_checksum = 2166136261U;
  std::uint32_t reference_checksum = 2166136261U;
  bool exact = true;
  bool guards = true;
  std::size_t workload_count = 0U;

  for (std::size_t pixel_phase = 0; pixel_phase <= kMaximumPixelPhase; ++pixel_phase) {
    for (std::size_t mask_phase = 0; mask_phase < 4U; ++mask_phase) {
      for (std::size_t workload_index = 0; workload_index < kWorkloads.size(); ++workload_index) {
        const Workload& workload = kWorkloads[workload_index];
        std::fill_n(candidate_pixels.get(), kPixelStorage, kPixelGuardValue);
        std::fill_n(reference_pixels.get(), kPixelStorage, kPixelGuardValue);
        std::fill_n(candidate_masks.get(), kMaskStorage, kMaskGuardValue);
        std::fill_n(reference_masks.get(), kMaskStorage, kMaskGuardValue);
        std::uint16_t* const candidate_output = candidate_pixels.get() + kPixelGuard + pixel_phase;
        std::uint16_t* const reference_output = reference_pixels.get() + kPixelGuard + pixel_phase;
        for (int pixel = 0; pixel < kWidth; ++pixel) {
          const std::uint16_t initial = static_cast<std::uint16_t>(0x1000U + pixel * 37U);
          candidate_output[pixel] = initial;
          reference_output[pixel] = initial;
        }
        for (std::size_t slot = 0; slot < kSlots; ++slot) {
          std::copy(workload.mask.begin(), workload.mask.end(),
                    candidate_masks.get() + slot * kMaskStride + mask_phase);
          std::copy(workload.mask.begin(), workload.mask.end(),
                    reference_masks.get() + slot * kMaskStride + mask_phase);
        }
        const vector_v2::RasterSurface candidate_surface{
            .zoom = vector_v2::ZoomLevel::k100Percent,
            .level_bounds = {0, 0, kWidth, 1},
            .pixels = {candidate_output, static_cast<std::size_t>(kWidth)},
            .stride = kWidth,
        };
        const vector_v2::RasterSurface reference_surface{
            .zoom = vector_v2::ZoomLevel::k100Percent,
            .level_bounds = {0, 0, kWidth, 1},
            .pixels = {reference_output, static_cast<std::size_t>(kWidth)},
            .stride = kWidth,
        };

        const auto run_candidate = [&]() {
          std::uint64_t finalized_count = 0U;
          const std::int64_t started = esp_timer_get_time();
          for (std::size_t slot = 0; slot < kSlots; ++slot) {
            finalized_count +=
                static_cast<std::uint64_t>(vector_v2::raster_internal::paint_masked_exact_span(
                    workload.first, workload.last, 0U, kColor, candidate_surface,
                    {candidate_masks.get() + slot * kMaskStride + mask_phase, kMaskBytes}));
          }
          candidate_us += static_cast<std::uint64_t>(esp_timer_get_time() - started);
          candidate_count += finalized_count;
        };
        const auto run_reference = [&]() {
          std::uint64_t finalized_count = 0U;
          const std::int64_t started = esp_timer_get_time();
          for (std::size_t slot = 0; slot < kSlots; ++slot) {
            finalized_count +=
                static_cast<std::uint64_t>(paint_masked_exact_span_halfword_reference(
                    workload.first, workload.last, 0U, kColor, reference_surface,
                    {reference_masks.get() + slot * kMaskStride + mask_phase, kMaskBytes}));
          }
          reference_us += static_cast<std::uint64_t>(esp_timer_get_time() - started);
          reference_count += finalized_count;
        };
        if (((pixel_phase + mask_phase + workload_index) & 1U) == 0U) {
          run_candidate();
          run_reference();
        } else {
          run_reference();
          run_candidate();
        }

        exact = exact &&
                std::equal(candidate_pixels.get(), candidate_pixels.get() + kPixelStorage,
                           reference_pixels.get()) &&
                std::equal(candidate_masks.get(), candidate_masks.get() + kMaskStorage,
                           reference_masks.get());
        guards = guards &&
                 std::all_of(candidate_pixels.get(), candidate_output,
                             [](std::uint16_t pixel) { return pixel == kPixelGuardValue; }) &&
                 std::all_of(candidate_output + kWidth, candidate_pixels.get() + kPixelStorage,
                             [](std::uint16_t pixel) { return pixel == kPixelGuardValue; }) &&
                 std::all_of(reference_pixels.get(), reference_output,
                             [](std::uint16_t pixel) { return pixel == kPixelGuardValue; }) &&
                 std::all_of(reference_output + kWidth, reference_pixels.get() + kPixelStorage,
                             [](std::uint16_t pixel) { return pixel == kPixelGuardValue; });
        for (std::size_t slot = 0; slot < kSlots; ++slot) {
          const std::size_t slot_at = slot * kMaskStride;
          guards = guards &&
                   std::all_of(candidate_masks.get() + slot_at,
                               candidate_masks.get() + slot_at + mask_phase,
                               [](std::uint8_t byte) { return byte == kMaskGuardValue; }) &&
                   std::all_of(candidate_masks.get() + slot_at + mask_phase + kMaskBytes,
                               candidate_masks.get() + slot_at + kMaskStride,
                               [](std::uint8_t byte) { return byte == kMaskGuardValue; }) &&
                   std::all_of(reference_masks.get() + slot_at,
                               reference_masks.get() + slot_at + mask_phase,
                               [](std::uint8_t byte) { return byte == kMaskGuardValue; }) &&
                   std::all_of(reference_masks.get() + slot_at + mask_phase + kMaskBytes,
                               reference_masks.get() + slot_at + kMaskStride,
                               [](std::uint8_t byte) { return byte == kMaskGuardValue; });
        }
        candidate_checksum =
            masked_span_checksum(candidate_checksum, {candidate_pixels.get(), kPixelStorage});
        candidate_checksum =
            masked_span_checksum(candidate_checksum, {candidate_masks.get(), kMaskStorage});
        reference_checksum =
            masked_span_checksum(reference_checksum, {reference_pixels.get(), kPixelStorage});
        reference_checksum =
            masked_span_checksum(reference_checksum, {reference_masks.get(), kMaskStorage});
        ++workload_count;
      }
    }
  }

  const std::uint64_t expected_count = kExpectedFinalizedPerSlot * kSlots * 8U * 4U;
  const bool passed = exact && guards && candidate_count == expected_count &&
                      reference_count == expected_count && candidate_count == reference_count &&
                      candidate_checksum == reference_checksum;
  const std::uint64_t speedup_x1000 =
      candidate_us == 0U ? 0U : reference_us * 1'000U / candidate_us;
  std::printf(
      "TINYDRAW_GATE1_MASKED_SPAN_AB allocation=1 workloads=%lu slots=%lu calls=%lu "
      "candidate_us=%llu halfword_us=%llu speedup_x1000=%llu candidate_count=%llu "
      "halfword_count=%llu candidate_checksum=%08lx halfword_checksum=%08lx exact=%u "
      "guards=%u pass=%u\n",
      static_cast<unsigned long>(workload_count), static_cast<unsigned long>(kSlots),
      static_cast<unsigned long>(workload_count * kSlots),
      static_cast<unsigned long long>(candidate_us), static_cast<unsigned long long>(reference_us),
      static_cast<unsigned long long>(speedup_x1000),
      static_cast<unsigned long long>(candidate_count),
      static_cast<unsigned long long>(reference_count),
      static_cast<unsigned long>(candidate_checksum),
      static_cast<unsigned long>(reference_checksum), exact, guards, passed);
  return passed;
}

[[gnu::noinline]] bool run_masked_exact_span_kernel_gate(std::size_t& cases) {
  constexpr int kWidth = 24;
  constexpr int kStride = 29;
  constexpr std::size_t kRow = kStride;
  constexpr std::size_t kFootprint = kRow + kWidth;
  constexpr std::size_t kMaskBytes = (kFootprint + 7U) / 8U;
  constexpr std::uint16_t kPixelGuard = 0xA55AU;
  constexpr std::uint8_t kMaskGuard = 0x69U;
  alignas(16) std::array<std::uint16_t, kFootprint + 32U> actual_pixels{};
  alignas(16) std::array<std::uint16_t, kFootprint + 32U> expected_pixels{};
  alignas(16) std::array<std::uint8_t, kMaskBytes + 16U> actual_mask{};
  alignas(16) std::array<std::uint8_t, kMaskBytes + 16U> expected_mask{};
  cases = 0;
  bool passed = true;

  for (unsigned pixel_phase = 0; pixel_phase < 8U; ++pixel_phase) {
    for (unsigned mask_phase = 0; mask_phase < 4U; ++mask_phase) {
      std::size_t mask_offset = 4U;
      while ((reinterpret_cast<std::uintptr_t>(actual_mask.data() + mask_offset) & 3U) !=
             mask_phase) {
        ++mask_offset;
      }
      const std::size_t pixel_offset = 8U + pixel_phase;
      for (unsigned pattern = 0; pattern <= UINT8_MAX; ++pattern) {
        actual_pixels.fill(kPixelGuard);
        actual_mask.fill(kMaskGuard);
        for (std::size_t pixel = 0; pixel < kFootprint; ++pixel) {
          actual_pixels[pixel_offset + pixel] = static_cast<std::uint16_t>(0x1000U + pixel * 37U);
        }
        for (std::size_t byte = 0; byte < kMaskBytes; ++byte) {
          actual_mask[mask_offset + byte] = static_cast<std::uint8_t>(pattern + byte * 0x35U);
        }
        actual_mask[mask_offset + 4U] = static_cast<std::uint8_t>(pattern);
        const int last = pattern == 0U ? 18 : 10;
        if (pattern == 0U) {
          actual_mask[mask_offset + 5U] = 0U;
        }
        expected_pixels = actual_pixels;
        expected_mask = actual_mask;
        int expected_count = 0;
        for (std::size_t pixel = kRow + 3U; pixel <= kRow + static_cast<std::size_t>(last);
             ++pixel) {
          auto& byte = expected_mask[mask_offset + (pixel >> 3U)];
          const std::uint8_t bit = static_cast<std::uint8_t>(1U << (pixel & 7U));
          if ((byte & bit) == 0U) {
            expected_pixels[pixel_offset + pixel] = 0x07E0U;
            byte = static_cast<std::uint8_t>(byte | bit);
            ++expected_count;
          }
        }
        const vector_v2::RasterSurface surface{
            .zoom = vector_v2::ZoomLevel::k100Percent,
            .level_bounds = {0, 0, kWidth, 2},
            .pixels = std::span(actual_pixels).subspan(pixel_offset, kFootprint),
            .stride = kStride,
        };
        const auto mask = std::span(actual_mask).subspan(mask_offset, kMaskBytes);
        const int actual_count = vector_v2::raster_internal::paint_masked_exact_span(
            3, last, kRow, 0x07E0U, surface, mask);
        passed = passed && actual_count == expected_count && actual_pixels == expected_pixels &&
                 actual_mask == expected_mask;
        ++cases;
      }
    }
  }
  return passed;
}

}  // namespace

bool run_native_kernel_gate() {
  alignas(16) std::array<std::uint16_t, 400> source{};
  alignas(16) std::array<std::uint16_t, 408> destination{};
  for (std::size_t index = 0; index < source.size(); ++index) {
    source[index] = static_cast<std::uint16_t>(index * 257U + 0x1234U);
  }

  bool passed = run_panel_staging_ab_benchmark();
  passed = run_masked_exact_span_ab_benchmark() && passed;
  std::size_t panel_cases = 0;
  // Pixel offsets 0..15 exercise every naturally aligned source/destination
  // byte phase twice. Every width covers scalar-only, aligned PIE, arbitrary
  // USAR/SRC, and scalar-tail boundaries with guards on both output sides.
  for (int source_phase = 0; source_phase < 16; ++source_phase) {
    for (int destination_phase = 0; destination_phase < 16; ++destination_phase) {
      for (int width = 0; width <= 368; ++width) {
        destination.fill(0xDEADU);
        vector_v2::stage_pixels_swapped(source.data() + source_phase,
                                        destination.data() + destination_phase, width);
        for (int column = 0; column < width; ++column) {
          passed =
              passed && destination[static_cast<std::size_t>(destination_phase + column)] ==
                            byte_swapped(source[static_cast<std::size_t>(source_phase + column)]);
        }
        passed =
            passed && destination[static_cast<std::size_t>(destination_phase + width)] == 0xDEADU;
        if (destination_phase != 0) {
          passed =
              passed && destination[static_cast<std::size_t>(destination_phase - 1)] == 0xDEADU;
        }
        ++panel_cases;
      }
    }
  }

  std::size_t tile_publish_cases = 0;
#if !defined(TINYDRAW_QEMU)
  bool tile_publish_passed = true;
  std::size_t tile_first_bad_source_phase = 0;
  std::size_t tile_first_bad_destination_phase = 0;
  std::size_t tile_first_bad_index = 0;
  std::uint16_t tile_first_bad_expected = 0;
  std::uint16_t tile_first_bad_actual = 0;
  constexpr std::size_t kTileSourceStride = 128U;
  constexpr std::size_t kTileSourcePixels = kTileSourceStride * vector_v2::kTileHeight;
  auto tile_source = std::unique_ptr<std::uint16_t, decltype(&heap_caps_free)>{
      static_cast<std::uint16_t*>(
          heap_caps_aligned_alloc(16U, (kTileSourcePixels + 8U) * sizeof(std::uint16_t),
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
      &heap_caps_free};
  auto tile_destination = std::unique_ptr<std::uint16_t, decltype(&heap_caps_free)>{
      static_cast<std::uint16_t*>(heap_caps_aligned_alloc(
          16U, (vector_v2::kTilePixels + 16U) * sizeof(std::uint16_t), kExternalCaps)),
      &heap_caps_free};
  tile_publish_passed = tile_source != nullptr && tile_destination != nullptr;
  if (tile_source != nullptr && tile_destination != nullptr) {
    tile_publish_passed =
        tile_publish_passed && ((reinterpret_cast<std::uintptr_t>(tile_source.get()) |
                                 reinterpret_cast<std::uintptr_t>(tile_destination.get())) &
                                0x0FU) == 0U;
    for (std::size_t index = 0; index < kTileSourcePixels + 8U; ++index) {
      tile_source.get()[index] = static_cast<std::uint16_t>(index * 313U + 0x2781U);
    }
    for (std::size_t source_phase = 0; source_phase < 8U; ++source_phase) {
      for (std::size_t destination_phase = 0; destination_phase < 8U; ++destination_phase) {
        std::fill_n(tile_destination.get(), vector_v2::kTilePixels + 16U, 0xDEADU);
        tinydraw_gate_copy_publication_rows(tile_source.get() + source_phase, kTileSourceStride,
                                            tile_destination.get() + destination_phase,
                                            vector_v2::kTileWidth, vector_v2::kTileHeight);
        for (int row = 0; row < vector_v2::kTileHeight; ++row) {
          for (int column = 0; column < vector_v2::kTileWidth; ++column) {
            const std::size_t source_index = source_phase +
                                             static_cast<std::size_t>(row) * kTileSourceStride +
                                             static_cast<std::size_t>(column);
            const std::size_t destination_index =
                destination_phase + static_cast<std::size_t>(row) * vector_v2::kTileWidth +
                static_cast<std::size_t>(column);
            const std::uint16_t expected = tile_source.get()[source_index];
            const std::uint16_t actual = tile_destination.get()[destination_index];
            if (tile_publish_passed && actual != expected) {
              tile_first_bad_source_phase = source_phase;
              tile_first_bad_destination_phase = destination_phase;
              tile_first_bad_index = destination_index;
              tile_first_bad_expected = expected;
              tile_first_bad_actual = actual;
            }
            tile_publish_passed = tile_publish_passed && actual == expected;
          }
        }
        tile_publish_passed =
            tile_publish_passed &&
            tile_destination.get()[destination_phase + vector_v2::kTilePixels] == 0xDEADU;
        if (destination_phase != 0U) {
          tile_publish_passed =
              tile_publish_passed && tile_destination.get()[destination_phase - 1U] == 0xDEADU;
        }
        ++tile_publish_cases;
      }
    }
  }
  std::printf(
      "TINYDRAW_GATE1_TILE_PUBLISH pass=%u first_source_phase=%lu "
      "first_destination_phase=%lu first_index=%lu expected=%04x actual=%04x\n",
      tile_publish_passed, static_cast<unsigned long>(tile_first_bad_source_phase),
      static_cast<unsigned long>(tile_first_bad_destination_phase),
      static_cast<unsigned long>(tile_first_bad_index), tile_first_bad_expected,
      tile_first_bad_actual);
  passed = passed && tile_publish_passed;
#endif

  constexpr int kRingWidth = 368;
  constexpr int kRingHeight = 5;
  constexpr int kStagedRows = kRingHeight;
  constexpr std::size_t kAlignmentPaddingPixels = 7U;
  auto ring_storage =
      allocate_external<std::uint16_t>(kRingWidth * kRingHeight + kAlignmentPaddingPixels);
  auto staged_storage =
      allocate_external<std::uint16_t>(kRingWidth * kStagedRows + 1U + kAlignmentPaddingPixels);
  passed = passed && ring_storage != nullptr && staged_storage != nullptr;
  if (ring_storage != nullptr && staged_storage != nullptr) {
    const auto align_16 = [](std::uint16_t* pointer) {
      const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(pointer);
      return reinterpret_cast<std::uint16_t*>((address + 15U) & ~std::uintptr_t{15U});
    };
    std::uint16_t* const ring = align_16(ring_storage.get());
    std::uint16_t* const staged = align_16(staged_storage.get());
    // A passing gate must exercise the assembly branch, not silently take its
    // pointer-phase fallback.
    passed = passed && (reinterpret_cast<std::uintptr_t>(ring) & 0x0FU) == 0U &&
             (reinterpret_cast<std::uintptr_t>(staged) & 0x0FU) == 0U;
    for (std::size_t index = 0; index < kRingWidth * kRingHeight; ++index) {
      ring[index] = static_cast<std::uint16_t>(index * 251U + 0x4A31U);
    }
    for (int first_row = 0; first_row < kRingHeight; ++first_row) {
      for (int rows = 1; rows <= kRingHeight; ++rows) {
        // Exhaust every horizontal phase and every possible vertical wrap.
        for (int shift_x = 0; shift_x < kRingWidth; ++shift_x) {
          std::fill_n(staged, kRingWidth * kStagedRows + 1U, 0xDEADU);
          bool case_passed = vector_v2::stage_full_ring_rows_swapped(
              ring, kRingWidth, first_row, rows, kRingHeight, shift_x, staged, kRingWidth);
          for (int row = 0; row < rows; ++row) {
            const int source_row = (first_row + row) % kRingHeight;
            for (int column = 0; column < kRingWidth; ++column) {
              const std::size_t source_index =
                  static_cast<std::size_t>(source_row) * kRingWidth +
                  static_cast<std::size_t>((column + shift_x) % kRingWidth);
              const std::size_t destination_index =
                  static_cast<std::size_t>(row) * kRingWidth + column;
              const std::uint16_t expected = byte_swapped(ring[source_index]);
              const std::uint16_t actual = staged[destination_index];
              case_passed = case_passed && actual == expected;
            }
          }
          const std::uint16_t guard = staged[static_cast<std::size_t>(rows) * kRingWidth];
          case_passed = case_passed && guard == 0xDEADU;
          passed = passed && case_passed;
          ++panel_cases;
        }
      }
    }
  }

  constexpr std::array<std::size_t, 7> kSurfaceSizes{0, 1, 7, 8, 9, 31, 64};
  constexpr std::array<std::size_t, 8> kMaskSizes{0, 1, 15, 16, 17, 31, 32, 79};
  constexpr std::uint16_t kColor = 0x39E7U;
  alignas(16) std::array<std::uint16_t, 80> surface{};
  alignas(16) std::array<std::uint8_t, 112> mask{};
  std::size_t producer_cases = 0;

  for (std::size_t phase = 0; phase < 8; ++phase) {
    for (const std::size_t size : kSurfaceSizes) {
      surface.fill(0xDEADU);
      mask.fill(0xA5U);
      tinydraw_gate_initialize_producer_buffers(surface.data() + phase, size, mask.data(), 0,
                                                kColor);
      for (std::size_t index = 0; index < size; ++index) {
        passed = passed && surface[phase + index] == kColor;
      }
      passed = passed && surface[phase + size] == 0xDEADU && mask.front() == 0xA5U;
      if (phase != 0) {
        passed = passed && surface[phase - 1U] == 0xDEADU;
      }
      ++producer_cases;
    }
  }
  for (std::size_t phase = 0; phase < 16; ++phase) {
    for (const std::size_t size : kMaskSizes) {
      surface.fill(0xDEADU);
      mask.fill(0xA5U);
      tinydraw_gate_initialize_producer_buffers(surface.data(), 0, mask.data() + phase, size,
                                                kColor);
      for (std::size_t index = 0; index < size; ++index) {
        passed = passed && mask[phase + index] == 0U;
      }
      passed = passed && mask[phase + size] == 0xA5U && surface.front() == 0xDEADU;
      if (phase != 0) {
        passed = passed && mask[phase - 1U] == 0xA5U;
      }
      ++producer_cases;
    }
  }
  for (std::size_t surface_phase = 0; surface_phase < 8; ++surface_phase) {
    for (std::size_t mask_phase = 0; mask_phase < 16; ++mask_phase) {
      for (const std::size_t surface_size : kSurfaceSizes) {
        for (const std::size_t mask_size : kMaskSizes) {
          surface.fill(0xDEADU);
          mask.fill(0xA5U);
          tinydraw_gate_initialize_producer_buffers(surface.data() + surface_phase, surface_size,
                                                    mask.data() + mask_phase, mask_size, kColor);
          for (std::size_t index = 0; index < surface_size; ++index) {
            passed = passed && surface[surface_phase + index] == kColor;
          }
          for (std::size_t index = 0; index < mask_size; ++index) {
            passed = passed && mask[mask_phase + index] == 0U;
          }
          passed = passed && surface[surface_phase + surface_size] == 0xDEADU &&
                   mask[mask_phase + mask_size] == 0xA5U;
          if (surface_phase != 0U) {
            passed = passed && surface[surface_phase - 1U] == 0xDEADU;
          }
          if (mask_phase != 0U) {
            passed = passed && mask[mask_phase - 1U] == 0xA5U;
          }
          ++producer_cases;
        }
      }
    }
  }

  // Exercise the generic copy/fill kernels from PSRAM to PSRAM. Non-power-of-
  // two strides change the 16-byte phase on every row, covering the aligned
  // vector body, scalar prefix/tail, and differing-phase scalar body.
  constexpr int kPixelMemoryRows = 3;
  constexpr int kPixelMemoryStorage = 192;
  auto memory_source = allocate_external<std::uint16_t>(kPixelMemoryStorage);
  auto memory_destination = allocate_external<std::uint16_t>(kPixelMemoryStorage);
  std::size_t pixel_copy_cases = 0;
  std::size_t pixel_fill_cases = 0;
  bool pixel_memory_passed = memory_source != nullptr && memory_destination != nullptr;
  if (memory_source != nullptr && memory_destination != nullptr) {
    for (int index = 0; index < kPixelMemoryStorage; ++index) {
      memory_source.get()[index] = static_cast<std::uint16_t>(index * 313U + 0x1957U);
    }
    for (int source_phase = 0; source_phase < 8; ++source_phase) {
      for (int destination_phase = 0; destination_phase < 8; ++destination_phase) {
        for (const int width : {1, 7, 8, 9, 16, 31}) {
          for (const int source_stride : {31, 32, 37}) {
            for (const int destination_stride : {31, 32, 41}) {
              if (source_stride < width || destination_stride < width) {
                continue;
              }
              std::fill_n(memory_destination.get(), kPixelMemoryStorage, 0xDEADU);
              bool case_passed = vector_v2::copy_pixel_rows_nonoverlapping(
                  memory_source.get() + source_phase, source_stride,
                  memory_destination.get() + destination_phase, destination_stride, width,
                  kPixelMemoryRows);
              for (int row = 0; row < kPixelMemoryRows; ++row) {
                for (int column = 0; column < width; ++column) {
                  case_passed =
                      case_passed &&
                      memory_destination
                              .get()[destination_phase + row * destination_stride + column] ==
                          memory_source.get()[source_phase + row * source_stride + column];
                }
                if (destination_stride > width) {
                  case_passed =
                      case_passed &&
                      memory_destination
                              .get()[destination_phase + row * destination_stride + width] ==
                          0xDEADU;
                }
              }
              if (destination_phase != 0) {
                case_passed =
                    case_passed && memory_destination.get()[destination_phase - 1] == 0xDEADU;
              }
              pixel_memory_passed = pixel_memory_passed && case_passed;
              ++pixel_copy_cases;
            }
          }
        }
      }
    }
    constexpr std::uint16_t kPixelMemoryColor = 0x39E7U;
    for (int phase = 0; phase < 8; ++phase) {
      for (const int width : {1, 7, 8, 9, 16, 31}) {
        for (const int stride : {31, 32, 41}) {
          if (stride < width) {
            continue;
          }
          std::fill_n(memory_destination.get(), kPixelMemoryStorage, 0xDEADU);
          bool case_passed = vector_v2::fill_pixel_rows(memory_destination.get() + phase, stride,
                                                        width, kPixelMemoryRows, kPixelMemoryColor);
          for (int row = 0; row < kPixelMemoryRows; ++row) {
            for (int column = 0; column < width; ++column) {
              case_passed =
                  case_passed &&
                  memory_destination.get()[phase + row * stride + column] == kPixelMemoryColor;
            }
            if (stride > width) {
              case_passed =
                  case_passed && memory_destination.get()[phase + row * stride + width] == 0xDEADU;
            }
          }
          if (phase != 0) {
            case_passed = case_passed && memory_destination.get()[phase - 1] == 0xDEADU;
          }
          pixel_memory_passed = pixel_memory_passed && case_passed;
          ++pixel_fill_cases;
        }
      }
    }
    const auto overlap_before = memory_destination.get()[1];
    pixel_memory_passed =
        pixel_memory_passed &&
        !vector_v2::copy_pixel_rows_nonoverlapping(memory_destination.get(), 32,
                                                   memory_destination.get() + 1, 32, 16, 2) &&
        memory_destination.get()[1] == overlap_before;
  }
  passed = passed && pixel_memory_passed;

  std::size_t tile_uniform_cases = 0;
  const bool tile_uniform_passed = run_tile_uniform_kernel_gate(tile_uniform_cases);
  passed = passed && tile_uniform_passed;

  std::size_t composite_cases = 0;
  const bool composite_passed = run_settled_composite_kernel_gate(composite_cases);
  passed = passed && composite_passed;

  std::size_t masked_span_cases = 0;
  const bool masked_span_passed = run_masked_exact_span_kernel_gate(masked_span_cases);
  passed = passed && masked_span_passed;

  std::printf(
      "TINYDRAW_GATE1_NATIVE_KERNELS panel_cases=%lu tile_publish_cases=%lu producer_cases=%lu "
      "pixel_copy_cases=%lu pixel_fill_cases=%lu tile_uniform_cases=%lu tile_uniform_pass=%u "
      "composite_cases=%lu composite_pass=%u masked_span_cases=%lu masked_span_pass=%u pass=%u\n",
      static_cast<unsigned long>(panel_cases), static_cast<unsigned long>(tile_publish_cases),
      static_cast<unsigned long>(producer_cases), static_cast<unsigned long>(pixel_copy_cases),
      static_cast<unsigned long>(pixel_fill_cases), static_cast<unsigned long>(tile_uniform_cases),
      tile_uniform_passed, static_cast<unsigned long>(composite_cases), composite_passed,
      static_cast<unsigned long>(masked_span_cases), masked_span_passed, passed);
  std::fflush(stdout);
  return passed;
}

}  // namespace tinydraw::esp32::gate_harness
