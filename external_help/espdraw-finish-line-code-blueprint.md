# ESPDraw Vector V2 Finish-Line Code Blueprint

**Status:** implementation-oriented sketch, not a drop-in patch. The interfaces and state transitions are designed against the `f66c808` packet source. Compile and hardware-test each slice before carrying it forward.

## Scope rule

The fastest path is not a generic scheduler, generalized render graph, retained-mode UI framework, or fully asynchronous document engine. It is a small set of explicit seams:

1. one closure metrics format;
2. one panel frame-stream API with a staging callback;
3. one replaceable provisional ink tail;
4. one compact replay index;
5. one durable repair-task key;
6. one active-prefix history model;
7. one immutable authority snapshot API used by autosave and SVG export.

Escalate to a resumable authority committer only if preview-first ink still misses the optical gate.

A useful source correction: the minimap renderer is already substantially implemented in `vector_v2/src/chrome.cpp` (`draw_minimap`, `draw_chrome_minimap_surface`, `draw_chrome_strip_overlays`). The missing work appears to be input/navigation and final integration, not rendering from scratch.

---

# 1. Dependency graph

```text
closure contract + metrics
        |
        +-----------------------+
        |                       |
        v                       v
panel edge/stream seam     OperationLogSnapshot seam
        |                       |
        v                       +--------------------------+
staging compositor             |             |            |
        |                       v             v            v
        +--> tear-free pan   replay index   undo/redo   autosave/SVG
        |                       |
        +--> provisional ink    v
        |                   durable repair
        v
minimap integration
        |
        v
feature-complete integrated closure
```

The two first interface freezes should be:

```cpp
struct PanelStagePatch;
struct OperationLogSnapshot;
```

Once those are stable, most work can proceed without repeated cross-branch redesign.

---

# 2. Minimal file map

## New core files

```text
vector_v2/include/tinydraw/vector_v2/closure_metrics.h
vector_v2/include/tinydraw/vector_v2/replay_block_index.h
vector_v2/src/replay_block_index.cpp
vector_v2/include/tinydraw/vector_v2/repair_queue.h
vector_v2/src/repair_queue.cpp
vector_v2/include/tinydraw/vector_v2/svg_export.h
vector_v2/src/svg_export.cpp
```

## New ESP32 files

```text
esp32/main/vector_v2/vector_v2_autosave.h
esp32/main/vector_v2/vector_v2_autosave.cpp
```

## Existing files with focused edits

```text
esp32/main/co5300_panel_transport.{h,cpp}
esp32/main/vector_v2/vector_v2_presenter.{h,cpp}
esp32/main/vector_v2/vector_v2_app.cpp
vector_v2/include/tinydraw/vector_v2/panel_staging.h
vector_v2/include/tinydraw/vector_v2/operation_log.h
vector_v2/src/operation_log.cpp
vector_v2/include/tinydraw/vector_v2/tile_producer.h
vector_v2/src/tile_producer.cpp
vector_v2/include/tinydraw/vector_v2/materialized_canvas.h
vector_v2/src/materialized_canvas.cpp
vector_v2/include/tinydraw/vector_v2/chrome.h
vector_v2/src/chrome.cpp
core/include/tinydraw/export/fat16_disk.h
core/src/fat16_disk.cpp
vector_v2/sources.cmake
esp32/main/CMakeLists.txt
```

Do not create a generic `Job`, `Layer`, or `TaskGraph` hierarchy. Fixed structs and explicit `if` chains are adequate here.

---

# 3. Closure metrics: one permanent truth format

Add a small fixed event buffer. Do not `printf` from the hot path; dump after a gesture or hardware trace.

```cpp
// vector_v2/include/tinydraw/vector_v2/closure_metrics.h
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tinydraw::vector_v2 {

enum class ClosureEventKind : std::uint8_t {
  kTouchSampled,
  kTouchConsumed,
  kInkGeometryReady,
  kFirstPanelSubmit,
  kLastPanelComplete,
  kFrameEdgeObserved,
  kColdGroupStarted,
  kColdGroupPublished,
  kRepairAttempted,
  kRepairCompleted,
  kRepairDiscarded,
};

struct ClosureEvent {
  std::uint32_t trace_id = 0;
  std::uint32_t sequence = 0;
  std::uint32_t timestamp_us = 0;  // wrap-safe differences
  std::int16_t x = 0;
  std::int16_t y = 0;
  ClosureEventKind kind = ClosureEventKind::kTouchSampled;
  std::uint8_t flags = 0;
};

static_assert(sizeof(ClosureEvent) <= 20U);

template <std::size_t Capacity>
class ClosureTraceBuffer {
 public:
  void begin(std::uint32_t trace_id) {
    trace_id_ = trace_id;
    count_ = 0;
    dropped_ = 0;
  }

  void record(ClosureEvent event) {
    event.trace_id = trace_id_;
    if (count_ == storage_.size()) {
      ++dropped_;
      return;
    }
    storage_[count_++] = event;
  }

  [[nodiscard]] std::span<const ClosureEvent> events() const {
    return std::span(storage_).first(count_);
  }

  [[nodiscard]] std::uint32_t dropped() const { return dropped_; }

 private:
  std::array<ClosureEvent, Capacity> storage_{};
  std::size_t count_ = 0;
  std::uint32_t trace_id_ = 0;
  std::uint32_t dropped_ = 0;
};

struct ReplayWorkCounters {
  std::uint64_t blocks_considered = 0;
  std::uint64_t blocks_rejected = 0;
  std::uint64_t operations_considered = 0;
  std::uint64_t operations_bbox_rejected = 0;
  std::uint64_t operations_rendered = 0;
};

struct RepairWorkCounters {
  std::uint32_t unique_required = 0;
  std::uint32_t attempts = 0;
  std::uint32_t completed = 0;
  std::uint32_t reused = 0;
  std::uint32_t canceled_before_work = 0;
  std::uint32_t work_discarded = 0;

  [[nodiscard]] float amplification() const {
    return unique_required == 0U ? 0.0F
                                 : static_cast<float>(attempts) /
                                       static_cast<float>(unique_required);
  }
};

}  // namespace tinydraw::vector_v2
```

Hardware dumping can remain simple JSONL-compatible text:

```cpp
void dump_closure_trace(std::span<const ClosureEvent> events, std::uint32_t dropped) {
  for (const auto& event : events) {
    std::printf(
        "TINYDRAW_CLOSURE trace=%lu seq=%lu kind=%u t=%lu x=%d y=%d flags=%u\n",
        static_cast<unsigned long>(event.trace_id),
        static_cast<unsigned long>(event.sequence),
        static_cast<unsigned>(event.kind),
        static_cast<unsigned long>(event.timestamp_us), event.x, event.y,
        static_cast<unsigned>(event.flags));
  }
  std::printf("TINYDRAW_CLOSURE_END dropped=%lu\n",
              static_cast<unsigned long>(dropped));
}
```

Add the missing touch-path measurements to `TouchSamplerMetrics`:

```cpp
std::uint32_t maximum_consumed_interval_us = 0;
std::uint32_t maximum_consumed_distance_q8 = 0;
std::uint32_t maximum_sequence_gap = 0;
```

Update these in `VectorV2TouchSampler::read_next()` using the prior consumed event. This does not fix coalescing; it makes under-sampling visible before changing policy.

---

# 4. Panel API: explicit edge, one frame stream, one completion wait

The panel experiment should remain configurable until glass proves the product policy.

```cpp
// esp32/main/co5300_panel_transport.h
namespace tinydraw::esp32 {

enum class TearEdge : std::uint8_t {
  kRising,
  kFalling,
};

struct PanelStageSurface {
  int panel_x = 0;
  int panel_y = 0;
  int width = 0;
  int height = 0;
  int stride = 0;
  // Host-order RGB565. The transport byte-swaps after the patch returns.
  std::span<std::uint16_t> pixels{};
};

struct PanelStagePatch {
  void* context = nullptr;
  void (*paint)(void* context, const PanelStageSurface& surface) = nullptr;

  void apply(const PanelStageSurface& surface) const {
    if (paint != nullptr) {
      paint(context, surface);
    }
  }
};

struct RingFrameSource {
  const std::uint16_t* pixels = nullptr;
  int stride = 0;
  int shift_x = 0;
  int shift_y = 0;
  int area_width = 0;
  int area_height = 0;
};

struct PanelFrameRequest {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  RingFrameSource source{};
  PanelStagePatch patch{};
  TearEdge edge = TearEdge::kRising;
  std::int64_t edge_timeout_us = 20'000;
};

struct PanelFrameResult {
  bool edge_observed = false;
  bool submitted = false;
  bool completed = false;
  std::uint32_t chunks = 0;
  std::int64_t edge_wait_us = 0;
  std::int64_t prepare_us = 0;
  std::int64_t complete_us = 0;
};

class Co5300PanelTransport final : public DisplayBackend {
 public:
  // ...existing API...
  [[nodiscard]] bool wait_for_tear_edge(TearEdge edge, std::int64_t timeout_us);
  [[nodiscard]] PanelFrameResult present_ring_frame(const PanelFrameRequest& request);

  // Partial/live updates retain the ordinary window API but can use the same
  // staging patch so chrome and the provisional tail are not baked into frame_.
  void push_rect_patched(int x, int y, int width, int height,
                         const std::uint16_t* pixels, int stride,
                         PanelStagePatch patch = {});
};

}  // namespace tinydraw::esp32
```

## Edge ISR

Give one semaphore on either edge. The counters identify which edge occurred, so a binary semaphore may coalesce notifications without losing correctness.

```cpp
static void on_tear_edge(void* context) {
  auto& self = *static_cast<Impl*>(context);
  const std::uint32_t now = static_cast<std::uint32_t>(esp_timer_get_time());
  const bool high = gpio_get_level(kTearPin) != 0;

  if (high) {
    const std::uint32_t prior =
        self.tear_last_rise_us_.exchange(now, std::memory_order_relaxed);
    if (prior != 0U) {
      self.tear_period_us_.store(now - prior, std::memory_order_relaxed);
    }
    self.tear_rising_edges_.fetch_add(1U, std::memory_order_release);
  } else {
    const std::uint32_t rise =
        self.tear_last_rise_us_.load(std::memory_order_relaxed);
    if (rise != 0U) {
      self.tear_high_us_.store(now - rise, std::memory_order_relaxed);
    }
    self.tear_last_fall_us_.store(now == 0U ? 1U : now,
                                  std::memory_order_release);
    self.tear_falling_edges_.fetch_add(1U, std::memory_order_release);
  }

  BaseType_t woke = pdFALSE;
  xSemaphoreGiveFromISR(self.tear_semaphore_, &woke);
  if (woke == pdTRUE) {
    portYIELD_FROM_ISR();
  }
}
```

```cpp
bool wait_for_tear_edge(TearEdge edge, std::int64_t timeout_us) {
  const auto count = [&]() {
    return edge == TearEdge::kRising
               ? tear_rising_edges_.load(std::memory_order_acquire)
               : tear_falling_edges_.load(std::memory_order_acquire);
  };

  const std::uint32_t before = count();
  const std::int64_t deadline = esp_timer_get_time() + timeout_us;
  while (esp_timer_get_time() < deadline) {
    if (count() != before) {
      return true;
    }
    const std::int64_t remaining = deadline - esp_timer_get_time();
    const TickType_t ticks = std::max<TickType_t>(
        1U, pdMS_TO_TICKS(static_cast<std::uint32_t>((remaining + 999) / 1000)));
    static_cast<void>(xSemaphoreTake(tear_semaphore_, ticks));
  }
  return count() != before;
}
```

No hard-coded beam row should be involved in the product path.

## Host-order staging helpers

Add these beside the existing swapped functions in `panel_staging.h`:

```cpp
inline void copy_ring_row(const std::uint16_t* source_row, int area_width,
                          int shift_x, int x, int width,
                          std::uint16_t* destination) {
  int source_column = x + shift_x;
  if (source_column >= area_width) {
    source_column -= area_width;
  }
  int written = 0;
  while (written < width) {
    const int chunk = std::min(area_width - source_column, width - written);
    std::copy_n(source_row + source_column, chunk, destination + written);
    written += chunk;
    source_column = 0;
  }
}

inline void swap_pixels_in_place(std::span<std::uint16_t> pixels) {
  for (std::uint16_t& pixel : pixels) {
    pixel = static_cast<std::uint16_t>((pixel >> 8U) | (pixel << 8U));
  }
}
```

This intentionally makes a second pass over internal RAM. Measure it before attempting a fused copy/patch/swap loop; it is still much simpler than a PSRAM gather/scatter/restore journey.

## Raw CO5300 frame stream

Use one address window, `RAMWR` for the first color chunk, and `RAMWRC` for continuation chunks. Keep this behind the A/B flag until optical proof.

```cpp
namespace {
constexpr std::uint32_t kQspiWriteCommand = 0x02U;
constexpr std::uint32_t kQspiWriteColor = 0x32U;
constexpr std::uint8_t kCaseT = 0x2A;
constexpr std::uint8_t kPageSet = 0x2B;
constexpr std::uint8_t kRamWrite = 0x2C;
constexpr std::uint8_t kRamWriteContinue = 0x3C;

constexpr int qspi_command(std::uint32_t opcode, std::uint8_t command) {
  return static_cast<int>((opcode << 24U) |
                          (static_cast<std::uint32_t>(command) << 8U));
}
}  // namespace

bool set_frame_window(int x, int y, int width, int height) {
  const int physical_x0 = x + kPanelXGap;
  const int physical_x1 = physical_x0 + width - 1;
  const int y1 = y + height - 1;
  const std::array<std::uint8_t, 4> columns{
      static_cast<std::uint8_t>(physical_x0 >> 8),
      static_cast<std::uint8_t>(physical_x0),
      static_cast<std::uint8_t>(physical_x1 >> 8),
      static_cast<std::uint8_t>(physical_x1),
  };
  const std::array<std::uint8_t, 4> rows{
      static_cast<std::uint8_t>(y >> 8), static_cast<std::uint8_t>(y),
      static_cast<std::uint8_t>(y1 >> 8), static_cast<std::uint8_t>(y1),
  };
  return esp_lcd_panel_io_tx_param(
             io_, qspi_command(kQspiWriteCommand, kCaseT),
             columns.data(), columns.size()) == ESP_OK &&
         esp_lcd_panel_io_tx_param(
             io_, qspi_command(kQspiWriteCommand, kPageSet),
             rows.data(), rows.size()) == ESP_OK;
}
```

Core frame loop:

```cpp
PanelFrameResult present_ring_frame(const PanelFrameRequest& request) {
  PanelFrameResult result;
  if (!valid_frame_request(request)) {
    return result;
  }

  const std::int64_t started = esp_timer_get_time();
  const std::int64_t edge_started = started;
  result.edge_observed = wait_for_tear_edge(request.edge,
                                             request.edge_timeout_us);
  result.edge_wait_us = esp_timer_get_time() - edge_started;
  if (!result.edge_observed || !set_frame_window(
          request.x, request.y, request.width, request.height)) {
    return result;  // fail closed for a reusable frame
  }

  const std::uint32_t first_submit =
      transfer_submits_.load(std::memory_order_acquire);
  int row = 0;
  bool first = true;
  while (row < request.height) {
    int rows = std::min(kTransferPixels / request.width,
                        request.height - row);
    if (rows <= 0) {
      return result;
    }

    if (xSemaphoreTake(transfer_semaphore_, portMAX_DELAY) != pdTRUE) {
      return result;
    }
    auto* transfer = transfer_pixels_ +
        static_cast<std::ptrdiff_t>(transfer_index_ * kTransferPixels);
    transfer_index_ = (transfer_index_ + 1U) % kTransferQueueDepth;

    const std::int64_t prepare_started = esp_timer_get_time();
    for (int local_y = 0; local_y < rows; ++local_y) {
      int source_y = request.y + row + local_y + request.source.shift_y;
      source_y %= request.source.area_height;
      const auto* source = request.source.pixels +
          static_cast<std::ptrdiff_t>(source_y * request.source.stride);
      copy_ring_row(source, request.source.area_width,
                    request.source.shift_x, request.x, request.width,
                    transfer + static_cast<std::ptrdiff_t>(local_y * request.width));
    }

    PanelStageSurface surface{
        .panel_x = request.x,
        .panel_y = request.y + row,
        .width = request.width,
        .height = rows,
        .stride = request.width,
        .pixels = std::span(transfer,
                            static_cast<std::size_t>(request.width * rows)),
    };
    request.patch.apply(surface);
    swap_pixels_in_place(surface.pixels);
    result.prepare_us += esp_timer_get_time() - prepare_started;

    const std::uint8_t command = first ? kRamWrite : kRamWriteContinue;
    transfer_submits_.fetch_add(1U, std::memory_order_release);
    if (esp_lcd_panel_io_tx_color(
            io_, qspi_command(kQspiWriteColor, command), transfer,
            static_cast<std::size_t>(request.width * rows) * sizeof(std::uint16_t)) != ESP_OK) {
      return result;
    }
    first = false;
    ++result.chunks;
    row += rows;
  }

  result.submitted = true;
  result.completed = wait_for_all(50'000);
  result.complete_us = esp_timer_get_time() - started;
  return result;
}
```

Preserve a compile-time clock A/B rather than making runtime clock reconfiguration another subsystem:

```cpp
#ifndef TINYDRAW_CO5300_PCLK_HZ
#define TINYDRAW_CO5300_PCLK_HZ 50000000
#endif

.pclk_hz = TINYDRAW_CO5300_PCLK_HZ,
```

Build and optically test at 40 and 50 MHz. Keep 60 MHz as a diagnostic build, not the initial correctness proof.

---

# 5. One staging compositor, not a layer framework

The callback paints only the fixed overlays and current provisional tail. It does not own scheduling or cache state.

```cpp
// vector_v2_presenter.h
struct ProvisionalTail {
  std::array<RibbonPrimitive, 8> primitives{};
  std::size_t count = 0;
  std::uint16_t color = 0;
  vector_v2::PixelRect bounds{};
  bool valid = false;

  void clear() { *this = {}; }
};

struct PresenterStageContext {
  VectorV2Presenter* presenter = nullptr;
  const vector_v2::ChromeState* chrome = nullptr;
  vector_v2::ChromeNavigation navigation{};
  bool include_provisional_tail = false;
};
```

```cpp
static void stage_patch_thunk(void* raw,
                              const PanelStageSurface& surface) {
  auto& context = *static_cast<PresenterStageContext*>(raw);
  context.presenter->paint_stage_surface(surface, *context.chrome,
                                         context.navigation,
                                         context.include_provisional_tail);
}
```

```cpp
void VectorV2Presenter::paint_stage_surface(
    const PanelStageSurface& surface,
    const vector_v2::ChromeState& chrome,
    const vector_v2::ChromeNavigation& navigation,
    bool include_provisional_tail) {
  vector_v2::draw_chrome_strip_overlays(
      {.pixels = surface.pixels,
       .width = surface.width,
       .height = surface.height,
       .origin_x = surface.panel_x,
       .origin_y = surface.panel_y},
      chrome, navigation);

  if (!include_provisional_tail || !provisional_tail_.valid) {
    return;
  }

  std::array<RibbonPrimitive, 8> local{};
  std::size_t written = 0;
  for (std::size_t i = 0; i < provisional_tail_.count; ++i) {
    const RibbonPrimitive& source = provisional_tail_.primitives[i];
    if (!primitive_intersects(source, surface_rect(surface))) {
      continue;
    }
    RibbonPrimitive translated = source;
    translate_primitive(translated, -static_cast<float>(surface.panel_x),
                        -static_cast<float>(surface.panel_y));
    local[written++] = translated;
  }
  if (written != 0U) {
    renderer_->render(std::span(local).first(written), surface.pixels,
                      surface.width, surface.height, provisional_tail_.color);
  }
}
```

This reuses the existing `draw_chrome_strip_overlays()` seam. Once this works, remove `copy_ring_region`, `write_ring_region`, overlay mutation, and restore from the pan path. The ring remains canvas-only.

---

# 6. No-lag ink: preview first, replaceable tail, no authority redesign yet

The cheapest correct first implementation is:

- stable `update.committed` geometry is painted into `frame_` immediately;
- `update.provisional` is stored only in `ProvisionalTail` and patched during staging;
- each presentation covers the union of old-tail, new-tail, and stable-geometry damage;
- the expensive chunk commit runs only after the newest sample has been submitted;
- `frame_reusable_` stays false until lift commit and authoritative refresh finish.

## Presenter update

```cpp
LivePresentationTiming VectorV2Presenter::show_update(
    const RibbonUpdate& update, std::uint16_t color,
    const vector_v2::ChromeState& chrome, std::uint32_t event_us) {
  if (frame_ring_bottom_ != 0 && !refresh(chrome, event_us).passed) {
    return {};
  }
  frame_reuse_.reset();

  const int canvas_bottom = vector_v2::chrome_input_bottom(chrome);
  if (canvas_bottom == 0) {
    provisional_tail_.clear();
    return {.passed = true};
  }

  const vector_v2::PixelRect old_tail =
      provisional_tail_.valid ? provisional_tail_.bounds
                              : vector_v2::PixelRect{};

  vector_v2::PixelRect stable_bounds{};
  bool stable_valid = false;
  if (!update.committed.empty()) {
    const auto committed =
        std::span(update.committed.begin(), update.committed.size());
    renderer_->render(committed, frame_, vector_v2::kOverviewWidth,
                      canvas_bottom, color);
    stable_bounds = primitive_bounds(committed, canvas_bottom);
    stable_valid = true;
  }

  provisional_tail_.clear();
  if (!update.provisional.empty()) {
    provisional_tail_.count = update.provisional.size();
    provisional_tail_.color = color;
    std::copy(update.provisional.begin(), update.provisional.end(),
              provisional_tail_.primitives.begin());
    provisional_tail_.bounds = primitive_bounds(
        std::span(provisional_tail_.primitives).first(
            provisional_tail_.count), canvas_bottom);
    provisional_tail_.valid = true;
  }

  std::optional<vector_v2::PixelRect> damage;
  if (old_tail.x1 > old_tail.x0 && old_tail.y1 > old_tail.y0) {
    damage = old_tail;
  }
  if (stable_valid) {
    include_bounds(damage, stable_bounds);
  }
  if (provisional_tail_.valid) {
    include_bounds(damage, provisional_tail_.bounds);
  }
  if (!damage.has_value()) {
    return {.passed = true};
  }

  // Unlike present_unobscured(), this is one ordered presentation. Fixed
  // overlays and the new provisional tail are restored by the stage patch.
  return present_composited(*damage, chrome, event_us,
                            /*include_provisional_tail=*/true);
}
```

`show_start()` can render its contact cap into `frame_`, clear the provisional tail, and call the same `present_composited()` path.

## Hot-loop order

At `vector_v2_app.cpp:981-1027`, keep `builder.add()` before preview only because it is cheap validation. Move the commit after the presentation:

```cpp
const auto add_point = presenter.operation_point(last_ink);
ChainedOperationStatus add_status = builder.add(add_point);
if (add_status == ChainedOperationStatus::kRejected) {
  reject_current_tail();
  continue;
}

const std::uint16_t color = chrome.tool == vector_v2::ChromeTool::kErase
                                ? 0xFFFFU
                                : vector_v2::selected_color(chrome);
const RibbonUpdate update = ribbon.append(last_ink, true);
live_metrics.include(
    presenter.show_update(update, color, chrome, event_us));

// The newest consumed sample is already on its way to glass.
if (add_status == ChainedOperationStatus::kChunkReady) {
  const auto continued = commit_ready_chunk(
      builder, log, canvas, workspace, presenter, stroke_world_bounds,
      stroke_chunks, stroke_append_us, stroke_append_max_us);
  // existing rejection handling...
}
```

Also change these timestamps:

```cpp
presenter.show_start(last_ink, color, chrome, event_us);  // not loop_us
presenter.pan_from(..., event_us);                        // not loop_us
```

At lift, use the actual Up event timestamp and present before draining chunks:

```cpp
const std::uint32_t finished_us = event_us;
ChainedOperationStatus finish_status =
    builder.finish(presenter.operation_point(last_ink));

last_ink = ink.finish({.x = last_ink.position.x,
                       .y = last_ink.position.y,
                       .timestamp_us = finished_us});
live_metrics.include(
    presenter.show_update(ribbon.finish(last_ink), color, chrome,
                          finished_us));

// Only now perform the authoritative drain.
while (finish_status == ChainedOperationStatus::kChunkReady ||
       finish_status == ChainedOperationStatus::kFinalChunkReady) {
  // existing commit loop
}
```

After the final authoritative refresh succeeds:

```cpp
presenter.clear_provisional_tail();
```

## Conditional escalation: only if optical ink still fails

Do not build this first. If preview-first still produces unacceptable gaps, add a two- or three-entry owned chunk queue and turn `append_incrementally_in_place()` into a resumable transaction.

```cpp
struct OwnedOperationChunk {
  OperationTool tool = OperationTool::kPen;
  std::uint16_t color = 0;
  std::uint16_t gesture_id = 0;
  std::array<CompactOperationSample, kInteractiveChunkSampleLimit> samples{};
  std::size_t sample_count = 0;
  bool final = false;

  [[nodiscard]] OperationAppend view() const {
    return {.tool = tool,
            .color = color,
            .gesture_id = gesture_id,
            .samples = std::span(samples).first(sample_count)};
  }
};

template <std::size_t Capacity>
class CommitQueue {
 public:
  [[nodiscard]] bool push(const OperationAppend& append, bool final);
  [[nodiscard]] OwnedOperationChunk* front();
  void pop();
 private:
  std::array<OwnedOperationChunk, Capacity> queue_{};
  std::size_t head_ = 0;
  std::size_t count_ = 0;
};
```

The resumable job should be a concrete state machine, not a generalized task system:

```cpp
enum class CommitPhase : std::uint8_t {
  kIdle,
  kPrepareLog,
  kCopyOverviewRows,
  kRasterOverviewSegments,
  kEnumerateAffected,
  kPaintPriorityTiles,
  kCommitMetadata,
  kDone,
  kFailed,
};

class IncrementalCommitJob {
 public:
  bool begin(const OwnedOperationChunk& chunk);
  bool step_until(std::int64_t deadline_us);
  [[nodiscard]] bool done() const;
  [[nodiscard]] bool failed() const;
 private:
  CommitPhase phase_ = CommitPhase::kIdle;
  std::size_t row_cursor_ = 0;
  std::size_t segment_cursor_ = 0;
  std::size_t tile_cursor_ = 0;
  // prepared append, scratch bounds, retained keys, etc.
};
```

Every phase must checkpoint inside workload-proportional loops. Target 1–2 ms units, not a nominal 10 ms deadline around an indivisible 14 ms operation.

---

# 7. Frame reuse becomes a token, not a bool

Replace `bool frame_reusable_` with evidence-bearing state.

```cpp
enum class FrameSyncStatus : std::uint8_t {
  kNone,
  kEdgeObserved,
  kValidatedPolicy,
};

struct FrameReuseToken {
  vector_v2::DocumentRevision revision{};
  vector_v2::ZoomLevel zoom = vector_v2::ZoomLevel::k25Percent;
  int level_x = 0;
  int level_y = 0;
  std::uint32_t chrome_generation = 0;
  FrameSyncStatus sync = FrameSyncStatus::kNone;
};

std::optional<FrameReuseToken> frame_reuse_{};
```

Only issue the token when all of these are true:

```cpp
if (timing.passed && timing.edge_observed &&
    presentation_policy_.optically_validated) {
  frame_reuse_ = FrameReuseToken{
      .revision = canvas_.current_revision(),
      .zoom = zoom(),
      .level_x = level_x(),
      .level_y = level_y(),
      .chrome_generation = chrome_generation_,
      .sync = FrameSyncStatus::kValidatedPolicy,
  };
} else {
  frame_reuse_.reset();
}
```

A timeout or unvalidated policy can still display a diagnostic frame, but it cannot seed cached pan.

---

# 8. Compact replay index: macrocell-to-operation-block bitsets

This index is fixed-size, append-friendly, undo-prefix-friendly, and only a few kilobytes.

Use 128×128 world macrocells and 16-operation blocks:

```cpp
// replay_block_index.h
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "tinydraw/vector_v2/memory_layout.h"
#include "tinydraw/vector_v2/operation_log.h"

namespace tinydraw::vector_v2 {

inline constexpr int kReplayCellWorldSize = 128;
inline constexpr int kReplayCellColumns =
    (kWorldWidth + kReplayCellWorldSize - 1) / kReplayCellWorldSize;   // 12
inline constexpr int kReplayCellRows =
    (kWorldHeight + kReplayCellWorldSize - 1) / kReplayCellWorldSize; // 14
inline constexpr std::size_t kReplayCellCount =
    static_cast<std::size_t>(kReplayCellColumns) * kReplayCellRows;   // 168
inline constexpr std::size_t kReplayOperationsPerBlock = 16;
inline constexpr std::size_t kReplayBlockCount =
    (kOperationCapacity + kReplayOperationsPerBlock - 1U) /
    kReplayOperationsPerBlock;                                       // 250
inline constexpr std::size_t kReplayBlockWords =
    (kReplayBlockCount + 31U) / 32U;                                 // 8
inline constexpr std::size_t kReplayIndexWords =
    kReplayCellCount * kReplayBlockWords;                             // 1344
inline constexpr std::size_t kReplayIndexBytes =
    kReplayIndexWords * sizeof(std::uint32_t);                        // 5376

struct ReplayCandidateCursor {
  std::array<std::uint32_t, kReplayBlockWords> candidate_blocks{};
  std::size_t first_operation = 0;
  std::size_t next_operation = 0;
};

class ReplayBlockIndex {
 public:
  explicit ReplayBlockIndex(std::span<std::uint32_t> cell_block_words)
      : words_(cell_block_words) {}

  [[nodiscard]] bool ready() const {
    return words_.size() >= kReplayIndexWords;
  }

  [[nodiscard]] bool sync(const OperationLog& log);
  [[nodiscard]] ReplayCandidateCursor query(
      PixelRect world_bounds, std::size_t first_operation,
      std::size_t operation_count) const;
  [[nodiscard]] std::optional<std::size_t> previous(
      ReplayCandidateCursor& cursor) const;
  void reset();

 private:
  void index_operation(std::size_t operation_index, PixelRect bounds);
  [[nodiscard]] std::span<std::uint32_t> cell_words(std::size_t cell);
  [[nodiscard]] std::span<const std::uint32_t> cell_words(
      std::size_t cell) const;

  std::span<std::uint32_t> words_{};
  OperationLogEpoch epoch_{};
  std::uint64_t storage_generation_ = 0;
  std::size_t indexed_operations_ = 0;
};

}  // namespace tinydraw::vector_v2
```

Append indexing:

```cpp
void ReplayBlockIndex::index_operation(std::size_t operation_index,
                                       PixelRect bounds) {
  const std::size_t block = operation_index / kReplayOperationsPerBlock;
  const std::size_t word = block / 32U;
  const std::uint32_t bit = 1U << (block % 32U);

  const int first_x = std::clamp(bounds.x0 / kReplayCellWorldSize,
                                 0, kReplayCellColumns - 1);
  const int first_y = std::clamp(bounds.y0 / kReplayCellWorldSize,
                                 0, kReplayCellRows - 1);
  const int last_x = std::clamp((bounds.x1 - 1) / kReplayCellWorldSize,
                                0, kReplayCellColumns - 1);
  const int last_y = std::clamp((bounds.y1 - 1) / kReplayCellWorldSize,
                                0, kReplayCellRows - 1);

  for (int y = first_y; y <= last_y; ++y) {
    for (int x = first_x; x <= last_x; ++x) {
      const std::size_t cell =
          static_cast<std::size_t>(y * kReplayCellColumns + x);
      cell_words(cell)[word] |= bit;
    }
  }
}
```

`sync()` rebuilds only when storage was reset/truncated; ordinary appends extend it:

```cpp
bool ReplayBlockIndex::sync(const OperationLog& log) {
  if (!ready()) {
    return false;
  }
  if (epoch_ != log.epoch() ||
      storage_generation_ != log.storage_generation() ||
      indexed_operations_ > log.stored_operation_count()) {
    reset();
    epoch_ = log.epoch();
    storage_generation_ = log.storage_generation();
  }
  while (indexed_operations_ < log.stored_operation_count()) {
    const auto operation = log.stored_operation(indexed_operations_);
    if (!operation.has_value()) {
      return false;
    }
    index_operation(indexed_operations_, operation->world_bounds);
    ++indexed_operations_;
  }
  return true;
}
```

Query ORs the small set of cells touched by a 128×128 group, then masks by the active replay range. Exact operation bounds remain the correctness gate.

```cpp
ReplayCandidateCursor ReplayBlockIndex::query(
    PixelRect bounds, std::size_t first_operation,
    std::size_t operation_count) const {
  ReplayCandidateCursor cursor{
      .first_operation = first_operation,
      .next_operation = first_operation + operation_count,
  };
  if (!ready() || operation_count == 0U) {
    return cursor;
  }

  const int first_x = std::clamp(bounds.x0 / kReplayCellWorldSize,
                                 0, kReplayCellColumns - 1);
  const int first_y = std::clamp(bounds.y0 / kReplayCellWorldSize,
                                 0, kReplayCellRows - 1);
  const int last_x = std::clamp((bounds.x1 - 1) / kReplayCellWorldSize,
                                0, kReplayCellColumns - 1);
  const int last_y = std::clamp((bounds.y1 - 1) / kReplayCellWorldSize,
                                0, kReplayCellRows - 1);

  for (int y = first_y; y <= last_y; ++y) {
    for (int x = first_x; x <= last_x; ++x) {
      const auto source = cell_words(
          static_cast<std::size_t>(y * kReplayCellColumns + x));
      for (std::size_t word = 0; word < kReplayBlockWords; ++word) {
        cursor.candidate_blocks[word] |= source[word];
      }
    }
  }
  return cursor;
}
```

```cpp
std::optional<std::size_t> ReplayBlockIndex::previous(
    ReplayCandidateCursor& cursor) const {
  while (cursor.next_operation > cursor.first_operation) {
    const std::size_t candidate = cursor.next_operation - 1U;
    const std::size_t block = candidate / kReplayOperationsPerBlock;
    const std::uint32_t mask = 1U << (block % 32U);
    if ((cursor.candidate_blocks[block / 32U] & mask) != 0U) {
      cursor.next_operation = candidate;
      return candidate;
    }
    const std::size_t block_first = block * kReplayOperationsPerBlock;
    cursor.next_operation = std::max(cursor.first_operation, block_first);
  }
  return std::nullopt;
}
```

## Tile producer integration

Change `ActiveGroup` from a contiguous decrementing cursor to an indexed candidate cursor:

```cpp
struct ActiveGroup {
  ZoomLevel zoom = ZoomLevel::k50Percent;
  TileKey origin{};
  PixelRect bounds{};
  OperationLogEpoch epoch{};
  DocumentRevision revision{};
  ReplayCandidateCursor candidates{};
  std::optional<std::size_t> operation_index{};
  std::size_t next_sample = 0;
  StoredOperation cached_operation{};
  bool active = false;
};
```

At group start:

```cpp
const PixelRect world_query = conservative_world_bounds(bounds, view.zoom);
active_group_.candidates = replay_index_.query(
    world_query, replay->first_operation, replay->operation_count);
```

When no operation is active:

```cpp
const auto next = replay_index_.previous(active_group_.candidates);
if (!next.has_value()) {
  return publish_group(...);
}
active_group_.operation_index = *next;
const auto stored = log_.operation(*next);
// existing exact bbox/saturation gates follow
```

Add to `TileProductionStep`:

```cpp
std::size_t candidate_blocks = 0;
std::size_t blocks_skipped = 0;
```

The go/no-go gate for this index is at least a 25% reduction in adversarial current-view completion, exact output unchanged. If it does not reach that, change block granularity or cell size before inventing dynamic postings.

---

# 9. Durable repair tasks: group identity survives camera changes

First remove exact-view identity from `TileProducer::ActiveGroup`. A group is determined by `(revision, zoom, group origin)`, not by the viewport that requested it.

```cpp
struct RepairKey {
  DocumentRevision revision{};
  ZoomLevel zoom = ZoomLevel::k50Percent;
  std::uint16_t group_column = 0; // even tile column
  std::uint16_t group_row = 0;    // even tile row
  bool operator==(const RepairKey&) const = default;
};

enum class RepairPriority : std::uint8_t {
  kOptional,
  kRememberedZoom,
  kVelocityHalo,
  kCurrentViewport,
};

struct RepairEntry {
  RepairKey key{};
  RepairPriority priority = RepairPriority::kOptional;
  std::uint32_t last_needed_generation = 0;
  std::uint32_t attempts = 0;
  bool active = false;
};
```

```cpp
class RepairQueue {
 public:
  explicit RepairQueue(std::span<RepairEntry> storage) : entries_(storage) {}

  void begin_plan(DocumentRevision revision) {
    revision_ = revision;
    ++plan_generation_;
  }

  bool require(RepairKey key, RepairPriority priority);
  [[nodiscard]] std::optional<RepairKey> next() const;
  void mark_attempt(RepairKey key);
  void mark_complete(RepairKey key);
  void prune();
  [[nodiscard]] RepairWorkCounters counters() const;

 private:
  std::span<RepairEntry> entries_{};
  DocumentRevision revision_{};
  std::uint32_t plan_generation_ = 0;
  RepairWorkCounters counters_{};
};
```

Planning a view enumerates 2×2 group origins rather than storing a whole `ViewRequest`:

```cpp
void require_view(RepairQueue& queue, const ViewRequest& view,
                  DocumentRevision revision, RepairPriority priority) {
  const int first_column = (view.level_pixels.x0 / kTileWidth) & ~1;
  const int first_row = (view.level_pixels.y0 / kTileHeight) & ~1;
  const int last_column =
      ((view.level_pixels.x1 + kTileWidth - 1) / kTileWidth + 1) & ~1;
  const int last_row =
      ((view.level_pixels.y1 + kTileHeight - 1) / kTileHeight + 1) & ~1;

  for (int row = first_row; row < last_row; row += 2) {
    for (int column = first_column; column < last_column; column += 2) {
      queue.require({.revision = revision,
                     .zoom = view.zoom,
                     .group_column = static_cast<std::uint16_t>(column),
                     .group_row = static_cast<std::uint16_t>(row)},
                    priority);
    }
  }
}
```

Add a direct producer entry point:

```cpp
std::optional<TileProductionStep> TileProducer::produce_group(
    RepairKey key, std::optional<ViewRequest> visible_view);
```

`visible_view` affects publication priority/statistics only. It does not affect whether an in-progress group remains valid.

The app’s background logic becomes explicit and small:

```cpp
if (view_or_revision_changed) {
  repair.begin_plan(canvas.current_revision());
  require_view(repair, navigation.view(), canvas.current_revision(),
               RepairPriority::kCurrentViewport);
  require_velocity_halo(repair, navigation, pan_velocity);
  require_remembered_views(repair, canvas.recent_views());
  repair.prune();
}

if (const auto key = repair.next(); key.has_value()) {
  repair.mark_attempt(*key);
  const auto step = producer.produce_group(*key, navigation.view());
  if (step.has_value() && step->complete) {
    repair.mark_complete(*key);
  }
}
```

A nearby camera move now changes priority, not task identity. An already published tile remains reusable through the canvas regardless of plan generation.

---

# 10. Freeze `OperationLogSnapshot` before undo, autosave, and SVG

This is the second key interface. It lets leaf features read authority without knowing cache or app state.

```cpp
struct OperationLogSnapshot {
  OperationLogEpoch epoch{};
  std::uint64_t state_generation = 0;
  DocumentRevision revision{};
  std::span<const OperationRecord> records{};
  std::span<const CompactOperationSample> samples{};
  std::size_t active_operation_count = 0;
  std::size_t active_sample_count = 0;
};
```

Add now, while behavior is still append-only:

```cpp
[[nodiscard]] OperationLogSnapshot OperationLog::snapshot() const {
  return {
      .epoch = epoch_,
      .state_generation = state_generation_,
      .revision = revision_,
      .records = records_.first(operation_count_),
      .samples = samples_.first(sample_count_),
      .active_operation_count = operation_count_,
      .active_sample_count = sample_count_,
  };
}
```

`WorldBandRenderer`, replay indexing, SVG, and autosave can migrate to this seam before undo changes the internal counts.

---

# 11. Undo/redo: active prefix by whole gesture

Do not implement selective tombstones or inverse paint. The log is already grouped by nonzero `gesture_id`; use an active prefix with a retained redo tail.

## OperationLog state

```cpp
std::size_t stored_operation_count_ = 0;
std::size_t stored_sample_count_ = 0;
std::size_t active_operation_count_ = 0;
std::size_t active_sample_count_ = 0;
std::uint64_t state_generation_ = 1;
std::uint64_t storage_generation_ = 1;
```

Public API:

```cpp
[[nodiscard]] std::size_t operation_count() const {
  return active_operation_count_;
}
[[nodiscard]] std::size_t stored_operation_count() const {
  return stored_operation_count_;
}
[[nodiscard]] std::uint64_t state_generation() const {
  return state_generation_;
}
[[nodiscard]] std::uint64_t storage_generation() const {
  return storage_generation_;
}
[[nodiscard]] bool can_undo() const {
  return active_operation_count_ != 0U;
}
[[nodiscard]] bool can_redo() const {
  return active_operation_count_ < stored_operation_count_;
}
[[nodiscard]] std::optional<StoredOperation> stored_operation(
    std::size_t index) const;
```

Add a born revision or stable operation serial to `OperationRecord`; after undo, `base_revision + index` is no longer a valid operation identity.

```cpp
struct OperationRecord {
  // existing fields...
  std::uint32_t born_revision = 0;
};
```

The 16 KiB maximum record growth should be included in the memory gate.

## Prepared history mutation

```cpp
enum class HistoryDirection : std::uint8_t { kUndo, kRedo };

struct HistoryPreview {
  HistoryDirection direction = HistoryDirection::kUndo;
  std::uint64_t expected_state_generation = 0;
  DocumentRevision destination_revision{};
  std::size_t target_operation_count = 0;
  std::size_t target_sample_count = 0;
  std::uint16_t gesture_id = 0;
  PixelRect affected_world_bounds{};
};

[[nodiscard]] std::optional<HistoryPreview> preview_undo() const;
[[nodiscard]] std::optional<HistoryPreview> preview_redo() const;
[[nodiscard]] OperationLogSnapshot snapshot_for(
    const HistoryPreview& preview) const;
[[nodiscard]] bool publish_history(const HistoryPreview& preview);
```

Undo preview:

```cpp
std::optional<HistoryPreview> OperationLog::preview_undo() const {
  if (!can_undo() || append_pending_ ||
      revision_.value == std::numeric_limits<std::uint32_t>::max()) {
    return std::nullopt;
  }

  const std::uint16_t gesture =
      records_[active_operation_count_ - 1U].gesture_id;
  std::size_t first = active_operation_count_ - 1U;
  PixelRect bounds = record_bounds(records_[first]);
  while (first != 0U && records_[first - 1U].gesture_id == gesture) {
    --first;
    include(bounds, record_bounds(records_[first]));
  }

  const std::size_t samples =
      first == 0U
          ? 0U
          : static_cast<std::size_t>(records_[first - 1U].first_sample) +
                records_[first - 1U].sample_count;
  return HistoryPreview{
      .direction = HistoryDirection::kUndo,
      .expected_state_generation = state_generation_,
      .destination_revision = {revision_.value + 1U},
      .target_operation_count = first,
      .target_sample_count = samples,
      .gesture_id = gesture,
      .affected_world_bounds = bounds,
  };
}
```

Redo walks forward over the next gesture ID.

Before a new append after undo:

```cpp
void OperationLog::truncate_redo() {
  if (!can_redo()) {
    return;
  }
  stored_operation_count_ = active_operation_count_;
  stored_sample_count_ = active_sample_count_;
  ++storage_generation_;
}
```

The replay index sees `storage_generation_` change and rebuilds. Undo/redo without truncation only changes the active range and does not rebuild.

## Applying history to the canvas

Add one exact region renderer that starts from white paper and forward-replays indexed active operations intersecting the affected overview rectangle.

```cpp
struct HistoryWorkspace {
  std::span<std::uint16_t> overview_scratch{};
};

std::optional<OverviewRevisionPublication> render_history_overview_region(
    const OperationLogSnapshot& snapshot, const ReplayBlockIndex& index,
    PixelRect affected_world_bounds, const HistoryWorkspace& workspace);
```

Then coordinate authority and materialization in one function:

```cpp
std::optional<PixelRect> apply_history_change(
    OperationLog& log, MaterializedCanvas& canvas,
    const ReplayBlockIndex& index, HistoryDirection direction,
    const HistoryWorkspace& workspace) {
  const auto preview = direction == HistoryDirection::kUndo
                           ? log.preview_undo()
                           : log.preview_redo();
  if (!preview.has_value()) {
    return std::nullopt;
  }

  const auto target = log.snapshot_for(*preview);
  const auto overview = render_history_overview_region(
      target, index, preview->affected_world_bounds, workspace);
  if (!overview.has_value() ||
      !canvas.can_commit_history_revision(
          preview->destination_revision, *overview,
          preview->affected_world_bounds)) {
    return std::nullopt;
  }

  // publish_history is now infallible because generation was validated;
  // commit_history_revision is infallible after can_* validation.
  if (!log.publish_history(*preview) ||
      !canvas.commit_history_revision(
          preview->destination_revision, *overview,
          preview->affected_world_bounds)) {
    std::abort(); // invariant failure in debug; fail-safe recovery in product
  }
  return preview->affected_world_bounds;
}
```

`commit_history_revision()` updates the overview rectangle and invalidates every intersecting tile identity at every zoom. It does not rerender unrelated tiles.

Wire the existing UI cases:

```cpp
case ChromeAction::kUndo:
  if (const auto changed = apply_history_change(
          log, canvas, replay_index, HistoryDirection::kUndo,
          history_workspace); changed.has_value()) {
    chrome.can_undo = log.can_undo();
    chrome.can_redo = log.can_redo();
    return presenter.refresh_region(
        operation_level_bounds(*changed, presenter.zoom()), chrome,
        now_us()).passed;
  }
  return true;

case ChromeAction::kRedo:
  // symmetric
```

Update `chrome.can_undo/can_redo` after append, undo, redo, restore, and New.

---

# 12. Autosave: two full authority slots in the existing 3 MiB drawing partition

The maximum current authority is roughly 720 KiB before headers, so two 1.5 MiB slots fit the existing partition. Persist only active authority and view state, not tiles or presentation caches.

```cpp
// vector_v2_autosave.h
class VectorV2Autosave {
 public:
  struct ViewState {
    vector_v2::ZoomLevel zoom = vector_v2::ZoomLevel::k25Percent;
    int origin_x = 0;
    int origin_y = 0;
  };

  VectorV2Autosave();
  ~VectorV2Autosave();

  [[nodiscard]] bool ready() const;
  [[nodiscard]] bool restore(vector_v2::OperationLog& log,
                             vector_v2::ReplayBlockIndex& index,
                             vector_v2::MaterializedCanvas& canvas,
                             ViewState& view,
                             std::span<std::uint16_t> overview_scratch);

  void activity(const vector_v2::OperationLog& log, ViewState view);
  void suspend_and_wait();
};
```

On-flash format:

```cpp
constexpr std::uint32_t kSnapshotMagic = 0x32564454U; // "TDV2"
constexpr std::uint32_t kSnapshotVersion = 1;
constexpr std::size_t kSectorBytes = 4096;
constexpr std::size_t kSlotBytes = 0x18'0000; // 1.5 MiB

struct SnapshotHeader {
  std::uint32_t magic = kSnapshotMagic;
  std::uint32_t version = kSnapshotVersion;
  std::uint32_t generation = 0;
  std::uint32_t revision = 0;
  std::uint32_t operation_count = 0;
  std::uint32_t sample_count = 0;
  std::uint32_t record_bytes = 0;
  std::uint32_t sample_bytes = 0;
  std::uint32_t payload_crc32 = 0;
  std::uint8_t zoom = 0;
  std::int32_t origin_x = 0;
  std::int32_t origin_y = 0;
};

struct SnapshotFooter {
  std::uint32_t magic = kSnapshotMagic ^ 0xFFFF'FFFFU;
  std::uint32_t generation = 0;
  std::uint32_t payload_crc32 = 0;
  std::uint32_t committed = 0xC011'17EDU;
};
```

Write the inactive slot in this order:

```text
erase sectors incrementally
header
active OperationRecord prefix
active CompactOperationSample prefix
footer last
```

A slot is valid only if header, footer, generation, sizes, and CRC all agree. Power loss before the footer leaves the previous slot authoritative.

The save task should borrow the Raster V1 pattern but copy only one 4 KiB page under the log mutex, then release the mutex before flash I/O:

```cpp
while (save_requested) {
  wait_until_500ms_quiet();
  if (activity_generation_changed()) {
    restart_from_latest_descriptor();
  }

  lock_log();
  const bool copied = log.copy_snapshot_bytes(descriptor, source_offset,
                                               sector_buffer);
  unlock_log();
  if (!copied) {
    restart_from_latest_descriptor();
    continue;
  }

  erase_or_write_one_sector();
  if (user_activity_seen()) {
    pause_after_this_sector();
  }
}
```

New/Clear must call `suspend_and_wait()` before resetting operation storage. Ordinary appends do not mutate the captured active prefix, so an older snapshot may finish safely; a new generation is scheduled afterward.

Restore flow:

```text
choose highest valid slot
restore active records/samples/revision into OperationLog
rebuild ReplayBlockIndex
render complete 25% overview from authority
canvas.restore_snapshot(revision, overview)
restore/clamp NavigationState
```

Add a host power-cut test that truncates the slot after every sector and proves restore always selects either the old complete generation or the new complete generation—never a hybrid.

---

# 13. SVG export: compact semantic export, one file at a time

Do not generalize the USB disk to multiple files yet. Generalize only the 8.3 filename, then expose either PNG or SVG per export action.

```cpp
// core/include/tinydraw/export/fat16_disk.h
struct Fat83Name {
  std::array<char, 11> bytes{'D','R','A','W','I','N','G',' ',
                             'S','V','G'};
};

class Fat16ExportDisk {
 public:
  Fat16ExportDisk(const ReadOnlyFile& file, Fat83Name name)
      : file_(file), name_(name) {}
 private:
  const ReadOnlyFile& file_;
  Fat83Name name_{};
};
```

The root-sector builder copies `name_.bytes` instead of the hard-coded `DRAWING PNG`.

## Authority-only exporter

```cpp
// vector_v2/include/tinydraw/vector_v2/svg_export.h
class TextSink {
 public:
  virtual ~TextSink() = default;
  virtual bool append(std::string_view text) = 0;
};

struct SvgExportStats {
  std::size_t operations = 0;
  std::size_t points = 0;
  std::size_t bytes = 0;
  bool complete = false;
  bool overflow = false;
};

class SvgExporter {
 public:
  [[nodiscard]] SvgExportStats encode(const OperationLogSnapshot& snapshot,
                                      TextSink& sink) const;
};
```

To avoid a worst-case 5+ MiB explosion, use a compact path per operation chunk. Quantize stroke width and start a new subpath only when radius changes materially. This is a semantic vector export, not a pixel-exact outline export.

```cpp
bool emit_operation(const OperationRecord& record,
                    std::span<const CompactOperationSample> samples,
                    TextSink& sink) {
  const std::uint16_t color =
      record.tool == OperationTool::kEraser ? 0xFFFFU : record.color;
  const float width = representative_width(samples); // e.g. median 2r

  append(sink, "<path fill=\"none\" stroke=\"");
  append_rgb565(sink, color);
  append(sink, "\" stroke-linecap=\"round\" stroke-linejoin=\"round\" stroke-width=\"");
  append_float(sink, width);
  append(sink, "\" d=\"M");

  for (std::size_t i = 0; i < samples.size(); ++i) {
    const float x = static_cast<float>(samples[i].x_quarter) * 0.25F;
    const float y = static_cast<float>(samples[i].y_quarter) * 0.25F;
    if (i != 0U) {
      append(sink, " L");
    }
    append_float(sink, x);
    append(sink, " ");
    append_float(sink, y);
  }
  return append(sink, "\"/>\n");
}
```

If pressure fidelity is product-critical, split an operation into width runs or emit explicit capsule geometry later. Do not make exact variable-width SVG outlines part of performance closure unless required.

Before encoding, estimate the upper bound and fail cleanly if it cannot fit the 5 MiB export partition. Stream directly into the partition; no complete SVG RAM buffer is needed.

---

# 14. Minimap input: rendering already exists

Expose the interior constants or add one mapping function in `chrome.h`:

```cpp
[[nodiscard]] std::optional<NavigationPoint> chrome_minimap_target(
    ChromePoint point, const ChromeState& state,
    const ChromeNavigation& navigation);
```

```cpp
std::optional<NavigationPoint> chrome_minimap_target(
    ChromePoint point, const ChromeState& state,
    const ChromeNavigation& navigation) {
  if (!canvas_overlays_visible(state) ||
      !inside(point, static_cast<float>(kMinimapLeft),
              static_cast<float>(kMinimapTop),
              static_cast<float>(kMinimapLeft + kMinimapWidth),
              static_cast<float>(kMinimapTop + kMinimapHeight))) {
    return std::nullopt;
  }

  const int center_x = static_cast<int>(
      (point.x - static_cast<float>(kMinimapLeft)) *
      static_cast<float>(navigation.level_width) /
      static_cast<float>(kMinimapWidth));
  const int center_y = static_cast<int>(
      (point.y - static_cast<float>(kMinimapTop)) *
      static_cast<float>(navigation.level_height) /
      static_cast<float>(kMinimapHeight));

  return NavigationState::clamp_origin(
      zoom_from_percent(navigation.zoom_percent),
      center_x - kOverviewWidth / 2,
      center_y - kChromeCanvasBottom / 2);
}
```

Add `kMinimapNavigate` to `ChromeAction`, include the minimap in `chrome_contains()`, and return the action from `chrome_action_at()`.

```cpp
case ChromeAction::kMinimapNavigate:
  if (const auto target = vector_v2::chrome_minimap_target(
          {point.x, point.y}, chrome, presenter.chrome_navigation());
      target.has_value()) {
    return presenter.set_view(presenter.zoom(), target->x, target->y,
                              chrome, now_us()).passed;
  }
  return true;
```

Ship tap-to-jump first. Dragging the minimap can be a later refinement; do not add another gesture state machine before closure.

---

# 15. Explicit cooperative coordinator

Keep this as an `if` chain in `vector_v2_app.cpp`. It is easier to audit than a general scheduler.

```cpp
void service_cooperative_work(AppWork& work, std::int64_t budget_us) {
  const std::int64_t deadline = esp_timer_get_time() + budget_us;

  while (esp_timer_get_time() < deadline) {
    if (work.touch_sampler.has_pending()) {
      return;
    }

    if (work.commit_job.active()) {
      if (!work.commit_job.step_until(deadline)) {
        return;
      }
      continue;
    }

    if (work.history_job.active()) {
      if (!work.history_job.step_until(deadline)) {
        return;
      }
      continue;
    }

    if (work.current_view_fill_needed()) {
      work.step_current_view_fill();
      continue;
    }

    if (work.autosave_can_advance()) {
      work.step_autosave();
      continue;
    }

    if (work.repair_can_advance()) {
      work.step_repair();
      continue;
    }

    return;
  }
}
```

Policy around it:

```text
pen down: presentation + at most commit micro-slices
panning: presentation only
quiet, current view inexact: current-view fill first
quiet, current view exact: autosave sector, then repair
```

If the initial preview-first ink passes without a resumable commit job, omit that branch entirely.

---

# 16. Safeguards and tests added immediately with each mechanism

## Host gates

```text
replay_block_index_test.cpp
  candidate set is a superset of exact intersecting operations
  reverse order preserved
  active prefix masks redo tail
  truncate/rebuild has no false negatives
  huge operation touching all cells remains exact

repair_queue_test.cpp
  nearby camera move retains overlapping active task
  current viewport outranks halo and remembered zoom
  revision change invalidates old tasks
  queue saturation evicts optional work first

operation_log_test.cpp
  undo removes all chunks sharing the final gesture_id
  redo restores exactly that gesture
  append after undo truncates redo
  revisions remain monotonic
  snapshot exposes only active prefix

autosave_format_test.cpp
  power cut after every sector restores an old or new complete slot
  CRC corruption rejects slot
  maximum-capacity authority fits

svg_export_test.cpp
  one path per operation/run
  eraser maps to white
  bounds and color conversion
  output-size guard

chrome_test.cpp
  minimap corners and center map/clamp correctly

panel_staging_test.cpp
  ring copy -> patch -> swap exactness
  patch receives correct absolute panel coordinates
  strip boundaries do not alter output
```

## Hardware gates

```text
PAN_GLASS:
  ≥10,000 canonical frames
  zero mixed frame IDs, white notches, stale bands, or seams
  p95 optical interval ≤41.7 ms

INK_GLASS:
  deterministic fast curve on dense 25% document
  newest sample is visibly represented before authority work
  optical p95/p99 inside contract
  maximum consumed time/spatial gaps reported

COLD_400:
  every fixed adversarial seed current-view exact ≤500 ms
  operations considered/group and PSRAM traffic recorded

REPAIR_CHURN:
  amplification ≤1.25
  discarded work ≤10%
```

Never print closure success from the same model being tested. Glass gates consume frame IDs and camera evidence; code counters attribute failures.

---

# 17. Parallel work without merge chaos

## Three-lane maximum

### Lane A — Glass/presentation

Owns:

```text
co5300_panel_transport.{h,cpp}
panel_staging.h
vector_v2_presenter.{h,cpp} pan/full-frame paths
panel_staging tests
hardware pan A/B harness
```

Sequence:

```text
edge API -> staging callback -> pure-ring overlays -> one-window stream -> optical proof
```

### Lane B — Authority/cache

Owns:

```text
operation_log.{h,cpp}
replay_block_index.*
tile_producer.{h,cpp}
repair_queue.*
materialized_canvas history APIs
host exactness/cold tests
```

Sequence:

```text
OperationLogSnapshot -> index spike -> durable group identity -> undo/redo
```

### Lane C — Interaction/leaf features

Owns initially:

```text
closure_metrics.h
vector_v2_touch_sampler.{h,cpp}
provisional-tail helper and its host tests
trace parser/scripts
```

After `OperationLogSnapshot` freezes:

```text
vector_v2_autosave.*
svg_export.*
minimap input mapping
```

Lane C should not independently edit the presenter integration while Lane A is rewriting it. It builds the small helper and tests; Lane A or the integration owner wires it into `VectorV2Presenter`.

## Two developers

```text
Developer 1: Lane A, then live-ink presenter integration
Developer 2: Lane B, then autosave/SVG/minimap
```

The closure harness is shared but one person owns its schema.

## Solo developer

Use at most two live worktrees:

```text
closure/glass
closure/document
```

Do not open separate branches for autosave, SVG, minimap, and ink simultaneously. Finish a proof-sized commit, merge it, rerun the scorecard, then switch context.

## Safe parallel pairs

Can proceed concurrently after the two interface freezes:

```text
panel stream             || replay index
provisional-tail helper  || replay index
pan optical harness      || undo core tests
SVG exporter             || autosave format
minimap input mapping    || autosave
```

Should not proceed concurrently in separate branches:

```text
presenter pan rewrite    X presenter live-ink integration
OperationLog undo model  X autosave before snapshot API freezes
repair queue integration X TileProducer cursor rewrite by another owner
final performance tuning X unfinished feature semantics
```

---

# 18. Recommended commit order

Each commit should be independently testable and should carry a scorecard before/after when behavior changes.

```text
1. closure: add contract, trace schema, and baseline result bundle
2. authority: add OperationLogSnapshot without behavior change
3. panel: add configurable tear-edge wait and timing capture
4. presenter: add stage patch; remove overlay PSRAM mutation/restore
5. panel: add one-window RAMWR/RAMWRC A/B path
6. ink: preview-first ordering + replaceable provisional tail + real event timestamps
7. cold: add macrocell/block replay index and exactness gates
8. repair: key work by revision/zoom/group; retain across camera replans
9. history: active-prefix whole-gesture undo/redo
10. persistence: two-slot authority autosave and restore
11. features: minimap tap navigation and compact SVG export
12. closure: all features enabled; final glass/cold/churn gates
13. cleanup: delete legacy beam-race and temporary A/B code after proof
```

Do not wait until commit 12 to add regression gates. Each mechanism’s test arrives in the same commit as the mechanism.

---

# 19. What not to build unless a gate demands it

```text
no generic async job framework
no dynamic spatial posting lists before the fixed bitset index is measured
no full retained-mode layer tree
no multi-file FAT volume
no exact pressure-outline SVG unless export fidelity requires it
no geometry-aware touch resampler before measuring spatial/time gaps
no fully asynchronous authority/materialization split before preview-first ink is measured
no more beam-racing constants
```

The first implementation should be deliberately plain. Every escalation requires a failed measured gate and a written hypothesis.

---

# 20. Immediate first slice

The highest-value low-risk slice is:

```text
A. add event_us plumbing and closure trace buffer
B. make RibbonStream produce provisional geometry
C. render stable geometry into frame_, stage only the replaceable tail
D. submit newest visual update before commit_ready_chunk
E. measure the dense 25% stroke again
```

This can answer whether the larger resumable-commit refactor is necessary before it is designed.

In parallel, implement `OperationLogSnapshot` plus the fixed replay index in host tests, while the panel lane builds the one-window staged A/B. Those three proofs resolve the only major feasibility questions without committing to the remaining feature work.
