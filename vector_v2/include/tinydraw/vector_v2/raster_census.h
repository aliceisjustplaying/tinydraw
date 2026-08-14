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
  // Masked constant-radius path (span search predicate calls not counted).
  std::uint64_t const_span_pixels = 0;
  std::uint64_t const_mask_skips = 0;
  // produce_next overhead.
  std::uint64_t remaining_scans = 0;
  std::uint64_t remaining_scan_ns = 0;

  void reset() { *this = {}; }
};

#if defined(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
extern RasterCensus g_raster_census;
#define TINYDRAW_V2_CENSUS_ADD(field, amount) \
  (::tinydraw::vector_v2::g_raster_census.field += (amount))
#else
#define TINYDRAW_V2_CENSUS_ADD(field, amount) ((void)0)
#endif

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_RASTER_CENSUS_H
