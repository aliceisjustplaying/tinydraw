#pragma once

#include <cstdint>
#include <span>

#include "tinydraw/geometry.h"
#include "tinydraw/vector_v2/chrome.h"

namespace tinydraw::vector_v2 {
class MaterializedCanvas;
class OperationLog;
class TileProducer;
}  // namespace tinydraw::vector_v2

namespace tinydraw::esp32 {

class RtcClock;
class TimeSyncController;
class VectorV2Export;
class VectorV2Presenter;
struct LivePresentationTiming;
enum class TimeSyncStatus : std::uint8_t;

[[nodiscard]] vector_v2::ChromeTimeSyncStatus chrome_time_sync_status(TimeSyncStatus status);
[[nodiscard]] bool sync_history_controls(vector_v2::ChromeState& chrome,
                                         const vector_v2::OperationLog& log);
[[nodiscard]] LivePresentationTiming present_history_controls(VectorV2Presenter& presenter,
                                                              const vector_v2::ChromeState& chrome,
                                                              std::uint32_t event_us);

class VectorV2ChromeController {
 public:
  VectorV2ChromeController(vector_v2::ChromeState& chrome, vector_v2::OperationLog& log,
                           vector_v2::MaterializedCanvas& canvas, vector_v2::TileProducer& producer,
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
                           std::span<const std::uint16_t> blank_snapshot,
#endif
                           std::span<std::uint16_t> history_scratch, VectorV2Presenter& presenter,
                           VectorV2Export& exporter, TimeSyncController& time_sync,
                           RtcClock& clock);

  [[nodiscard]] bool apply(vector_v2::ChromeAction action, Point point);

 private:
  vector_v2::ChromeState& chrome_;
  vector_v2::OperationLog& log_;
  vector_v2::MaterializedCanvas& canvas_;
  vector_v2::TileProducer& producer_;
#ifdef TINYDRAW_VECTOR_V2_GATE_HARNESS
  std::span<const std::uint16_t> blank_snapshot_;
#endif
  std::span<std::uint16_t> history_scratch_;
  VectorV2Presenter& presenter_;
  VectorV2Export& exporter_;
  TimeSyncController& time_sync_;
  RtcClock& clock_;
};

}  // namespace tinydraw::esp32
