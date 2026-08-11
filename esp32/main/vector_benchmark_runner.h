#pragma once

#include <cstdint>
#include <span>

namespace tinydraw::esp32 {

void run_vector_benchmarks(std::span<std::uint16_t> destination);

}  // namespace tinydraw::esp32
