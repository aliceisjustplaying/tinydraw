#pragma once

#include <cstdint>

namespace tinydraw::vector_v2::raster_census_tool {

int run_fuzz_collinear(std::uint32_t cases, std::uint32_t seed);
int run_fuzz_docs(std::uint32_t cases, std::uint32_t seed);

}  // namespace tinydraw::vector_v2::raster_census_tool
