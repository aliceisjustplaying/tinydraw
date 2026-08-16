#include "tinydraw/vector_v2/svg_export.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "tinydraw/ink/ribbon_geometry.h"

namespace tinydraw::vector_v2 {
namespace {

class Writer {
 public:
  explicit Writer(SvgByteSink& sink) : sink_(sink) {}

  bool append(std::string_view text) {
    if (!valid_) {
      return false;
    }
    if (text.size() > buffer_.size()) {
      return flush() && (valid_ = sink_.append(text));
    }
    if (text.size() > buffer_.size() - buffered_ && !flush()) {
      return false;
    }
    std::copy(text.begin(), text.end(), buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_));
    buffered_ += text.size();
    return true;
  }

  bool flush() {
    if (!valid_ || buffered_ == 0U) {
      return valid_;
    }
    valid_ = sink_.append({buffer_.data(), buffered_});
    if (valid_) {
      buffered_ = 0U;
    }
    return valid_;
  }

  bool integer(int value) {
    std::array<char, 16> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return result.ec == std::errc{} &&
           append({buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())});
  }

  bool number(float value) {
    // The operation log is quantized to 1/16 px positions and 1/256 px radii.
    // Ribbon intersections add fractional coordinates, but four SVG decimal
    // places keep their maximum rounding error below 0.00005 px. Formatting
    // the bounded fixed-point value directly is dramatically cheaper than the
    // generic floating-point charconv implementation on the ESP32-S3.
    constexpr int scale = 10'000;
    const float scaled_value = value * static_cast<float>(scale);
    const int scaled = static_cast<int>(scaled_value + (scaled_value < 0.0F ? -0.5F : 0.5F));
    if (scaled == 0) {
      return append("0");
    }

    const bool negative = scaled < 0;
    const std::uint32_t magnitude = static_cast<std::uint32_t>(negative ? -scaled : scaled);
    if ((negative && !append("-")) || !integer(static_cast<int>(magnitude / scale))) {
      return false;
    }
    std::uint32_t remainder = magnitude % scale;
    if (remainder == 0U) {
      return true;
    }

    std::array<char, 5> fraction{'.', '0', '0', '0', '0'};
    for (std::size_t index = fraction.size(); index > 1U; --index) {
      fraction[index - 1U] = static_cast<char>('0' + remainder % 10U);
      remainder /= 10U;
    }
    std::size_t length = fraction.size();
    while (length > 1U && fraction[length - 1U] == '0') {
      --length;
    }
    return append({fraction.data(), length});
  }

  bool color(std::uint16_t rgb565) {
    constexpr std::string_view digits = "0123456789ABCDEF";
    const std::uint8_t red = static_cast<std::uint8_t>(((rgb565 >> 8U) & 0xF8U) | (rgb565 >> 13U));
    const std::uint8_t green =
        static_cast<std::uint8_t>(((rgb565 >> 3U) & 0xFCU) | ((rgb565 >> 9U) & 0x03U));
    const std::uint8_t blue =
        static_cast<std::uint8_t>(((rgb565 & 0x1FU) << 3U) | ((rgb565 & 0x1CU) >> 2U));
    const std::array text{'#',
                          digits[red >> 4U],
                          digits[red & 0x0FU],
                          digits[green >> 4U],
                          digits[green & 0x0FU],
                          digits[blue >> 4U],
                          digits[blue & 0x0FU]};
    return append({text.data(), text.size()});
  }

 private:
  SvgByteSink& sink_;
  std::array<char, 1'024> buffer_{};
  std::size_t buffered_ = 0;
  bool valid_ = true;
};

InkPoint ink_point(CompactOperationSample sample) {
  return {
      .position = {.x = static_cast<float>(sample.x_quarter) * 0.0625F,
                   .y = static_cast<float>(sample.y_quarter) * 0.0625F},
      .pressure = 0.0F,
      .radius = static_cast<float>(sample.radius_256) / 256.0F,
      .distance = 0.0F,
      .running_length = 0.0F,
      .timestamp_us = static_cast<std::uint32_t>(sample.elapsed_ms) * 1'000U,
  };
}

bool emit_point(Writer& writer, Point point) {
  return writer.number(point.x) && writer.append(" ") && writer.number(point.y);
}

bool emit_circle_subpath(Writer& writer, const RibbonPrimitive& primitive) {
  const float left = primitive.center.x - primitive.radius;
  const float right = primitive.center.x + primitive.radius;
  return writer.append("M") && writer.number(right) && writer.append(" ") &&
         writer.number(primitive.center.y) && writer.append("A") &&
         writer.number(primitive.radius) && writer.append(" ") && writer.number(primitive.radius) &&
         writer.append(" 0 1 1 ") && writer.number(left) && writer.append(" ") &&
         writer.number(primitive.center.y) && writer.append("A") &&
         writer.number(primitive.radius) && writer.append(" ") && writer.number(primitive.radius) &&
         writer.append(" 0 1 1 ") && writer.number(right) && writer.append(" ") &&
         writer.number(primitive.center.y) && writer.append("Z");
}

float signed_area_twice(const RibbonPrimitive& primitive) {
  float area = 0.0F;
  for (std::uint8_t index = 0; index < primitive.point_count; ++index) {
    const Point first = primitive.points[index];
    const Point second = primitive.points[(index + 1U) % primitive.point_count];
    area += first.x * second.y - second.x * first.y;
  }
  return area;
}

bool emit_convex_subpath(Writer& writer, const RibbonPrimitive& primitive) {
  if (primitive.point_count < 3U || primitive.point_count > primitive.points.size()) {
    return false;
  }
  const bool reverse = signed_area_twice(primitive) < 0.0F;
  for (std::uint8_t output_index = 0; output_index < primitive.point_count; ++output_index) {
    const std::uint8_t index =
        reverse ? static_cast<std::uint8_t>(primitive.point_count - 1U - output_index)
                : output_index;
    if (!writer.append(output_index == 0U ? "M" : "L") ||
        !emit_point(writer, primitive.points[index])) {
      return false;
    }
  }
  return writer.append("Z");
}

bool emit_batch(Writer& writer, const RibbonPrimitiveBatch& batch) {
  for (const RibbonPrimitive& primitive : batch) {
    const bool emitted = primitive.kind == RibbonPrimitiveKind::kCircle
                             ? emit_circle_subpath(writer, primitive)
                             : emit_convex_subpath(writer, primitive);
    if (!emitted) {
      return false;
    }
  }
  return true;
}

bool emit_operation(Writer& writer, const StoredOperation& operation, std::uint16_t background) {
  const std::uint16_t color =
      operation.tool == OperationTool::kEraser ? background : operation.color;
  if (!writer.append("<path fill=\"") || !writer.color(color) ||
      !writer.append("\" fill-rule=\"nonzero\" d=\"")) {
    return false;
  }

  CurvedRibbonStream ribbon;
  for (std::size_t index = 0; index < operation.samples.size(); ++index) {
    const bool final = index + 1U == operation.samples.size();
    const RibbonUpdate update = final ? ribbon.finish(ink_point(operation.samples[index]))
                                      : ribbon.append(ink_point(operation.samples[index]), false);
    if (!emit_batch(writer, update.committed)) {
      return false;
    }
  }
  return writer.append("\"/>\n");
}

bool valid_bounds(PixelRect bounds) {
  return bounds.x0 >= 0 && bounds.y0 >= 0 && bounds.x1 <= kWorldWidth &&
         bounds.y1 <= kWorldHeight && bounds.x1 > bounds.x0 && bounds.y1 > bounds.y0;
}

}  // namespace

bool export_svg(const OperationLog& log, SvgByteSink& sink, SvgExportOptions options) {
  if (!log.ready() || !valid_bounds(options.world_bounds)) {
    return false;
  }

  const OperationLogEpoch epoch = log.epoch();
  const DocumentRevision revision = log.current_revision();
  const std::size_t operation_count = log.operation_count();
  Writer writer(sink);
  const int width = options.world_bounds.x1 - options.world_bounds.x0;
  const int height = options.world_bounds.y1 - options.world_bounds.y0;

  if (!writer.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n") ||
      !writer.append("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"") ||
      !writer.integer(width) || !writer.append("\" height=\"") || !writer.integer(height) ||
      !writer.append("\" viewBox=\"") || !writer.integer(options.world_bounds.x0) ||
      !writer.append(" ") || !writer.integer(options.world_bounds.y0) || !writer.append(" ") ||
      !writer.integer(width) || !writer.append(" ") || !writer.integer(height) ||
      !writer.append("\">\n<rect x=\"") || !writer.integer(options.world_bounds.x0) ||
      !writer.append("\" y=\"") || !writer.integer(options.world_bounds.y0) ||
      !writer.append("\" width=\"") || !writer.integer(width) || !writer.append("\" height=\"") ||
      !writer.integer(height) || !writer.append("\" fill=\"") ||
      !writer.color(options.background) || !writer.append("\"/>\n")) {
    return false;
  }

  if (options.progress != nullptr) {
    options.progress(0U, operation_count, options.progress_context);
  }
  for (std::size_t index = 0; index < operation_count; ++index) {
    const auto operation = log.operation(index);
    if (!operation.has_value() || !emit_operation(writer, *operation, options.background)) {
      return false;
    }
    if (options.progress != nullptr) {
      options.progress(index + 1U, operation_count, options.progress_context);
    }
  }

  if (!writer.append("</svg>\n") || !writer.flush()) {
    return false;
  }
  return log.epoch() == epoch && log.current_revision() == revision &&
         log.operation_count() == operation_count;
}

}  // namespace tinydraw::vector_v2
