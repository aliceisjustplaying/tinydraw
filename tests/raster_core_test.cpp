#include "tinydraw/app/raster_core.h"

#include <doctest.h>

#include <algorithm>
#include <cstdint>
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

void report(RasterFixture& fixture, bool down, float x, float y, std::uint32_t time_us) {
  fixture.app.touch(down, {x, y}, time_us);
}

void tap(RasterFixture& fixture, float x, float y, std::uint32_t time_us) {
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

void draw_trace(RasterFixture& fixture) {
  report(fixture, true, 60.0F, 80.0F, 1'000U);
  report(fixture, true, 100.0F, 100.0F, 9'000U);
  report(fixture, true, 150.0F, 130.0F, 17'000U);
  report(fixture, false, 150.0F, 130.0F, 25'000U);
}

}  // namespace

TEST_CASE("shared Raster core deterministically draws partial-refresh strokes") {
  RasterFixture first;
  RasterFixture second;
  REQUIRE(first.app.ready());
  REQUIRE(second.app.ready());
  first.display.pushes.clear();
  second.display.pushes.clear();

  draw_trace(first);
  draw_trace(second);

  CHECK(hash(first.app.framebuffer()) == hash(second.app.framebuffer()));
  CHECK(hash(first.app.framebuffer()) == 0xf5122b6bU);
  CHECK(first.display.pushes == second.display.pushes);
  const std::vector<RecordingDisplay::Push> expected_pushes{
      {56, 76, 10, 10}, {56, 76, 38, 24}, {56, 76, 80, 48}, {106, 102, 50, 34}, {0, 0, 368, 448},
  };
  CHECK(first.display.pushes == expected_pushes);
  CHECK(std::any_of(first.app.framebuffer().begin(), first.app.framebuffer().begin() + 300 * 368,
                    [](std::uint16_t pixel) { return pixel != kBackground; }));
  REQUIRE_FALSE(first.display.pushes.empty());
  CHECK(std::any_of(first.display.pushes.begin(), first.display.pushes.end(), [](const auto& push) {
    return push.width < tinydraw::kCanvasWidth || push.height < tinydraw::kCanvasHeight;
  }));
  CHECK(std::all_of(first.display.pushes.begin(), first.display.pushes.end(), [](const auto& push) {
    return push.x >= 0 && push.y >= 0 && push.width > 0 && push.height > 0 &&
           push.x + push.width <= tinydraw::kCanvasWidth &&
           push.y + push.height <= tinydraw::kCanvasHeight;
  }));
}

TEST_CASE("shared Raster core owns toolbar size pan undo and new reducers") {
  RasterFixture fixture;
  REQUIRE(fixture.app.ready());

  draw_trace(fixture);
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
  draw_trace(fixture);
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
