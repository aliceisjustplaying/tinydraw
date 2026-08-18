#include "tinydraw/vector_v2/world_export.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "tinydraw/export/png_encoder.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

struct ExportFixture {
  std::array<vector_v2::OperationRecord, 16> records{};
  std::array<vector_v2::CompactOperationSample, 256> samples{};
  vector_v2::OperationLog log{records, samples};

  void append_document() {
    // Pen strokes across band boundaries, a tapered stroke, and an eraser,
    // including geometry at the world edges.
    const std::array first{
        vector_v2::CompactOperationSample{.x_quarter = 32, .y_quarter = 32, .radius_256 = 1'024},
        vector_v2::CompactOperationSample{
            .x_quarter = 23200, .y_quarter = 28400, .radius_256 = 1'024, .elapsed_ms = 8},
    };
    const std::array second{
        vector_v2::CompactOperationSample{.x_quarter = 8000, .y_quarter = 400, .radius_256 = 2'048},
        vector_v2::CompactOperationSample{
            .x_quarter = 8400, .y_quarter = 14000, .radius_256 = 384, .elapsed_ms = 8},
        vector_v2::CompactOperationSample{
            .x_quarter = 3600, .y_quarter = 27600, .radius_256 = 2'400, .elapsed_ms = 16},
    };
    const std::array erased{
        vector_v2::CompactOperationSample{
            .x_quarter = 7200, .y_quarter = 7200, .radius_256 = 1'600},
        vector_v2::CompactOperationSample{
            .x_quarter = 12000, .y_quarter = 10400, .radius_256 = 1'600, .elapsed_ms = 8},
    };
    const std::array dot{
        vector_v2::CompactOperationSample{.x_quarter = 23520, .y_quarter = 80, .radius_256 = 900},
    };
    REQUIRE(log.append({.tool = vector_v2::OperationTool::kPen, .color = 0xF800U, .samples = first})
                .has_value());
    REQUIRE(
        log.append({.tool = vector_v2::OperationTool::kPen, .color = 0x001FU, .samples = second})
            .has_value());
    REQUIRE(
        log.append({.tool = vector_v2::OperationTool::kEraser, .color = 0xFFFFU, .samples = erased})
            .has_value());
    REQUIRE(log.append({.tool = vector_v2::OperationTool::kPen, .color = 0x07E0U, .samples = dot})
                .has_value());
  }
};

class MemoryPngOutput final : public tinydraw::PngOutput {
 public:
  bool write(std::size_t offset, std::span<const std::uint8_t> input) override {
    if (offset > bytes.max_size() - input.size()) {
      return false;
    }
    if (offset + input.size() > bytes.size()) {
      bytes.resize(offset + input.size());
    }
    std::copy(input.begin(), input.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
  }

  bool read(std::size_t offset, std::span<std::uint8_t> output) override {
    if (offset > bytes.size() || output.size() > bytes.size() - offset) {
      return false;
    }
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), output.size(), output.begin());
    return true;
  }

  std::vector<std::uint8_t> bytes;
};

struct SettledWorkspaceStorage {
  std::vector<std::uint8_t> operation_alpha = std::vector<std::uint8_t>(vector_v2::kTilePixels);
  std::vector<std::uint8_t> accumulated_alpha = std::vector<std::uint8_t>(vector_v2::kTilePixels);
  std::vector<std::uint16_t> red = std::vector<std::uint16_t>(vector_v2::kTilePixels);
  std::vector<std::uint16_t> green = std::vector<std::uint16_t>(vector_v2::kTilePixels);
  std::vector<std::uint16_t> blue = std::vector<std::uint16_t>(vector_v2::kTilePixels);

  [[nodiscard]] vector_v2::SettledTileWorkspace workspace() {
    return {.operation_alpha = operation_alpha,
            .accumulated_alpha = accumulated_alpha,
            .red = red,
            .green = green,
            .blue = blue};
  }
};

}  // namespace

TEST_CASE("settled world band renderer stitches the production AA windows exactly") {
  ExportFixture fixture;
  fixture.append_document();
  constexpr int kBandRows = 17;
  constexpr int kFirstRow = 31;
  std::vector<std::uint16_t> band(static_cast<std::size_t>(vector_v2::kWorldWidth) * kBandRows);
  std::vector<std::uint16_t> window(vector_v2::kTilePixels);
  SettledWorkspaceStorage storage;
  vector_v2::SettledWorldBandRenderer renderer(fixture.log, band, window, storage.workspace());
  REQUIRE(renderer.ready());
  CHECK(renderer.band_rows() == kBandRows);

  std::vector<std::uint16_t> expected(band.size());
  for (int first_column = 0; first_column < vector_v2::kWorldWidth;
       first_column += vector_v2::kTileWidth) {
    const int width = std::min(vector_v2::kTileWidth, vector_v2::kWorldWidth - first_column);
    const auto rendered = std::span(window).first(static_cast<std::size_t>(width) * kBandRows);
    REQUIRE(vector_v2::render_settled_window(
        fixture.log, vector_v2::ZoomLevel::k100Percent,
        {first_column, kFirstRow, first_column + width, kFirstRow + kBandRows}, storage.workspace(),
        rendered));
    for (int row = 0; row < kBandRows; ++row) {
      std::copy_n(rendered.begin() + static_cast<std::ptrdiff_t>(row * width), width,
                  expected.begin() +
                      static_cast<std::ptrdiff_t>(row * vector_v2::kWorldWidth + first_column));
    }
  }

  std::vector<std::uint16_t> row(vector_v2::kWorldWidth);
  std::size_t blended_pixels = 0;
  for (int y = kFirstRow; y < kFirstRow + kBandRows; ++y) {
    REQUIRE(renderer.render_row(y, row));
    const auto expected_row = std::span(expected).subspan(
        static_cast<std::size_t>(y - kFirstRow) * vector_v2::kWorldWidth, vector_v2::kWorldWidth);
    CHECK(std::equal(row.begin(), row.end(), expected_row.begin()));
    blended_pixels +=
        static_cast<std::size_t>(std::count_if(row.begin(), row.end(), [](auto pixel) {
          return pixel != 0xFFFFU && pixel != 0xF800U && pixel != 0x001FU && pixel != 0x07E0U;
        }));
  }
  CHECK(blended_pixels > 0U);
}

TEST_CASE("settled world export renders a one-sample tap") {
  ExportFixture fixture;
  fixture.append_document();
  std::vector<std::uint16_t> band(static_cast<std::size_t>(vector_v2::kWorldWidth) * 3U);
  std::vector<std::uint16_t> window(vector_v2::kTilePixels);
  SettledWorkspaceStorage storage;
  vector_v2::SettledWorldBandRenderer renderer(fixture.log, band, window, storage.workspace());
  REQUIRE(renderer.ready());

  std::vector<std::uint16_t> row(vector_v2::kWorldWidth);
  REQUIRE(renderer.render_row(5, row));
  CHECK(row[1470] == 0x07E0U);
}

TEST_CASE("settled world rows stream through the production PNG encoder") {
  ExportFixture fixture;
  fixture.append_document();
  std::vector<std::uint16_t> band(static_cast<std::size_t>(vector_v2::kWorldWidth) * 11U);
  std::vector<std::uint16_t> window(vector_v2::kTilePixels);
  SettledWorkspaceStorage storage;
  vector_v2::SettledWorldBandRenderer renderer(fixture.log, band, window, storage.workspace());
  REQUIRE(renderer.ready());

  class RowSource final : public tinydraw::PngRowSource {
   public:
    explicit RowSource(vector_v2::SettledWorldBandRenderer& renderer) : renderer_(renderer) {}
    bool row(int y, std::span<std::uint16_t> destination) override {
      return renderer_.render_row(y, destination);
    }

   private:
    vector_v2::SettledWorldBandRenderer& renderer_;
  } source(renderer);

  std::vector<std::max_align_t> encoder_workspace(
      (tinydraw::png_encoder_workspace_bytes() + sizeof(std::max_align_t) - 1U) /
      sizeof(std::max_align_t));
  std::vector<std::uint8_t> row_storage(tinydraw::png_encoder_row_bytes(vector_v2::kWorldWidth));
  std::vector<std::uint16_t> row_pixels(vector_v2::kWorldWidth);
  MemoryPngOutput output;
  const auto encoded = tinydraw::encode_png_rgb565_rows(
      source, vector_v2::kWorldWidth, vector_v2::kWorldHeight, output, encoder_workspace.data(),
      encoder_workspace.size() * sizeof(std::max_align_t), row_storage, row_pixels);

  REQUIRE(encoded.success());
  CHECK(encoded.bytes_written == output.bytes.size());
  constexpr std::array<std::uint8_t, 8> kSignature{0x89U, 0x50U, 0x4EU, 0x47U,
                                                   0x0DU, 0x0AU, 0x1AU, 0x0AU};
  REQUIRE(output.bytes.size() >= 24U);
  CHECK(std::equal(kSignature.begin(), kSignature.end(), output.bytes.begin()));
  const auto big_endian = [&](std::size_t offset) {
    return static_cast<std::uint32_t>(output.bytes[offset]) << 24U |
           static_cast<std::uint32_t>(output.bytes[offset + 1U]) << 16U |
           static_cast<std::uint32_t>(output.bytes[offset + 2U]) << 8U |
           static_cast<std::uint32_t>(output.bytes[offset + 3U]);
  };
  CHECK(big_endian(16U) == static_cast<std::uint32_t>(vector_v2::kWorldWidth));
  CHECK(big_endian(20U) == static_cast<std::uint32_t>(vector_v2::kWorldHeight));
}

TEST_CASE("settled world band renderer rejects an authority change during a band") {
  ExportFixture fixture;
  fixture.append_document();
  std::vector<std::uint16_t> band(static_cast<std::size_t>(vector_v2::kWorldWidth) * 4U);
  std::vector<std::uint16_t> window(vector_v2::kTilePixels);
  SettledWorkspaceStorage storage;
  const std::array extra{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 16, .radius_256 = 256}};
  struct Mutation {
    vector_v2::OperationLog* log = nullptr;
    std::span<const vector_v2::CompactOperationSample> samples;
    bool appended = false;
  } mutation{.log = &fixture.log, .samples = extra};
  const auto mutate = [](void* raw_context) {
    auto& context = *static_cast<Mutation*>(raw_context);
    if (!context.appended) {
      context.appended =
          context.log->append({.color = 0x0000U, .samples = context.samples}).has_value();
    }
  };
  vector_v2::SettledWorldBandRenderer renderer(fixture.log, band, window, storage.workspace(),
                                               mutate, &mutation);
  REQUIRE(renderer.ready());

  std::vector<std::uint16_t> row(vector_v2::kWorldWidth);
  CHECK_FALSE(renderer.render_row(0, row));
  CHECK(mutation.appended);
  CHECK_FALSE(renderer.ready());
}
