#include "tinydraw/storage/drawing_snapshot.h"

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

constexpr std::uint16_t kWhite = 0xFFFFU;
constexpr std::uint16_t kBlue = 0x433DU;

std::size_t world_index(int x, int y) {
  return static_cast<std::size_t>(y * tinydraw::WorldCanvas::kWidth + x);
}

}  // namespace

TEST_CASE("drawing snapshot round-trips a complete world and viewport origin") {
  tinydraw::DrawingSnapshot snapshot;
  std::vector<std::uint16_t> world(tinydraw::WorldCanvas::kRequiredPixels, kWhite);
  world[world_index(0, 0)] = 0x1111U;
  world[world_index(367, 447)] = 0x2222U;
  world[world_index(tinydraw::WorldCanvas::kWidth - 1, tinydraw::WorldCanvas::kHeight - 1)] =
      0x3333U;

  snapshot.include_all();
  CHECK(snapshot.schedule({123, 234}) == tinydraw::DrawingSnapshot::kTileCount);
  CHECK(snapshot.pending_sector_count() == tinydraw::DrawingSnapshot::kSectorCount);
  CHECK(snapshot.metadata_pending());

  tinydraw::DrawingSnapshot restored_snapshot;
  std::vector<std::uint16_t> restored(world.size(), 0U);
  std::vector<std::uint16_t> sector(tinydraw::DrawingSnapshot::kSectorPixels);
  for (std::size_t index = 0; index < tinydraw::DrawingSnapshot::kSectorCount; ++index) {
    REQUIRE(snapshot.copy_sector(index, world, sector));
    REQUIRE(restored_snapshot.load_sector(index, sector, restored));
  }
  restored_snapshot.load_origin(snapshot.origin());

  CHECK(restored == world);
  CHECK(restored_snapshot.origin() == tinydraw::ViewOrigin{123, 234});
}

TEST_CASE("drawing snapshot schedules only sectors touched by a stroke") {
  tinydraw::DrawingSnapshot snapshot;
  std::vector<std::uint16_t> world(tinydraw::WorldCanvas::kRequiredPixels, kWhite);
  world[world_index(40, 40)] = kBlue;
  world[world_index(400, 400)] = kBlue;

  snapshot.include_segment({40.0F, 40.0F}, {42.0F, 42.0F}, 2.0F, {0, 0});
  CHECK(snapshot.schedule({0, 0}) == 1U);
  CHECK(snapshot.pending_sector_count() == 1U);
  CHECK(snapshot.sector_pending(12U));

  std::vector<std::uint16_t> sector(tinydraw::DrawingSnapshot::kSectorPixels);
  REQUIRE(snapshot.copy_sector(12U, world, sector));
  CHECK(snapshot.sector_matches(12U, world, sector));
  world[world_index(40, 40)] = kWhite;
  CHECK_FALSE(snapshot.sector_matches(12U, world, sector));

  snapshot.acknowledge_sector(12U);
  CHECK(snapshot.pending_sector_count() == 0U);
}

TEST_CASE("drawing snapshot marks every tile crossed by a sparse segment") {
  tinydraw::DrawingSnapshot snapshot;
  snapshot.include_segment({30.0F, 30.0F}, {70.0F, 70.0F}, 3.0F, {0, 0});
  CHECK(snapshot.schedule({0, 0}) == 9U);
}
