#include "tinydraw/graphics/world_canvas.h"

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

constexpr std::uint16_t kWhite = 0xFFFFU;
constexpr std::uint16_t kInk = 0x433DU;
constexpr std::size_t screen_index(int x, int y) {
  return static_cast<std::size_t>(y * tinydraw::kCanvasWidth + x);
}

constexpr std::size_t world_index(int x, int y) {
  return static_cast<std::size_t>(y * tinydraw::WorldCanvas::kWidth + x);
}

}  // namespace

TEST_CASE("a captured viewport can be shown from another world origin") {
  std::vector<std::uint16_t> storage(tinydraw::WorldCanvas::kRequiredPixels);
  tinydraw::WorldCanvas world(storage);
  std::vector<std::uint16_t> committed(tinydraw::WorldCanvas::kViewportPixels, kWhite);
  std::vector<std::uint16_t> visible(tinydraw::WorldCanvas::kViewportPixels, 0U);

  const auto initial = world.origin();
  CHECK(initial.x == tinydraw::kCanvasWidth / 2);
  CHECK(initial.y == tinydraw::kCanvasHeight / 2);
  committed[screen_index(100, 100)] = kInk;
  CHECK(world.capture(committed));

  CHECK(world.show({initial.x + 50, initial.y + 25}, committed, visible));
  CHECK(world.origin() == tinydraw::ViewOrigin{initial.x + 50, initial.y + 25});
  CHECK(committed[screen_index(50, 75)] == kInk);
  CHECK(visible == committed);
}

TEST_CASE("moving the world origin does not copy a viewport") {
  std::vector<std::uint16_t> storage(tinydraw::WorldCanvas::kRequiredPixels);
  tinydraw::WorldCanvas world(storage);
  std::vector<std::uint16_t> viewport(tinydraw::WorldCanvas::kViewportPixels, kInk);

  const auto initial = world.origin();
  CHECK(world.move_to({initial.x + 30, initial.y + 40}));
  CHECK(world.origin() == tinydraw::ViewOrigin{initial.x + 30, initial.y + 40});
  CHECK(viewport[screen_index(10, 10)] == kInk);
  CHECK_FALSE(world.move_to(world.origin()));
}

TEST_CASE("a persisted world replaces pixels and restores its viewport") {
  std::vector<std::uint16_t> storage(tinydraw::WorldCanvas::kRequiredPixels);
  tinydraw::WorldCanvas world(storage);
  std::vector<std::uint16_t> persisted(tinydraw::WorldCanvas::kRequiredPixels, kWhite);
  std::vector<std::uint16_t> committed(tinydraw::WorldCanvas::kViewportPixels, 0U);
  std::vector<std::uint16_t> visible(tinydraw::WorldCanvas::kViewportPixels, 0U);
  persisted[world_index(130, 240)] = kInk;

  CHECK(world.replace(persisted, {100, 200}, committed, visible));
  CHECK(world.origin() == tinydraw::ViewOrigin{100, 200});
  CHECK(committed[screen_index(30, 40)] == kInk);
  CHECK(visible == committed);
}

TEST_CASE("world origins clamp to the pannable area and clear back to center") {
  std::vector<std::uint16_t> storage(tinydraw::WorldCanvas::kRequiredPixels);
  tinydraw::WorldCanvas world(storage);
  std::vector<std::uint16_t> committed(tinydraw::WorldCanvas::kViewportPixels, 0U);

  CHECK(world.show({-100, 10'000}, committed));
  CHECK(world.origin() ==
        tinydraw::ViewOrigin{0, tinydraw::WorldCanvas::kHeight - tinydraw::kCanvasHeight});
  CHECK_FALSE(world.show(world.origin(), committed));

  committed[screen_index(20, 20)] = kInk;
  CHECK(world.capture(committed));
  CHECK(world.clear(committed));
  CHECK(world.origin() ==
        tinydraw::ViewOrigin{tinydraw::kCanvasWidth / 2, tinydraw::kCanvasHeight / 2});
  CHECK(committed[screen_index(20, 20)] == kWhite);
}
