#include "tinydraw/graphics/tile_undo_history.h"

#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

constexpr std::size_t kPixelCount =
    static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight);
constexpr std::uint16_t kWhite = 0xFFFFU;
constexpr std::uint16_t kBlue = 0x001FU;

std::size_t index_of(int x, int y) {
  return static_cast<std::size_t>(y * tinydraw::kCanvasWidth + x);
}

}  // namespace

TEST_CASE("tile undo restores only captured pixels") {
  std::vector<std::uint16_t> storage(tinydraw::TileUndoHistory::kRequiredPixels);
  tinydraw::TileUndoHistory history(storage);
  std::vector<std::uint16_t> canvas(kPixelCount, kWhite);
  std::vector<std::uint16_t> visible = canvas;
  std::vector<std::uint16_t> before(64U * 64U, kWhite);

  history.begin_entry();
  history.capture_tile(64, 128, 64, 64, before);
  CHECK(history.commit_entry() == 1U);
  std::fill_n(canvas.data() + index_of(64, 128), 64, kBlue);
  visible = canvas;

  const auto stats = history.undo(canvas, visible);

  CHECK(stats.tiles_restored == 1U);
  CHECK(stats.history_bytes_read == 64U * 64U * 2U);
  CHECK(stats.canvas_bytes_written == stats.history_bytes_read);
  CHECK(canvas == visible);
  CHECK(canvas[index_of(64, 128)] == kWhite);
  CHECK_FALSE(history.can_undo());
}

TEST_CASE("tile undo keeps ten entries and evicts the oldest") {
  std::vector<std::uint16_t> storage(tinydraw::TileUndoHistory::kRequiredPixels);
  tinydraw::TileUndoHistory history(storage);
  std::vector<std::uint16_t> canvas(kPixelCount, kWhite);
  std::vector<std::uint16_t> tile(64U * 64U);

  for (std::uint16_t operation = 1U; operation <= 11U; ++operation) {
    std::fill(tile.begin(), tile.end(), canvas.front());
    history.begin_entry();
    history.capture_tile(0, 0, 64, 64, tile);
    CHECK(history.commit_entry() == 1U);
    canvas.front() = operation;
  }

  CHECK(history.entry_count() == 10U);
  for (std::uint16_t expected = 10U; expected >= 1U; --expected) {
    CHECK(history.undo(canvas).tiles_restored == 1U);
    CHECK(canvas.front() == expected);
  }
  CHECK_FALSE(history.can_undo());
}

TEST_CASE("clear can be saved as one all-tile undo entry") {
  std::vector<std::uint16_t> storage(tinydraw::TileUndoHistory::kRequiredPixels);
  tinydraw::TileUndoHistory history(storage);
  std::vector<std::uint16_t> canvas(kPixelCount, kBlue);
  const auto expected = canvas;

  history.begin_entry();
  history.capture_canvas(canvas);
  CHECK(history.commit_entry() == tinydraw::TileUndoHistory::kTileCount);
  std::fill(canvas.begin(), canvas.end(), kWhite);

  const auto stats = history.undo(canvas);
  CHECK(stats.tiles_restored == tinydraw::TileUndoHistory::kTileCount);
  CHECK(stats.history_bytes_read == kPixelCount * sizeof(std::uint16_t));
  CHECK(canvas == expected);
}
