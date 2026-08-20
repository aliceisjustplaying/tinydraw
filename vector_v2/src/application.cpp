#include "tinydraw/vector_v2/application.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/vector_v2/chained_operation_builder.h"
#include "tinydraw/vector_v2/idle_repair.h"
#include "tinydraw/vector_v2/incremental_document.h"
#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/operation_log.h"
#include "tinydraw/vector_v2/settled_tile.h"

namespace tinydraw::vector_v2 {
namespace {

constexpr std::uint16_t kPaper = 0xFFFFU;
constexpr std::size_t kOperationsPerWorkQuantum = 32U;
constexpr std::size_t kCompositionSlicesPerWorkQuantum = 8U;
constexpr std::size_t kSettleWorkPixelsPerQuantum = 512U;
constexpr std::size_t kImportHeaderBytes = 12U;
constexpr std::size_t kImportOperationHeaderBytes = 5U;
constexpr std::size_t kImportSampleBytes = sizeof(CompactOperationSample);
constexpr NavigationPoint kDefaultFocus{kOverviewWidth / 2, kChromeCanvasBottom / 2};
constexpr PixelRect kFullFrame{0, 0, kOverviewWidth, kOverviewHeight};

struct ImportShape {
  std::size_t operation_count = 0U;
  std::size_t sample_count = 0U;
  std::size_t metadata_bytes = 0U;
};

struct ImportCursor {
  std::size_t metadata_at = kImportHeaderBytes;
  std::size_t sample_at = 0U;
  std::size_t first_sample = 0U;
};

bool valid_pixels(std::span<std::uint16_t> pixels) { return pixels.size() == kOverviewPixels; }

template <typename T>
std::span<const std::byte> bytes_of(std::span<T> values) {
  return std::as_bytes(values);
}

bool disjoint_storage(const ApplicationStorage& storage) {
  const std::array ranges{
      bytes_of(storage.records),
      bytes_of(storage.samples),
      bytes_of(storage.stroke_samples),
      bytes_of(storage.staged_stroke_samples),
      bytes_of(storage.staged_stroke_appends),
      bytes_of(storage.import_records),
      bytes_of(storage.import_samples),
      bytes_of(storage.demo_samples),
      bytes_of(storage.canvas_pixels),
      bytes_of(storage.working_pixels),
      bytes_of(storage.frame_pixels),
      bytes_of(storage.live_pixels),
      bytes_of(storage.overview_pixels),
      bytes_of(storage.working_overview_pixels),
      bytes_of(storage.chrome_cache_pixels),
      bytes_of(storage.materialized_uniforms),
      bytes_of(storage.materialized_occupancy),
      bytes_of(storage.materialized_slots),
      bytes_of(storage.materialized_tile_pixels),
      bytes_of(storage.materialized_raw_slot_directory),
      bytes_of(storage.producer_supertask_pixels),
      bytes_of(storage.producer_finalized_pixels),
      bytes_of(storage.producer_summary_rows),
      bytes_of(storage.producer_summary_words),
      bytes_of(storage.producer_chord_plans),
      bytes_of(storage.producer_candidate_indices),
      bytes_of(storage.settle_operation_alpha),
      bytes_of(storage.settle_accumulated_alpha),
      bytes_of(storage.settle_red),
      bytes_of(storage.settle_green),
      bytes_of(storage.settle_blue),
      bytes_of(storage.settle_pixels),
      bytes_of(storage.rerender_ledger_entries),
  };
  for (std::size_t first = 0U; first < ranges.size(); ++first) {
    if (ranges[first].empty()) {
      continue;
    }
    const auto first_begin = reinterpret_cast<std::uintptr_t>(ranges[first].data());
    const auto first_end = first_begin + ranges[first].size_bytes();
    for (std::size_t second = first + 1U; second < ranges.size(); ++second) {
      if (ranges[second].empty()) {
        continue;
      }
      const auto second_begin = reinterpret_cast<std::uintptr_t>(ranges[second].data());
      const auto second_end = second_begin + ranges[second].size_bytes();
      if (first_begin < second_end && second_begin < first_end) {
        return false;
      }
    }
  }
  return true;
}

bool has_production_storage(const ApplicationStorage& storage) {
  return !storage.materialized_uniforms.empty() || !storage.materialized_occupancy.empty() ||
         !storage.materialized_slots.empty() || !storage.materialized_tile_pixels.empty() ||
         !storage.materialized_raw_slot_directory.empty() ||
         !storage.producer_supertask_pixels.empty() || !storage.producer_finalized_pixels.empty() ||
         !storage.producer_summary_rows.empty() || !storage.producer_summary_words.empty() ||
         !storage.producer_chord_plans.empty() || !storage.producer_candidate_indices.empty() ||
         !storage.settle_operation_alpha.empty() || !storage.settle_accumulated_alpha.empty() ||
         !storage.settle_red.empty() || !storage.settle_green.empty() ||
         !storage.settle_blue.empty() || !storage.settle_pixels.empty();
}

bool valid_settle_storage(const ApplicationStorage& storage) {
  return storage.settle_operation_alpha.size() >= kTilePixels &&
         storage.settle_accumulated_alpha.size() >= kTilePixels &&
         storage.settle_red.size() >= kTilePixels && storage.settle_green.size() >= kTilePixels &&
         storage.settle_blue.size() >= kTilePixels && storage.settle_pixels.size() >= kTilePixels;
}

void mark_may_ink(PixelRect bounds, std::span<std::uint8_t> occupancy) {
  const int first_column = bounds.x0 / kOccupancyCellWorldSize;
  const int last_column = (bounds.x1 - 1) / kOccupancyCellWorldSize;
  const int first_row = bounds.y0 / kOccupancyCellWorldSize;
  const int last_row = (bounds.y1 - 1) / kOccupancyCellWorldSize;
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      const std::size_t bit =
          static_cast<std::size_t>(row) * kOccupancyColumns + static_cast<std::size_t>(column);
      occupancy[bit / 8U] |= static_cast<std::uint8_t>(1U << (bit % 8U));
    }
  }
}

OperationAppend append_view(const StoredOperation& operation) {
  return {.tool = operation.tool,
          .color = operation.color,
          .gesture_id = operation.gesture_id,
          .samples = operation.samples};
}

ChromeNavigation chrome_navigation(const NavigationState& navigation,
                                   std::span<const std::uint16_t> overview) {
  const int percent = zoom_percent(navigation.zoom());
  const NavigationExtent extent = navigation.extent();
  return {
      .zoom_percent = percent,
      .level_x = navigation.origin().x,
      .level_y = navigation.origin().y,
      .level_width = kWorldWidth * percent / 100,
      .level_height = kWorldHeight * percent / 100,
      .can_pan_top = extent.top,
      .can_pan_left = extent.left,
      .can_pan_right = extent.right,
      .can_pan_bottom = extent.bottom,
      .overview_pixels = overview,
  };
}

bool point_in_panel(float x, float y) {
  return std::isfinite(x) && std::isfinite(y) && x >= 0.0F && y >= 0.0F &&
         x < static_cast<float>(kOverviewWidth) && y < static_cast<float>(kOverviewHeight);
}

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

std::optional<ImportShape> import_shape(std::span<const std::byte> document) {
  if (document.size() < kImportHeaderBytes || document[0] != std::byte{'T'} ||
      document[1] != std::byte{'D'} || document[2] != std::byte{'O'} ||
      document[3] != std::byte{'C'}) {
    return std::nullopt;
  }
  const std::size_t operation_count = read_u32(document, 4U);
  const std::size_t sample_count = read_u32(document, 8U);
  if (operation_count > (document.size() - kImportHeaderBytes) / kImportOperationHeaderBytes ||
      operation_count > std::numeric_limits<std::uint16_t>::max()) {
    return std::nullopt;
  }
  const std::size_t metadata_bytes = operation_count * kImportOperationHeaderBytes;
  const std::size_t remaining = document.size() - kImportHeaderBytes - metadata_bytes;
  if (sample_count > remaining / kImportSampleBytes ||
      sample_count * kImportSampleBytes != remaining) {
    return std::nullopt;
  }
  return ImportShape{
      .operation_count = operation_count,
      .sample_count = sample_count,
      .metadata_bytes = metadata_bytes,
  };
}

CompactOperationSample import_sample(std::span<const std::byte> document, std::size_t offset) {
  return {
      .x_quarter = read_u16(document, offset),
      .y_quarter = read_u16(document, offset + 2U),
      .radius_256 = read_u16(document, offset + 4U),
      .elapsed_ms = read_u16(document, offset + 6U),
  };
}

bool elapsed_time_is_monotonic(std::span<const CompactOperationSample> samples) {
  for (std::size_t index = 1U; index < samples.size(); ++index) {
    if (samples[index].elapsed_ms < samples[index - 1U].elapsed_ms) {
      return false;
    }
  }
  return true;
}

void fingerprint_byte(std::uint64_t& hash, std::uint8_t value) {
  hash ^= value;
  hash *= 1'099'511'628'211ULL;
}

void fingerprint_u16(std::uint64_t& hash, std::uint16_t value) {
  fingerprint_byte(hash, static_cast<std::uint8_t>(value));
  fingerprint_byte(hash, static_cast<std::uint8_t>(value >> 8U));
}

void fingerprint_u64(std::uint64_t& hash, std::uint64_t value) {
  for (unsigned shift = 0U; shift < 64U; shift += 8U) {
    fingerprint_byte(hash, static_cast<std::uint8_t>(value >> shift));
  }
}

}  // namespace

class Application::Impl {
 public:
  explicit Impl(ApplicationStorage storage)
      : storage_(storage),
        canvas_(storage.canvas_pixels),
        working_(storage.working_pixels),
        frame_(storage.frame_pixels),
        live_(storage.live_pixels),
        overview_(storage.overview_pixels),
        working_overview_(storage.working_overview_pixels),
        log_(storage.records, storage.samples),
        demo_(storage.demo_samples),
        preview_storage_(
            std::min(storage.stroke_samples.size(), kApplicationStrokeChunkSampleLimit)),
        builder_(storage.stroke_samples,
                 std::min(storage.stroke_samples.size(), kApplicationStrokeChunkSampleLimit)),
        preview_builder_(preview_storage_),
        chrome_cache_(storage.chrome_cache_pixels) {
    ready_ = !storage.records.empty() && !storage.samples.empty() &&
             !storage.stroke_samples.empty() && !storage.staged_stroke_samples.empty() &&
             !storage.staged_stroke_appends.empty() && valid_pixels(canvas_) &&
             valid_pixels(working_) && valid_pixels(frame_) && valid_pixels(overview_) &&
             valid_pixels(live_) && valid_pixels(working_overview_) && log_.ready() &&
             builder_.ready() && preview_builder_.ready() && chrome_cache_.ready() &&
             disjoint_storage(storage);
    if (!ready_) {
      return;
    }
    std::fill(canvas_.begin(), canvas_.end(), kPaper);
    std::fill(working_.begin(), working_.end(), kPaper);
    std::fill(live_.begin(), live_.end(), kPaper);
    std::fill(overview_.begin(), overview_.end(), kPaper);
    std::fill(working_overview_.begin(), working_overview_.end(), kPaper);
    if (has_production_storage(storage_)) {
      if (!valid_settle_storage(storage_)) {
        ready_ = false;
        return;
      }
      materialized_ = std::make_unique<MaterializedCanvas>(MaterializedCanvasStorage{
          .overview_pixels = overview_,
          .uniform_catalog = storage_.materialized_uniforms,
          .occupancy_bits = storage_.materialized_occupancy,
          .slots = storage_.materialized_slots,
          .tile_pixels = storage_.materialized_tile_pixels,
          .initial_revision = log_.current_revision(),
          .raw_slot_directory = storage_.materialized_raw_slot_directory,
      });
      if (!materialized_->ready() || !materialized_->reset_blank(log_.current_revision())) {
        ready_ = false;
        return;
      }
      producer_ = std::make_unique<TileProducer>(
          log_, *materialized_,
          TileProducerWorkspace{
              .supertask_pixels = storage_.producer_supertask_pixels,
              .finalized_pixels = storage_.producer_finalized_pixels,
              .summary_row_unset = storage_.producer_summary_rows,
              .summary_saturated_words = storage_.producer_summary_words,
              .operation_chord_plans = storage_.producer_chord_plans,
              .candidate_indices = storage_.producer_candidate_indices,
          },
          log_.current_revision(), kPaper);
      if (!producer_->ready()) {
        ready_ = false;
        return;
      }
      if (!storage_.rerender_ledger_entries.empty()) {
        rerender_ledger_ = std::make_unique<RerenderLedger>(storage_.rerender_ledger_entries);
        if (!rerender_ledger_->ready()) {
          ready_ = false;
          return;
        }
        materialized_->set_rerender_ledger(rerender_ledger_.get());
        producer_->set_rerender_ledger(rerender_ledger_.get());
      }
    } else if (!storage_.rerender_ledger_entries.empty()) {
      ready_ = false;
      return;
    }
    chrome_.tool = ChromeTool::kDraw;
    chrome_.size = ChromeSize::kLarge;
    chrome_.color_index = 12U;
    canvas_view_ = navigation_.view();
    sync_history();
    ready_ = publish_frame();
  }

  [[nodiscard]] bool ready() const { return ready_; }

  ApplicationAdvanceResult advance(std::uint32_t now_us, std::span<const ApplicationEvent> events,
                                   std::size_t work_quanta) {
    ApplicationAdvanceResult result{.frame_epoch = frame_epoch_};
    if (!ready_) {
      result.error = ApplicationError::kInvalidStorage;
      return result;
    }

    frame_dirty_ = false;
    const bool initial_frame = initial_frame_pending_;
    initial_frame_pending_ = false;
    process_input_events(events, now_us, result);
    process_demo_replay(now_us, result);
    run_background_work(work_quanta, result);
    publish_advance_frame(result);
    finish_advance(initial_frame, result);
    return result;
  }

  [[nodiscard]] std::span<const std::uint16_t> frame() const { return frame_; }

  [[nodiscard]] ApplicationError import_tdoc(std::span<const std::byte> document) {
    const std::optional<ImportShape> shape = import_shape(document);
    if (!shape.has_value()) {
      return ApplicationError::kMalformedDocument;
    }
    if (!import_fits(*shape)) {
      return ApplicationError::kImportCapacity;
    }
    if (!valid_import_declarations(document, *shape) || !decode_import(document, *shape)) {
      return ApplicationError::kMalformedDocument;
    }
    const std::uint64_t generation =
        static_cast<std::uint64_t>(log_.current_revision().value) + shape->operation_count + 1U;
    if (generation > std::numeric_limits<std::uint32_t>::max()) {
      return ApplicationError::kAuthorityFull;
    }
    std::span<std::uint8_t> imported_may_ink;
    const ApplicationError preflight = preflight_import_render(*shape, imported_may_ink);
    if (preflight != ApplicationError::kNone) {
      return preflight;
    }
    return publish_import(*shape, static_cast<std::uint32_t>(generation), imported_may_ink);
  }

  [[nodiscard]] ApplicationStatus status() const {
    const AuthorityReadView authority = log_.read_view();
    return {
        .chrome = chrome_,
        .zoom = navigation_.zoom(),
        .origin = navigation_.origin(),
        .revision = log_.current_revision(),
        .operation_count = log_.operation_count(),
        .retained_operation_count = authority.retained_operation_count,
        .sample_count = log_.sample_count(),
        .authority_fingerprint = authority_fingerprint(),
        .demo_mode = demo_mode(),
        .demo_sample_count = demo_.size(),
        .demo_overflowed = demo_.overflowed(),
        .frame_epoch = frame_epoch_,
        .stroke_active = interaction_ == Interaction::kStroke,
        .background_pending = rebuild_pending_ || maintenance_pending_,
    };
  }

  [[nodiscard]] ApplicationDiagnostics diagnostics() const {
    ApplicationDiagnostics result;
    if (materialized_ == nullptr) {
      return result;
    }
    result.production_enabled = true;
    result.maintenance_pending = maintenance_pending_;
    result.slot_capacity = materialized_->slot_capacity();
    result.resident_raw_tiles = materialized_->resident_raw_tiles();
    result.visible_tiles_remaining =
        producer_->visible_tiles_remaining(navigation_.view()).value_or(0U);
    result.recent_view_count = static_cast<std::size_t>(
        std::count_if(materialized_->recent_views().begin(), materialized_->recent_views().end(),
                      [](const ViewFootprint& view) { return view.valid; }));
    result.last_composition = last_composition_;
    tile_census(navigation_.zoom(), result.current_zoom_raw, result.current_zoom_uniform,
                result.current_zoom_fallback);
    tile_census(ZoomLevel::k100Percent, result.zoom100_raw, result.zoom100_uniform,
                result.zoom100_fallback);
    if (rerender_ledger_ != nullptr) {
      result.rerender = rerender_ledger_->totals();
    }
    return result;
  }

 private:
  enum class Interaction : std::uint8_t {
    kIdle,
    kDismissOverlay,
    kToolbar,
    kPan,
    kMinimap,
    kStroke,
  };

  enum class RebuildPhase : std::uint8_t {
    kIdle,
    kComposeFallback,
    kProduce,
    kComposeFinal,
  };

  enum class ComposeProgress : std::uint8_t {
    kInProgress,
    kComplete,
    kError,
  };

  struct SettleRender {
    SettledRenderCursor cursor{};
    TileKey key{};
    PixelRect level_bounds{};
    bool overview = false;
    bool active = false;
  };

  static void record_first_error(ApplicationError error, ApplicationAdvanceResult& result) {
    if (result.error == ApplicationError::kNone && error != ApplicationError::kNone) {
      result.error = error;
    }
  }

  void process_input_events(std::span<const ApplicationEvent> events, std::uint32_t now_us,
                            ApplicationAdvanceResult& result) {
    for (const ApplicationEvent& event : events) {
      record_first_error(handle_input_event(event, now_us), result);
    }
  }

  void process_demo_replay(std::uint32_t now_us, ApplicationAdvanceResult& result) {
    while (demo_.replay_due(now_us)) {
      const std::optional<DemoEvent> event = demo_.pop_replay(now_us);
      if (!event.has_value()) {
        return;
      }
      record_first_error(dispatch_demo_event(*event), result);
    }
  }

  [[nodiscard]] bool run_compact_rebuild_quantum() {
    for (std::size_t operation = 0U; operation < kOperationsPerWorkQuantum && rebuild_pending_;
         ++operation) {
      if (!run_rebuild_operation()) {
        return false;
      }
    }
    return true;
  }

  void run_rebuild_work(std::size_t& work_quanta, ApplicationAdvanceResult& result) {
    while (work_quanta > 0U && rebuild_pending_) {
      --work_quanta;
      const bool rebuilt = materialized_ != nullptr ? run_production_rebuild_quantum()
                                                    : run_compact_rebuild_quantum();
      if (!rebuilt) {
        result.error = ApplicationError::kRenderFailed;
        cancel_rebuild();
      }
    }
  }

  void cancel_settle() {
    settle_render_.cursor.cancel();
    settle_render_ = {};
    settle_cursor_ = 0U;
    settle_pending_ = false;
    settle_final_compose_ = false;
    settle_changed_ = false;
  }

  [[nodiscard]] std::size_t settle_column_count() const {
    if (settle_view_.zoom == ZoomLevel::k25Percent) {
      return (static_cast<std::size_t>(kOverviewWidth) + kTileWidth - 1U) / kTileWidth;
    }
    const int first = settle_view_.level_pixels.x0 / static_cast<int>(kTileWidth);
    const int last = (settle_view_.level_pixels.x1 - 1) / static_cast<int>(kTileWidth);
    return static_cast<std::size_t>(last) - static_cast<std::size_t>(first) + 1U;
  }

  [[nodiscard]] std::size_t settle_row_count() const {
    if (settle_view_.zoom == ZoomLevel::k25Percent) {
      return (static_cast<std::size_t>(kOverviewHeight) + kTileHeight - 1U) / kTileHeight;
    }
    const int first = settle_view_.level_pixels.y0 / static_cast<int>(kTileHeight);
    const int last = (settle_view_.level_pixels.y1 - 1) / static_cast<int>(kTileHeight);
    return static_cast<std::size_t>(last) - static_cast<std::size_t>(first) + 1U;
  }

  void begin_settle() {
    cancel_settle();
    if (materialized_ == nullptr || log_.operation_count() == 0U) {
      maintenance_pending_ = false;
      return;
    }
    settle_authority_ = log_.read_view();
    settle_view_ = navigation_.view();
    if (settle_view_.zoom == ZoomLevel::k25Percent) {
      std::copy(canvas_.begin(), canvas_.end(), working_.begin());
    }
    settle_pending_ = true;
    maintenance_pending_ = true;
  }

  [[nodiscard]] bool prepare_settle_window() {
    const std::size_t columns = settle_column_count();
    const std::size_t rows = settle_row_count();
    const std::size_t total = columns * rows;
    while (settle_cursor_ < total) {
      const int column = static_cast<int>(settle_cursor_ % columns);
      const int row = static_cast<int>(settle_cursor_ / columns);
      settle_render_.overview = settle_view_.zoom == ZoomLevel::k25Percent;
      if (settle_render_.overview) {
        settle_render_.level_bounds = {
            column * static_cast<int>(kTileWidth), row * static_cast<int>(kTileHeight),
            std::min((column + 1) * static_cast<int>(kTileWidth), kOverviewWidth),
            std::min((row + 1) * static_cast<int>(kTileHeight), kOverviewHeight)};
      } else {
        const int first_column = settle_view_.level_pixels.x0 / static_cast<int>(kTileWidth);
        const int first_row = settle_view_.level_pixels.y0 / static_cast<int>(kTileHeight);
        settle_render_.key = {settle_view_.zoom, static_cast<std::uint16_t>(first_column + column),
                              static_cast<std::uint16_t>(first_row + row)};
        const auto source = materialized_->lookup(settle_render_.key);
        if (!source.has_value() || source->kind != SourceKind::kTileSlot ||
            source->quality >= MaterializationQuality::kSettled) {
          ++settle_cursor_;
          continue;
        }
        settle_render_.level_bounds = tile_pixel_bounds(settle_render_.key);
      }
      settle_render_.cursor.cancel();
      settle_render_.active = true;
      return true;
    }
    return false;
  }

  void stage_settled_overview_window(PixelRect bounds, std::span<const std::uint16_t> pixels) {
    const int source_width = bounds.x1 - bounds.x0;
    const std::size_t source_stride = static_cast<std::size_t>(source_width);
    for (int row = bounds.y0; row < bounds.y1; ++row) {
      const std::size_t source_at = static_cast<std::size_t>(row - bounds.y0) * source_stride;
      const std::size_t destination_at =
          static_cast<std::size_t>(row) * kOverviewWidth + static_cast<std::size_t>(bounds.x0);
      std::copy_n(pixels.begin() + static_cast<std::ptrdiff_t>(source_at), source_width,
                  working_.begin() + static_cast<std::ptrdiff_t>(destination_at));
    }
  }

  [[nodiscard]] bool settled_overview_window_changes(PixelRect bounds,
                                                     std::span<const std::uint16_t> pixels) const {
    const int width = bounds.x1 - bounds.x0;
    const std::size_t source_stride = static_cast<std::size_t>(width);
    for (int row = bounds.y0; row < bounds.y1; ++row) {
      const std::size_t source_at = static_cast<std::size_t>(row - bounds.y0) * source_stride;
      const std::size_t destination_at =
          static_cast<std::size_t>(row) * kOverviewWidth + static_cast<std::size_t>(bounds.x0);
      if (!std::equal(pixels.begin() + static_cast<std::ptrdiff_t>(source_at),
                      pixels.begin() + static_cast<std::ptrdiff_t>(source_at + source_stride),
                      working_.begin() + static_cast<std::ptrdiff_t>(destination_at))) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool publish_settle_window(bool no_ink) {
    if (no_ink) {
      return true;
    }
    const PixelRect bounds = settle_render_.level_bounds;
    if (settle_render_.overview) {
      if (!settled_overview_window_changes(bounds, storage_.settle_pixels)) {
        return true;
      }
      stage_settled_overview_window(bounds, storage_.settle_pixels);
    } else if (!materialized_
                    ->publish_tile(settle_render_.key, materialized_->current_revision(),
                                   MaterializationQuality::kSettled, storage_.settle_pixels)
                    .has_value()) {
      return false;
    }
    settle_changed_ = true;
    return true;
  }

  void finish_settle() {
    cancel_settle();
    maintenance_pending_ = false;
  }

  [[nodiscard]] bool run_settle_quantum() {
    if (!settle_pending_) {
      return true;
    }
    if (log_.read_view() != settle_authority_ || navigation_.view() != settle_view_ ||
        materialized_->current_revision() != log_.current_revision()) {
      finish_settle();
      return true;
    }
    if (settle_final_compose_) {
      const ComposeProgress progress = compose_production_view();
      if (progress == ComposeProgress::kError) {
        return false;
      }
      if (progress == ComposeProgress::kComplete) {
        finish_settle();
      }
      return true;
    }
    if (!settle_render_.active && !prepare_settle_window()) {
      if (!settle_changed_) {
        finish_settle();
        return true;
      }
      if (settle_view_.zoom == ZoomLevel::k25Percent) {
        std::swap(canvas_, working_);
        ++canvas_epoch_;
        canvas_view_ = settle_view_;
        mark_frame_dirty();
        finish_settle();
      } else {
        composition_.cancel();
        settle_final_compose_ = true;
      }
      return true;
    }
    const SettledRenderSlice slice = render_settled_window_slice(
        {.log = log_,
         .zoom = settle_view_.zoom,
         .window_bounds = settle_render_.level_bounds,
         .workspace = {.operation_alpha = storage_.settle_operation_alpha,
                       .accumulated_alpha = storage_.settle_accumulated_alpha,
                       .red = storage_.settle_red,
                       .green = storage_.settle_green,
                       .blue = storage_.settle_blue,
                       .candidate_indices = storage_.producer_candidate_indices},
         .out_pixels = storage_.settle_pixels,
         .cursor = settle_render_.cursor,
         .max_work_px = kSettleWorkPixelsPerQuantum});
    if (slice.status == SettledRenderStatus::kError) {
      return false;
    }
    if (slice.status == SettledRenderStatus::kComplete) {
      if (!publish_settle_window(slice.no_ink)) {
        return false;
      }
      settle_render_.cursor.cancel();
      settle_render_.active = false;
      ++settle_cursor_;
    }
    return true;
  }

  void run_maintenance_work(std::size_t& work_quanta, ApplicationAdvanceResult& result) {
    while (work_quanta > 0U && maintenance_pending_) {
      if (settle_pending_ && (interaction_ != Interaction::kIdle ||
                              chrome_.popup != ChromePopup::kNone || chrome_.confirm_new)) {
        break;
      }
      --work_quanta;
      const bool advanced = settle_pending_ ? run_settle_quantum() : run_idle_repair_quantum();
      if (!advanced) {
        result.error = ApplicationError::kRenderFailed;
        maintenance_pending_ = false;
        cancel_settle();
      }
    }
  }

  void run_background_work(std::size_t work_quanta, ApplicationAdvanceResult& result) {
    run_rebuild_work(work_quanta, result);
    run_maintenance_work(work_quanta, result);
  }

  void publish_advance_frame(ApplicationAdvanceResult& result) {
    if (!frame_dirty_) {
      return;
    }
    if (canvas_view_ != navigation_.view()) {
      return;
    }
    if (!publish_frame()) {
      result.error = ApplicationError::kRenderFailed;
      return;
    }
    result.frame_changed = true;
    result.damage = kFullFrame;
  }

  void finish_advance(bool initial_frame, ApplicationAdvanceResult& result) const {
    result.frame_epoch = frame_epoch_;
    if (initial_frame && !result.frame_changed) {
      result.frame_changed = true;
      result.damage = kFullFrame;
    }
    result.quiescent = !rebuild_pending_ && !maintenance_pending_ &&
                       interaction_ == Interaction::kIdle && !demo_.replaying();
    result.wants_immediate_advance = rebuild_pending_ || maintenance_pending_;
  }

  [[nodiscard]] bool import_fits(const ImportShape& shape) const {
    return shape.operation_count <= storage_.records.size() &&
           shape.sample_count <= storage_.samples.size() &&
           shape.operation_count <= storage_.import_records.size() &&
           shape.sample_count <= storage_.import_samples.size();
  }

  static bool valid_import_declarations(std::span<const std::byte> document,
                                        const ImportShape& shape) {
    std::size_t declared_samples = 0U;
    std::size_t metadata_at = kImportHeaderBytes;
    for (std::size_t index = 0U; index < shape.operation_count; ++index) {
      const std::uint8_t tool = std::to_integer<std::uint8_t>(document[metadata_at]);
      const std::size_t count = read_u16(document, metadata_at + 3U);
      metadata_at += kImportOperationHeaderBytes;
      if (tool > static_cast<std::uint8_t>(OperationTool::kEraser) || count == 0U ||
          count > shape.sample_count - declared_samples) {
        return false;
      }
      declared_samples += count;
    }
    return declared_samples == shape.sample_count;
  }

  [[nodiscard]] bool decode_import_operation(std::span<const std::byte> document,
                                             std::size_t operation_index, ImportCursor& cursor) {
    const auto tool =
        static_cast<OperationTool>(std::to_integer<std::uint8_t>(document[cursor.metadata_at]));
    const std::uint16_t color = read_u16(document, cursor.metadata_at + 1U);
    const std::uint16_t count = read_u16(document, cursor.metadata_at + 3U);
    cursor.metadata_at += kImportOperationHeaderBytes;
    for (std::size_t sample_index = 0U; sample_index < count; ++sample_index) {
      storage_.import_samples[cursor.first_sample + sample_index] =
          import_sample(document, cursor.sample_at);
      cursor.sample_at += kImportSampleBytes;
    }
    const auto operation_samples = storage_.import_samples.subspan(cursor.first_sample, count);
    const std::optional<PixelRect> bounds = operation_world_bounds(operation_samples);
    if (!elapsed_time_is_monotonic(operation_samples) || !bounds.has_value()) {
      return false;
    }
    storage_.import_records[operation_index] = {
        .first_sample = static_cast<std::uint32_t>(cursor.first_sample),
        .sample_count = count,
        .color = color,
        .bounds_x0 = static_cast<std::uint16_t>(bounds->x0),
        .bounds_y0 = static_cast<std::uint16_t>(bounds->y0),
        .bounds_x1 = static_cast<std::uint16_t>(bounds->x1),
        .bounds_y1 = static_cast<std::uint16_t>(bounds->y1),
        .tool = tool,
        .flags = 0U,
        .gesture_id = static_cast<std::uint16_t>(operation_index + 1U),
    };
    cursor.first_sample += count;
    return true;
  }

  [[nodiscard]] bool decode_import(std::span<const std::byte> document, const ImportShape& shape) {
    ImportCursor cursor{.sample_at = kImportHeaderBytes + shape.metadata_bytes};
    for (std::size_t index = 0U; index < shape.operation_count; ++index) {
      if (!decode_import_operation(document, index, cursor)) {
        return false;
      }
    }
    return true;
  }

  [[nodiscard]] ApplicationError preflight_import_render(
      const ImportShape& shape, std::span<std::uint8_t>& imported_may_ink) {
    if (materialized_ == nullptr) {
      return ApplicationError::kNone;
    }
    std::fill(working_overview_.begin(), working_overview_.end(), kPaper);
    imported_may_ink = storage_.producer_finalized_pixels.first(kOccupancyBytes);
    std::fill(imported_may_ink.begin(), imported_may_ink.end(), std::uint8_t{0});
    for (std::size_t index = 0U; index < shape.operation_count; ++index) {
      const OperationRecord& record = storage_.import_records[index];
      const auto samples =
          storage_.import_samples.subspan(record.first_sample, record.sample_count);
      if (!apply_incremental_operation(
              {.tool = record.tool, .color = record.color, .samples = samples},
              overview_surface(working_overview_))) {
        return ApplicationError::kRenderFailed;
      }
      if (record.tool == OperationTool::kPen) {
        mark_may_ink({record.bounds_x0, record.bounds_y0, record.bounds_x1, record.bounds_y1},
                     imported_may_ink);
      }
    }
    return ApplicationError::kNone;
  }

  void reset_import_session(std::size_t operation_count) {
    if (interaction_ == Interaction::kStroke) {
      ink_.end();
    }
    interaction_ = Interaction::kIdle;
    builder_.cancel();
    preview_builder_.cancel();
    clear_staged_stroke();
    cancel_rebuild();
    demo_.begin_recording(0U);
    demo_.stop_recording();
    navigation_ = {};
    reset_chrome(false);
    next_gesture_ = operation_count == std::numeric_limits<std::uint16_t>::max()
                        ? 1U
                        : static_cast<std::uint16_t>(operation_count + 1U);
    stroke_has_segment_ = false;
    start_rebuild();
  }

  [[nodiscard]] ApplicationError publish_import(const ImportShape& shape, std::uint32_t generation,
                                                std::span<std::uint8_t> imported_may_ink) {
    const AuthorityRestore restore{
        .epoch = {log_.epoch().value + 1U},
        .generation = {generation},
        .active_operation_count = shape.operation_count,
        .records = storage_.import_records.first(shape.operation_count),
        .samples = storage_.import_samples.first(shape.sample_count),
    };
    if (!log_.restore(restore)) {
      return ApplicationError::kMalformedDocument;
    }
    if (materialized_ != nullptr) {
      // The parser and preflight render validate every fallible input first;
      // restore_snapshot cannot reject this fixed, disjoint storage shape.
      if (!materialized_->restore_snapshot(log_.current_revision(), working_overview_,
                                           imported_may_ink)) {
        return ApplicationError::kRenderFailed;
      }
      producer_->cancel_pending_work();
    }
    reset_import_session(shape.operation_count);
    return ApplicationError::kNone;
  }

  static void count_source(const std::optional<SourceSelection>& source, std::size_t& raw,
                           std::size_t& uniform, std::size_t& fallback) {
    if (!source.has_value() || source->kind == SourceKind::kOverview) {
      ++fallback;
      return;
    }
    if (source->kind == SourceKind::kUniform) {
      ++uniform;
      return;
    }
    ++raw;
  }

  void tile_census(ZoomLevel zoom, std::size_t& raw, std::size_t& uniform,
                   std::size_t& fallback) const {
    const TileGrid grid = tile_grid(zoom);
    for (int row = 0; row < grid.rows; ++row) {
      for (int column = 0; column < grid.columns; ++column) {
        count_source(materialized_->lookup({zoom, static_cast<std::uint16_t>(column),
                                            static_cast<std::uint16_t>(row)}),
                     raw, uniform, fallback);
      }
    }
  }

  [[nodiscard]] RasterSurface view_surface(std::span<std::uint16_t> pixels) const {
    return {.zoom = navigation_.zoom(),
            .level_bounds = navigation_.view().level_pixels,
            .pixels = pixels,
            .stride = kOverviewWidth};
  }

  [[nodiscard]] static RasterSurface overview_surface(std::span<std::uint16_t> pixels) {
    return {.zoom = ZoomLevel::k25Percent,
            .level_bounds = kFullFrame,
            .pixels = pixels,
            .stride = kOverviewWidth};
  }

  [[nodiscard]] static bool render_operation(const StoredOperation& operation,
                                             const RasterSurface& surface) {
    return apply_incremental_operation(append_view(operation), surface);
  }

  [[nodiscard]] static bool render_operation(const BuiltOperation& operation,
                                             const RasterSurface& surface) {
    return apply_incremental_operation(operation.operation(), surface);
  }

  [[nodiscard]] static bool render_operation(const OperationAppend& operation,
                                             const RasterSurface& surface) {
    return apply_incremental_operation(operation, surface);
  }

  [[nodiscard]] bool render_staged_stroke(const RasterSurface& surface) const {
    const auto appends = storage_.staged_stroke_appends.first(staged_stroke_count_);
    return std::all_of(appends.begin(), appends.end(), [&](const OperationAppend& operation) {
      return render_operation(operation, surface);
    });
  }

  [[nodiscard]] bool live_canvas_current() const {
    return live_canvas_epoch_ == canvas_epoch_ && live_view_ == canvas_view_;
  }

  [[nodiscard]] bool rebase_live_canvas() {
    std::copy(canvas_.begin(), canvas_.end(), live_.begin());
    if (!render_staged_stroke(view_surface(live_))) {
      return false;
    }
    live_canvas_epoch_ = canvas_epoch_;
    live_view_ = canvas_view_;
    return true;
  }

  void sync_history() {
    chrome_.can_undo = log_.can_undo();
    chrome_.can_redo = log_.can_redo();
  }

  [[nodiscard]] bool publish_frame() {
    if (interaction_ == Interaction::kStroke) {
      if (!live_canvas_current() && !rebase_live_canvas()) {
        return false;
      }
      std::copy(live_.begin(), live_.end(), frame_.begin());
      const auto live = preview_builder_.collected();
      const RasterSurface surface = view_surface(frame_);
      if (live.has_value() && !render_operation(*live, surface)) {
        return false;
      }
    } else {
      std::copy(canvas_.begin(), canvas_.end(), frame_.begin());
    }
    const ChromeNavigation navigation = chrome_navigation(navigation_, overview_);
    const std::uint32_t revision = log_.current_revision().value;
    if (!chrome_cache_.prepare(chrome_, navigation, revision) ||
        !chrome_cache_.paint_prepared({.pixels = frame_,
                                       .width = kOverviewWidth,
                                       .height = kOverviewHeight,
                                       .origin_x = 0,
                                       .origin_y = 0,
                                       .byte_swapped = false},
                                      chrome_, navigation, revision)) {
      return false;
    }
    ++frame_epoch_;
    frame_dirty_ = false;
    return true;
  }

  void mark_frame_dirty() { frame_dirty_ = true; }

  void start_rebuild() {
    if (materialized_ != nullptr) {
      cancel_settle();
      composition_.cancel();
      producer_->cancel_pending_work();
      rebuild_authority_ = log_.read_view();
      rebuild_view_ = navigation_.view();
      idle_repair_cursor_ = 0U;
      maintenance_pending_ = false;
      rebuild_phase_ = RebuildPhase::kComposeFallback;
      rebuild_pending_ = true;
      return;
    }
    std::fill(working_.begin(), working_.end(), kPaper);
    std::fill(working_overview_.begin(), working_overview_.end(), kPaper);
    rebuild_authority_ = log_.read_view();
    rebuild_view_ = navigation_.view();
    rebuild_cursor_ = 0U;
    idle_repair_cursor_ = 0U;
    rebuild_pending_ = true;
    if (rebuild_authority_.active_operation_count == 0U) {
      finish_rebuild();
    }
  }

  void cancel_rebuild() {
    cancel_settle();
    composition_.cancel();
    if (producer_ != nullptr) {
      producer_->cancel_pending_work();
    }
    rebuild_pending_ = false;
    rebuild_cursor_ = 0U;
    rebuild_phase_ = RebuildPhase::kIdle;
  }

  void finish_rebuild() {
    std::swap(canvas_, working_);
    ++canvas_epoch_;
    canvas_view_ = rebuild_view_;
    std::swap(overview_, working_overview_);
    rebuild_pending_ = false;
    rebuild_cursor_ = 0U;
    rebuild_phase_ = RebuildPhase::kIdle;
    chrome_.history_busy = false;
    mark_frame_dirty();
  }

  [[nodiscard]] bool run_rebuild_operation() {
    if (!rebuild_pending_) {
      return true;
    }
    if (log_.read_view() != rebuild_authority_ || navigation_.view() != rebuild_view_) {
      start_rebuild();
      return true;
    }
    if (rebuild_cursor_ >= rebuild_authority_.active_operation_count) {
      finish_rebuild();
      return true;
    }
    const std::optional<StoredOperation> operation = log_.operation(rebuild_cursor_);
    if (!operation.has_value() || !render_operation(*operation, view_surface(working_)) ||
        !render_operation(*operation, overview_surface(working_overview_))) {
      return false;
    }
    ++rebuild_cursor_;
    if (rebuild_cursor_ == rebuild_authority_.active_operation_count) {
      finish_rebuild();
    }
    return true;
  }

  [[nodiscard]] ComposeProgress compose_production_view() {
    for (std::size_t slice = 0U; slice < kCompositionSlicesPerWorkQuantum; ++slice) {
      const ViewCompositionSliceResult result =
          materialized_->compose_view_slice(rebuild_view_, working_, composition_);
      if (result.status == ViewCompositionStatus::kError) {
        return ComposeProgress::kError;
      }
      if (result.status == ViewCompositionStatus::kComplete) {
        last_composition_ = result.stats;
        composition_.cancel();
        std::swap(canvas_, working_);
        ++canvas_epoch_;
        canvas_view_ = rebuild_view_;
        mark_frame_dirty();
        return ComposeProgress::kComplete;
      }
    }
    return ComposeProgress::kInProgress;
  }

  [[nodiscard]] bool run_fallback_quantum() {
    const ComposeProgress progress = compose_production_view();
    if (progress == ComposeProgress::kError) {
      return false;
    }
    if (progress == ComposeProgress::kComplete) {
      if (rebuild_view_.zoom == ZoomLevel::k25Percent) {
        begin_idle_repair();
      } else {
        rebuild_phase_ = RebuildPhase::kProduce;
      }
    }
    return true;
  }

  [[nodiscard]] bool run_producer_quantum() {
    const std::optional<TileProductionStep> step = producer_->produce_next(rebuild_view_);
    if (!step.has_value()) {
      return false;
    }
    if (step->complete) {
      static_cast<void>(materialized_->remember_view(rebuild_view_));
      rebuild_phase_ = RebuildPhase::kComposeFinal;
    }
    return true;
  }

  [[nodiscard]] bool run_final_composition_quantum() {
    const ComposeProgress progress = compose_production_view();
    if (progress == ComposeProgress::kError) {
      return false;
    }
    if (progress == ComposeProgress::kComplete) {
      begin_idle_repair();
    }
    return true;
  }

  [[nodiscard]] bool run_production_rebuild_quantum() {
    if (!rebuild_pending_) {
      return true;
    }
    if (log_.read_view() != rebuild_authority_ || navigation_.view() != rebuild_view_ ||
        materialized_->current_revision() != log_.current_revision()) {
      start_rebuild();
      return true;
    }
    switch (rebuild_phase_) {
      case RebuildPhase::kComposeFallback:
        return run_fallback_quantum();
      case RebuildPhase::kProduce:
        return run_producer_quantum();
      case RebuildPhase::kComposeFinal:
        return run_final_composition_quantum();
      case RebuildPhase::kIdle:
        return false;
    }
    return false;
  }

  void begin_idle_repair() {
    idle_repair_ = plan_idle_repair(rebuild_view_, materialized_->recent_views());
    idle_repair_cursor_ = 0U;
    maintenance_pending_ = idle_repair_.count != 0U;
    finish_production_rebuild();
    if (!maintenance_pending_) {
      begin_settle();
    }
  }

  [[nodiscard]] bool run_idle_repair_quantum() {
    if (!maintenance_pending_) {
      return true;
    }
    if (log_.read_view() != rebuild_authority_ || navigation_.view() != rebuild_view_ ||
        materialized_->current_revision() != log_.current_revision()) {
      maintenance_pending_ = false;
      producer_->cancel_pending_work();
      return true;
    }
    if (idle_repair_cursor_ >= idle_repair_.count ||
        (idle_repair_cursor_ >= idle_repair_.grid_start &&
         materialized_->resident_raw_tiles() >= materialized_->slot_capacity())) {
      begin_settle();
      return true;
    }
    const auto step = producer_->produce_next(idle_repair_.views[idle_repair_cursor_]);
    if (!step.has_value()) {
      return false;
    }
    if (step->complete) {
      ++idle_repair_cursor_;
    }
    return true;
  }

  void finish_production_rebuild() {
    rebuild_pending_ = false;
    rebuild_phase_ = RebuildPhase::kIdle;
    chrome_.history_busy = false;
  }

  [[nodiscard]] OperationPoint operation_point(const InkPoint& point) const {
    const float scale = static_cast<float>(zoom_percent(navigation_.zoom())) / 100.0F;
    const float inverse_scale = 1.0F / scale;
    return {
        .world_x = std::clamp(
            (static_cast<float>(navigation_.origin().x) + point.position.x) * inverse_scale, 0.0F,
            static_cast<float>(kWorldWidth)),
        .world_y = std::clamp(
            (static_cast<float>(navigation_.origin().y) + point.position.y) * inverse_scale, 0.0F,
            static_cast<float>(kWorldHeight)),
        .radius = point.radius * inverse_scale,
        .timestamp_us = point.timestamp_us,
    };
  }

  [[nodiscard]] ApplicationDemoMode demo_mode() const {
    if (storage_.demo_samples.empty()) {
      return ApplicationDemoMode::kUnavailable;
    }
    if (demo_.recording()) {
      return ApplicationDemoMode::kRecording;
    }
    if (demo_.replaying()) {
      return ApplicationDemoMode::kReplaying;
    }
    return demo_.size() == 0U ? ApplicationDemoMode::kEmpty : ApplicationDemoMode::kReady;
  }

  [[nodiscard]] std::uint64_t authority_fingerprint() const {
    std::uint64_t hash = 14'695'981'039'346'656'037ULL;
    fingerprint_u64(hash, static_cast<std::uint64_t>(log_.operation_count()));
    for (std::size_t index = 0U; index < log_.operation_count(); ++index) {
      const std::optional<StoredOperation> operation = log_.operation(index);
      if (!operation.has_value()) {
        fingerprint_byte(hash, 0xFFU);
        continue;
      }
      fingerprint_byte(hash, static_cast<std::uint8_t>(operation->tool));
      fingerprint_u16(hash, operation->color);
      fingerprint_u16(hash, operation->gesture_id);
      fingerprint_u64(hash, static_cast<std::uint64_t>(operation->samples.size()));
      for (const CompactOperationSample sample : operation->samples) {
        fingerprint_u16(hash, sample.x_quarter);
        fingerprint_u16(hash, sample.y_quarter);
        fingerprint_u16(hash, sample.radius_256);
        fingerprint_u16(hash, sample.elapsed_ms);
      }
    }
    return hash;
  }

  void reset_chrome(bool recording) {
    chrome_ = {};
    chrome_.tool = ChromeTool::kDraw;
    chrome_.size = ChromeSize::kLarge;
    chrome_.color_index = 12U;
    chrome_.recording = recording;
    sync_history();
  }

  [[nodiscard]] bool reset_authority_blank(DocumentRevision revision) {
    if (materialized_ == nullptr) {
      return log_.reset(revision);
    }
    producer_->cancel_pending_work();
    composition_.cancel();
    return reset_blank_document(log_, *materialized_, revision) &&
           producer_->reset_uniform_baseline(revision, kPaper);
  }

  [[nodiscard]] ApplicationError reset_to_demo_baseline() {
    if (log_.current_revision().value == std::numeric_limits<std::uint32_t>::max()) {
      return ApplicationError::kAuthorityFull;
    }
    if (interaction_ == Interaction::kStroke) {
      ink_.end();
    }
    interaction_ = Interaction::kIdle;
    builder_.cancel();
    preview_builder_.cancel();
    clear_staged_stroke();
    cancel_rebuild();
    if (!reset_authority_blank({log_.current_revision().value + 1U})) {
      return ApplicationError::kAuthorityFull;
    }
    navigation_ = {};
    reset_chrome(false);
    next_gesture_ = 1U;
    stroke_has_segment_ = false;
    start_rebuild();
    return ApplicationError::kNone;
  }

  [[nodiscard]] ApplicationError handle_demo_long_press(std::uint32_t event_us) {
    if (storage_.demo_samples.empty() || demo_.replaying()) {
      return ApplicationError::kNone;
    }
    if (demo_.recording()) {
      demo_.stop_recording();
      chrome_.recording = false;
      mark_frame_dirty();
      return ApplicationError::kNone;
    }
    if (demo_.size() != 0U && !demo_.overflowed()) {
      const ApplicationError reset = reset_to_demo_baseline();
      if (reset != ApplicationError::kNone) {
        return reset;
      }
      static_cast<void>(demo_.begin_replay(event_us));
      return ApplicationError::kNone;
    }
    const ApplicationError reset = reset_to_demo_baseline();
    if (reset != ApplicationError::kNone) {
      return reset;
    }
    demo_.begin_recording(event_us);
    chrome_.recording = true;
    mark_frame_dirty();
    return ApplicationError::kNone;
  }

  [[nodiscard]] ApplicationError dispatch_demo_event(const DemoEvent& event) {
    if (event.kind == DemoEventKind::kZoom) {
      return dispatch_event(
          {.kind = ApplicationEventKind::kZoomNext, .timestamp_us = event.timestamp_us},
          event.timestamp_us);
    }
    const std::optional<TouchEventKind> touch_kind = demo_touch_kind(event.kind);
    if (!touch_kind.has_value()) {
      return ApplicationError::kInvalidEvent;
    }
    const ApplicationEventKind kind =
        *touch_kind == TouchEventKind::kDown   ? ApplicationEventKind::kTouchDown
        : *touch_kind == TouchEventKind::kMove ? ApplicationEventKind::kTouchMove
                                               : ApplicationEventKind::kTouchUp;
    return dispatch_event(
        {.kind = kind, .x = event.point.x, .y = event.point.y, .timestamp_us = event.timestamp_us},
        event.timestamp_us);
  }

  void record_demo_event(const ApplicationEvent& event, std::uint32_t event_us) {
    if (!demo_.recording()) {
      return;
    }
    if (event.kind == ApplicationEventKind::kZoomNext) {
      static_cast<void>(demo_.record_zoom(event_us));
    } else if (event.kind == ApplicationEventKind::kTouchDown ||
               event.kind == ApplicationEventKind::kTouchMove ||
               event.kind == ApplicationEventKind::kTouchUp) {
      const TouchEventKind kind =
          event.kind == ApplicationEventKind::kTouchDown   ? TouchEventKind::kDown
          : event.kind == ApplicationEventKind::kTouchMove ? TouchEventKind::kMove
                                                           : TouchEventKind::kUp;
      static_cast<void>(demo_.record_touch(
          {.point = {event.x, event.y}, .timestamp_us = event_us, .sequence = 0U, .kind = kind}));
    }
    if (!demo_.recording()) {
      chrome_.recording = false;
      mark_frame_dirty();
    }
  }

  [[nodiscard]] ApplicationError handle_input_event(const ApplicationEvent& event,
                                                    std::uint32_t now_us) {
    const std::uint32_t event_us = event.timestamp_us == 0U ? now_us : event.timestamp_us;
    if (event.kind == ApplicationEventKind::kDemoLongPress) {
      return handle_demo_long_press(event_us);
    }
    if (demo_.replaying()) {
      return ApplicationError::kNone;
    }
    const bool recordable_touch = (event.kind == ApplicationEventKind::kTouchDown ||
                                   event.kind == ApplicationEventKind::kTouchMove ||
                                   event.kind == ApplicationEventKind::kTouchUp) &&
                                  point_in_panel(event.x, event.y);
    if (event.kind == ApplicationEventKind::kZoomNext || recordable_touch) {
      record_demo_event(event, event_us);
    }
    return dispatch_event(event, now_us);
  }

  [[nodiscard]] ApplicationError dispatch_event(const ApplicationEvent& event,
                                                std::uint32_t now_us) {
    const std::uint32_t event_us = event.timestamp_us == 0U ? now_us : event.timestamp_us;
    switch (event.kind) {
      case ApplicationEventKind::kZoomNext:
        return set_zoom(navigation_.zoom() == ZoomLevel::k400Percent
                            ? ZoomLevel::k25Percent
                            : next_zoom(navigation_.zoom()));
      case ApplicationEventKind::kZoomPrevious:
        return set_zoom(previous_zoom(navigation_.zoom()));
      case ApplicationEventKind::kDemoLongPress:
        return ApplicationError::kNone;
      case ApplicationEventKind::kTouchDown:
      case ApplicationEventKind::kTouchMove:
      case ApplicationEventKind::kTouchUp:
        if (!point_in_panel(event.x, event.y)) {
          return ApplicationError::kInvalidEvent;
        }
        break;
    }
    const ChromePoint point{event.x, event.y};
    if (event.kind == ApplicationEventKind::kTouchDown) {
      return touch_down(point, event_us);
    }
    if (event.kind == ApplicationEventKind::kTouchMove) {
      return touch_move(point, event_us);
    }
    return touch_up(point, event_us);
  }

  [[nodiscard]] ApplicationError touch_down(ChromePoint point, std::uint32_t event_us) {
    if (interaction_ != Interaction::kIdle) {
      return ApplicationError::kInvalidEvent;
    }
    last_touch_ = point;
    gesture_start_ = point;
    pan_start_origin_ = navigation_.origin();

    if (chrome_minimap_contains(point, chrome_)) {
      interaction_ = Interaction::kMinimap;
      move_minimap(point);
      return ApplicationError::kNone;
    }
    if (chrome_contains(point, chrome_)) {
      interaction_ = Interaction::kToolbar;
      return ApplicationError::kNone;
    }
    if (chrome_.popup != ChromePopup::kNone || chrome_.confirm_new) {
      chrome_.popup = ChromePopup::kNone;
      chrome_.confirm_new = false;
      interaction_ = Interaction::kDismissOverlay;
      mark_frame_dirty();
      return ApplicationError::kNone;
    }
    if (chrome_.tool == ChromeTool::kPan) {
      interaction_ = Interaction::kPan;
      return ApplicationError::kNone;
    }
    if (point.y >= static_cast<float>(chrome_ink_bottom(chrome_))) {
      return ApplicationError::kInvalidEvent;
    }

    InkConfig config = ink_.config();
    config.size = brush_size(chrome_.size);
    config.streamline = 0.4F;
    ink_.set_config(config);
    const InkPoint ink = ink_.begin({.x = point.x, .y = point.y, .timestamp_us = event_us});
    const OperationTool tool =
        chrome_.tool == ChromeTool::kErase ? OperationTool::kEraser : OperationTool::kPen;
    const std::uint16_t color = tool == OperationTool::kEraser ? kPaper : selected_color(chrome_);
    const OperationPoint first = operation_point(ink);
    clear_staged_stroke();
    if (!builder_.begin(tool, color, next_gesture_, first) ||
        !preview_builder_.begin(tool, color, first, next_gesture_)) {
      ink_.end();
      builder_.cancel();
      preview_builder_.cancel();
      return ApplicationError::kAuthorityFull;
    }
    std::copy(canvas_.begin(), canvas_.end(), live_.begin());
    live_canvas_epoch_ = canvas_epoch_;
    live_view_ = canvas_view_;
    interaction_ = Interaction::kStroke;
    stroke_start_ = point;
    last_canvas_touch_ = point;
    stroke_has_segment_ = false;
    stroke_tool_ = tool;
    stroke_color_ = color;
    last_authority_point_ = first;
    mark_frame_dirty();
    return ApplicationError::kNone;
  }

  void clear_staged_stroke() {
    std::fill_n(storage_.staged_stroke_appends.begin(), staged_stroke_count_, OperationAppend{});
    staged_stroke_count_ = 0U;
    staged_stroke_sample_count_ = 0U;
  }

  [[nodiscard]] std::optional<ChainedOperationStatus> stage_pending_chunk() {
    const auto built = builder_.pending_append();
    if (!built.has_value() || staged_stroke_count_ == storage_.staged_stroke_appends.size() ||
        built->operation().samples.size() >
            storage_.staged_stroke_samples.size() - staged_stroke_sample_count_) {
      return std::nullopt;
    }
    const bool update_live = canvas_view_ == navigation_.view();
    if (update_live && !live_canvas_current() && !rebase_live_canvas()) {
      return std::nullopt;
    }
    auto destination = storage_.staged_stroke_samples.subspan(staged_stroke_sample_count_,
                                                              built->operation().samples.size());
    std::copy(built->operation().samples.begin(), built->operation().samples.end(),
              destination.begin());
    const OperationAppend staged{
        .tool = built->operation().tool,
        .color = built->operation().color,
        .gesture_id = built->operation().gesture_id,
        .samples = destination,
    };
    if (update_live && !render_operation(staged, view_surface(live_))) {
      return std::nullopt;
    }
    storage_.staged_stroke_appends[staged_stroke_count_] = staged;
    staged_stroke_sample_count_ += destination.size();
    ++staged_stroke_count_;
    return builder_.acknowledge_commit();
  }

  void recover_materialization_from_authority() {
    const bool recovered =
        replay_active_overview(log_, working_overview_) &&
        materialized_->restore_snapshot(log_.current_revision(), working_overview_);
    assert(recovered);
    static_cast<void>(recovered);
  }

  void commit_staged_materialization(DocumentRevision initial_revision) {
    const bool can_adopt_live =
        !rebuild_pending_ && live_canvas_current() && canvas_view_ == navigation_.view();
    for (std::size_t index = 0U; index < staged_stroke_count_; ++index) {
      const OperationAppend& operation = storage_.staged_stroke_appends[index];
      const auto world_bounds = operation_world_bounds(operation.samples);
      if (!world_bounds.has_value()) {
        recover_materialization_from_authority();
        start_rebuild();
        return;
      }
      const PixelRect overview_bounds = overview_bounds_for_world(*world_bounds);
      const int width = overview_bounds.x1 - overview_bounds.x0;
      const int height = overview_bounds.y1 - overview_bounds.y0;
      const std::size_t patch_width = static_cast<std::size_t>(width);
      const std::size_t patch_height = static_cast<std::size_t>(height);
      auto overview_patch = working_overview_.first(patch_width * patch_height);
      const auto source = materialized_->overview_pixels();
      for (int row = 0; row < height; ++row) {
        const auto source_at = static_cast<std::size_t>(overview_bounds.y0 + row) * kOverviewWidth +
                               static_cast<std::size_t>(overview_bounds.x0);
        const auto destination_at = static_cast<std::ptrdiff_t>(row) * width;
        std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(source_at), width,
                    overview_patch.begin() + destination_at);
      }
      const RasterSurface patch_surface{
          .zoom = ZoomLevel::k25Percent,
          .level_bounds = overview_bounds,
          .pixels = overview_patch,
          .stride = width,
      };
      const DocumentRevision revision{initial_revision.value + static_cast<std::uint32_t>(index) +
                                      1U};
      if (!render_operation(operation, patch_surface) ||
          !materialized_->commit_incremental_revision(
              revision, {.bounds = overview_bounds, .pixels = overview_patch}, *world_bounds, {})) {
        recover_materialization_from_authority();
        start_rebuild();
        return;
      }
    }
    if (can_adopt_live) {
      std::copy(live_.begin(), live_.end(), canvas_.begin());
      ++canvas_epoch_;
    }
    start_rebuild();
  }

  [[nodiscard]] bool commit_staged_stroke() {
    const auto appends = storage_.staged_stroke_appends.first(staged_stroke_count_);
    if (appends.empty()) {
      return false;
    }
    const DocumentRevision initial_revision = log_.current_revision();
    if (!log_.append_group(appends).has_value()) {
      return false;
    }
    if (materialized_ != nullptr) {
      commit_staged_materialization(initial_revision);
    } else if (!rebuild_pending_) {
      for (const OperationAppend& operation : appends) {
        if (!render_operation(operation, view_surface(canvas_)) ||
            !render_operation(operation, overview_surface(overview_))) {
          start_rebuild();
          break;
        }
        ++canvas_epoch_;
      }
    } else {
      start_rebuild();
    }
    clear_staged_stroke();
    sync_history();
    mark_frame_dirty();
    return true;
  }

  [[nodiscard]] bool reset_preview_chunk(OperationPoint boundary, OperationPoint next) {
    preview_builder_.cancel();
    return preview_builder_.begin(stroke_tool_, stroke_color_, boundary, next_gesture_) &&
           preview_builder_.add(next);
  }

  void move_toolbar(ChromePoint point) {
    ChromeState pan_promotion = chrome_;
    pan_promotion.tool = ChromeTool::kPan;
    if (chrome_promotes_pan_drag(gesture_start_, point, pan_promotion)) {
      interaction_ = Interaction::kPan;
      move_pan(point);
    }
  }

  [[nodiscard]] ApplicationError reject_stroke_move() {
    builder_.cancel();
    preview_builder_.cancel();
    clear_staged_stroke();
    ink_.end();
    interaction_ = Interaction::kIdle;
    mark_frame_dirty();
    return ApplicationError::kInvalidEvent;
  }

  [[nodiscard]] ApplicationError append_stroke_move(OperationPoint next, OperationPoint boundary) {
    const ChainedOperationStatus chain_result = builder_.add(next);
    if (chain_result == ChainedOperationStatus::kAccepted) {
      if (preview_builder_.add(next)) {
        return ApplicationError::kNone;
      }
      static_cast<void>(reject_stroke_move());
      return ApplicationError::kRenderFailed;
    }
    if (chain_result != ChainedOperationStatus::kChunkReady) {
      return reject_stroke_move();
    }
    const auto continued = stage_pending_chunk();
    if (!continued.has_value() || *continued != ChainedOperationStatus::kAccepted) {
      static_cast<void>(reject_stroke_move());
      return ApplicationError::kAuthorityFull;
    }
    if (reset_preview_chunk(boundary, next)) {
      return ApplicationError::kNone;
    }
    static_cast<void>(reject_stroke_move());
    return ApplicationError::kRenderFailed;
  }

  [[nodiscard]] ApplicationError move_stroke(ChromePoint point, std::uint32_t event_us) {
    const std::optional<ChromePoint> clipped =
        clip_canvas_segment(last_canvas_touch_, point, chrome_);
    if (!clipped.has_value()) {
      return ApplicationError::kNone;
    }
    last_canvas_touch_ = *clipped;
    stroke_has_segment_ = true;
    const InkPoint ink = ink_.update({.x = clipped->x, .y = clipped->y, .timestamp_us = event_us});
    const OperationPoint next = operation_point(ink);
    const OperationPoint boundary = last_authority_point_;
    const ApplicationError appended = append_stroke_move(next, boundary);
    if (appended != ApplicationError::kNone) {
      return appended;
    }
    last_authority_point_ = next;
    mark_frame_dirty();
    return ApplicationError::kNone;
  }

  [[nodiscard]] ApplicationError touch_move(ChromePoint point, std::uint32_t event_us) {
    if (interaction_ == Interaction::kIdle || interaction_ == Interaction::kDismissOverlay) {
      return ApplicationError::kNone;
    }
    if (point.x == last_touch_.x && point.y == last_touch_.y) {
      return ApplicationError::kNone;
    }
    last_touch_ = point;
    switch (interaction_) {
      case Interaction::kPan:
        move_pan(point);
        break;
      case Interaction::kMinimap:
        move_minimap(point);
        break;
      case Interaction::kToolbar:
        move_toolbar(point);
        break;
      case Interaction::kStroke:
        return move_stroke(point, event_us);
      case Interaction::kIdle:
      case Interaction::kDismissOverlay:
        break;
    }
    return ApplicationError::kNone;
  }

  [[nodiscard]] ApplicationError touch_up(ChromePoint point, std::uint32_t event_us) {
    const Interaction completed = interaction_;
    interaction_ = Interaction::kIdle;
    if (completed == Interaction::kToolbar) {
      return apply_chrome_action(chrome_action_at(point, chrome_), point);
    }
    if (completed != Interaction::kStroke) {
      return ApplicationError::kNone;
    }

    if (chrome_accepts_stroke_finish(stroke_start_, stroke_has_segment_)) {
      const std::optional<ChromePoint> clipped =
          clip_canvas_segment(last_canvas_touch_, point, chrome_);
      const ChromePoint finish_point = clipped.value_or(last_canvas_touch_);
      const InkPoint ink =
          ink_.finish({.x = finish_point.x, .y = finish_point.y, .timestamp_us = event_us});
      const OperationPoint final = operation_point(ink);
      const OperationPoint boundary = last_authority_point_;
      ChainedOperationStatus chain_result = builder_.finish(final);
      if (chain_result == ChainedOperationStatus::kChunkReady) {
        const auto continued = stage_pending_chunk();
        if (!continued.has_value() || *continued != ChainedOperationStatus::kFinalChunkReady ||
            !reset_preview_chunk(boundary, final) || !preview_builder_.finish().has_value()) {
          builder_.cancel();
          preview_builder_.cancel();
          clear_staged_stroke();
          mark_frame_dirty();
          return ApplicationError::kAuthorityFull;
        }
        chain_result = *continued;
      } else if (chain_result == ChainedOperationStatus::kFinalChunkReady &&
                 !preview_builder_.finish(final).has_value()) {
        builder_.cancel();
        preview_builder_.cancel();
        clear_staged_stroke();
        mark_frame_dirty();
        return ApplicationError::kRenderFailed;
      }
      if (chain_result != ChainedOperationStatus::kFinalChunkReady) {
        builder_.cancel();
        preview_builder_.cancel();
        clear_staged_stroke();
        mark_frame_dirty();
        return ApplicationError::kInvalidEvent;
      }
      const auto complete = stage_pending_chunk();
      if (!complete.has_value() || *complete != ChainedOperationStatus::kComplete ||
          !commit_staged_stroke()) {
        builder_.cancel();
        preview_builder_.cancel();
        clear_staged_stroke();
        mark_frame_dirty();
        return ApplicationError::kAuthorityFull;
      }
    } else {
      ink_.end();
      builder_.cancel();
      preview_builder_.cancel();
      clear_staged_stroke();
      mark_frame_dirty();
      return ApplicationError::kInvalidEvent;
    }
    preview_builder_.cancel();
    ++next_gesture_;
    if (next_gesture_ == 0U) {
      next_gesture_ = 1U;
    }
    mark_frame_dirty();
    return ApplicationError::kNone;
  }

  void move_pan(ChromePoint point) {
    const int requested_x =
        pan_start_origin_.x + static_cast<int>(std::lround(gesture_start_.x - point.x));
    const int requested_y =
        pan_start_origin_.y + static_cast<int>(std::lround(gesture_start_.y - point.y));
    const NavigationPoint focus{
        std::clamp(static_cast<int>(std::lround(point.x)), 0, kOverviewWidth - 1),
        std::clamp(static_cast<int>(std::lround(point.y)), 0, kOverviewHeight - 1)};
    const NavigationPoint before = navigation_.origin();
    if (navigation_.set_origin(requested_x, requested_y, focus) && navigation_.origin() != before) {
      start_rebuild();
    }
  }

  void move_minimap(ChromePoint point) {
    ChromeNavigation navigation = chrome_navigation(navigation_, overview_);
    navigation.level_x = pan_start_origin_.x;
    navigation.level_y = pan_start_origin_.y;
    const ChromeLevelPoint requested =
        chrome_minimap_drag_origin(point, {.x = kDefaultFocus.x, .y = kDefaultFocus.y}, navigation);
    const NavigationPoint before = navigation_.origin();
    if (navigation_.set_origin(requested.x, requested.y, kDefaultFocus) &&
        navigation_.origin() != before) {
      start_rebuild();
    }
  }

  [[nodiscard]] ApplicationError set_zoom(ZoomLevel target) {
    if (target == navigation_.zoom()) {
      return ApplicationError::kNone;
    }
    if (!navigation_.set_zoom(target, kDefaultFocus)) {
      return ApplicationError::kInvalidEvent;
    }
    start_rebuild();
    return ApplicationError::kNone;
  }

  [[nodiscard]] ApplicationError move_history(bool undo) {
    if (materialized_ != nullptr) {
      const bool available = undo ? log_.can_undo() : log_.can_redo();
      if (!available) {
        sync_history();
        mark_frame_dirty();
        return ApplicationError::kNone;
      }
      producer_->cancel_pending_work();
      composition_.cancel();
      if (!move_history_incrementally(log_, *materialized_,
                                      undo ? HistoryDirection::kUndo : HistoryDirection::kRedo,
                                      working_overview_)
               .has_value()) {
        return ApplicationError::kRenderFailed;
      }
      chrome_.popup = ChromePopup::kNone;
      chrome_.history_busy = true;
      sync_history();
      start_rebuild();
      return ApplicationError::kNone;
    }
    std::optional<PreparedHistoryChange> change = undo ? log_.prepare_undo() : log_.prepare_redo();
    if (!change.has_value()) {
      sync_history();
      mark_frame_dirty();
      return ApplicationError::kNone;
    }
    change->publish();
    chrome_.popup = ChromePopup::kNone;
    chrome_.history_busy = true;
    sync_history();
    start_rebuild();
    return ApplicationError::kNone;
  }

  [[nodiscard]] ApplicationError apply_chrome_action(ChromeAction action, ChromePoint point) {
    const auto toggle = [this](ChromePopup popup) {
      chrome_.popup = chrome_.popup == popup ? ChromePopup::kNone : popup;
    };
    switch (action) {
      case ChromeAction::kSelectDraw:
        chrome_.tool = ChromeTool::kDraw;
        chrome_.popup = ChromePopup::kNone;
        break;
      case ChromeAction::kSelectErase:
        chrome_.tool = ChromeTool::kErase;
        chrome_.popup = ChromePopup::kNone;
        break;
      case ChromeAction::kSelectPan:
        chrome_.tool = ChromeTool::kPan;
        chrome_.popup = ChromePopup::kNone;
        break;
      case ChromeAction::kSelectColor:
        if (const auto color = chrome_color_at(point, chrome_); color.has_value()) {
          chrome_.color_index = *color;
          chrome_.tool = ChromeTool::kDraw;
          chrome_.popup = ChromePopup::kNone;
        }
        break;
      case ChromeAction::kToggleTools:
        toggle(ChromePopup::kTools);
        break;
      case ChromeAction::kToggleColors:
        toggle(ChromePopup::kColors);
        break;
      case ChromeAction::kToggleSizes:
        toggle(ChromePopup::kSizes);
        break;
      case ChromeAction::kToggleDocument:
        toggle(ChromePopup::kDocument);
        break;
      case ChromeAction::kSelectSmall:
      case ChromeAction::kSelectMedium:
      case ChromeAction::kSelectLarge:
      case ChromeAction::kSelectExtraLarge:
        chrome_.size = action == ChromeAction::kSelectSmall    ? ChromeSize::kSmall
                       : action == ChromeAction::kSelectMedium ? ChromeSize::kMedium
                       : action == ChromeAction::kSelectLarge  ? ChromeSize::kLarge
                                                               : ChromeSize::kExtraLarge;
        chrome_.popup = ChromePopup::kNone;
        break;
      case ChromeAction::kPreviousPalette:
        chrome_.palette_page = 0U;
        break;
      case ChromeAction::kNextPalette:
        chrome_.palette_page = 1U;
        break;
      case ChromeAction::kNewDrawing:
        chrome_.popup = ChromePopup::kNone;
        chrome_.confirm_new = true;
        break;
      case ChromeAction::kCancelNewDrawing:
        chrome_.confirm_new = false;
        break;
      case ChromeAction::kConfirmNewDrawing: {
        if (log_.current_revision().value == std::numeric_limits<std::uint32_t>::max()) {
          return ApplicationError::kAuthorityFull;
        }
        if (!reset_authority_blank({log_.current_revision().value + 1U})) {
          return ApplicationError::kAuthorityFull;
        }
        chrome_.confirm_new = false;
        chrome_.popup = ChromePopup::kNone;
        sync_history();
        start_rebuild();
        break;
      }
      case ChromeAction::kZoomIn:
        return set_zoom(next_zoom(navigation_.zoom()));
      case ChromeAction::kZoomOut:
        return set_zoom(previous_zoom(navigation_.zoom()));
      case ChromeAction::kUndo:
        return move_history(true);
      case ChromeAction::kRedo:
        return move_history(false);
      case ChromeAction::kNone:
      case ChromeAction::kExport:
      case ChromeAction::kExitExport:
      case ChromeAction::kSyncTime:
        break;
    }
    mark_frame_dirty();
    return ApplicationError::kNone;
  }

  ApplicationStorage storage_{};
  std::span<std::uint16_t> canvas_{};
  std::span<std::uint16_t> working_{};
  std::span<std::uint16_t> frame_{};
  std::span<std::uint16_t> live_{};
  std::span<std::uint16_t> overview_{};
  std::span<std::uint16_t> working_overview_{};
  OperationLog log_;
  std::unique_ptr<MaterializedCanvas> materialized_{};
  std::unique_ptr<TileProducer> producer_{};
  std::unique_ptr<RerenderLedger> rerender_ledger_{};
  ViewCompositionCursor composition_{};
  ViewCompositionStats last_composition_{};
  IdleRepairPlan idle_repair_{};
  std::size_t idle_repair_cursor_ = 0U;
  SettleRender settle_render_{};
  AuthorityReadView settle_authority_{};
  ViewRequest settle_view_{};
  std::size_t settle_cursor_ = 0U;
  DemoTape demo_;
  std::vector<CompactOperationSample> preview_storage_;
  ChainedOperationBuilder builder_;
  OperationBuilder preview_builder_;
  std::size_t staged_stroke_count_ = 0U;
  std::size_t staged_stroke_sample_count_ = 0U;
  ChromeStagingCache chrome_cache_;
  NavigationState navigation_{};
  ChromeState chrome_{};
  InkStream ink_{};
  Interaction interaction_ = Interaction::kIdle;
  ChromePoint gesture_start_{};
  ChromePoint last_touch_{};
  ChromePoint stroke_start_{};
  ChromePoint last_canvas_touch_{};
  NavigationPoint pan_start_origin_{};
  OperationPoint last_authority_point_{};
  AuthorityReadView rebuild_authority_{};
  ViewRequest rebuild_view_{};
  std::size_t rebuild_cursor_ = 0U;
  RebuildPhase rebuild_phase_ = RebuildPhase::kIdle;
  std::uint32_t frame_epoch_ = 0U;
  std::uint64_t canvas_epoch_ = 1U;
  std::uint64_t live_canvas_epoch_ = 0U;
  ViewRequest canvas_view_{};
  ViewRequest live_view_{};
  std::uint16_t next_gesture_ = 1U;
  OperationTool stroke_tool_ = OperationTool::kPen;
  std::uint16_t stroke_color_ = 0U;
  bool ready_ = false;
  bool frame_dirty_ = false;
  bool initial_frame_pending_ = true;
  bool rebuild_pending_ = false;
  bool maintenance_pending_ = false;
  bool settle_pending_ = false;
  bool settle_final_compose_ = false;
  bool settle_changed_ = false;
  bool stroke_has_segment_ = false;
};

Application::Application(ApplicationStorage storage) : impl_(std::make_unique<Impl>(storage)) {}

Application::~Application() = default;
Application::Application(Application&&) noexcept = default;
Application& Application::operator=(Application&&) noexcept = default;

bool Application::ready() const { return impl_ != nullptr && impl_->ready(); }

ApplicationAdvanceResult Application::advance(std::uint32_t now_us,
                                              std::span<const ApplicationEvent> events,
                                              std::size_t work_quanta) {
  if (impl_ == nullptr) {
    return {.error = ApplicationError::kInvalidStorage};
  }
  return impl_->advance(now_us, events, work_quanta);
}

ApplicationError Application::import_tdoc(std::span<const std::byte> document) {
  return impl_ == nullptr ? ApplicationError::kInvalidStorage : impl_->import_tdoc(document);
}

std::span<const std::uint16_t> Application::frame() const {
  return impl_ == nullptr ? std::span<const std::uint16_t>{} : impl_->frame();
}

ApplicationStatus Application::status() const {
  return impl_ == nullptr ? ApplicationStatus{} : impl_->status();
}

ApplicationDiagnostics Application::diagnostics() const {
  return impl_ == nullptr ? ApplicationDiagnostics{} : impl_->diagnostics();
}

}  // namespace tinydraw::vector_v2
