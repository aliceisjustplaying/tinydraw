#include "tinydraw/export/png_encoder.h"

#include <cstdint>
#include <limits>
#include <new>

#include "PNGenc.h"

namespace tinydraw {
namespace {

struct OutputContext {
  PngOutput* output = nullptr;
  std::size_t position = 0;
  bool valid = true;
};

OutputContext* active_context = nullptr;

void* open_output(const char*) { return active_context; }

void close_output(PNGFILE*) {}

int32_t read_output(PNGFILE* file, std::uint8_t* data, int32_t length) {
  auto* context = static_cast<OutputContext*>(file->fHandle);
  if (context == nullptr || length < 0 ||
      !context->output->read(context->position,
                             std::span(data, static_cast<std::size_t>(length)))) {
    if (context != nullptr) {
      context->valid = false;
    }
    return 0;
  }
  context->position += static_cast<std::size_t>(length);
  return length;
}

int32_t write_output(PNGFILE* file, std::uint8_t* data, int32_t length) {
  auto* context = static_cast<OutputContext*>(file->fHandle);
  if (context == nullptr || length < 0 ||
      !context->output->write(context->position,
                              std::span(data, static_cast<std::size_t>(length)))) {
    if (context != nullptr) {
      context->valid = false;
    }
    return 0;
  }
  context->position += static_cast<std::size_t>(length);
  return length;
}

int32_t seek_output(PNGFILE* file, int32_t position) {
  auto* context = static_cast<OutputContext*>(file->fHandle);
  if (context == nullptr || position < 0) {
    return -1;
  }
  context->position = static_cast<std::size_t>(position);
  return position;
}

}  // namespace

std::size_t png_encoder_workspace_bytes() { return sizeof(PNGENC); }

std::size_t png_encoder_workspace_alignment() { return alignof(PNGENC); }

std::size_t png_encoder_row_bytes(int width) {
  return width > 0 ? static_cast<std::size_t>(width) * 3U : 0U;
}

namespace {

// pngenc's RGB565 -> RGB888 expansion (bit replication), kept identical so
// exported pixel values never change. The conversion runs here because the
// library's own addRGB565Line drives PNG_addRGB565Line, a duplicated
// compression path that neither loops its deflate drain when one scanline
// overflows the 2 KiB output window (silently dropping the remainder of the
// filtered line) nor loops Z_FINISH at the last line (truncating the stream).
// PNG_addLine, used by addLine, handles both correctly, so this wrapper
// converts and feeds that path instead.
void expand_rgb565_row(std::span<const std::uint16_t> row_pixels, int width,
                       std::span<std::uint8_t> row_storage) {
  std::uint8_t* destination = row_storage.data();
  for (int x = 0; x < width; ++x) {
    const std::uint32_t pixel = row_pixels[static_cast<std::size_t>(x)];
    *destination++ = static_cast<std::uint8_t>(((pixel >> 8U) & 0xF8U) | (pixel >> 13U));
    *destination++ = static_cast<std::uint8_t>(((pixel >> 3U) & 0xFCU) | ((pixel >> 9U) & 0x3U));
    *destination++ = static_cast<std::uint8_t>(((pixel & 0x1FU) << 3U) | ((pixel & 0x1CU) >> 2U));
  }
}

// Shared driver for both entry points. next_row returns the RGB565 scanline
// for one row or nullptr on source failure.
template <typename NextRow>
PngEncodeResult run_png_encoder(int width, int height, PngOutput& output, void* workspace,
                                std::size_t workspace_bytes, std::span<std::uint8_t> row_storage,
                                std::uint8_t compression_level, NextRow&& next_row) {
  // pngenc filters each scanline into fixed PNG_MAX_BUFFERED_PIXELS-byte line
  // buffers without its own bounds check; a wider row would silently overrun
  // them into the adjacent compression buffer and corrupt the stream.
  if (active_context != nullptr || workspace == nullptr || width <= 0 || height <= 0 ||
      compression_level > 9U || workspace_bytes < sizeof(PNGENC) ||
      reinterpret_cast<std::uintptr_t>(workspace) % alignof(PNGENC) != 0U ||
      row_storage.size() < png_encoder_row_bytes(width) ||
      png_encoder_row_bytes(width) + 1U > static_cast<std::size_t>(PNG_MAX_BUFFERED_PIXELS) ||
      static_cast<std::size_t>(width) >
          std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height)) {
    return {.error = PNG_INVALID_PARAMETER};
  }

  auto* encoder = new (workspace) PNGENC;
  OutputContext context{.output = &output};
  active_context = &context;
  int error = encoder->open("DRAWING.PNG", open_output, close_output, read_output, write_output,
                            seek_output);
  if (error == PNG_SUCCESS) {
    error =
        encoder->encodeBegin(width, height, PNG_PIXEL_TRUECOLOR, 24U, nullptr, compression_level);
  }
  for (int row = 0; row < height && error == PNG_SUCCESS && context.valid; ++row) {
    const std::uint16_t* line = next_row(row);
    if (line == nullptr) {
      error = PNG_ENCODE_ERROR;
      break;
    }
    expand_rgb565_row(std::span(line, static_cast<std::size_t>(width)), width, row_storage);
    error = encoder->addLine(row_storage.data());
  }
  const int encoded_bytes = encoder->close();
  encoder->~PNGENC();
  active_context = nullptr;

  if (!context.valid || error != PNG_SUCCESS || encoded_bytes <= 0) {
    return {.error = context.valid ? error : PNG_ENCODE_ERROR};
  }
  return {.bytes_written = static_cast<std::size_t>(encoded_bytes), .error = PNG_SUCCESS};
}

}  // namespace

PngEncodeResult encode_png_rgb565(std::span<const std::uint16_t> pixels, int width, int height,
                                  PngOutput& output, void* workspace, std::size_t workspace_bytes,
                                  std::span<std::uint8_t> row_storage,
                                  std::uint8_t compression_level) {
  if (width > 0 && height > 0 &&
      (static_cast<std::size_t>(width) <=
       std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height)) &&
      pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    return {.error = PNG_INVALID_PARAMETER};
  }
  return run_png_encoder(width, height, output, workspace, workspace_bytes, row_storage,
                         compression_level, [&](int row) {
                           return pixels.data() +
                                  static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
                         });
}

PngEncodeResult encode_png_rgb565_rows(PngRowSource& source, int width, int height,
                                       PngOutput& output, void* workspace,
                                       std::size_t workspace_bytes,
                                       std::span<std::uint8_t> row_storage,
                                       std::span<std::uint16_t> row_pixels,
                                       std::uint8_t compression_level) {
  if (width > 0 && row_pixels.size() < static_cast<std::size_t>(width)) {
    return {.error = PNG_INVALID_PARAMETER};
  }
  return run_png_encoder(
      width, height, output, workspace, workspace_bytes, row_storage, compression_level,
      [&](int row) -> const std::uint16_t* {
        if (!source.row(row, row_pixels.first(static_cast<std::size_t>(width)))) {
          return nullptr;
        }
        return row_pixels.data();
      });
}

}  // namespace tinydraw
