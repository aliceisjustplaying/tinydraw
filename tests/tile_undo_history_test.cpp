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
constexpr int kUndoTileSize = tinydraw::TileUndoHistory::kTileSize;

std::size_t index_of(int x, int y) {
  return static_cast<std::size_t>(y * tinydraw::kCanvasWidth + x);
}

struct RecordingDisplay final : tinydraw::DisplayBackend {
  std::vector<std::uint16_t> pixels = std::vector<std::uint16_t>(kPixelCount, kWhite);
  std::uint32_t pushes = 0U;

  void push_rect(int x, int y, int width, int height, const std::uint16_t* rgb565,
                 int stride = 0) override {
    const int source_stride = stride == 0 ? width : stride;
    ++pushes;
    for (int row = 0; row < height; ++row) {
      std::copy_n(rgb565 + row * source_stride, width,
                  pixels.begin() + (y + row) * tinydraw::kCanvasWidth + x);
    }
  }
};

}  // namespace

TEST_CASE("tile undo restores only captured pixels") {
  std::vector<std::uint16_t> storage(tinydraw::TileUndoHistory::kRequiredPixels);
  tinydraw::TileUndoHistory history(storage);
  std::vector<std::uint16_t> canvas(kPixelCount, kWhite);
  std::vector<std::uint16_t> visible = canvas;
  std::vector<std::uint16_t> before(static_cast<std::size_t>(kUndoTileSize * kUndoTileSize),
                                    kWhite);

  history.begin_entry();
  history.capture_tile(64, 128, kUndoTileSize, kUndoTileSize, before);
  CHECK(history.commit_entry() == 1U);
  std::fill_n(canvas.data() + index_of(64, 128), kUndoTileSize, kBlue);
  visible = canvas;

  const auto stats = history.undo(canvas, visible);

  CHECK(stats.tiles_restored == 1U);
  CHECK(stats.history_bytes_read == static_cast<std::uint32_t>(kUndoTileSize * kUndoTileSize * 2));
  CHECK(stats.canvas_bytes_written == stats.history_bytes_read);
  CHECK(canvas == visible);
  CHECK(canvas[index_of(64, 128)] == kWhite);
  CHECK_FALSE(history.can_undo());
}

TEST_CASE("tile undo submits adjacent restored tiles as one display run") {
  std::vector<std::uint16_t> storage(tinydraw::TileUndoHistory::kRequiredPixels);
  tinydraw::TileUndoHistory history(storage);
  std::vector<std::uint16_t> canvas(kPixelCount, kWhite);
  std::vector<std::uint16_t> visible = canvas;
  std::vector<std::uint16_t> before(static_cast<std::size_t>(kUndoTileSize * kUndoTileSize),
                                    kWhite);
  RecordingDisplay display;

  history.begin_entry();
  history.capture_tile(64, 128, kUndoTileSize, kUndoTileSize, before);
  history.capture_tile(96, 128, kUndoTileSize, kUndoTileSize, before);
  CHECK(history.commit_entry() == 2U);
  for (int y = 128; y < 128 + kUndoTileSize; ++y) {
    std::fill_n(canvas.begin() + static_cast<std::ptrdiff_t>(index_of(64, y)),
                2 * kUndoTileSize, kBlue);
  }
  visible = canvas;
  display.pixels = canvas;

  const auto stats = history.undo(canvas, visible, &display);

  CHECK(stats.tiles_restored == 2U);
  CHECK(stats.display_bytes ==
        static_cast<std::uint32_t>(2 * kUndoTileSize * kUndoTileSize * 2));
  CHECK(display.pushes == 1U);
  CHECK(display.pixels == canvas);
  CHECK(canvas == visible);
}

TEST_CASE("tile undo keeps ten entries and evicts the oldest") {
  std::vector<std::uint16_t> storage(tinydraw::TileUndoHistory::kRequiredPixels);
  tinydraw::TileUndoHistory history(storage);
  std::vector<std::uint16_t> canvas(kPixelCount, kWhite);
  std::vector<std::uint16_t> tile(static_cast<std::size_t>(kUndoTileSize * kUndoTileSize));

  for (std::uint16_t operation = 1U; operation <= 11U; ++operation) {
    std::fill(tile.begin(), tile.end(), canvas.front());
    history.begin_entry();
    history.capture_tile(0, 0, kUndoTileSize, kUndoTileSize, tile);
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

TEST_CASE("empty operation preserves a full undo history") {
  std::vector<std::uint16_t> storage(tinydraw::TileUndoHistory::kRequiredPixels);
  tinydraw::TileUndoHistory history(storage);
  std::vector<std::uint16_t> tile(static_cast<std::size_t>(kUndoTileSize * kUndoTileSize), kWhite);

  for (std::size_t entry = 0U; entry < tinydraw::TileUndoHistory::kMaxEntries; ++entry) {
    history.begin_entry();
    history.capture_tile(0, 0, kUndoTileSize, kUndoTileSize, tile);
    static_cast<void>(history.commit_entry());
  }
  history.begin_entry();

  CHECK(history.commit_entry() == 0U);
  CHECK(history.entry_count() == tinydraw::TileUndoHistory::kMaxEntries);
}

TEST_CASE("noncanonical tile dimensions are rejected") {
  std::vector<std::uint16_t> storage(tinydraw::TileUndoHistory::kRequiredPixels);
  tinydraw::TileUndoHistory history(storage);
  constexpr int partial_size = kUndoTileSize / 2;
  std::vector<std::uint16_t> partial_tile(static_cast<std::size_t>(partial_size * partial_size),
                                          kWhite);

  history.begin_entry();
  history.capture_tile(0, 0, partial_size, partial_size, partial_tile);

  CHECK(history.commit_entry() == 0U);
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
