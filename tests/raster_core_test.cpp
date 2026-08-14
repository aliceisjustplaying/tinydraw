#include "tinydraw/app/raster_core.h"

#include <doctest.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;

struct RecordingDisplay final : tinydraw::DisplayBackend {
  struct Push {
    int x;
    int y;
    int width;
    int height;
    friend bool operator==(const Push&, const Push&) = default;
  };

  std::vector<Push> pushes;

  void push_rect(int x, int y, int width, int height, const std::uint16_t*, int = 0) override {
    pushes.push_back({x, y, width, height});
  }
};

struct RasterFixture {
  std::vector<std::uint16_t> committed =
      std::vector<std::uint16_t>(tinydraw::RasterCore::kPixelCount, kBackground);
  std::vector<std::uint16_t> visible = committed;
  std::vector<std::uint8_t> coverage =
      std::vector<std::uint8_t>(tinydraw::RasterCore::kPixelCount, 0U);
  std::vector<std::uint16_t> undo =
      std::vector<std::uint16_t>(tinydraw::TileUndoHistory::kRequiredPixels);
  std::vector<std::uint16_t> world =
      std::vector<std::uint16_t>(tinydraw::WorldCanvas::kRequiredPixels);
  RecordingDisplay display;
  tinydraw::RasterCore app{{committed, visible, coverage, undo, world}, display};
};

void report(RasterFixture& fixture, bool down, float x, float y, std::uint64_t time_us) {
  fixture.app.touch(down, {x, y}, time_us);
}

void tap(RasterFixture& fixture, float x, float y, std::uint64_t time_us) {
  report(fixture, true, x, y, time_us);
  report(fixture, false, x, y, time_us + 8'000U);
}

std::uint32_t hash(std::span<const std::uint16_t> pixels) {
  std::uint32_t value = 2166136261U;
  for (std::uint16_t pixel : pixels) {
    value = (value ^ static_cast<std::uint8_t>(pixel)) * 16777619U;
    value = (value ^ static_cast<std::uint8_t>(pixel >> 8U)) * 16777619U;
  }
  return value;
}

struct PinnedTrace {
  struct Event {
    std::uint64_t time_ms;
    bool down;
    int x;
    int y;
  };

  std::vector<Event> events;
  std::uint32_t framebuffer_hash = 0U;
  std::vector<std::vector<RecordingDisplay::Push>> frames;
};

[[noreturn]] void malformed_trace(const std::string& detail) {
  throw std::runtime_error("malformed pinned Puck trace: " + detail);
}

std::uint64_t read_decimal(std::istream& input, const std::string& label, std::uint64_t maximum) {
  std::string text;
  if (!(input >> text) || text.empty() || (text.size() > 1U && text.front() == '0') ||
      !std::all_of(text.begin(), text.end(),
                   [](char value) { return value >= '0' && value <= '9'; })) {
    malformed_trace("invalid " + label);
  }
  std::uint64_t value = 0U;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
  if (error != std::errc{} || end != text.data() + text.size() || value > maximum) {
    malformed_trace(label + " is out of range");
  }
  return value;
}

PinnedTrace load_pinned_trace() {
  const std::string path = std::string(TINYDRAW_SOURCE_DIR) + "/testdata/puck/pinned_trace.txt";
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("missing pinned Puck trace: " + path);
  }

  std::string token;
  if (!(input >> token) || token != "tinydraw-puck-trace-v1") {
    malformed_trace("missing version header");
  }

  constexpr std::uint64_t kJavaScriptMaxSafeInteger = (1ULL << 53U) - 1U;
  constexpr std::uint64_t kIntMaximum = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
  PinnedTrace trace;
  while (input >> token && token == "event") {
    PinnedTrace::Event event{};
    event.time_ms = read_decimal(input, "event time", kJavaScriptMaxSafeInteger);
    const std::uint64_t down = read_decimal(input, "touch level", 1U);
    event.x = static_cast<int>(read_decimal(input, "event x", kIntMaximum));
    event.y = static_cast<int>(read_decimal(input, "event y", kIntMaximum));
    event.down = down != 0U;
    trace.events.push_back(event);
  }
  if (trace.events.empty() || token != "hash") {
    malformed_trace("events must be followed by hash");
  }

  std::string hash_text;
  if (!(input >> hash_text) || hash_text.size() != 8U ||
      !std::all_of(hash_text.begin(), hash_text.end(), [](char value) {
        return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
      })) {
    malformed_trace("invalid framebuffer hash");
  }
  const auto [hash_end, hash_error] = std::from_chars(
      hash_text.data(), hash_text.data() + hash_text.size(), trace.framebuffer_hash, 16);
  if (hash_error != std::errc{} || hash_end != hash_text.data() + hash_text.size()) {
    malformed_trace("invalid framebuffer hash");
  }

  if (!(input >> token) || token != "frames") {
    malformed_trace("missing frame count");
  }
  const auto frame_count =
      static_cast<std::size_t>(read_decimal(input, "frame count", kJavaScriptMaxSafeInteger));
  if (frame_count != trace.events.size()) {
    malformed_trace("frame count must equal event count");
  }
  trace.frames.resize(frame_count);
  for (std::size_t frame = 0; frame < frame_count; ++frame) {
    if (!(input >> token) || token != "frame") {
      malformed_trace("invalid frame header");
    }
    const auto frame_number = read_decimal(input, "frame number", kJavaScriptMaxSafeInteger);
    const auto rect_count = read_decimal(input, "rect count", kJavaScriptMaxSafeInteger);
    if (frame_number != frame + 1U) {
      malformed_trace("invalid frame number");
    }
    for (std::uint64_t rect = 0; rect < rect_count; ++rect) {
      if (!(input >> token) || token != "rect") {
        malformed_trace("invalid rect");
      }
      RecordingDisplay::Push push{};
      push.x = static_cast<int>(read_decimal(input, "rect x", kIntMaximum));
      push.y = static_cast<int>(read_decimal(input, "rect y", kIntMaximum));
      push.width = static_cast<int>(read_decimal(input, "rect width", kIntMaximum));
      push.height = static_cast<int>(read_decimal(input, "rect height", kIntMaximum));
      trace.frames[frame].push_back(push);
    }
  }
  if (!(input >> token) || token != "end" || (input >> token)) {
    malformed_trace("missing end marker or trailing content");
  }
  return trace;
}

std::vector<std::vector<RecordingDisplay::Push>> draw_trace(RasterFixture& fixture,
                                                            const PinnedTrace& trace) {
  std::vector<std::vector<RecordingDisplay::Push>> frames;
  for (const auto& event : trace.events) {
    fixture.display.pushes.clear();
    const std::uint64_t now_us = event.time_ms * 1'000U;
    report(fixture, event.down, static_cast<float>(event.x), static_cast<float>(event.y), now_us);
    fixture.app.tick(now_us);
    frames.push_back(fixture.display.pushes);
  }
  return frames;
}

}  // namespace

TEST_CASE("shared Raster core deterministically draws partial-refresh strokes") {
  const PinnedTrace trace = load_pinned_trace();
  RasterFixture first;
  RasterFixture second;
  REQUIRE(first.app.ready());
  REQUIRE(second.app.ready());

  const auto first_frames = draw_trace(first, trace);
  const auto second_frames = draw_trace(second, trace);

  CHECK(hash(first.app.framebuffer()) == hash(second.app.framebuffer()));
  CHECK(hash(first.app.framebuffer()) == trace.framebuffer_hash);
  CHECK(first_frames == second_frames);
  CHECK(first_frames == trace.frames);
  CHECK(std::any_of(first.app.framebuffer().begin(), first.app.framebuffer().begin() + 300 * 368,
                    [](std::uint16_t pixel) { return pixel != kBackground; }));
  CHECK(std::any_of(first_frames.begin(), first_frames.end(), [](const auto& frame) {
    return std::any_of(frame.begin(), frame.end(), [](const auto& push) {
      return push.width < tinydraw::kCanvasWidth || push.height < tinydraw::kCanvasHeight;
    });
  }));
  CHECK(std::all_of(first_frames.begin(), first_frames.end(), [](const auto& frame) {
    return std::all_of(frame.begin(), frame.end(), [](const auto& push) {
      return push.x >= 0 && push.y >= 0 && push.width > 0 && push.height > 0 &&
             push.x + push.width <= tinydraw::kCanvasWidth &&
             push.y + push.height <= tinydraw::kCanvasHeight;
    });
  }));
}

TEST_CASE("shared Raster core owns toolbar size pan undo and new reducers") {
  RasterFixture fixture;
  REQUIRE(fixture.app.ready());

  static_cast<void>(draw_trace(fixture, load_pinned_trace()));
  const std::uint32_t drawn = hash(fixture.app.framebuffer());

  tap(fixture, 272.0F, 401.0F, 40'000U);
  tap(fixture, 316.0F, 331.0F, 56'000U);
  CHECK(fixture.app.toolbar().size == tinydraw::PenSize::kExtraLarge);

  tap(fixture, 37.0F, 401.0F, 72'000U);
  CHECK(hash(fixture.app.framebuffer()) != drawn);
  CHECK(std::all_of(fixture.app.framebuffer().begin(),
                    fixture.app.framebuffer().begin() + 300 * tinydraw::kCanvasWidth,
                    [](std::uint16_t pixel) { return pixel == kBackground; }));
  CHECK_FALSE(fixture.app.toolbar().can_undo);

  tap(fixture, 213.0F, 401.0F, 88'000U);
  tap(fixture, 316.0F, 339.0F, 104'000U);
  CHECK(fixture.app.toolbar().color == tinydraw::InkColor::kRed);
  tap(fixture, 155.0F, 401.0F, 120'000U);
  CHECK(fixture.app.toolbar().tool == tinydraw::DrawingTool::kEraser);

  tap(fixture, 96.0F, 401.0F, 136'000U);
  tap(fixture, 272.0F, 331.0F, 152'000U);
  CHECK(fixture.app.toolbar().tool == tinydraw::DrawingTool::kPan);
  const auto origin = fixture.app.origin();
  report(fixture, true, 180.0F, 180.0F, 168'000U);
  report(fixture, true, 140.0F, 160.0F, 176'000U);
  report(fixture, false, 140.0F, 160.0F, 184'000U);
  CHECK(fixture.app.origin() != origin);

  tap(fixture, 331.0F, 401.0F, 200'000U);
  CHECK(fixture.app.toolbar().confirm_new);
  report(fixture, true, 260.0F, 230.0F, 216'000U);
  CHECK_FALSE(fixture.app.toolbar().confirm_new);
  CHECK(fixture.app.toolbar().can_undo);
  report(fixture, false, 260.0F, 230.0F, 224'000U);
}

TEST_CASE("shared Raster core restores drawing pixels after New and Undo") {
  RasterFixture fixture;
  REQUIRE(fixture.app.ready());
  static_cast<void>(draw_trace(fixture, load_pinned_trace()));
  constexpr std::size_t drawing_pixels =
      static_cast<std::size_t>(tinydraw::kMainToolbarOverlayTop * tinydraw::kCanvasWidth);
  const std::vector<std::uint16_t> drawn(fixture.app.framebuffer().begin(),
                                         fixture.app.framebuffer().begin() + drawing_pixels);

  tap(fixture, 331.0F, 401.0F, 40'000U);
  report(fixture, true, 260.0F, 230.0F, 56'000U);
  report(fixture, false, 260.0F, 230.0F, 64'000U);
  CHECK(std::all_of(fixture.app.framebuffer().begin(),
                    fixture.app.framebuffer().begin() + drawing_pixels,
                    [](std::uint16_t pixel) { return pixel == kBackground; }));

  tap(fixture, 37.0F, 401.0F, 72'000U);
  CHECK(std::equal(drawn.begin(), drawn.end(), fixture.app.framebuffer().begin()));
}

TEST_CASE("shared Raster core rejects undersized caller storage") {
  for (int undersized = 0; undersized < 5; ++undersized) {
    std::vector<std::uint16_t> committed(tinydraw::RasterCore::kPixelCount, kBackground);
    std::vector<std::uint16_t> visible = committed;
    std::vector<std::uint8_t> coverage(tinydraw::RasterCore::kPixelCount, 0U);
    std::vector<std::uint16_t> undo(tinydraw::TileUndoHistory::kRequiredPixels);
    std::vector<std::uint16_t> world(tinydraw::WorldCanvas::kRequiredPixels);
    switch (undersized) {
      case 0:
        committed.pop_back();
        break;
      case 1:
        visible.pop_back();
        break;
      case 2:
        coverage.pop_back();
        break;
      case 3:
        undo.pop_back();
        break;
      case 4:
        world.pop_back();
        break;
    }
    RecordingDisplay display;
    tinydraw::RasterCore app{{committed, visible, coverage, undo, world}, display};
    CHECK_FALSE(app.ready());
    CHECK(display.pushes.empty());
  }
}
