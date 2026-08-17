#ifndef TINYDRAW_ESP32_VECTOR_V2_BACKGROUND_PIPELINE_H
#define TINYDRAW_ESP32_VECTOR_V2_BACKGROUND_PIPELINE_H

#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/chrome.h"
#include "tinydraw/vector_v2/idle_repair.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/settled_tile.h"
#include "tinydraw/vector_v2/tile_producer.h"

namespace tinydraw::esp32 {

class VectorV2Presenter;

enum class BackgroundDrainBoundary : std::uint8_t {
  kPan,
  kHistory,
};

struct BackgroundSliceInput {
  std::uint32_t loop_us = 0;
  bool pressed = false;
  bool panning = false;
  bool sample_ready = false;
  bool lift_report_pending = false;
};

struct BackgroundSliceResult {
  bool fill_busy = false;
  bool drain_completed = false;
};

// Owns every quiet-time document transition. Callers report gesture outcomes
// and run one bounded slice; fill, repair, settle, and committed-overlay drain
// ordering stays private to this module.
class VectorV2BackgroundPipeline {
 public:
  VectorV2BackgroundPipeline(vector_v2::OperationLog& log, vector_v2::MaterializedCanvas& canvas,
                             vector_v2::TileProducer& producer,
                             vector_v2::InPlaceAppendWorkspace append_workspace,
                             vector_v2::SettledTileWorkspace settle_workspace,
                             std::span<std::uint16_t> settle_pixels, VectorV2Presenter& presenter,
                             vector_v2::ChromeState& chrome);

  void note_committed_bounds(vector_v2::PixelRect world_bounds);
  void mark_history_controls_dirty();
  void history_controls_presented();
  void reset_document_state();
  [[nodiscard]] bool drain_boundary(BackgroundDrainBoundary boundary);
  [[nodiscard]] BackgroundSliceResult run_slice(const BackgroundSliceInput& input);

 private:
  struct PendingFillPresentation {
    vector_v2::PixelRect level_bounds{};
    vector_v2::ZoomLevel zoom = vector_v2::ZoomLevel::k25Percent;
    int x = 0;
    int y = 0;
    bool pending = false;
  };

  struct FillTiming {
    std::int64_t started_us = 0;
    std::uint32_t steps = 0;
    std::int64_t compute_total_us = 0;
    std::int64_t compute_max_us = 0;
    std::int64_t present_total_us = 0;
    std::int64_t present_max_us = 0;
    std::int64_t tick_max_us = 0;
    std::uint32_t producer_failures = 0;
    std::uint32_t presentation_failures = 0;
  };

  void reset_settle_fingerprint();
  void reset_settle_pass();
  void print_fill(const char* result) const;
  void run_fill(const vector_v2::ViewRequest& view);
  void run_repair(const vector_v2::ViewRequest& view);
  void run_settle(std::uint32_t loop_us);

  vector_v2::OperationLog& log_;
  vector_v2::MaterializedCanvas& canvas_;
  vector_v2::TileProducer& producer_;
  vector_v2::InPlaceAppendWorkspace append_workspace_;
  vector_v2::SettledTileWorkspace settle_workspace_;
  std::span<std::uint16_t> settle_pixels_;
  VectorV2Presenter& presenter_;
  vector_v2::ChromeState& chrome_;

  vector_v2::ZoomLevel fill_zoom_ = vector_v2::ZoomLevel::k25Percent;
  int fill_x_ = 0;
  int fill_y_ = 0;
  vector_v2::DocumentRevision fill_revision_{};
  bool fill_complete_ = true;
  FillTiming fill_timing_{};
  bool fill_measurement_active_ = false;
  PendingFillPresentation pending_fill_{};

  vector_v2::IdleRepairPlan repair_plan_{};
  std::size_t repair_cursor_ = 0;
  std::size_t repair_steps_ = 0;
  bool repair_planned_ = false;

  std::size_t settle_cursor_ = 0;
  bool settle_complete_ = false;
  std::uint32_t settle_tiles_ = 0;
  std::int64_t settle_total_us_ = 0;
  std::int64_t settle_max_us_ = 0;
  std::uint32_t settle_failures_ = 0;
  vector_v2::DocumentRevision settle_revision_{};
  vector_v2::ZoomLevel settle_zoom_ = vector_v2::ZoomLevel::k25Percent;
  int settle_x_ = -1;
  int settle_y_ = -1;

  std::optional<vector_v2::PixelRect> drain_swap_world_;
  std::uint32_t drain_operations_ = 0;
  std::int64_t drain_total_us_ = 0;
  std::int64_t drain_max_us_ = 0;
  std::uint32_t drain_failures_ = 0;
  bool history_controls_dirty_ = false;
};

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_BACKGROUND_PIPELINE_H
