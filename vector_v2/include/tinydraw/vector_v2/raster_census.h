#ifndef TINYDRAW_VECTOR_V2_RASTER_CENSUS_H
#define TINYDRAW_VECTOR_V2_RASTER_CENSUS_H

// Raster census instrumentation. Counters are compiled out unless
// TINYDRAW_VECTOR_V2_RASTER_CENSUS is defined; production builds pay nothing.
// The census tool and hardware receipts use these to attribute cold-replay
// cost to span visits, mask skips, predicate calls, and producer rescans.

#include <cstdint>

namespace tinydraw::vector_v2 {

struct RasterCensus {
  // Producer-level culling.
  std::uint64_t operations_rendered = 0;
  std::uint64_t operations_bbox_rejected = 0;
  std::uint64_t segments_painted = 0;
  std::uint64_t segments_bbox_rejected = 0;
  std::uint64_t segments_saturation_skipped = 0;    // O(1) saturated-row-range skips
  std::uint64_t operations_saturation_skipped = 0;  // whole ops skipped via saturation
  std::uint64_t groups_saturated_early = 0;         // groups completed before oldest op
  // Masked tapered path.
  std::uint64_t rows_scanned = 0;       // rows entering span computation
  std::uint64_t rows_prefinalized = 0;  // rows skipped by mask byte scan
  std::uint64_t rows_empty_span = 0;    // conservative span rejected the row
  std::uint64_t span_pixels = 0;        // sum of conservative span widths (visits)
  std::uint64_t mask_skips = 0;         // finalized pixels skipped before the predicate
  std::uint64_t covers_calls = 0;       // covers_pixel evaluations (tapered masked path)
  std::uint64_t covers_hits = 0;        // predicate true -> pixel written + finalized
  // Masked constant-radius path.
  std::uint64_t const_span_pixels = 0;
  std::uint64_t const_mask_skips = 0;
  std::uint64_t const_rows_scanned = 0;  // rows entering the span search
  std::uint64_t const_search_calls = 0;  // covers_pixel evaluations in span search
  std::uint64_t const_search_last_calls = 0;  // …of which in the right-edge search
  std::uint64_t const_rows_probed_empty = 0;  // rows probed end to end without coverage
  // produce_next overhead.
  std::uint64_t remaining_scans = 0;
  std::uint64_t remaining_scan_ns = 0;
  // Coarse produce_next phase attribution in census ticks (CPU cycles on
  // Xtensa, steady-clock nanoseconds elsewhere).
  std::uint64_t gate_ticks = 0;     // operation fetch/bounds/saturation gating
  std::uint64_t setup_ticks = 0;    // curve-unit counting, bounds, clipping
  std::uint64_t paint_ticks = 0;    // masked curve-step application
  std::uint64_t publish_ticks = 0;  // tile packing and canvas publication

  void reset() { *this = {}; }
};

#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
#if defined(__XTENSA__)
inline std::uint32_t raster_census_now() {
  std::uint32_t cycles = 0;
  asm volatile("rsr.ccount %0" : "=a"(cycles));
  return cycles;
}
#else
std::uint32_t raster_census_now();
#endif
#endif

#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
extern RasterCensus g_raster_census;
#define TINYDRAW_V2_CENSUS_ADD(field, amount) \
  (::tinydraw::vector_v2::g_raster_census.field += (amount))
#else
#define TINYDRAW_V2_CENSUS_ADD(field, amount) ((void)0)
#endif

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_RASTER_CENSUS_H
