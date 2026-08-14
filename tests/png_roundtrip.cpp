// Encodes representative worlds through the production PNG encoder and fully
// decodes them again with the system zlib: signature, chunk CRCs, inflate,
// per-row defilter, and exact pixel equality against the RGB565 source. This
// is the gate that catches silent scanline-buffer overruns inside pngenc,
// which keep chunk structure and CRCs valid while corrupting the stream.
#include <zlib.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

#include "tinydraw/export/png_encoder.h"

namespace {

class MemoryOutput final : public tinydraw::PngOutput {
 public:
  bool write(std::size_t offset, std::span<const std::uint8_t> input) override {
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

std::uint32_t big_endian(const std::uint8_t* bytes) {
  return static_cast<std::uint32_t>(bytes[0]) << 24U | static_cast<std::uint32_t>(bytes[1]) << 16U |
         static_cast<std::uint32_t>(bytes[2]) << 8U | static_cast<std::uint32_t>(bytes[3]);
}

std::uint8_t paeth(int a, int b, int c) {
  const int p = a + b - c;
  const int pa = std::abs(p - a);
  const int pb = std::abs(p - b);
  const int pc = std::abs(p - c);
  if (pa <= pb && pa <= pc) {
    return static_cast<std::uint8_t>(a);
  }
  return pb <= pc ? static_cast<std::uint8_t>(b) : static_cast<std::uint8_t>(c);
}

bool roundtrip(int width, int height, const char* label) {
  std::vector<std::uint16_t> pixels(static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(height));
  for (std::size_t index = 0; index < pixels.size(); ++index) {
    // Mixed content: white paper, hard runs, and pseudo-random ink.
    const std::size_t x = index % static_cast<std::size_t>(width);
    pixels[index] = x < 64U ? 0xFFFFU : static_cast<std::uint16_t>(index * 2'654'435'761U >> 11U);
  }
  std::vector<std::uint8_t> workspace_storage(tinydraw::png_encoder_workspace_bytes() +
                                              tinydraw::png_encoder_workspace_alignment());
  void* workspace = workspace_storage.data();
  const auto misalignment =
      reinterpret_cast<std::uintptr_t>(workspace) % tinydraw::png_encoder_workspace_alignment();
  if (misalignment != 0U) {
    workspace =
        workspace_storage.data() + (tinydraw::png_encoder_workspace_alignment() - misalignment);
  }
  std::vector<std::uint8_t> row(tinydraw::png_encoder_row_bytes(width));
  MemoryOutput output;
  const auto result = tinydraw::encode_png_rgb565(pixels, width, height, output, workspace,
                                                  tinydraw::png_encoder_workspace_bytes(), row);
  if (!result.success()) {
    std::printf("FAIL %s: encode error %d\n", label, result.error);
    return false;
  }
  const auto& data = output.bytes;
  static constexpr std::array<std::uint8_t, 8> kSignature{0x89U, 0x50U, 0x4EU, 0x47U,
                                                          0x0DU, 0x0AU, 0x1AU, 0x0AU};
  if (data.size() < 45U || !std::equal(kSignature.begin(), kSignature.end(), data.begin())) {
    std::printf("FAIL %s: bad signature\n", label);
    return false;
  }
  std::vector<std::uint8_t> compressed;
  std::size_t position = 8;
  bool saw_end = false;
  while (position + 12U <= data.size()) {
    const std::uint32_t length = big_endian(&data[position]);
    const std::uint8_t* type = &data[position + 4U];
    if (position + 12U + length > data.size()) {
      std::printf("FAIL %s: truncated chunk\n", label);
      return false;
    }
    const std::uint32_t stored_crc = big_endian(&data[position + 8U + length]);
    const auto computed_crc =
        static_cast<std::uint32_t>(crc32(crc32(0L, type, 4U), &data[position + 8U], length));
    if (stored_crc != computed_crc) {
      std::printf("FAIL %s: chunk CRC mismatch\n", label);
      return false;
    }
    if (std::memcmp(type, "IDAT", 4U) == 0) {
      compressed.insert(compressed.end(), &data[position + 8U], &data[position + 8U + length]);
    }
    if (std::memcmp(type, "IEND", 4U) == 0) {
      saw_end = true;
      break;
    }
    position += 12U + length;
  }
  if (!saw_end || compressed.empty()) {
    std::printf("FAIL %s: missing IEND or IDAT\n", label);
    return false;
  }
  const std::size_t stride = static_cast<std::size_t>(width) * 3U + 1U;
  std::vector<std::uint8_t> raw(stride * static_cast<std::size_t>(height));
  uLongf raw_size = raw.size();
  const int inflate_status =
      uncompress(raw.data(), &raw_size, compressed.data(), compressed.size());
  if (inflate_status != Z_OK || raw_size != raw.size()) {
    std::printf("FAIL %s: inflate status %d size %lu of %zu\n", label, inflate_status,
                static_cast<unsigned long>(raw_size), raw.size());
    return false;
  }
  std::vector<std::uint8_t> previous(static_cast<std::size_t>(width) * 3U, 0U);
  std::vector<std::uint8_t> current(static_cast<std::size_t>(width) * 3U, 0U);
  for (int y = 0; y < height; ++y) {
    const std::uint8_t filter = raw[static_cast<std::size_t>(y) * stride];
    const std::uint8_t* line = &raw[static_cast<std::size_t>(y) * stride + 1U];
    for (std::size_t i = 0; i < current.size(); ++i) {
      const int left = i >= 3U ? current[i - 3U] : 0;
      const int up = previous[i];
      const int corner = i >= 3U ? previous[i - 3U] : 0;
      int value = line[i];
      switch (filter) {
        case 1:
          value += left;
          break;
        case 2:
          value += up;
          break;
        case 3:
          value += (left + up) / 2;
          break;
        case 4:
          value += paeth(left, up, corner);
          break;
        default:
          break;
      }
      current[i] = static_cast<std::uint8_t>(value);
    }
    for (int x = 0; x < width; ++x) {
      const std::uint16_t source =
          pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)];
      // Match pngenc's RGB565 -> RGB888 expansion (bit replication).
      const auto value = static_cast<std::uint32_t>(source);
      const auto red = static_cast<std::uint8_t>(((value >> 11U) << 3U) | ((value >> 13U) & 7U));
      const auto green =
          static_cast<std::uint8_t>((((value >> 5U) & 0x3FU) << 2U) | ((value >> 9U) & 3U));
      const auto blue = static_cast<std::uint8_t>(((value & 0x1FU) << 3U) | ((value >> 2U) & 7U));
      if (current[static_cast<std::size_t>(x) * 3U] != red ||
          current[static_cast<std::size_t>(x) * 3U + 1U] != green ||
          current[static_cast<std::size_t>(x) * 3U + 2U] != blue) {
        std::printf("FAIL %s: pixel mismatch at (%d,%d)\n", label, x, y);
        return false;
      }
    }
    std::swap(previous, current);
  }
  std::printf("OK %s: %dx%d file=%zu bytes\n", label, width, height, data.size());
  return true;
}

}  // namespace

int main() {
  bool passed = roundtrip(1104, 96, "raster_v1_width");
  passed = roundtrip(1472, 96, "vector_v2_width") && passed;
  passed = roundtrip(61, 23, "odd_small") && passed;
  // Wider than the configured scanline buffer must be rejected, not corrupted.
  {
    std::vector<std::uint16_t> pixels(2'000U * 4U, 0xFFFFU);
    std::vector<std::uint8_t> workspace(tinydraw::png_encoder_workspace_bytes() + 64U);
    std::vector<std::uint8_t> row(tinydraw::png_encoder_row_bytes(2'000));
    MemoryOutput output;
    const auto result = tinydraw::encode_png_rgb565(pixels, 2'000, 4, output, workspace.data(),
                                                    tinydraw::png_encoder_workspace_bytes(), row);
    if (result.success()) {
      std::printf("FAIL guard: oversized width accepted\n");
      passed = false;
    } else {
      std::printf("OK guard: oversized width rejected\n");
    }
  }
  std::printf(passed ? "PNG_ROUNDTRIP_OK\n" : "PNG_ROUNDTRIP_FAIL\n");
  return passed ? 0 : 1;
}
