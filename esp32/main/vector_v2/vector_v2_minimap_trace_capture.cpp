#include "vector_v2_minimap_trace_capture.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace tinydraw::esp32 {
namespace {

std::uint16_t quantize(float value, std::uint16_t maximum) {
  const long rounded = std::lroundf(value);
  return static_cast<std::uint16_t>(
      std::clamp(rounded, 0L, static_cast<long>(maximum)));
}

const char* kind_text(vector_v2::TouchEventKind kind) {
  switch (kind) {
    case vector_v2::TouchEventKind::kDown:
      return "Down";
    case vector_v2::TouchEventKind::kMove:
      return "Move";
    case vector_v2::TouchEventKind::kUp:
      return "Up";
  }
  return "Up";
}

}  // namespace

void MinimapTraceCapture::record(std::uint32_t timestamp_us, std::uint32_t sequence,
                                 vector_v2::TouchEventKind kind, float x, float y,
                                 int zoom_percent, MinimapTraceState before,
                                 MinimapTraceState after) {
  last_activity_us_ = timestamp_us;
  if (count_ >= storage_.size()) {
    overflowed_ = true;
    return;
  }
  storage_[count_++] = {
      .timestamp_us = timestamp_us,
      .sequence = sequence,
      .x = quantize(x, 367U),
      .y = quantize(y, 447U),
      .zoom_percent = static_cast<std::uint16_t>(std::clamp(zoom_percent, 0, 65'535)),
      .before_x = static_cast<std::int16_t>(before.level_x),
      .before_y = static_cast<std::int16_t>(before.level_y),
      .after_x = static_cast<std::int16_t>(after.level_x),
      .after_y = static_cast<std::int16_t>(after.level_y),
      .before_flags = before.flags,
      .after_flags = after.flags,
      .kind = kind,
  };
}

void MinimapTraceCapture::include_append_us(std::uint32_t duration_us) {
  append_total_us_ += duration_us;
  append_max_us_ = std::max(append_max_us_, duration_us);
}

void MinimapTraceCapture::dump_and_reset() {
  std::printf(
      "TINYDRAW_MINIMAP_CAPTURE_BEGIN events=%u overflow=%u append_total_us=%llu "
      "append_max_us=%u\n",
      static_cast<unsigned>(count_), overflowed_ ? 1U : 0U,
      static_cast<unsigned long long>(append_total_us_), static_cast<unsigned>(append_max_us_));
  std::printf(
      "t_us,sequence,kind,x,y,zoom,before_x,before_y,after_x,after_y,before_flags,after_flags\n");
  std::uint64_t relative_us = 0;
  for (std::size_t index = 0; index < count_; ++index) {
    const MinimapTraceRecord& record = storage_[index];
    if (index != 0U) {
      relative_us += record.timestamp_us - storage_[index - 1U].timestamp_us;
    }
    std::printf("%llu,%lu,%s,%u,%u,%u,%d,%d,%d,%d,0x%02x,0x%02x\n",
                static_cast<unsigned long long>(relative_us),
                static_cast<unsigned long>(record.sequence), kind_text(record.kind),
                static_cast<unsigned>(record.x), static_cast<unsigned>(record.y),
                static_cast<unsigned>(record.zoom_percent), static_cast<int>(record.before_x),
                static_cast<int>(record.before_y), static_cast<int>(record.after_x),
                static_cast<int>(record.after_y), static_cast<unsigned>(record.before_flags),
                static_cast<unsigned>(record.after_flags));
    if ((index & 0x7FU) == 0x7FU) {
      std::fflush(stdout);
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  std::printf("TINYDRAW_MINIMAP_CAPTURE_END events=%u\n", static_cast<unsigned>(count_));
  std::fflush(stdout);

  count_ = 0;
  append_total_us_ = 0;
  append_max_us_ = 0;
  overflowed_ = false;
}

}  // namespace tinydraw::esp32
