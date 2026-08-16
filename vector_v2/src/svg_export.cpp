#include "tinydraw/vector_v2/svg_export.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "tinydraw/ink/ribbon_geometry.h"

namespace tinydraw::vector_v2 {
namespace {

class Writer {
 public:
  explicit Writer(SvgByteSink& sink) : sink_(sink) {}

  bool append(std::string_view text) { return valid_ && (valid_ = sink_.append(text)); }

  bool integer(int value) {
    std::array<char, 16> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return result.ec == std::errc{} &&
           append({buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())});
  }

  bool number(float value) {
    std::array<char, 24> buffer{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                      std::chars_format::general, std::numeric_limits<float>::max_digits10);
    return result.ec == std::errc{} &&
           append({buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())});
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

bool emit_primitive(Writer& writer, const RibbonPrimitive& primitive) {
  if (primitive.kind == RibbonPrimitiveKind::kCircle) {
    return writer.append("<circle cx=\"") && writer.number(primitive.center.x) &&
           writer.append("\" cy=\"") && writer.number(primitive.center.y) &&
           writer.append("\" r=\"") && writer.number(primitive.radius) && writer.append("\"/>\n");
  }

  if (primitive.point_count < 3U || primitive.point_count > primitive.points.size() ||
      !writer.append("<path d=\"M")) {
    return false;
  }
  for (std::uint8_t index = 0; index < primitive.point_count; ++index) {
    if (index != 0U && !writer.append("L")) {
      return false;
    }
    if (!emit_point(writer, primitive.points[index])) {
      return false;
    }
  }
  return writer.append("Z\"/>\n");
}

bool emit_batch(Writer& writer, const RibbonPrimitiveBatch& batch) {
  for (const RibbonPrimitive& primitive : batch) {
    if (!emit_primitive(writer, primitive)) {
      return false;
    }
  }
  return true;
}

bool emit_operation(Writer& writer, const StoredOperation& operation, std::uint16_t background) {
  const std::uint16_t color =
      operation.tool == OperationTool::kEraser ? background : operation.color;
  if (!writer.append("<g fill=\"") || !writer.color(color) || !writer.append("\">\n")) {
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
  return writer.append("</g>\n");
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

  for (std::size_t index = 0; index < operation_count; ++index) {
    const auto operation = log.operation(index);
    if (!operation.has_value() || !emit_operation(writer, *operation, options.background)) {
      return false;
    }
  }

  if (!writer.append("</svg>\n")) {
    return false;
  }
  return log.epoch() == epoch && log.current_revision() == revision &&
         log.operation_count() == operation_count;
}

}  // namespace tinydraw::vector_v2
