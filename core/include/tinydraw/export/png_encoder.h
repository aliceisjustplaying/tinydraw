#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace tinydraw {

class PngOutput {
 public:
  virtual ~PngOutput() = default;

  [[nodiscard]] virtual bool write(std::size_t offset, std::span<const std::uint8_t> bytes) = 0;
  [[nodiscard]] virtual bool read(std::size_t offset, std::span<std::uint8_t> bytes) = 0;
};

struct PngEncodeResult {
  std::size_t bytes_written = 0;
  int error = 0;
  [[nodiscard]] bool success() const { return error == 0 && bytes_written > 0; }
};

[[nodiscard]] std::size_t png_encoder_workspace_bytes();
[[nodiscard]] std::size_t png_encoder_workspace_alignment();
[[nodiscard]] std::size_t png_encoder_row_bytes(int width);

// Encodes row-major RGB565 pixels without allocating. Workspace must satisfy the
// reported size/alignment; row_storage holds one temporary RGB888 scanline.
[[nodiscard]] PngEncodeResult encode_png_rgb565(std::span<const std::uint16_t> pixels, int width,
                                                int height, PngOutput& output, void* workspace,
                                                std::size_t workspace_bytes,
                                                std::span<std::uint8_t> row_storage,
                                                std::uint8_t compression_level = 3U);

// Streams RGB565 scanlines from a caller-owned source, for worlds that exist
// only as tiles or vector authority rather than one flat buffer. Rows are
// requested exactly once each, top to bottom.
class PngRowSource {
 public:
  virtual ~PngRowSource() = default;
  [[nodiscard]] virtual bool row(int y, std::span<std::uint16_t> destination) = 0;
};

// Row-source sibling of encode_png_rgb565 producing byte-identical output for
// identical pixels. row_pixels must hold one RGB565 scanline of width pixels.
[[nodiscard]] PngEncodeResult encode_png_rgb565_rows(PngRowSource& source, int width, int height,
                                                     PngOutput& output, void* workspace,
                                                     std::size_t workspace_bytes,
                                                     std::span<std::uint8_t> row_storage,
                                                     std::span<std::uint16_t> row_pixels,
                                                     std::uint8_t compression_level = 3U);

}  // namespace tinydraw
