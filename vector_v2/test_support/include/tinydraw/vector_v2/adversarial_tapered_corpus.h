#ifndef TINYDRAW_VECTOR_V2_ADVERSARIAL_TAPERED_CORPUS_H
#define TINYDRAW_VECTOR_V2_ADVERSARIAL_TAPERED_CORPUS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "tinydraw/vector_v2/operation.h"

namespace tinydraw::vector_v2::test_support {

inline constexpr std::size_t kAdversarialTaperedOperationCount = 128;
inline constexpr std::size_t kAdversarialTaperedSamplesPerOperation = 32;
inline constexpr std::size_t kAdversarialTaperedSampleCount =
    kAdversarialTaperedOperationCount * kAdversarialTaperedSamplesPerOperation;

struct AdversarialTaperedCorpusStats {
  std::size_t operations = 0;
  std::size_t samples = 0;
  std::size_t erasers = 0;
};

// Emits a deterministic pressure-varying knot concentrated in the upper-left
// 400% viewport. Its 4,096 samples are slightly more than four times the dense
// physical corpus that exposed the constant-radius benchmark blind spot.
// Every neighboring sample changes radius, preventing the constant-radius
// raster fast path from hiding tapered replay cost.
template <typename AppendOperation>
bool emit_adversarial_tapered_corpus(
    AppendOperation&& append_operation, AdversarialTaperedCorpusStats* output = nullptr,
    std::size_t operation_count = kAdversarialTaperedOperationCount,
    std::size_t samples_per_operation = kAdversarialTaperedSamplesPerOperation) {
  if (operation_count > kAdversarialTaperedOperationCount || samples_per_operation < 2U ||
      samples_per_operation > kAdversarialTaperedSamplesPerOperation) {
    return false;
  }
  AdversarialTaperedCorpusStats stats{};
  std::array<CompactOperationSample, kAdversarialTaperedSamplesPerOperation> samples{};
  for (std::size_t operation = 0; operation < operation_count; ++operation) {
    for (std::size_t index = 0; index < samples.size(); ++index) {
      const std::size_t x_phase = (index * 11U + operation * 17U) % 80U;
      const std::size_t y_phase = (index * 19U + operation * 13U) % 80U;
      const std::size_t x_triangle = x_phase <= 40U ? x_phase : 80U - x_phase;
      const std::size_t y_triangle = y_phase <= 40U ? y_phase : 80U - y_phase;
      const std::size_t radius_step = (index * 5U + operation * 3U) % 7U;
      samples[index] = {
          .x_quarter = static_cast<std::uint16_t>((20U + x_triangle * 2U) * 16U),
          .y_quarter = static_cast<std::uint16_t>((20U + y_triangle * 2U) * 16U),
          .radius_256 = static_cast<std::uint16_t>((3U + radius_step) * 256U + 64U),
          .elapsed_ms = static_cast<std::uint16_t>(index * 8U),
      };
    }
    const bool eraser = operation % 10U == 9U;
    if (!append_operation(OperationAppend{
            .tool = eraser ? OperationTool::kEraser : OperationTool::kPen,
            .color = static_cast<std::uint16_t>(0x001FU + (operation * 977U) % 0xD000U),
            .samples = std::span(samples).first(samples_per_operation),
        })) {
      return false;
    }
    ++stats.operations;
    stats.samples += samples_per_operation;
    stats.erasers += eraser;
  }
  if (output != nullptr) {
    *output = stats;
  }
  return true;
}

}  // namespace tinydraw::vector_v2::test_support

#endif  // TINYDRAW_VECTOR_V2_ADVERSARIAL_TAPERED_CORPUS_H
