#include "vector_v2_gate_harness_internal.h"

// Canonical recorded owner traces embedded by the gate-harness build
// (esp32/main/CMakeLists.txt). under-overlay (9,284 events, ~190 KiB) is
// deliberately not embedded; it needs streamed delivery and is covered by
// the capture-side receipts for now.
extern "C" {
extern const char _binary_fast_curve_dense_25_csv_start[];
extern const char _binary_fast_curve_dense_25_csv_end[];
extern const char _binary_fast_curve_400_csv_start[];
extern const char _binary_fast_curve_400_csv_end[];
extern const char _binary_fast_curve_400_xl_csv_start[];
extern const char _binary_fast_curve_400_xl_csv_end[];
extern const char _binary_slow_precise_100_csv_start[];
extern const char _binary_slow_precise_100_csv_end[];
extern const char _binary_scribble_multistroke_csv_start[];
extern const char _binary_scribble_multistroke_csv_end[];
}

namespace tinydraw::esp32::gate_harness {

class TouchTraceReplayer {
 public:
  ~TouchTraceReplayer() { stop(); }

  [[nodiscard]] bool start(std::span<const vector_v2::TraceEvent> events) {
    if (task_ != nullptr) {
      return false;
    }
    trace_ = events;
    buffer_ = vector_v2::TouchEventBuffer(storage_);
    done_.store(false, std::memory_order_release);
    offered_ = 0;
    coalesced_ = 0;
    overflows_ = 0;
    resyncs_ = 0;
    if (xTaskCreatePinnedToCore(task_entry, "v2_replay", 3'072U, this, 5U, &task_, 1) != pdPASS) {
      task_ = nullptr;
      return false;
    }
    return true;
  }

  void stop() {
    if (task_ == nullptr) {
      return;
    }
    while (!done_.load(std::memory_order_acquire)) {
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    vTaskDelay(pdMS_TO_TICKS(2));  // Let the task reach its suspend point.
    vTaskDelete(task_);
    task_ = nullptr;
  }

  [[nodiscard]] std::optional<SampledTouch> read_next() {
    portENTER_CRITICAL(&lock_);
    const auto event = buffer_.pop();
    portEXIT_CRITICAL(&lock_);
    if (!event.has_value()) {
      return std::nullopt;
    }
    return SampledTouch{
        .point = {.x = event->point.x, .y = event->point.y},
        .timestamp_us = event->timestamp_us,
        .sequence = event->sequence,
        .kind = event->kind,
    };
  }

  [[nodiscard]] bool exhausted() {
    if (!done_.load(std::memory_order_acquire)) {
      return false;
    }
    portENTER_CRITICAL(&lock_);
    const std::size_t pending = buffer_.pending();
    portEXIT_CRITICAL(&lock_);
    return pending == 0U;
  }

  [[nodiscard]] bool sample_ready() {
    portENTER_CRITICAL(&lock_);
    const bool ready = buffer_.pending() != 0U;
    portEXIT_CRITICAL(&lock_);
    return ready;
  }

  [[nodiscard]] std::uint32_t offered() const { return offered_; }
  [[nodiscard]] std::uint32_t coalesced() const { return coalesced_; }
  [[nodiscard]] std::uint32_t overflows() const { return overflows_; }
  [[nodiscard]] std::uint32_t resyncs() const { return resyncs_; }

 private:
  static void task_entry(void* argument) {
    static_cast<TouchTraceReplayer*>(argument)->run();
    vTaskSuspend(nullptr);
  }

  void offer(vector_v2::TouchContactRead read, vector_v2::TouchContactPoint point) {
    const auto stamp = static_cast<std::uint32_t>(esp_timer_get_time());
    portENTER_CRITICAL(&lock_);
    const auto result = buffer_.offer(read, point, stamp);
    portEXIT_CRITICAL(&lock_);
    coalesced_ += result == vector_v2::TouchOfferResult::kMoveCoalesced;
    overflows_ += result == vector_v2::TouchOfferResult::kOverflow;
    resyncs_ += result == vector_v2::TouchOfferResult::kResynchronized;
  }

  void run() {
    const std::int64_t base_us = esp_timer_get_time();
    for (const vector_v2::TraceEvent& event : trace_) {
      const std::int64_t target_us = base_us + static_cast<std::int64_t>(event.t_us);
      std::int64_t now_us = esp_timer_get_time();
      while (now_us < target_us) {
        const std::int64_t remaining_us = target_us - now_us;
        vTaskDelay(remaining_us >= 2'000 ? pdMS_TO_TICKS(remaining_us / 1'000) : 1);
        now_us = esp_timer_get_time();
      }
      ++offered_;
      if (event.kind == vector_v2::TraceEventKind::kUp) {
        // Match the production lift-confirmation debounce exactly.
        for (std::uint8_t read = 0; read < vector_v2::kTouchLiftConfirmationReads; ++read) {
          offer(vector_v2::TouchContactRead::kNoTouch, {});
          if (read + 1U < vector_v2::kTouchLiftConfirmationReads) {
            vTaskDelay(pdMS_TO_TICKS(1));
          }
        }
      } else {
        offer(vector_v2::TouchContactRead::kPoint,
              {.x = static_cast<float>(event.x), .y = static_cast<float>(event.y)});
      }
    }
    done_.store(true, std::memory_order_release);
  }

  std::span<const vector_v2::TraceEvent> trace_{};
  std::array<vector_v2::TouchEvent, kVectorV2TouchEventCapacity> storage_{};
  vector_v2::TouchEventBuffer buffer_{storage_};
  TaskHandle_t task_ = nullptr;
  portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
  std::atomic<bool> done_{false};
  std::uint32_t offered_ = 0;
  std::uint32_t coalesced_ = 0;
  std::uint32_t overflows_ = 0;
  std::uint32_t resyncs_ = 0;
};

struct InkTraceSpec {
  const char* name;
  const char* begin;
  const char* end;
  ZoomLevel zoom;
  float brush_size;
};

struct LatencyDeltas {
  std::uint32_t* values = nullptr;
  std::size_t count = 0;

  void push(std::uint32_t value, std::size_t capacity) {
    if (count < capacity) {
      values[count++] = value;
    }
  }
};

struct LatencySummaryLine {
  std::uint32_t p50 = 0;
  std::uint32_t p95 = 0;
  std::uint32_t max = 0;
};

LatencySummaryLine summarize_deltas(LatencyDeltas& deltas) {
  if (deltas.count == 0U) {
    return {};
  }
  std::sort(deltas.values, deltas.values + deltas.count);
  const auto rank = [&](double percentile) {
    const auto index =
        static_cast<std::size_t>(std::ceil(percentile * static_cast<double>(deltas.count)));
    return deltas.values[index == 0U ? 0U : std::min(deltas.count, index) - 1U];
  };
  return {.p50 = rank(0.50), .p95 = rank(0.95), .max = deltas.values[deltas.count - 1U]};
}

// Counts viewport tile identities whose lookup() falls back to the
// pixelated overview (or has no source at all) at a tiled zoom. This is the
// mid-stroke pixelation oracle: an append commit that invalidates a
// viewport identity without retaining it drops that tile from raw/uniform
// to SourceKind::kOverview until a producer pass repairs it. 25% has no
// tile identities by design (the overview IS the authority), so the probe
// reports zero there. ~56 O(1) lookups per call; cheap enough to run after
// every chunk commit.
struct ViewportFallbackProbe {
  std::uint32_t tiles = 0;
  std::uint32_t fallback = 0;
};

ViewportFallbackProbe probe_viewport_overview_fallback(const VectorV2Presenter& presenter,
                                                       const MaterializedCanvas& canvas,
                                                       ZoomLevel zoom) {
  ViewportFallbackProbe probe{};
  if (zoom == ZoomLevel::k25Percent) {
    return probe;
  }
  const int x0 = presenter.level_x();
  const int y0 = presenter.level_y();
  const int first_column = x0 / vector_v2::kTileWidth;
  const int last_column = (x0 + vector_v2::kOverviewWidth - 1) / vector_v2::kTileWidth;
  const int first_row = y0 / vector_v2::kTileHeight;
  const int last_row = (y0 + vector_v2::kOverviewHeight - 1) / vector_v2::kTileHeight;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      ++probe.tiles;
      const auto selection = canvas.lookup(
          {zoom, static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
      probe.fallback += static_cast<std::uint32_t>(
          !selection.has_value() || selection->kind == vector_v2::SourceKind::kOverview);
    }
  }
  return probe;
}

// Replays one recorded trace through the production interaction pipeline
// (InkStream -> curved ribbon -> visual-first coordinator -> in-place
// authority commits) and reports the event->consumed->geometry->submit->DMA
// chain per docs/INK_TRACE_HARNESS.md §3. The consumption loop polls
// tighter than the product loop; the receipt notes that cadence.
bool run_ink_trace_replay_gate(VectorV2Presenter& presenter, vector_v2::TileProducer& producer,
                               OperationLog& log, MaterializedCanvas& canvas,
                               const vector_v2::ChromeState& chrome,
                               const vector_v2::InPlaceAppendWorkspace& in_place_workspace,
                               std::span<CompactOperationSample> builder_storage) {
  const std::array<InkTraceSpec, 5> specs{{
      {"fast-curve-dense-25", _binary_fast_curve_dense_25_csv_start,
       _binary_fast_curve_dense_25_csv_end, ZoomLevel::k25Percent, 5.0F},
      {"fast-curve-400", _binary_fast_curve_400_csv_start, _binary_fast_curve_400_csv_end,
       ZoomLevel::k400Percent, 5.0F},
      {"fast-curve-400-xl", _binary_fast_curve_400_xl_csv_start, _binary_fast_curve_400_xl_csv_end,
       ZoomLevel::k400Percent, 20.0F},
      {"slow-precise-100", _binary_slow_precise_100_csv_start, _binary_slow_precise_100_csv_end,
       ZoomLevel::k100Percent, 5.0F},
      {"scribble-multistroke", _binary_scribble_multistroke_csv_start,
       _binary_scribble_multistroke_csv_end, ZoomLevel::k100Percent, 5.0F},
  }};
  constexpr std::size_t kMaximumTraceEvents = 4'096;
  constexpr std::size_t kMaximumLatencySamples = 4'096;
  auto* events = static_cast<vector_v2::TraceEvent*>(
      heap_caps_malloc(kMaximumTraceEvents * sizeof(vector_v2::TraceEvent), kExternalCaps));
  auto* delta_storage = static_cast<std::uint32_t*>(
      heap_caps_malloc(4U * kMaximumLatencySamples * sizeof(std::uint32_t), kExternalCaps));
  if (events == nullptr || delta_storage == nullptr) {
    heap_caps_free(events);
    heap_caps_free(delta_storage);
    std::printf("TINYDRAW_INKTRACE_FAIL reason=allocation\n");
    return false;
  }
  bool all_pass = true;
  std::uint16_t gesture_id = 40'000U;
  for (const InkTraceSpec& spec : specs) {
    const std::size_t csv_size = static_cast<std::size_t>(spec.end - spec.begin);
    const std::string_view csv(
        spec.begin, csv_size != 0U && spec.begin[csv_size - 1U] == '\0' ? csv_size - 1U : csv_size);
    const auto parsed = vector_v2::parse_ink_trace_csv(csv, std::span(events, kMaximumTraceEvents));
    if (!parsed.ok()) {
      std::printf("TINYDRAW_INKTRACE_FAIL trace=%s reason=parse line=%u\n", spec.name,
                  static_cast<unsigned>(parsed.line));
      all_pass = false;
      continue;
    }
    if (!presenter.set_view(spec.zoom, 0, 0, chrome, now_us()).passed) {
      std::printf("TINYDRAW_INKTRACE_FAIL trace=%s reason=set_view\n", spec.name);
      all_pass = false;
      continue;
    }
    LiveStrokeSession stroke(builder_storage, log, presenter);
    // Mid-stroke pixelation observability: fb_start is the pre-ink state of
    // the viewport, fb_mid_max the worst overview fallback seen right after
    // any chunk commit, fb_up_max the worst state at any lift, fb_end the
    // state after the whole trace. Counts, not verdicts: pass is unchanged.
    const ViewportFallbackProbe fallback_start =
        probe_viewport_overview_fallback(presenter, canvas, spec.zoom);
    std::uint32_t fallback_up_max = 0;
    vector_v2::InPlaceRetainDrops trace_drops{};
    std::uint32_t drain_ops = 0;
    std::uint32_t drain_slices = 0;
    std::uint32_t drain_skipped_ready = 0;
    std::size_t max_pending_operations = 0;
    std::int64_t drain_total_us = 0;
    std::int64_t drain_max_slice_us = 0;
    vector_v2::PendingAbsorptionWorkUnit drain_max_unit =
        vector_v2::PendingAbsorptionWorkUnit::kNone;
    vector_v2::PendingOperationAbsorption absorption;
    const std::optional<vector_v2::ViewRequest> priority_view =
        spec.zoom == ZoomLevel::k25Percent
            ? std::optional<vector_v2::ViewRequest>{}
            : std::optional{vector_v2::ViewRequest{
                  .zoom = spec.zoom,
                  .level_pixels = {presenter.level_x(), presenter.level_y(),
                                   presenter.level_x() + vector_v2::kOverviewWidth,
                                   presenter.level_y() + vector_v2::kOverviewHeight}}};
    const auto observe_pending = [&]() {
      max_pending_operations =
          std::max(max_pending_operations, vector_v2::pending_operation_count(log, canvas));
    };
    const auto absorb_slice = [&]() -> bool {
      if (!absorption.active() && vector_v2::pending_operation_count(log, canvas) == 0U) {
        return true;
      }
      if (!absorption.active()) {
        producer.cancel_pending_work();
      }
      const std::int64_t started_us = esp_timer_get_time();
      const MixedDrawAbsorbLimit limit{.deadline_us = started_us + kInkTraceAbsorbSliceBudgetUs};
      const auto absorbed = vector_v2::absorb_pending_operation_slice(
          {log,
           canvas,
           in_place_workspace,
           absorption,
           priority_view,
           {.requested = &MixedDrawAbsorbLimit::requested,
            .context = &limit,
            .raster_work_px = kInkTraceAbsorbRasterWorkPixels},
           {.now_us = &esp_timer_get_time, .budget_us = kIdleAbsorbBudgetUs}});
      const std::int64_t elapsed_us = esp_timer_get_time() - started_us;
      ++drain_slices;
      drain_total_us += elapsed_us;
      if (elapsed_us > drain_max_slice_us) {
        drain_max_slice_us = elapsed_us;
        drain_max_unit = absorbed.work_unit;
      }
      if (absorbed.status == vector_v2::PendingAbsorptionStatus::kError) {
        absorption.cancel();
        return false;
      }
      if (absorbed.status == vector_v2::PendingAbsorptionStatus::kComplete) {
        ++drain_ops;
        trace_drops.visible_uniform_no_slot += absorbed.result.drops.visible_uniform_no_slot;
        trace_drops.visible_uniform_paint_fail += absorbed.result.drops.visible_uniform_paint_fail;
        trace_drops.visible_raw_edit_fail += absorbed.result.drops.visible_raw_edit_fail;
        trace_drops.visible_raw_paint_fail += absorbed.result.drops.visible_raw_paint_fail;
        trace_drops.offscreen_skipped += absorbed.result.drops.offscreen_skipped;
      }
      return true;
    };
    LatencyDeltas event_to_consumed{delta_storage, 0};
    LatencyDeltas event_to_geometry{delta_storage + kMaximumLatencySamples, 0};
    LatencyDeltas event_to_submit{delta_storage + 2U * kMaximumLatencySamples, 0};
    LatencyDeltas event_to_complete{delta_storage + 3U * kMaximumLatencySamples, 0};
    vector_v2::InkStrokeCounters counters{};
    for (const vector_v2::TraceEvent& event : std::span(events, parsed.event_count)) {
      counters.trace_down_events += event.kind == vector_v2::TraceEventKind::kDown;
      counters.trace_up_events += event.kind == vector_v2::TraceEventKind::kUp;
    }

    TouchTraceReplayer replayer;
    if (!replayer.start(std::span(events, parsed.event_count))) {
      std::printf("TINYDRAW_INKTRACE_FAIL trace=%s reason=replayer_start\n", spec.name);
      all_pass = false;
      continue;
    }
    const auto absorb_between_samples = [&]() -> bool {
      observe_pending();
      if (replayer.sample_ready()) {
        ++drain_skipped_ready;
        return true;
      }
      return absorb_slice();
    };
    const std::uint16_t color = 0x0000U;
    bool pressed = false;
    std::uint32_t presentation_failures = 0;
    std::uint32_t commit_failures = 0;
    std::uint32_t previous_consumed_us = 0;
    Point previous_point{};
    bool have_previous = false;
    while (true) {
      const auto sampled = replayer.read_next();
      if (!sampled.has_value()) {
        if (!pressed && replayer.exhausted()) {
          break;
        }
        if (!absorb_between_samples()) {
          ++commit_failures;
        }
        vTaskDelay(1);
        continue;
      }
      const auto consumed_us = static_cast<std::uint32_t>(esp_timer_get_time());
      const std::uint32_t event_us = sampled->timestamp_us;
      ++counters.consumed_events;
      counters.consumed_down_events += sampled->kind == vector_v2::TouchEventKind::kDown;
      counters.consumed_up_events += sampled->kind == vector_v2::TouchEventKind::kUp;
      event_to_consumed.push(consumed_us - event_us, kMaximumLatencySamples);
      if (pressed && have_previous) {
        counters.max_consumed_sample_time_gap_us = std::max<std::uint64_t>(
            counters.max_consumed_sample_time_gap_us, consumed_us - previous_consumed_us);
        const float gap_x = sampled->point.x - previous_point.x;
        const float gap_y = sampled->point.y - previous_point.y;
        counters.max_consumed_sample_space_gap_px = std::max(
            counters.max_consumed_sample_space_gap_px, std::sqrt(gap_x * gap_x + gap_y * gap_y));
      }
      previous_consumed_us = consumed_us;
      previous_point = sampled->point;
      have_previous = true;

      if (sampled->kind == vector_v2::TouchEventKind::kDown && !pressed) {
        pressed = true;
        const LiveStrokeStartResult started =
            stroke.begin(sampled->point, event_us, spec.brush_size, OperationTool::kPen, color,
                         gesture_id, chrome);
        if (!started.accepted) {
          pressed = false;
          if (!absorb_between_samples()) {
            ++commit_failures;
          }
          continue;
        }
        ++gesture_id;
        presentation_failures += !started.presentation.passed;
        if (!absorb_between_samples()) {
          ++commit_failures;
        }
        continue;
      }
      if (sampled->kind == vector_v2::TouchEventKind::kUp && pressed) {
        const LiveStrokeFinishResult finished = stroke.finish(event_us, chrome);
        presentation_failures += !finished.preview.passed;
        commit_failures += finished.commit_failed;
        if (!absorb_between_samples()) {
          ++commit_failures;
        }
        static_cast<void>(presenter.refresh(chrome, now_us()));
        fallback_up_max =
            std::max(fallback_up_max,
                     probe_viewport_overview_fallback(presenter, canvas, spec.zoom).fallback);
        pressed = false;
        continue;
      }
      if (!pressed || !stroke.active()) {
        if (!absorb_between_samples()) {
          ++commit_failures;
        }
        continue;
      }
      std::uint32_t geometry_delta = 0;
      std::uint32_t submit_delta = 0;
      std::uint32_t complete_delta = 0;
      bool submitted = false;
      const LiveStrokeMoveResult move = stroke.move(sampled->point, event_us, chrome);
      geometry_delta = move.geometry_us;
      if (move.presented && move.presentation.passed && move.presentation.first_submit_us > 0) {
        submit_delta = static_cast<std::uint32_t>(move.presentation.first_submit_us);
        complete_delta = static_cast<std::uint32_t>(move.presentation.first_complete_us);
        submitted = true;
      }
      presentation_failures += move.presented && !move.presentation.passed;
      if (submitted) {
        event_to_geometry.push(geometry_delta, kMaximumLatencySamples);
        event_to_submit.push(submit_delta, kMaximumLatencySamples);
        event_to_complete.push(complete_delta, kMaximumLatencySamples);
      }
      if (!absorb_between_samples()) {
        ++commit_failures;
      }
    }
    replayer.stop();
    // No trace event remains urgent. Finish product-sized background slices
    // before recording the terminal canvas/fallback state.
    while (absorption.active() || vector_v2::pending_operation_count(log, canvas) != 0U) {
      observe_pending();
      if (!absorb_slice()) {
        ++commit_failures;
        break;
      }
    }
    const ViewportFallbackProbe fallback_end =
        probe_viewport_overview_fallback(presenter, canvas, spec.zoom);

    counters.received_events = replayer.offered();
    counters.coalesced_events = replayer.coalesced();
    const auto consumed_line = summarize_deltas(event_to_consumed);
    const auto geometry_line = summarize_deltas(event_to_geometry);
    const auto submit_line = summarize_deltas(event_to_submit);
    const auto complete_line = summarize_deltas(event_to_complete);
    const bool conserved = counters.down_up_conserved();
    const bool latency_pass = complete_line.p95 <= 28'000U;
    const bool cooperative_pass = drain_max_slice_us <= kInkTraceAbsorbSliceGuardUs &&
                                  max_pending_operations <= kPendingOperationHighWater;
    const bool trace_pass = conserved && replayer.overflows() == 0U && replayer.resyncs() == 0U &&
                            commit_failures == 0U && presentation_failures == 0U &&
                            cooperative_pass;
    std::printf(
        "TINYDRAW_INKTRACE trace=%s zoom=%s events=%lu consumed=%lu coalesced=%lu "
        "down=%lu/%lu up=%lu/%lu max_time_gap_us=%llu max_space_gap_px=%.2f "
        "e2c_p50=%lu e2c_p95=%lu e2c_max=%lu e2g_p95=%lu e2s_p95=%lu "
        "e2d_p50=%lu e2d_p95=%lu e2d_max=%lu latency_samples=%lu "
        "presentation_failures=%lu commit_failures=%lu overflows=%lu resyncs=%lu revision=%lu "
        "fb_tiles=%lu fb_start=%lu fb_up_max=%lu fb_end=%lu "
        "drop_uni_slot=%lu drop_uni_paint=%lu drop_raw_edit=%lu drop_raw_paint=%lu "
        "off_skip=%lu drain_ops=%lu drain_slices=%lu drain_skipped_ready=%lu "
        "max_pending=%lu drain_total_us=%lld drain_max_slice_us=%lld drain_max_unit=%s "
        "drain_guard_us=%lld "
        "publication=operation absorb_cadence=cooperative_after_lift cooperative_pass=%u "
        "latency_pass=%u pass=%u\n",
        spec.name, zoom_name(spec.zoom), static_cast<unsigned long>(counters.received_events),
        static_cast<unsigned long>(counters.consumed_events),
        static_cast<unsigned long>(counters.coalesced_events),
        static_cast<unsigned long>(counters.consumed_down_events),
        static_cast<unsigned long>(counters.trace_down_events),
        static_cast<unsigned long>(counters.consumed_up_events),
        static_cast<unsigned long>(counters.trace_up_events),
        static_cast<unsigned long long>(counters.max_consumed_sample_time_gap_us),
        static_cast<double>(counters.max_consumed_sample_space_gap_px),
        static_cast<unsigned long>(consumed_line.p50),
        static_cast<unsigned long>(consumed_line.p95),
        static_cast<unsigned long>(consumed_line.max),
        static_cast<unsigned long>(geometry_line.p95), static_cast<unsigned long>(submit_line.p95),
        static_cast<unsigned long>(complete_line.p50),
        static_cast<unsigned long>(complete_line.p95),
        static_cast<unsigned long>(complete_line.max),
        static_cast<unsigned long>(event_to_complete.count),
        static_cast<unsigned long>(presentation_failures),
        static_cast<unsigned long>(commit_failures),
        static_cast<unsigned long>(replayer.overflows()),
        static_cast<unsigned long>(replayer.resyncs()),
        static_cast<unsigned long>(canvas.current_revision().value),
        static_cast<unsigned long>(fallback_start.tiles),
        static_cast<unsigned long>(fallback_start.fallback),
        static_cast<unsigned long>(fallback_up_max),
        static_cast<unsigned long>(fallback_end.fallback),
        static_cast<unsigned long>(trace_drops.visible_uniform_no_slot),
        static_cast<unsigned long>(trace_drops.visible_uniform_paint_fail),
        static_cast<unsigned long>(trace_drops.visible_raw_edit_fail),
        static_cast<unsigned long>(trace_drops.visible_raw_paint_fail),
        static_cast<unsigned long>(trace_drops.offscreen_skipped),
        static_cast<unsigned long>(drain_ops), static_cast<unsigned long>(drain_slices),
        static_cast<unsigned long>(drain_skipped_ready),
        static_cast<unsigned long>(max_pending_operations), static_cast<long long>(drain_total_us),
        static_cast<long long>(drain_max_slice_us), absorption_unit_name(drain_max_unit),
        static_cast<long long>(kInkTraceAbsorbSliceGuardUs), cooperative_pass, latency_pass,
        trace_pass);
    std::fflush(stdout);
    all_pass = all_pass && trace_pass;
  }
  heap_caps_free(events);
  heap_caps_free(delta_storage);
  return all_pass;
}

}  // namespace tinydraw::esp32::gate_harness
