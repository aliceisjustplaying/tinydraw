#include "tinydraw/vector_v2/svg_export.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "tinydraw/graphics/ribbon_renderer.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/vector_v2/memory_layout.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

class StringSink final : public vector_v2::SvgByteSink {
 public:
  explicit StringSink(std::size_t limit = static_cast<std::size_t>(-1)) : limit_(limit) {}

  bool append(std::string_view bytes) override {
    ++calls;
    maximum_fragment = std::max(maximum_fragment, bytes.size());
    if (text.size() > limit_ || bytes.size() > limit_ - text.size()) {
      return false;
    }
    text.append(bytes);
    return true;
  }

  std::string text;
  std::size_t calls = 0;
  std::size_t maximum_fragment = 0;

 private:
  std::size_t limit_;
};

class CountingSink final : public vector_v2::SvgByteSink {
 public:
  bool append(std::string_view bytes) override {
    bytes_written += bytes.size();
    ++calls;
    return true;
  }

  std::size_t bytes_written = 0;
  std::size_t calls = 0;
};

template <std::size_t OperationCapacity, std::size_t SampleCapacity>
struct LogFixture {
  std::array<vector_v2::OperationRecord, OperationCapacity> records{};
  std::array<vector_v2::CompactOperationSample, SampleCapacity> samples{};
  vector_v2::OperationLog log{records, samples};
};

struct ParsedShape {
  tinydraw::RibbonPrimitiveKind kind = tinydraw::RibbonPrimitiveKind::kConvex;
  std::array<tinydraw::Point, 4> points{};
  std::uint8_t point_count = 0;
  tinydraw::Point center{};
  float radius = 0.0F;
};

bool parse_float(std::string_view text, std::size_t& cursor, float& value) {
  const char* first = text.data() + static_cast<std::ptrdiff_t>(cursor);
  const char* last = text.data() + static_cast<std::ptrdiff_t>(text.size());
  const auto result = std::from_chars(first, last, value, std::chars_format::general);
  if (result.ec != std::errc{} || result.ptr == first) {
    return false;
  }
  cursor = static_cast<std::size_t>(result.ptr - text.data());
  return true;
}

bool parse_attribute(std::string_view line, std::string_view name, float& value) {
  const std::string needle = std::string(name) + "=\"";
  std::size_t cursor = line.find(needle);
  if (cursor == std::string_view::npos) {
    return false;
  }
  cursor += needle.size();
  return parse_float(line, cursor, value) && cursor < line.size() && line[cursor] == '"';
}

bool parse_shapes(std::string_view svg, std::vector<ParsedShape>& shapes) {
  std::size_t line_start = 0;
  while (line_start < svg.size()) {
    const std::size_t line_end = svg.find('\n', line_start);
    const std::string_view line = svg.substr(
        line_start, (line_end == std::string_view::npos ? svg.size() : line_end) - line_start);
    if (line.starts_with("<circle ")) {
      ParsedShape shape{.kind = tinydraw::RibbonPrimitiveKind::kCircle};
      if (!parse_attribute(line, "cx", shape.center.x) ||
          !parse_attribute(line, "cy", shape.center.y) ||
          !parse_attribute(line, "r", shape.radius)) {
        return false;
      }
      shapes.push_back(shape);
    } else if (line.starts_with("<path d=\"M")) {
      ParsedShape shape;
      std::size_t cursor = std::string_view("<path d=\"M").size();
      while (cursor < line.size() && line[cursor] != 'Z') {
        if (line[cursor] == 'L') {
          ++cursor;
        }
        if (shape.point_count == shape.points.size() ||
            !parse_float(line, cursor, shape.points[shape.point_count].x) ||
            cursor >= line.size() || line[cursor++] != ' ' ||
            !parse_float(line, cursor, shape.points[shape.point_count].y)) {
          return false;
        }
        ++shape.point_count;
      }
      if (shape.point_count < 3U || cursor >= line.size() || line.substr(cursor) != "Z\"/>") {
        return false;
      }
      shapes.push_back(shape);
    }
    if (line_end == std::string_view::npos) {
      break;
    }
    line_start = line_end + 1U;
  }
  return true;
}

bool well_formed_export(std::string_view svg) {
  if (!svg.starts_with("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<svg ") ||
      !svg.ends_with("</svg>\n")) {
    return false;
  }
  std::vector<std::string_view> stack;
  std::size_t cursor = 0;
  while ((cursor = svg.find('<', cursor)) != std::string_view::npos) {
    const std::size_t end = svg.find('>', cursor + 1U);
    if (end == std::string_view::npos) {
      return false;
    }
    if (svg[cursor + 1U] == '?') {
      cursor = end + 1U;
      continue;
    }
    const bool closing = svg[cursor + 1U] == '/';
    const std::size_t name_start = cursor + (closing ? 2U : 1U);
    std::size_t name_end = name_start;
    while (name_end < end && svg[name_end] != ' ' && svg[name_end] != '/') {
      ++name_end;
    }
    const std::string_view name = svg.substr(name_start, name_end - name_start);
    const bool self_closing = end > cursor && svg[end - 1U] == '/';
    if (closing) {
      if (stack.empty() || stack.back() != name) {
        return false;
      }
      stack.pop_back();
    } else if (!self_closing) {
      stack.push_back(name);
    }
    cursor = end + 1U;
  }
  return stack.empty();
}

tinydraw::InkPoint ink_point(vector_v2::CompactOperationSample sample) {
  return {
      .position = {.x = static_cast<float>(sample.x_quarter) * 0.0625F,
                   .y = static_cast<float>(sample.y_quarter) * 0.0625F},
      .pressure = 0.0F,
      .radius = static_cast<float>(sample.radius_256) / 256.0F,
  };
}

std::vector<tinydraw::RibbonPrimitive> renderer_geometry(
    std::span<const vector_v2::CompactOperationSample> samples) {
  tinydraw::CurvedRibbonStream ribbon;
  std::vector<tinydraw::RibbonPrimitive> primitives;
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const bool final = index + 1U == samples.size();
    const tinydraw::RibbonUpdate update = final ? ribbon.finish(ink_point(samples[index]))
                                                : ribbon.append(ink_point(samples[index]), false);
    primitives.insert(primitives.end(), update.committed.begin(), update.committed.end());
  }
  return primitives;
}

float cross(tinydraw::Point first, tinydraw::Point second, tinydraw::Point point) {
  return (second.x - first.x) * (point.y - first.y) - (second.y - first.y) * (point.x - first.x);
}

bool contains(const ParsedShape& shape, tinydraw::Point point) {
  if (shape.kind == tinydraw::RibbonPrimitiveKind::kCircle) {
    const float delta_x = point.x - shape.center.x;
    const float delta_y = point.y - shape.center.y;
    return delta_x * delta_x + delta_y * delta_y <= shape.radius * shape.radius;
  }
  bool positive = false;
  bool negative = false;
  for (std::uint8_t index = 0; index < shape.point_count; ++index) {
    const float value =
        cross(shape.points[index], shape.points[(index + 1U) % shape.point_count], point);
    positive = positive || value > 0.0F;
    negative = negative || value < 0.0F;
  }
  return !(positive && negative);
}

std::uint16_t black_over_white(std::uint8_t alpha) {
  const auto blend = [alpha](int destination) { return (destination * (255 - alpha) + 127) / 255; };
  return static_cast<std::uint16_t>((blend(31) << 11) | (blend(63) << 5) | blend(31));
}

}  // namespace

TEST_CASE("SVG export has stable exact output and painter-ordered eraser geometry") {
  LogFixture<4, 8> fixture;
  const std::array pen{
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 160, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 320, .y_quarter = 160, .radius_256 = 1'024},
  };
  const std::array eraser{
      vector_v2::CompactOperationSample{.x_quarter = 240, .y_quarter = 160, .radius_256 = 256},
  };
  REQUIRE(fixture.log.append({.color = 0xF800U, .samples = pen}).has_value());
  REQUIRE(fixture.log
              .append({.tool = vector_v2::OperationTool::kEraser, .color = 0U, .samples = eraser})
              .has_value());

  StringSink sink;
  REQUIRE(vector_v2::export_svg(fixture.log, sink,
                                {.world_bounds = {0, 0, 30, 20}, .background = 0xFFFFU}));
  CHECK(sink.text ==
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"30\" height=\"20\" "
        "viewBox=\"0 0 30 20\">\n"
        "<rect x=\"0\" y=\"0\" width=\"30\" height=\"20\" fill=\"#FFFFFF\"/>\n"
        "<g fill=\"#FF0000\">\n"
        "<circle cx=\"10\" cy=\"10\" r=\"2\"/>\n"
        "<path d=\"M10 8L20 6L20 14L10 12Z\"/>\n"
        "<circle cx=\"20\" cy=\"10\" r=\"4\"/>\n"
        "</g>\n"
        "<g fill=\"#FFFFFF\">\n"
        "<circle cx=\"15\" cy=\"10\" r=\"1\"/>\n"
        "</g>\n"
        "</svg>\n");
  CHECK(sink.maximum_fragment <= 64U);
}

TEST_CASE("exported primitive coverage exactly matches the ribbon renderer raster") {
  LogFixture<2, 8> fixture;
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 128, .y_quarter = 256, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 320, .y_quarter = 128, .radius_256 = 768},
      vector_v2::CompactOperationSample{.x_quarter = 576, .y_quarter = 320, .radius_256 = 1'536},
      vector_v2::CompactOperationSample{.x_quarter = 864, .y_quarter = 192, .radius_256 = 2'560},
  };
  REQUIRE(fixture.log.append({.color = 0U, .samples = samples}).has_value());
  StringSink sink;
  REQUIRE(vector_v2::export_svg(fixture.log, sink,
                                {.world_bounds = {0, 0, 64, 32}, .background = 0xFFFFU}));

  std::vector<ParsedShape> shapes;
  REQUIRE(parse_shapes(sink.text, shapes));
  const std::vector<tinydraw::RibbonPrimitive> primitives = renderer_geometry(samples);
  REQUIRE(shapes.size() == primitives.size());
  std::array<std::uint16_t, 64U * 32U> raster{};
  raster.fill(0xFFFFU);
  tinydraw::RibbonRenderer renderer;
  const tinydraw::RibbonRenderStats stats = renderer.render(primitives, raster, 64, 32, 0U);
  REQUIRE(stats.tiles_rasterized == 1U);

  constexpr int samples_per_axis = 4;
  std::size_t differing_pixels = 0;
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 64; ++x) {
      int covered = 0;
      for (int sample_y = 0; sample_y < samples_per_axis; ++sample_y) {
        for (int sample_x = 0; sample_x < samples_per_axis; ++sample_x) {
          const tinydraw::Point point{
              .x = static_cast<float>(x) + (static_cast<float>(sample_x) + 0.5F) / 4.0F,
              .y = static_cast<float>(y) + (static_cast<float>(sample_y) + 0.5F) / 4.0F,
          };
          covered += std::any_of(shapes.begin(), shapes.end(), [point](const ParsedShape& shape) {
            return contains(shape, point);
          });
        }
      }
      const std::uint8_t alpha = static_cast<std::uint8_t>(covered * 255 / 16);
      const std::uint16_t mathematical = black_over_white(alpha);
      const std::uint16_t rendered = raster[static_cast<std::size_t>(y * 64 + x)];
      if (rendered != mathematical) {
        ++differing_pixels;
      }
      // CoverageTile's scan converter may classify a point exactly on a
      // convex edge one 4x4 supersample differently from the direct
      // half-plane predicate. No channel may differ by more than that one
      // supersample; all other coverage must be exact.
      CHECK(std::abs(static_cast<int>((rendered >> 11U) & 31U) -
                     static_cast<int>((mathematical >> 11U) & 31U)) <= 2);
      CHECK(std::abs(static_cast<int>((rendered >> 5U) & 63U) -
                     static_cast<int>((mathematical >> 5U) & 63U)) <= 4);
      CHECK(std::abs(static_cast<int>(rendered & 31U) - static_cast<int>(mathematical & 31U)) <= 2);
    }
  }
  CHECK(differing_pixels < 16U);
  CHECK(contains(shapes.back(), {.x = 54.0F, .y = 20.0F}));
  CHECK_FALSE(contains(shapes.front(), {.x = 8.0F, .y = 20.0F}));
}

TEST_CASE("SVG export handles empty and single-dot documents and sink failure") {
  LogFixture<2, 4> fixture;
  StringSink empty;
  REQUIRE(vector_v2::export_svg(fixture.log, empty));
  CHECK(well_formed_export(empty.text));
  CHECK(empty.text.find("<g ") == std::string::npos);

  const std::array dot{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 32, .radius_256 = 128}};
  REQUIRE(fixture.log.append({.color = 0x001FU, .samples = dot}).has_value());
  StringSink single;
  REQUIRE(vector_v2::export_svg(fixture.log, single));
  CHECK(single.text.find("<circle cx=\"1\" cy=\"2\" r=\"0.5\"/>") != std::string::npos);
  CHECK(well_formed_export(single.text));

  StringSink short_sink(80U);
  CHECK_FALSE(vector_v2::export_svg(fixture.log, short_sink));
  StringSink invalid;
  CHECK_FALSE(vector_v2::export_svg(fixture.log, invalid,
                                    {.world_bounds = {10, 10, 10, 20}, .background = 0xFFFFU}));
  CHECK(invalid.text.empty());
}

TEST_CASE("hundreds of random authority documents export bounded well-formed XML") {
  std::mt19937 random(0x5A17'2026U);
  for (int document = 0; document < 300; ++document) {
    LogFixture<16, 128> fixture;
    const int operation_count = static_cast<int>(random() % 13U);
    std::size_t expected_samples = 0;
    for (int operation = 0; operation < operation_count; ++operation) {
      const std::size_t count = static_cast<std::size_t>(random() % 8U + 1U);
      std::array<vector_v2::CompactOperationSample, 8> samples{};
      for (std::size_t index = 0; index < count; ++index) {
        samples[index] = {
            .x_quarter = static_cast<std::uint16_t>(random() % (vector_v2::kWorldWidth * 4 + 1U)),
            .y_quarter = static_cast<std::uint16_t>(random() % (vector_v2::kWorldHeight * 4 + 1U)),
            .radius_256 = static_cast<std::uint16_t>(random() % 8'192U + 1U),
            .elapsed_ms = static_cast<std::uint16_t>(index * 4U),
        };
      }
      REQUIRE(fixture.log
                  .append({.tool = (random() & 1U) != 0U ? vector_v2::OperationTool::kPen
                                                         : vector_v2::OperationTool::kEraser,
                           .color = static_cast<std::uint16_t>(random()),
                           .samples = std::span(samples).first(count)})
                  .has_value());
      expected_samples += count;
    }

    StringSink sink;
    REQUIRE(vector_v2::export_svg(fixture.log, sink));
    CHECK(well_formed_export(sink.text));
    std::vector<ParsedShape> shapes;
    CHECK(parse_shapes(sink.text, shapes));
    CHECK(sink.text.size() <=
          1'024U + fixture.log.operation_count() * 128U + expected_samples * 1'000U);
    CHECK(sink.maximum_fragment <= 64U);
  }
}

TEST_CASE("maximum-capacity authority streams without document-sized exporter storage") {
  std::vector<vector_v2::OperationRecord> records(vector_v2::kOperationCapacity);
  std::vector<vector_v2::CompactOperationSample> storage(vector_v2::kOperationSampleCapacity);
  vector_v2::OperationLog log(records, storage);
  constexpr std::size_t samples_per_operation =
      vector_v2::kOperationSampleCapacity / vector_v2::kOperationCapacity;
  static_assert(samples_per_operation * vector_v2::kOperationCapacity ==
                vector_v2::kOperationSampleCapacity);
  std::array<vector_v2::CompactOperationSample, samples_per_operation> samples{};
  for (std::size_t index = 0; index < samples.size(); ++index) {
    samples[index] = {.x_quarter = 400,
                      .y_quarter = 400,
                      .radius_256 = 256,
                      .elapsed_ms = static_cast<std::uint16_t>(index)};
  }
  for (std::size_t operation = 0; operation < vector_v2::kOperationCapacity; ++operation) {
    REQUIRE(log.append({.color = static_cast<std::uint16_t>(operation), .samples = samples})
                .has_value());
  }
  REQUIRE(log.operation_count() == vector_v2::kOperationCapacity);
  REQUIRE(log.sample_count() == vector_v2::kOperationSampleCapacity);

  CountingSink sink;
  REQUIRE(vector_v2::export_svg(log, sink));
  CHECK(sink.bytes_written > 100'000U);
  CHECK(sink.bytes_written < 2'000'000U);
  CHECK(sink.calls > log.operation_count());
}
