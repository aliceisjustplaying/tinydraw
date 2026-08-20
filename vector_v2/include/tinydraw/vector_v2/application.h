#ifndef TINYDRAW_VECTOR_V2_APPLICATION_H
#define TINYDRAW_VECTOR_V2_APPLICATION_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "tinydraw/vector_v2/chrome.h"
#include "tinydraw/vector_v2/demo_tape.h"
#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/navigation_state.h"
#include "tinydraw/vector_v2/operation.h"
#include "tinydraw/vector_v2/rerender_ledger.h"
#include "tinydraw/vector_v2/tile_producer.h"

namespace tinydraw::vector_v2 {

inline constexpr std::size_t kApplicationStrokeChunkSampleLimit = 32U;

// Chunks after the first retain one boundary sample so their raster geometry
// remains continuous. These helpers size a provisional transaction for a
// maximum number of logical Down/Move/Up points without release-time loss.
[[nodiscard]] constexpr std::size_t application_staged_sample_capacity(
    std::size_t logical_point_capacity) {
  return logical_point_capacity +
         (logical_point_capacity >= 2U
              ? (logical_point_capacity - 2U) / (kApplicationStrokeChunkSampleLimit - 1U)
              : 0U);
}

[[nodiscard]] constexpr std::size_t application_staged_append_capacity(
    std::size_t logical_point_capacity) {
  return logical_point_capacity == 0U
             ? 0U
             : 1U + (logical_point_capacity >= 2U
                         ? (logical_point_capacity - 2U) / (kApplicationStrokeChunkSampleLimit - 1U)
                         : 0U);
}

// Caller-owned, fixed-capacity storage for one complete application. The six
// panel/overview buffers contain exactly kOverviewPixels entries; chrome and
// production spans use their subsystem-specific constants. Authority capacity
// is selected by the records/samples spans. No allocation is performed while
// the application advances.
struct ApplicationStorage {
  std::span<OperationRecord> records{};
  std::span<CompactOperationSample> samples{};
  std::span<CompactOperationSample> stroke_samples{};
  // Provisional Stroke transaction storage. Chunks remain outside document
  // authority until TouchUp publishes the complete logical Stroke. Size these
  // with application_staged_*_capacity() for the host's logical point limit.
  std::span<CompactOperationSample> staged_stroke_samples{};
  std::span<OperationAppend> staged_stroke_appends{};
  // Optional fixed-capacity transaction workspace for import_tdoc(). It must
  // be disjoint from authority and all other Application storage.
  std::span<OperationRecord> import_records{};
  std::span<CompactOperationSample> import_samples{};
  // Optional. An empty span disables demo recording and replay.
  std::span<DemoSample> demo_samples{};
  std::span<std::uint16_t> canvas_pixels{};
  std::span<std::uint16_t> working_pixels{};
  std::span<std::uint16_t> frame_pixels{};
  std::span<std::uint16_t> live_pixels{};
  std::span<std::uint16_t> overview_pixels{};
  std::span<std::uint16_t> working_overview_pixels{};
  std::span<std::uint16_t> chrome_cache_pixels{};
  // Optional production view-cache backing. Supplying the complete group
  // makes Application rebuild views through MaterializedCanvas and
  // TileProducer. An entirely empty group retains the compact replay backend.
  std::span<MaterializedUniformStorage> materialized_uniforms{};
  std::span<std::uint8_t> materialized_occupancy{};
  std::span<MaterializedSlotStorage> materialized_slots{};
  std::span<std::uint16_t> materialized_tile_pixels{};
  std::span<std::uint16_t> materialized_raw_slot_directory{};
  std::span<std::uint16_t> producer_supertask_pixels{};
  std::span<std::uint8_t> producer_finalized_pixels{};
  std::span<std::uint16_t> producer_summary_rows{};
  std::span<std::uint32_t> producer_summary_words{};
  std::span<std::byte> producer_chord_plans{};
  std::span<std::uint16_t> producer_candidate_indices{};
  // Settled analytic-coverage AA scratch. This group is required with the
  // production view cache. Candidate storage is shared with TileProducer;
  // production, repair, and settle phases are serialized by Application.
  std::span<std::uint8_t> settle_operation_alpha{};
  std::span<std::uint8_t> settle_accumulated_alpha{};
  std::span<std::uint16_t> settle_red{};
  std::span<std::uint16_t> settle_green{};
  std::span<std::uint16_t> settle_blue{};
  std::span<std::uint16_t> settle_pixels{};
  // Optional diagnostic ledger. Empty disables cause attribution.
  std::span<RerenderLedgerEntry> rerender_ledger_entries{};
};

enum class ApplicationEventKind : std::uint8_t {
  kTouchDown,
  kTouchMove,
  kTouchUp,
  kZoomNext,
  kZoomPrevious,
  // Semantic long-press verdict from a host button. This controls the demo
  // state machine and is never recorded into the demo tape.
  kDemoLongPress,
};

// Coordinates are panel-space pixels. Touch timestamps are the application's
// only stroke clock and use ordinary uint32_t rollover arithmetic. Hosts that
// receive untimestamped input stamp it with the next advance() now_us value.
struct ApplicationEvent {
  ApplicationEventKind kind = ApplicationEventKind::kTouchUp;
  float x = 0.0F;
  float y = 0.0F;
  std::uint32_t timestamp_us = 0;
};

enum class ApplicationError : std::uint8_t {
  kNone,
  kInvalidStorage,
  kInvalidEvent,
  kAuthorityFull,
  kRenderFailed,
  kMalformedDocument,
  kImportCapacity,
};

struct ApplicationAdvanceResult {
  ApplicationError error = ApplicationError::kNone;
  PixelRect damage{};
  std::uint32_t frame_epoch = 0;
  bool frame_changed = false;
  bool quiescent = false;
  bool wants_immediate_advance = false;
};

enum class ApplicationDemoMode : std::uint8_t {
  kUnavailable,
  kEmpty,
  kRecording,
  kReady,
  kReplaying,
};

struct ApplicationStatus {
  ChromeState chrome{};
  ZoomLevel zoom = ZoomLevel::k25Percent;
  NavigationPoint origin{};
  DocumentRevision revision{};
  std::size_t operation_count = 0;
  std::size_t retained_operation_count = 0;
  std::size_t sample_count = 0;
  // Stable over authority resets and replay clocks. Intended for exact host
  // verification of the active painter-ordered document.
  std::uint64_t authority_fingerprint = 0;
  ApplicationDemoMode demo_mode = ApplicationDemoMode::kUnavailable;
  std::size_t demo_sample_count = 0;
  bool demo_overflowed = false;
  std::uint32_t frame_epoch = 0;
  bool stroke_active = false;
  bool background_pending = false;
};

// Read-only receipts for deterministic host batteries. Counts describe the
// current materialized revision and never advance background work.
struct ApplicationDiagnostics {
  bool production_enabled = false;
  bool maintenance_pending = false;
  std::size_t slot_capacity = 0;
  std::size_t resident_raw_tiles = 0;
  std::size_t visible_tiles_remaining = 0;
  std::size_t recent_view_count = 0;
  ViewCompositionStats last_composition{};
  std::size_t current_zoom_raw = 0;
  std::size_t current_zoom_uniform = 0;
  std::size_t current_zoom_fallback = 0;
  std::size_t zoom100_raw = 0;
  std::size_t zoom100_uniform = 0;
  std::size_t zoom100_fallback = 0;
  RerenderLedgerTotals rerender{};
};

// Host-neutral Vector V2 product brain. Input, document authority, navigation,
// chrome, rendering order, and bounded background convergence live behind this
// interface. advance() is serialized and non-reentrant. One work quantum
// advances a bounded authority-replay, composition, or tile-production slice;
// zero quanta still processes foreground input.
class Application {
 public:
  explicit Application(ApplicationStorage storage);
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;
  Application(Application&&) noexcept;
  Application& operator=(Application&&) noexcept;

  [[nodiscard]] bool ready() const;
  [[nodiscard]] ApplicationAdvanceResult advance(std::uint32_t now_us,
                                                 std::span<const ApplicationEvent> events,
                                                 std::size_t work_quanta);
  // Atomically imports the compact little-endian TDOC authority format. On
  // success the document is active, session/demo state returns to defaults,
  // and bounded rendering begins. Any error preserves all observable state.
  [[nodiscard]] ApplicationError import_tdoc(std::span<const std::byte> document);
  // Complete 368x448 RGB565 framebuffer in host byte order. The span remains
  // stable for the Application lifetime and is immutable until advance().
  [[nodiscard]] std::span<const std::uint16_t> frame() const;
  [[nodiscard]] ApplicationStatus status() const;
  [[nodiscard]] ApplicationDiagnostics diagnostics() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace tinydraw::vector_v2

#endif  // TINYDRAW_VECTOR_V2_APPLICATION_H
