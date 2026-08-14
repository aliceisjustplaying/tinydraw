#include "tinydraw/export/png_encoder.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "tinydraw/graphics/world_canvas.h"

namespace {

class MemoryOutput final : public tinydraw::PngOutput {
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

std::uint32_t big_endian_u32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) << 24U |
         static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U |
         static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U |
         static_cast<std::uint32_t>(bytes[offset + 3U]);
}

struct Workspace {
  explicit Workspace(std::size_t bytes)
      : words((bytes + sizeof(std::max_align_t) - 1U) / sizeof(std::max_align_t)) {}
  [[nodiscard]] void* data() { return words.data(); }
  [[nodiscard]] std::size_t bytes() const { return words.size() * sizeof(std::max_align_t); }
  std::vector<std::max_align_t> words;
};

}  // namespace

TEST_CASE("PNG encoder writes a complete RGB565 image through random-access callbacks") {
  constexpr int width = 4;
  constexpr int height = 3;
  const std::array<std::uint16_t, width * height> pixels{
      0xF800U, 0x07E0U, 0x001FU, 0xFFFFU, 0x0000U, 0x8410U,
      0xFFE0U, 0xF81FU, 0x07FFU, 0xFFFFU, 0x433DU, 0xE031U,
  };
  Workspace workspace(tinydraw::png_encoder_workspace_bytes());
  std::vector<std::uint8_t> row(tinydraw::png_encoder_row_bytes(width));
  MemoryOutput output;

  const auto result = tinydraw::encode_png_rgb565(pixels, width, height, output, workspace.data(),
                                                  workspace.bytes(), row);

  REQUIRE(result.success());
  CHECK(result.bytes_written == output.bytes.size());
  REQUIRE(output.bytes.size() >= 45U);
  constexpr std::array<std::uint8_t, 8> signature{0x89U, 0x50U, 0x4EU, 0x47U,
                                                  0x0DU, 0x0AU, 0x1AU, 0x0AU};
  CHECK(std::equal(signature.begin(), signature.end(), output.bytes.begin()));
  CHECK(big_endian_u32(output.bytes, 8U) == 13U);
  CHECK(big_endian_u32(output.bytes, 12U) == 0x49484452U);
  CHECK(big_endian_u32(output.bytes, 16U) == width);
  CHECK(big_endian_u32(output.bytes, 20U) == height);
  CHECK(big_endian_u32(output.bytes, output.bytes.size() - 8U) == 0x49454E44U);
}

TEST_CASE("PNG encoder handles the complete 3x3 world within a small workspace") {
  constexpr int width = tinydraw::WorldCanvas::kWidth;
  constexpr int height = tinydraw::WorldCanvas::kHeight;
  std::vector<std::uint16_t> pixels(tinydraw::WorldCanvas::kRequiredPixels, 0xFFFFU);
  for (int y = 0; y < height; ++y) {
    const int x = y % width;
    pixels[static_cast<std::size_t>(y * width + x)] = 0x433DU;
  }
  Workspace workspace(tinydraw::png_encoder_workspace_bytes());
  std::vector<std::uint8_t> row(tinydraw::png_encoder_row_bytes(width));
  MemoryOutput output;

  const auto result = tinydraw::encode_png_rgb565(pixels, width, height, output, workspace.data(),
                                                  workspace.bytes(), row);

  CHECK(tinydraw::png_encoder_workspace_bytes() < 64U * 1024U);
  REQUIRE(result.success());
  CHECK(result.bytes_written == output.bytes.size());
  CHECK(result.bytes_written < 256U * 1024U);
  CHECK(big_endian_u32(output.bytes, 16U) == width);
  CHECK(big_endian_u32(output.bytes, 20U) == height);
}

namespace {

class SpanRowSource final : public tinydraw::PngRowSource {
 public:
  SpanRowSource(std::span<const std::uint16_t> pixels, int width, int fail_at_row = -1)
      : pixels_(pixels), width_(width), fail_at_row_(fail_at_row) {}

  bool row(int y, std::span<std::uint16_t> destination) override {
    if (y == fail_at_row_) {
      return false;
    }
    const auto source =
        pixels_.subspan(static_cast<std::size_t>(y) * static_cast<std::size_t>(width_),
                        static_cast<std::size_t>(width_));
    std::copy(source.begin(), source.end(), destination.begin());
    return true;
  }

 private:
  std::span<const std::uint16_t> pixels_;
  int width_;
  int fail_at_row_;
};

}  // namespace

TEST_CASE("row-source encoding is byte-identical to span encoding") {
  constexpr int width = 61;
  constexpr int height = 23;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width) * height);
  for (std::size_t index = 0; index < pixels.size(); ++index) {
    pixels[index] = static_cast<std::uint16_t>(index * 2'654'435'761U >> 13U);
  }
  Workspace workspace(tinydraw::png_encoder_workspace_bytes());
  std::vector<std::uint8_t> row_storage(tinydraw::png_encoder_row_bytes(width));

  MemoryOutput span_output;
  const auto span_result = tinydraw::encode_png_rgb565(
      pixels, width, height, span_output, workspace.data(), workspace.bytes(), row_storage);
  REQUIRE(span_result.success());

  MemoryOutput rows_output;
  SpanRowSource source(pixels, width);
  std::vector<std::uint16_t> row_pixels(width);
  const auto rows_result =
      tinydraw::encode_png_rgb565_rows(source, width, height, rows_output, workspace.data(),
                                       workspace.bytes(), row_storage, row_pixels);
  REQUIRE(rows_result.success());
  CHECK(rows_result.bytes_written == span_result.bytes_written);
  CHECK(rows_output.bytes == span_output.bytes);
}

TEST_CASE("row-source encoding fails cleanly on source failure and short row buffer") {
  constexpr int width = 16;
  constexpr int height = 8;
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width) * height, 0x1234U);
  Workspace workspace(tinydraw::png_encoder_workspace_bytes());
  std::vector<std::uint8_t> row_storage(tinydraw::png_encoder_row_bytes(width));
  std::vector<std::uint16_t> row_pixels(width);

  MemoryOutput output;
  SpanRowSource failing(pixels, width, 3);
  const auto failed = tinydraw::encode_png_rgb565_rows(
      failing, width, height, output, workspace.data(), workspace.bytes(), row_storage, row_pixels);
  CHECK_FALSE(failed.success());

  SpanRowSource source(pixels, width);
  const auto short_row = tinydraw::encode_png_rgb565_rows(
      source, width, height, output, workspace.data(), workspace.bytes(), row_storage,
      std::span(row_pixels).first(width - 1));
  CHECK_FALSE(short_row.success());
}
