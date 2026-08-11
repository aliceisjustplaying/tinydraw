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

PngEncodeResult encode_png_rgb565(std::span<const std::uint16_t> pixels, int width, int height,
                                  PngOutput& output, void* workspace, std::size_t workspace_bytes,
                                  std::span<std::uint8_t> row_storage,
                                  std::uint8_t compression_level) {
  if (active_context != nullptr || workspace == nullptr || width <= 0 || height <= 0 ||
      compression_level > 9U || workspace_bytes < sizeof(PNGENC) ||
      reinterpret_cast<std::uintptr_t>(workspace) % alignof(PNGENC) != 0U ||
      row_storage.size() < png_encoder_row_bytes(width) ||
      static_cast<std::size_t>(width) >
          std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(height) ||
      pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
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
    const auto offset = static_cast<std::size_t>(row) * static_cast<std::size_t>(width);
    error = encoder->addRGB565Line(const_cast<std::uint16_t*>(pixels.data() + offset),
                                   row_storage.data());
  }
  const int encoded_bytes = encoder->close();
  encoder->~PNGENC();
  active_context = nullptr;

  if (!context.valid || error != PNG_SUCCESS || encoded_bytes <= 0) {
    return {.error = context.valid ? error : PNG_ENCODE_ERROR};
  }
  return {.bytes_written = static_cast<std::size_t>(encoded_bytes), .error = PNG_SUCCESS};
}

}  // namespace tinydraw
