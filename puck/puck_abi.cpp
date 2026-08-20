#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <span>
#include <vector>

#include "tinydraw/vector_v2/application.h"
#include "tinydraw/vector_v2/chrome.h"
#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/memory_layout.h"

namespace {

using tinydraw::vector_v2::Application;
using tinydraw::vector_v2::ApplicationError;
using tinydraw::vector_v2::ApplicationEvent;
using tinydraw::vector_v2::ApplicationEventKind;
using tinydraw::vector_v2::ApplicationStorage;
using tinydraw::vector_v2::CompactOperationSample;
using tinydraw::vector_v2::DemoSample;
using tinydraw::vector_v2::MaterializedSlotStorage;
using tinydraw::vector_v2::MaterializedUniformStorage;
using tinydraw::vector_v2::OperationAppend;
using tinydraw::vector_v2::OperationRecord;
using tinydraw::vector_v2::PixelRect;
using tinydraw::vector_v2::RerenderLedgerEntry;

constexpr std::size_t kActiveStrokeLogicalPointCapacity = 4'096U;
constexpr std::size_t kStrokeSampleCapacity =
    tinydraw::vector_v2::kApplicationStrokeChunkSampleLimit;
constexpr std::size_t kStagedStrokeSampleCapacity =
    tinydraw::vector_v2::application_staged_sample_capacity(kActiveStrokeLogicalPointCapacity);
constexpr std::size_t kStagedStrokeAppendCapacity =
    tinydraw::vector_v2::application_staged_append_capacity(kActiveStrokeLogicalPointCapacity);
static_assert(kStagedStrokeSampleCapacity == 4'228U);
static_assert(kStagedStrokeAppendCapacity == 133U);
static_assert(kStagedStrokeSampleCapacity <= tinydraw::vector_v2::kOperationSampleCapacity);
static_assert(kStagedStrokeAppendCapacity <= tinydraw::vector_v2::kOperationCapacity);
constexpr std::size_t kDemoSampleCapacity = 16'384U;
constexpr std::size_t kPendingEventCapacity = 32U;
constexpr std::size_t kMaximumPushes = 256U;
constexpr std::size_t kForegroundWorkQuantaPerTick = 8U;
constexpr std::size_t kIdleWorkQuantaPerTick = 32U;
constexpr std::size_t kOwnerDocumentCapacity = 32U * 1'024U;
constexpr std::size_t kOwnerHeaderBytes = 12U;
constexpr std::size_t kOwnerOperationBytes = 5U;
constexpr int kPanelWidth = tinydraw::vector_v2::kOverviewWidth;
constexpr int kPanelHeight = tinydraw::vector_v2::kOverviewHeight;

extern "C" __attribute__((import_module("env"), import_name("js_log"))) void js_log(
    const char* pointer, int length);

void log_message(const char* message) {
  int length = 0;
  while (message[length] != '\0') {
    ++length;
  }
  js_log(message, length);
}

// wasi-libc's reactor startup may retain close/seek references even though
// TinyDraw has no host filesystem. Resolve them inside the guest so Puck does
// not need nondeterministic filesystem shims. If either is reached, BADF is the
// only truthful result because this module owns no descriptors.
extern "C" std::uint16_t guest_fd_close(int) __asm__("__imported_wasi_snapshot_preview1_fd_close");
extern "C" std::uint16_t guest_fd_close(int) { return 8U; }

extern "C" std::uint16_t guest_fd_seek(int, std::int64_t, std::uint8_t, std::uint64_t*) __asm__(
    "__imported_wasi_snapshot_preview1_fd_seek");
extern "C" std::uint16_t guest_fd_seek(int, std::int64_t, std::uint8_t, std::uint64_t*) {
  return 8U;
}

struct PushRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

std::uint16_t read_u16(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U);
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
         static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U |
         static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2U])) << 16U |
         static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3U])) << 24U;
}

bool valid_owner_document(std::span<const std::byte> document) {
  if (document.size() < kOwnerHeaderBytes || std::to_integer<char>(document[0]) != 'T' ||
      std::to_integer<char>(document[1]) != 'D' || std::to_integer<char>(document[2]) != 'O' ||
      std::to_integer<char>(document[3]) != 'C') {
    return false;
  }
  const std::size_t operation_count = read_u32(document, 4U);
  const std::size_t sample_count = read_u32(document, 8U);
  if (operation_count == 0U || sample_count == 0U ||
      operation_count > tinydraw::vector_v2::kOperationCapacity ||
      sample_count > tinydraw::vector_v2::kOperationSampleCapacity ||
      operation_count > (document.size() - kOwnerHeaderBytes) / kOwnerOperationBytes) {
    return false;
  }
  const std::size_t metadata_end = kOwnerHeaderBytes + operation_count * kOwnerOperationBytes;
  if (sample_count > (document.size() - metadata_end) / sizeof(CompactOperationSample) ||
      metadata_end + sample_count * sizeof(CompactOperationSample) != document.size()) {
    return false;
  }

  std::size_t sample_index = 0U;
  for (std::size_t operation = 0U; operation < operation_count; ++operation) {
    const std::size_t metadata = kOwnerHeaderBytes + operation * kOwnerOperationBytes;
    const std::uint8_t tool = std::to_integer<std::uint8_t>(document[metadata]);
    const std::size_t count = read_u16(document, metadata + 3U);
    if (tool > 1U || count == 0U || count > sample_count - sample_index) {
      return false;
    }
    std::uint16_t previous_elapsed = 0U;
    for (std::size_t sample = 0U; sample < count; ++sample) {
      const std::size_t offset = metadata_end + sample_index * sizeof(CompactOperationSample);
      const std::uint16_t x = read_u16(document, offset);
      const std::uint16_t y = read_u16(document, offset + 2U);
      const std::uint16_t radius = read_u16(document, offset + 4U);
      const std::uint16_t elapsed = read_u16(document, offset + 6U);
      if (x > tinydraw::vector_v2::kWorldWidth * tinydraw::vector_v2::kSampleUnitsPerWorldUnit ||
          y > tinydraw::vector_v2::kWorldHeight * tinydraw::vector_v2::kSampleUnitsPerWorldUnit ||
          radius == 0U || (sample != 0U && elapsed < previous_elapsed)) {
        return false;
      }
      previous_elapsed = elapsed;
      ++sample_index;
    }
  }
  return sample_index == sample_count;
}

class PendingEvents {
 public:
  void reset() {
    size_ = 0U;
    touching_ = false;
    delivered_touching_ = false;
    button_down_ = false;
    button_verdict_seen_ = false;
    x_ = 0;
    y_ = 0;
    delivered_x_ = 0;
    delivered_y_ = 0;
  }

  void touch(bool down, int x, int y) {
    if (down == touching_ && (!down || (x == x_ && y == y_))) {
      return;
    }

    ApplicationEventKind kind = ApplicationEventKind::kTouchMove;
    if (down && !touching_) {
      kind = ApplicationEventKind::kTouchDown;
    } else if (!down && touching_) {
      kind = ApplicationEventKind::kTouchUp;
      x = x_;
      y = y_;
    } else if (!down) {
      return;
    }

    touching_ = down;
    x_ = x;
    y_ = y;
    enqueue({.kind = kind, .x = static_cast<float>(x), .y = static_cast<float>(y)});
  }

  void button(int index, bool down) {
    if (index != 0 || down == button_down_) {
      return;
    }
    button_down_ = down;
    if (down) {
      button_verdict_seen_ = false;
    }
  }

  void button_verdict(int index, bool is_long) {
    if (index != 0 || button_verdict_seen_) {
      return;
    }
    if (is_long) {
      if (!button_down_) {
        return;
      }
      enqueue({.kind = ApplicationEventKind::kDemoLongPress});
    } else {
      if (button_down_) {
        return;
      }
      enqueue({.kind = ApplicationEventKind::kZoomNext});
    }
    button_verdict_seen_ = true;
  }

  [[nodiscard]] std::span<ApplicationEvent> stamp_and_take(std::uint32_t now_us) {
    for (std::size_t index = 0; index < size_; ++index) {
      // emu_touch has no clock argument. The next emu_tick is therefore the
      // narrowest truthful time boundary Puck exposes for every latched input.
      // Do not rewrite an explicitly timestamped event if the adapter gains
      // such an input source later.
      if (events_[index].timestamp_us == 0U) {
        events_[index].timestamp_us = now_us;
      }
    }
    return std::span(events_).first(size_);
  }

  void consumed() {
    size_ = 0U;
    delivered_touching_ = touching_;
    delivered_x_ = x_;
    delivered_y_ = y_;
  }

 private:
  void enqueue(ApplicationEvent event) {
    // Puck reports touch as a level. Only the newest coordinate in one
    // uninterrupted run of Moves can affect the level observed at the next
    // tick, so coalesce it before the fixed buffer is under pressure.
    if (event.kind == ApplicationEventKind::kTouchMove && size_ > 0U &&
        events_[size_ - 1U].kind == ApplicationEventKind::kTouchMove) {
      events_[size_ - 1U] = event;
      return;
    }
    if (size_ < events_.size()) {
      events_[size_++] = event;
      return;
    }

    // Every semantic edge outranks a sampled coordinate. Remove the oldest
    // Move, retain the order of all remaining events, and append the newest
    // event at the correct end of the timeline.
    for (std::size_t index = 0U; index < size_; ++index) {
      if (events_[index].kind == ApplicationEventKind::kTouchMove) {
        std::move(events_.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                  events_.begin() + static_cast<std::ptrdiff_t>(size_),
                  events_.begin() + static_cast<std::ptrdiff_t>(index));
        events_[size_ - 1U] = event;
        return;
      }
    }

    // More than 32 edge-only transitions occurred without a tick. Their full
    // history is unknowable to a bounded guest, but the final touch level is
    // not: collapse to the minimum transition that reconciles the last level
    // delivered to Application with the current physical latch. Preserve the
    // incoming button release too because it has no persistent level in the
    // Application event model.
    size_ = 0U;
    if (delivered_touching_ != touching_) {
      enqueue_direct(
          {.kind = touching_ ? ApplicationEventKind::kTouchDown : ApplicationEventKind::kTouchUp,
           .x = static_cast<float>(x_),
           .y = static_cast<float>(y_)});
    } else if (touching_ && (delivered_x_ != x_ || delivered_y_ != y_)) {
      enqueue_direct({.kind = ApplicationEventKind::kTouchMove,
                      .x = static_cast<float>(x_),
                      .y = static_cast<float>(y_)});
    }
    if (event.kind == ApplicationEventKind::kZoomNext ||
        event.kind == ApplicationEventKind::kDemoLongPress) {
      enqueue_direct(event);
    }
  }

  void enqueue_direct(ApplicationEvent event) { events_[size_++] = event; }

  std::array<ApplicationEvent, kPendingEventCapacity> events_{};
  std::size_t size_ = 0U;
  int x_ = 0;
  int y_ = 0;
  int delivered_x_ = 0;
  int delivered_y_ = 0;
  bool touching_ = false;
  bool delivered_touching_ = false;
  bool button_down_ = false;
  bool button_verdict_seen_ = false;
};

struct WasmState {
  std::vector<OperationRecord> records =
      std::vector<OperationRecord>(tinydraw::vector_v2::kOperationCapacity);
  std::vector<CompactOperationSample> samples =
      std::vector<CompactOperationSample>(tinydraw::vector_v2::kOperationSampleCapacity);
  std::vector<CompactOperationSample> stroke_samples =
      std::vector<CompactOperationSample>(kStrokeSampleCapacity);
  std::vector<CompactOperationSample> staged_stroke_samples =
      std::vector<CompactOperationSample>(kStagedStrokeSampleCapacity);
  std::vector<OperationAppend> staged_stroke_appends =
      std::vector<OperationAppend>(kStagedStrokeAppendCapacity);
  std::vector<OperationRecord> import_records =
      std::vector<OperationRecord>(tinydraw::vector_v2::kOperationCapacity);
  std::vector<CompactOperationSample> import_samples =
      std::vector<CompactOperationSample>(tinydraw::vector_v2::kOperationSampleCapacity);
  std::vector<DemoSample> demo_samples = std::vector<DemoSample>(kDemoSampleCapacity);
  std::vector<std::uint16_t> canvas =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kOverviewPixels, 0xFFFFU);
  std::vector<std::uint16_t> working =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kOverviewPixels, 0xFFFFU);
  std::vector<std::uint16_t> frame =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kOverviewPixels, 0xFFFFU);
  std::vector<std::uint16_t> live =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kOverviewPixels, 0xFFFFU);
  std::vector<std::uint16_t> overview =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kOverviewPixels, 0xFFFFU);
  std::vector<std::uint16_t> working_overview =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kOverviewPixels, 0xFFFFU);
  std::vector<std::uint16_t> chrome_cache =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kChromeStagingCachePixels, 0xFFFFU);
  std::vector<MaterializedUniformStorage> materialized_uniforms =
      std::vector<MaterializedUniformStorage>(tinydraw::vector_v2::kMaterializedTileIdentityCount);
  std::vector<std::uint8_t> materialized_occupancy =
      std::vector<std::uint8_t>(tinydraw::vector_v2::kOccupancyBytes);
  std::vector<MaterializedSlotStorage> materialized_slots =
      std::vector<MaterializedSlotStorage>(tinydraw::vector_v2::kTileSlotCount);
  std::vector<std::uint16_t> materialized_tile_pixels = std::vector<std::uint16_t>(
      tinydraw::vector_v2::kTileSlotCount * tinydraw::vector_v2::kTilePixels);
  std::vector<std::uint16_t> materialized_raw_slot_directory =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kMaterializedTileIdentityCount);
  std::vector<std::uint16_t> producer_supertask_pixels =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kTileProducerPixels);
  std::vector<std::uint8_t> producer_finalized_pixels =
      std::vector<std::uint8_t>(tinydraw::vector_v2::kTileProducerMaskBytes);
  std::vector<std::uint16_t> producer_summary_rows =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kTileProducerSummaryRows);
  std::vector<std::uint32_t> producer_summary_words =
      std::vector<std::uint32_t>(tinydraw::vector_v2::kTileProducerSummaryWords);
  // uint32_t backing makes the opaque rasterizer workspace's alignment an
  // explicit part of the host contract instead of relying on byte allocation.
  std::vector<std::uint32_t> producer_chord_words = std::vector<std::uint32_t>(
      (tinydraw::vector_v2::kOperationChordStorageBytes + sizeof(std::uint32_t) - 1U) /
      sizeof(std::uint32_t));
  std::vector<std::uint16_t> producer_candidate_indices =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kOperationCapacity);
  std::vector<std::uint8_t> settle_operation_alpha =
      std::vector<std::uint8_t>(tinydraw::vector_v2::kTilePixels);
  std::vector<std::uint8_t> settle_accumulated_alpha =
      std::vector<std::uint8_t>(tinydraw::vector_v2::kTilePixels);
  std::vector<std::uint16_t> settle_red =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kTilePixels);
  std::vector<std::uint16_t> settle_green =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kTilePixels);
  std::vector<std::uint16_t> settle_blue =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kTilePixels);
  std::vector<std::uint16_t> settle_pixels =
      std::vector<std::uint16_t>(tinydraw::vector_v2::kTilePixels);
  std::vector<RerenderLedgerEntry> rerender_ledger_entries =
      std::vector<RerenderLedgerEntry>(tinydraw::vector_v2::kRerenderLedgerEntryCount);
  Application application{
      {.records = records,
       .samples = samples,
       .stroke_samples = stroke_samples,
       .staged_stroke_samples = staged_stroke_samples,
       .staged_stroke_appends = staged_stroke_appends,
       .import_records = import_records,
       .import_samples = import_samples,
       .demo_samples = demo_samples,
       .canvas_pixels = canvas,
       .working_pixels = working,
       .frame_pixels = frame,
       .live_pixels = live,
       .overview_pixels = overview,
       .working_overview_pixels = working_overview,
       .chrome_cache_pixels = chrome_cache,
       .materialized_uniforms = materialized_uniforms,
       .materialized_occupancy = materialized_occupancy,
       .materialized_slots = materialized_slots,
       .materialized_tile_pixels = materialized_tile_pixels,
       .materialized_raw_slot_directory = materialized_raw_slot_directory,
       .producer_supertask_pixels = producer_supertask_pixels,
       .producer_finalized_pixels = producer_finalized_pixels,
       .producer_summary_rows = producer_summary_rows,
       .producer_summary_words = producer_summary_words,
       .producer_chord_plans = std::as_writable_bytes(std::span(producer_chord_words))
                                   .first(tinydraw::vector_v2::kOperationChordStorageBytes),
       .producer_candidate_indices = producer_candidate_indices,
       .settle_operation_alpha = settle_operation_alpha,
       .settle_accumulated_alpha = settle_accumulated_alpha,
       .settle_red = settle_red,
       .settle_green = settle_green,
       .settle_blue = settle_blue,
       .settle_pixels = settle_pixels,
       .rerender_ledger_entries = rerender_ledger_entries}};
  PendingEvents pending{};
  std::array<PushRect, kMaximumPushes> pushes{};
  std::array<std::byte, kOwnerDocumentCapacity> owner_buffer{};
  std::array<std::byte, kOwnerDocumentCapacity> owner_pending{};
  std::size_t owner_pending_size = 0U;
  std::size_t push_count = 0U;
  bool push_degraded = false;
  bool first_tick = true;

  [[nodiscard]] bool stage_owner_document(std::size_t size) {
    if (size == 0U || size > owner_buffer.size()) {
      return false;
    }
    const std::span<const std::byte> document(owner_buffer.data(), size);
    if (!valid_owner_document(document)) {
      return false;
    }
    std::copy(document.begin(), document.end(), owner_pending.begin());
    owner_pending_size = size;
    return true;
  }

  [[nodiscard]] std::optional<ApplicationError> import_pending_owner() {
    if (owner_pending_size == 0U) {
      return std::nullopt;
    }
    const std::size_t size = owner_pending_size;
    owner_pending_size = 0U;
    return application.import_tdoc(std::span<const std::byte>(owner_pending.data(), size));
  }

  void clear_pushes() {
    push_count = 0U;
    push_degraded = false;
  }

  void record_damage(PixelRect damage) {
    if (push_degraded) {
      return;
    }
    const int width = damage.x1 - damage.x0;
    const int height = damage.y1 - damage.y0;
    if (damage.x0 < 0 || damage.y0 < 0 || width <= 0 || height <= 0 || damage.x1 > kPanelWidth ||
        damage.y1 > kPanelHeight) {
      collapse_to_full_panel();
      return;
    }
    if (push_count == pushes.size()) {
      collapse_to_full_panel();
      return;
    }
    pushes[push_count++] = {.x = damage.x0, .y = damage.y0, .width = width, .height = height};
  }

  [[nodiscard]] const PushRect& push(int index) const {
    static constexpr PushRect kEmpty{};
    return index >= 0 && static_cast<std::size_t>(index) < push_count
               ? pushes[static_cast<std::size_t>(index)]
               : kEmpty;
  }

 private:
  void collapse_to_full_panel() {
    pushes[0] = {.x = 0, .y = 0, .width = kPanelWidth, .height = kPanelHeight};
    push_count = 1U;
    push_degraded = true;
  }
};

alignas(WasmState) std::array<std::byte, sizeof(WasmState)> state_storage{};
WasmState* state = nullptr;

constexpr char kDeviceJson[] =
    "{"
    "\"name\":\"TinyDraw Vector V2\","
    "\"panel\":{\"w\":368,\"h\":448,\"format\":\"rgb565\"},"
    "\"buttons\":[{\"id\":\"boot\",\"label\":\"BOOT\",\"edge\":\"right\",\"at\":0.38,"
    "\"longPressMs\":800}],"
    "\"touch\":{\"points\":1}"
    "}";

}  // namespace

extern "C" {

int emu_device() { return static_cast<int>(reinterpret_cast<std::uintptr_t>(kDeviceJson)); }

int tinydraw_owner_buffer() {
  return state == nullptr
             ? 0
             : static_cast<int>(reinterpret_cast<std::uintptr_t>(state->owner_buffer.data()));
}

int tinydraw_owner_capacity() { return static_cast<int>(kOwnerDocumentCapacity); }

int tinydraw_owner_load(int size) {
  if (state == nullptr || size <= 0) {
    return 0;
  }
  return state->stage_owner_document(static_cast<std::size_t>(size)) ? 1 : 0;
}

int tinydraw_diag_production_enabled() {
  return state != nullptr && state->application.diagnostics().production_enabled ? 1 : 0;
}
int tinydraw_diag_operation_count() {
  return state == nullptr ? 0 : static_cast<int>(state->application.status().operation_count);
}
int tinydraw_diag_sample_count() {
  return state == nullptr ? 0 : static_cast<int>(state->application.status().sample_count);
}
int tinydraw_diag_active_stroke_point_capacity() {
  return static_cast<int>(kActiveStrokeLogicalPointCapacity);
}
int tinydraw_diag_stroke_active() {
  return state != nullptr && state->application.status().stroke_active ? 1 : 0;
}
int tinydraw_diag_slot_capacity() {
  return state == nullptr ? 0 : static_cast<int>(state->application.diagnostics().slot_capacity);
}
int tinydraw_diag_resident_raw_tiles() {
  return state == nullptr ? 0
                          : static_cast<int>(state->application.diagnostics().resident_raw_tiles);
}
int tinydraw_diag_visible_tiles_remaining() {
  return state == nullptr
             ? 0
             : static_cast<int>(state->application.diagnostics().visible_tiles_remaining);
}
int tinydraw_diag_recent_view_count() {
  return state == nullptr ? 0
                          : static_cast<int>(state->application.diagnostics().recent_view_count);
}
int tinydraw_diag_maintenance_pending() {
  return state != nullptr && state->application.diagnostics().maintenance_pending ? 1 : 0;
}
int tinydraw_diag_last_fallback_pixels() {
  return state == nullptr
             ? 0
             : static_cast<int>(state->application.diagnostics().last_composition.fallback_pixels);
}
int tinydraw_diag_last_fallback_tiles() {
  return state == nullptr
             ? 0
             : static_cast<int>(state->application.diagnostics().last_composition.fallback_tiles);
}
int tinydraw_diag_current_raw() {
  return state == nullptr ? 0 : static_cast<int>(state->application.diagnostics().current_zoom_raw);
}
int tinydraw_diag_current_uniform() {
  return state == nullptr ? 0
                          : static_cast<int>(state->application.diagnostics().current_zoom_uniform);
}
int tinydraw_diag_current_fallback() {
  return state == nullptr
             ? 0
             : static_cast<int>(state->application.diagnostics().current_zoom_fallback);
}
int tinydraw_diag_zoom100_raw() {
  return state == nullptr ? 0 : static_cast<int>(state->application.diagnostics().zoom100_raw);
}
int tinydraw_diag_zoom100_uniform() {
  return state == nullptr ? 0 : static_cast<int>(state->application.diagnostics().zoom100_uniform);
}
int tinydraw_diag_zoom100_fallback() {
  return state == nullptr ? 0 : static_cast<int>(state->application.diagnostics().zoom100_fallback);
}
int tinydraw_diag_rerender_renders() {
  return state == nullptr ? 0 : static_cast<int>(state->application.diagnostics().rerender.renders);
}
int tinydraw_diag_rerender_unique() {
  return state == nullptr
             ? 0
             : static_cast<int>(state->application.diagnostics().rerender.unique_groups);
}
int tinydraw_diag_rerender_cold() {
  return state == nullptr ? 0
                          : static_cast<int>(state->application.diagnostics().rerender.cold_miss);
}
int tinydraw_diag_rerender_damage() {
  return state == nullptr
             ? 0
             : static_cast<int>(state->application.diagnostics().rerender.expected_damage);
}
int tinydraw_diag_rerender_eviction() {
  return state == nullptr ? 0
                          : static_cast<int>(state->application.diagnostics().rerender.eviction);
}
int tinydraw_diag_rerender_stale() {
  return state == nullptr
             ? 0
             : static_cast<int>(state->application.diagnostics().rerender.stale_revision);
}
int tinydraw_diag_rerender_unexplained() {
  return state == nullptr ? 0
                          : static_cast<int>(state->application.diagnostics().rerender.unexplained);
}

int emu_init() {
  if (state != nullptr) {
    state->~WasmState();
  }
  state = new (state_storage.data()) WasmState;
  if (!state->application.ready()) {
    log_message("TinyDraw Puck: Vector V2 application initialization failed");
    state->~WasmState();
    state = nullptr;
    return 0;
  }
  state->pending.reset();
  state->clear_pushes();
  return 1;
}

void emu_tick(std::uint32_t now_ms) {
  if (state == nullptr) {
    return;
  }
  state->clear_pushes();
  const std::optional<ApplicationError> owner_error = state->import_pending_owner();
  if (owner_error == ApplicationError::kNone) {
    // An owner import is a new serialized application session. Input latched
    // against the previous authority must not land on the replacement document
    // during the same tick.
    state->pending.reset();
  }
  const std::uint32_t now_us = now_ms * 1'000U;
  const std::span<ApplicationEvent> events = state->pending.stamp_and_take(now_us);
  const std::size_t work_quanta =
      events.empty() ? kIdleWorkQuantaPerTick : kForegroundWorkQuantaPerTick;
  const auto result = state->application.advance(now_us, events, work_quanta);
  state->pending.consumed();
  if (state->first_tick) {
    state->record_damage({0, 0, kPanelWidth, kPanelHeight});
    state->first_tick = false;
  } else if (result.frame_changed) {
    state->record_damage(result.damage);
  }
  // ApplicationError describes input/capacity/render recovery; it does not
  // revoke a frame already published by the same serialized advance. Report
  // its damage first so Puck never displays an older image than emu_fb().
  if (result.error != ApplicationError::kNone) {
    log_message("TinyDraw Puck: Vector V2 advance failed");
  }
  if (owner_error.has_value() && *owner_error != ApplicationError::kNone) {
    log_message("TinyDraw Puck: owner document import failed");
  }
}

int emu_fb() {
  if (state == nullptr) {
    return 0;
  }
  return static_cast<int>(reinterpret_cast<std::uintptr_t>(state->application.frame().data()));
}

int emu_push_count() { return state == nullptr ? 0 : static_cast<int>(state->push_count); }
int emu_push_x(int index) { return state == nullptr ? 0 : state->push(index).x; }
int emu_push_y(int index) { return state == nullptr ? 0 : state->push(index).y; }
int emu_push_w(int index) { return state == nullptr ? 0 : state->push(index).width; }
int emu_push_h(int index) { return state == nullptr ? 0 : state->push(index).height; }

void emu_touch(int down, int x, int y) {
  if (state != nullptr) {
    state->pending.touch(down != 0, x, y);
  }
}

void emu_button(int index, int down) {
  if (state != nullptr) {
    state->pending.button(index, down != 0);
  }
}

void emu_button_verdict(int index, int is_long) {
  if (state != nullptr) {
    state->pending.button_verdict(index, is_long != 0);
  }
}
void emu_sensor_event(int) {}

}  // extern "C"
