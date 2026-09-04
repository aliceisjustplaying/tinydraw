#ifndef TINYDRAW_ESP32_VECTOR_V2_FRAME_TRACE_H
#define TINYDRAW_ESP32_VECTOR_V2_FRAME_TRACE_H

#include <cstdint>

#include "vector_v2_presenter.h"

#ifdef TINYDRAW_FRAME_TRACE
#include "vector_v2_demo_controller.h"
#endif

namespace tinydraw::esp32 {

#ifdef TINYDRAW_FRAME_TRACE
class FrameTraceScope {
 public:
  FrameTraceScope(const char* kind, std::uint32_t event_sequence, std::uint32_t event_us);
  void finish(const LivePresentationTiming& timing);

 private:
  const char* kind_ = nullptr;
  std::uint64_t started_us_ = 0;
  std::uint32_t event_sequence_ = 0;
  std::uint32_t event_us_ = 0;
  std::uint32_t started_ccount_ = 0;
  bool finished_ = false;
};

[[nodiscard]] bool start_frame_trace();
[[nodiscard]] bool load_frame_trace_workload(VectorV2DemoController& demo);
void trace_input(std::uint32_t event_sequence, const char* kind, float x, float y,
                 std::uint32_t event_us, bool replay);
#else
class FrameTraceScope {
 public:
  FrameTraceScope(const char*, std::uint32_t, std::uint32_t) {}
  void finish(const LivePresentationTiming&) {}
};

[[nodiscard]] inline bool start_frame_trace() { return true; }
inline void trace_input(std::uint32_t, const char*, float, float, std::uint32_t, bool) {}
#endif

}  // namespace tinydraw::esp32

#endif  // TINYDRAW_ESP32_VECTOR_V2_FRAME_TRACE_H
