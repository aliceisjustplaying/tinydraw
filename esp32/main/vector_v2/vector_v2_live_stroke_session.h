#ifndef TINYDRAW_ESP32_VECTOR_V2_LIVE_STROKE_SESSION_H
#define TINYDRAW_ESP32_VECTOR_V2_LIVE_STROKE_SESSION_H

#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/vector_v2/operation_builder.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "vector_v2_presenter.h"

namespace tinydraw::esp32 {

struct LiveStrokeMetrics {
  std::uint64_t submit_total_us = 0;
  std::uint64_t complete_total_us = 0;
  std::uint32_t samples = 0;
  std::uint32_t submit_max_us = 0;
  std::uint32_t complete_max_us = 0;
  std::uint32_t submit_over_16ms = 0;
  std::uint32_t complete_over_33ms = 0;
  std::uint32_t failures = 0;

  void include(const LivePresentationTiming& timing);
};

struct LiveStrokeStartResult {
  LivePresentationTiming presentation{};
  bool accepted = false;
};

struct LiveStrokeMoveResult {
  LivePresentationTiming presentation{};
  LivePresentationTiming rejection_refresh{};
  std::uint32_t geometry_us = 0;
  bool presented = false;
  bool rejected = false;
};

struct LiveStrokeFinishResult {
  std::size_t first_operation = 0;
  std::optional<vector_v2::PixelRect> world_bounds;
  LivePresentationTiming preview{};
  LivePresentationTiming refresh{};
  LiveStrokeMetrics metrics{};
  std::int64_t finish_preview_us = 0;
  std::int64_t builder_finish_us = 0;
  std::int64_t append_us = 0;
  std::int64_t refresh_wall_us = 0;
  bool committed = false;
  bool commit_failed = false;
};

// Owns one complete live-ink gesture, from filtered screen samples through
// provisional presentation and one atomic authority publication. Navigation,
// chrome actions, autosave, and idle drain remain app-level concerns.
class LiveStrokeSession {
 public:
  LiveStrokeSession(std::span<vector_v2::CompactOperationSample> builder_storage,
                    vector_v2::OperationLog& log, VectorV2Presenter& presenter);

  [[nodiscard]] bool ready() const { return builder_.ready(); }
  [[nodiscard]] bool active() const { return ink_.active(); }

  [[nodiscard]] LiveStrokeStartResult begin(Point screen_point, std::uint32_t event_us,
                                            float brush_size, vector_v2::OperationTool tool,
                                            std::uint16_t color, std::uint16_t gesture_id,
                                            const vector_v2::ChromeState& chrome,
                                            std::uint32_t event_sequence = 0U);
  [[nodiscard]] LiveStrokeMoveResult move(Point screen_point, std::uint32_t event_us,
                                          const vector_v2::ChromeState& chrome,
                                          std::uint32_t event_sequence = 0U);
  [[nodiscard]] LiveStrokeFinishResult finish(std::uint32_t event_us,
                                              const vector_v2::ChromeState& chrome,
                                              std::uint32_t event_sequence = 0U);

 private:
  [[nodiscard]] bool commit_operation(const vector_v2::BuiltOperation& operation);
  void reset_stroke_stats();

  vector_v2::OperationLog& log_;
  VectorV2Presenter& presenter_;
  vector_v2::OperationBuilder builder_;
  InkStream ink_;
  CurvedRibbonStream ribbon_;
  Point start_touch_{};
  Point previous_touch_{};
  Point last_touch_{};
  Point last_canvas_touch_{};
  InkPoint last_ink_{};
  std::uint32_t previous_touch_us_ = 0;
  std::uint32_t last_touch_us_ = 0;
  std::uint16_t color_ = 0;
  std::size_t first_operation_ = 0;
  std::optional<vector_v2::PixelRect> world_bounds_;
  LiveStrokeMetrics metrics_{};
  std::int64_t append_us_ = 0;
  bool commit_failed_ = false;
};

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_LIVE_STROKE_SESSION_H
