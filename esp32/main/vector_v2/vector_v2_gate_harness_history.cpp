#include "vector_v2_gate_harness_internal.h"

namespace tinydraw::esp32::gate_harness {

namespace {

// One deterministic stroke: a chained multi-chunk gesture committed through
// the same authority-only append + absorption path the product uses, leaving
// log and canvas revisions equal (the precondition for history moves).
// Every stroke is a billiard path: the point advances by (velocity_x,
// velocity_y) world pixels per sample and reflects at the region walls.
// Steep varied angles produce the owner's "evil hairline" field — many thin
// strokes densely crossing one another — while |vx| >> |vy| produces the
// familiar broad horizontal sweep bands.
struct HistoryStrokeSpec {
  OperationTool tool;
  std::uint16_t color;
  float radius;
  float start_x;
  float start_y;
  float velocity_x;
  float velocity_y;
  std::size_t samples;
};

// The corpus lives inside the 400% viewport at level origin (0,0):
// world x in [0, 92], y in [0, 112]. The same world region is fully visible
// at 200% from the same origin.
constexpr float kStrokeMinX = 6.0F;
constexpr float kStrokeMaxX = 86.0F;
constexpr float kStrokeMinY = 8.0F;
constexpr float kStrokeMaxY = 104.0F;

bool append_history_stroke(OperationLog& log, MaterializedCanvas& canvas,
                           const InPlaceAppendWorkspace& workspace,
                           const vector_v2::ViewRequest& view,
                           std::span<CompactOperationSample> builder_storage,
                           std::uint16_t gesture_id, const HistoryStrokeSpec& spec) {
  vector_v2::ChainedOperationBuilder builder(builder_storage, kInteractiveChunkSampleLimit);
  const auto commit_ready = [&](vector_v2::ChainedOperationStatus status) -> bool {
    while (status == vector_v2::ChainedOperationStatus::kChunkReady ||
           status == vector_v2::ChainedOperationStatus::kFinalChunkReady) {
      const auto pending = builder.pending_append();
      if (!pending.has_value() ||
          !append_and_absorb(log, canvas, *pending, workspace, view,
                             {.now_us = &esp_timer_get_time, .budget_us = kIdleAbsorbBudgetUs})
               .has_value()) {
        return false;
      }
      status = builder.acknowledge_commit();
    }
    return status == vector_v2::ChainedOperationStatus::kAccepted ||
           status == vector_v2::ChainedOperationStatus::kComplete;
  };

  float x = spec.start_x;
  float y = spec.start_y;
  float velocity_x = spec.velocity_x;
  float velocity_y = spec.velocity_y;
  std::uint32_t timestamp_us = now_us();
  if (!builder.begin(spec.tool, spec.color, gesture_id,
                     {.world_x = x, .world_y = y, .radius = spec.radius,
                      .timestamp_us = timestamp_us})) {
    return false;
  }
  for (std::size_t index = 1; index < spec.samples; ++index) {
    x += velocity_x;
    y += velocity_y;
    if (x > kStrokeMaxX || x < kStrokeMinX) {
      velocity_x = -velocity_x;
      x = std::clamp(x, kStrokeMinX, kStrokeMaxX);
    }
    if (y > kStrokeMaxY || y < kStrokeMinY) {
      velocity_y = -velocity_y;
      y = std::clamp(y, kStrokeMinY, kStrokeMaxY);
    }
    timestamp_us += 8'000U;
    const vector_v2::OperationPoint point{
        .world_x = x, .world_y = y, .radius = spec.radius, .timestamp_us = timestamp_us};
    const bool final_sample = index + 1U == spec.samples;
    if (!commit_ready(final_sample ? builder.finish(point) : builder.add(point))) {
      return false;
    }
  }
  return !builder.active();
}

bool fill_history_view(vector_v2::TileProducer& producer, const vector_v2::ViewRequest& view,
                       std::size_t& steps, std::size_t& tiles, std::size_t& scanned,
                       std::size_t& rendered) {
  while (true) {
    const auto step = producer.produce_next(view);
    if (!step.has_value()) {
      return false;
    }
    ++steps;
    tiles += step->tiles_published;
    scanned += step->operations_scanned;
    rendered += step->operations_rendered;
    if (step->complete) {
      return true;
    }
  }
}

struct HistoryMoveMeasurement {
  std::int64_t move_us = 0;
  std::int64_t first_us = 0;
  std::int64_t repair_us = 0;
  std::int64_t max_repair_tick_us = 0;
  std::size_t steps = 0;
  std::size_t tiles = 0;
  std::size_t scanned = 0;
  std::size_t rendered = 0;
  std::size_t presents = 0;
  vector_v2::PixelRect affected_world{};
};

enum class HistoryPresentPolicy : std::uint8_t {
  kPerPublication,  // legacy: one region present per producer publication
  kHoldback,        // product treatment: one union present at completion
};

// Mirrors the product transition exactly: cancel outstanding producer work,
// move authority across one whole Stroke, present the damaged region (the
// user-visible overview-fallback moment), then drive the producer with
// per-publication presentations until the affected detail is exact — the
// same block-by-block repair the owner sees on glass.
bool measure_history_move(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                          OperationLog& log, MaterializedCanvas& canvas,
                          const vector_v2::ChromeState& chrome,
                          std::span<std::uint16_t> overview_scratch, ZoomLevel zoom,
                          vector_v2::HistoryDirection direction, HistoryPresentPolicy policy,
                          HistoryMoveMeasurement& measurement) {
  const vector_v2::ViewRequest view{
      .zoom = zoom,
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + vector_v2::kOverviewWidth,
                       presenter.level_y() + vector_v2::kOverviewHeight},
  };
  producer.cancel_pending_work();
  const std::int64_t move_started = esp_timer_get_time();
  const auto change = vector_v2::move_history_incrementally(log, canvas, direction,
                                                            overview_scratch);
  measurement.move_us = esp_timer_get_time() - move_started;
  if (!change.has_value()) {
    return false;
  }
  measurement.affected_world = change->affected_world_bounds;
  const std::int64_t first_started = esp_timer_get_time();
  const auto first = presenter.refresh_region(
      vector_v2::operation_level_bounds(change->affected_world_bounds, zoom), chrome, now_us());
  measurement.first_us = esp_timer_get_time() - first_started;
  if (!first.passed) {
    return false;
  }
  const vector_v2::PixelRect hold_bounds =
      vector_v2::operation_level_bounds(change->affected_world_bounds, zoom);
  const std::int64_t repair_started = esp_timer_get_time();
  bool complete = false;
  vector_v2::PixelRect published_union{};
  bool union_valid = false;
  while (!complete) {
    const std::int64_t tick_started = esp_timer_get_time();
    const auto step = producer.produce_next(view);
    if (!step.has_value()) {
      return false;
    }
    ++measurement.steps;
    measurement.tiles += step->tiles_published;
    measurement.scanned += step->operations_scanned;
    measurement.rendered += step->operations_rendered;
    complete = step->complete;
    if (step->tiles_published != 0U) {
      const bool held = policy == HistoryPresentPolicy::kHoldback &&
                        step->level_bounds.x0 < hold_bounds.x1 &&
                        hold_bounds.x0 < step->level_bounds.x1 &&
                        step->level_bounds.y0 < hold_bounds.y1 &&
                        hold_bounds.y0 < step->level_bounds.y1;
      if (held) {
        if (union_valid) {
          published_union.x0 = std::min(published_union.x0, step->level_bounds.x0);
          published_union.y0 = std::min(published_union.y0, step->level_bounds.y0);
          published_union.x1 = std::max(published_union.x1, step->level_bounds.x1);
          published_union.y1 = std::max(published_union.y1, step->level_bounds.y1);
        } else {
          published_union = step->level_bounds;
          union_valid = true;
        }
      } else {
        if (!presenter.refresh_region(step->level_bounds, chrome).passed) {
          return false;
        }
        ++measurement.presents;
      }
    }
    measurement.max_repair_tick_us =
        std::max(measurement.max_repair_tick_us, esp_timer_get_time() - tick_started);
  }
  if (union_valid) {
    if (!presenter.refresh_region(published_union, chrome).passed) {
      return false;
    }
    ++measurement.presents;
  }
  measurement.repair_us = esp_timer_get_time() - repair_started;
  return log.current_revision() == canvas.current_revision();
}

}  // namespace

// Deterministic high-zoom whole-Stroke Undo/Redo latency baseline
// (V2_ROADMAP §2). Reports, per move: authority+overview replay wall, the
// first fallback presentation wall, and the visible block-by-block repair
// wall. The 520 ms development guard is reported but only mechanical
// failures gate the verdict; thresholds tighten once a treatment exists.
bool run_history_latency_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                              OperationLog& log, MaterializedCanvas& canvas,
                              const vector_v2::ChromeState& chrome,
                              const InPlaceAppendWorkspace& workspace,
                              std::span<const std::uint16_t> blank_snapshot,
                              std::span<CompactOperationSample> builder_storage,
                              std::span<std::uint16_t> overview_scratch) {
  const DocumentRevision baseline{canvas.current_revision().value + 1U};
  if (!vector_v2::restore_document_snapshot(log, canvas, baseline, blank_snapshot) ||
      !producer.reset_uniform_baseline(baseline)) {
    return false;
  }
  if (!presenter.set_view(ZoomLevel::k400Percent, 0, 0, chrome, now_us()).passed) {
    return false;
  }
  const vector_v2::ViewRequest build_view{
      .zoom = ZoomLevel::k400Percent,
      .level_pixels = {presenter.level_x(), presenter.level_y(),
                       presenter.level_x() + vector_v2::kOverviewWidth,
                       presenter.level_y() + vector_v2::kOverviewHeight},
  };
  // Twenty whole Strokes. The base layer is the owner-named "evil
  // hairlines": twelve minimum-radius strokes on varied billiard angles,
  // densely crossing one another across the whole viewport. On top: a
  // scribble stack — erasers crossing the field, an XL band, medium and
  // thin scribbles, and a broad closing scribble. Eight undo levels reach
  // down through the scribble stack into the field, so the gate measures
  // broad damage over dense crossings — the worst replay case — plus
  // eraser and hairline undo itself.
  const std::array<HistoryStrokeSpec, 20> strokes{{
      {OperationTool::kPen, 0x0000U, 0.6F, 6.0F, 8.0F, 3.1F, 1.7F, 128U},
      {OperationTool::kPen, 0x001FU, 0.6F, 86.0F, 8.0F, -2.3F, 2.9F, 128U},
      {OperationTool::kPen, 0xF800U, 0.6F, 6.0F, 104.0F, 1.3F, -3.3F, 128U},
      {OperationTool::kPen, 0x07E0U, 0.6F, 86.0F, 104.0F, -4.0F, -0.9F, 128U},
      {OperationTool::kPen, 0x0000U, 0.6F, 46.0F, 8.0F, -1.1F, 3.7F, 128U},
      {OperationTool::kPen, 0x001FU, 0.6F, 46.0F, 104.0F, 2.7F, -2.7F, 128U},
      {OperationTool::kPen, 0xF800U, 0.6F, 6.0F, 56.0F, 3.7F, 1.1F, 128U},
      {OperationTool::kPen, 0x07E0U, 0.6F, 86.0F, 56.0F, -1.7F, -3.1F, 128U},
      {OperationTool::kPen, 0x0000U, 0.6F, 26.0F, 8.0F, 2.1F, 3.5F, 128U},
      {OperationTool::kPen, 0x001FU, 0.6F, 66.0F, 104.0F, -3.5F, -2.1F, 128U},
      {OperationTool::kPen, 0xF800U, 0.6F, 26.0F, 104.0F, 3.3F, -1.3F, 128U},
      {OperationTool::kPen, 0x07E0U, 0.6F, 66.0F, 8.0F, -0.9F, 4.0F, 128U},
      {OperationTool::kEraser, 0xFFFFU, 4.0F, 6.0F, 36.0F, 3.5F, 1.2F, 96U},
      {OperationTool::kPen, 0x781FU, 5.0F, 6.0F, 20.0F, 4.2F, 0.7F, 96U},
      {OperationTool::kPen, 0xFD20U, 3.0F, 6.0F, 8.0F, 3.8F, 2.1F, 160U},
      {OperationTool::kPen, 0x07FFU, 2.0F, 86.0F, 96.0F, -2.9F, -2.5F, 160U},
      {OperationTool::kPen, 0x0000U, 0.6F, 46.0F, 56.0F, 3.9F, -1.9F, 128U},
      {OperationTool::kEraser, 0xFFFFU, 5.0F, 86.0F, 72.0F, -4.1F, 1.5F, 96U},
      {OperationTool::kPen, 0xF81FU, 3.5F, 26.0F, 96.0F, 2.5F, -3.5F, 160U},
      {OperationTool::kPen, 0xFFE0U, 2.5F, 6.0F, 88.0F, 4.4F, -1.2F, 192U},
  }};
  std::uint16_t gesture_id = 41'000U;
  for (const auto& spec : strokes) {
    if (!append_history_stroke(log, canvas, workspace, build_view, builder_storage, gesture_id++,
                               spec)) {
      return false;
    }
  }

  constexpr std::array kZooms{ZoomLevel::k400Percent, ZoomLevel::k200Percent};
  constexpr std::size_t kMovesPerDirection = 8;
  constexpr std::int64_t kGuardUs = 520'000;
  bool mechanical_ok = true;
  std::size_t over_guard = 0;
  for (const ZoomLevel zoom : kZooms) {
    if (!presenter.set_view(zoom, 0, 0, chrome, now_us()).passed) {
      return false;
    }
    const vector_v2::ViewRequest view{
        .zoom = zoom,
        .level_pixels = {presenter.level_x(), presenter.level_y(),
                         presenter.level_x() + vector_v2::kOverviewWidth,
                         presenter.level_y() + vector_v2::kOverviewHeight},
    };
    std::size_t warm_steps = 0;
    std::size_t warm_tiles = 0;
    std::size_t warm_scanned = 0;
    std::size_t warm_rendered = 0;
    if (!fill_history_view(producer, view, warm_steps, warm_tiles, warm_scanned, warm_rendered) ||
        !presenter.refresh(chrome, now_us()).passed) {
      return false;
    }
    constexpr std::array kPolicies{HistoryPresentPolicy::kPerPublication,
                                   HistoryPresentPolicy::kHoldback};
    for (const HistoryPresentPolicy policy : kPolicies) {
      const char* policy_name =
          policy == HistoryPresentPolicy::kPerPublication ? "per_publication" : "holdback";
      std::int64_t move_max_us = 0;
      std::int64_t first_max_us = 0;
      std::int64_t repair_max_us = 0;
      std::int64_t total_max_us = 0;
      for (std::size_t index = 0; index < 2U * kMovesPerDirection; ++index) {
        const bool undo = index < kMovesPerDirection;
        const auto direction =
            undo ? vector_v2::HistoryDirection::kUndo : vector_v2::HistoryDirection::kRedo;
        HistoryMoveMeasurement measurement{};
        if (!measure_history_move(presenter, producer, log, canvas, chrome, overview_scratch,
                                  zoom, direction, policy, measurement)) {
          mechanical_ok = false;
          break;
        }
        const std::int64_t total_us =
            measurement.move_us + measurement.first_us + measurement.repair_us;
        move_max_us = std::max(move_max_us, measurement.move_us);
        first_max_us = std::max(first_max_us, measurement.first_us);
        repair_max_us = std::max(repair_max_us, measurement.repair_us);
        total_max_us = std::max(total_max_us, total_us);
        over_guard += total_us > kGuardUs;
        std::printf(
            "TINYDRAW_GATE1_HISTORY policy=%s move=%s index=%lu zoom=%s move_us=%lld "
            "first_us=%lld repair_us=%lld total_us=%lld max_repair_tick_us=%lld steps=%lu "
            "tiles=%lu scanned=%lu rendered=%lu presents=%lu affected=%dx%d\n",
            policy_name, undo ? "undo" : "redo",
            static_cast<unsigned long>(index % kMovesPerDirection + 1U), zoom_name(zoom),
            static_cast<long long>(measurement.move_us),
            static_cast<long long>(measurement.first_us),
            static_cast<long long>(measurement.repair_us), static_cast<long long>(total_us),
            static_cast<long long>(measurement.max_repair_tick_us),
            static_cast<unsigned long>(measurement.steps),
            static_cast<unsigned long>(measurement.tiles),
            static_cast<unsigned long>(measurement.scanned),
            static_cast<unsigned long>(measurement.rendered),
            static_cast<unsigned long>(measurement.presents),
            measurement.affected_world.x1 - measurement.affected_world.x0,
            measurement.affected_world.y1 - measurement.affected_world.y0);
      }
      std::printf(
          "TINYDRAW_GATE1_HISTORY_SUMMARY policy=%s zoom=%s moves=%lu warm_tiles=%lu "
          "move_max_us=%lld first_max_us=%lld repair_max_us=%lld total_max_us=%lld "
          "guard_us=%lld over_guard=%lu mechanical=%u\n",
          policy_name, zoom_name(zoom), static_cast<unsigned long>(2U * kMovesPerDirection),
          static_cast<unsigned long>(warm_tiles), static_cast<long long>(move_max_us),
          static_cast<long long>(first_max_us), static_cast<long long>(repair_max_us),
          static_cast<long long>(total_max_us), static_cast<long long>(kGuardUs),
          static_cast<unsigned long>(over_guard), mechanical_ok);
      std::fflush(stdout);
      if (!mechanical_ok) {
        break;
      }
    }
    if (!mechanical_ok) {
      break;
    }
  }

  return mechanical_ok && log.current_revision() == canvas.current_revision();
}

}  // namespace tinydraw::esp32::gate_harness
