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

#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/vector_v2/incremental_rasterizer.h"
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
  tinydraw::Point second_center{};
  float second_radius = 0.0F;
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
    } else if (line.starts_with("<path d=\"M") &&
               line.find("data-tinydraw-segment=\"1\"") != std::string_view::npos) {
      ParsedShape shape{.kind = tinydraw::RibbonPrimitiveKind::kTaperedSegment};
      if (!parse_attribute(line, "x1", shape.center.x) ||
          !parse_attribute(line, "y1", shape.center.y) ||
          !parse_attribute(line, "r1", shape.radius) ||
          !parse_attribute(line, "x2", shape.second_center.x) ||
          !parse_attribute(line, "y2", shape.second_center.y) ||
          !parse_attribute(line, "r2", shape.second_radius)) {
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

float cross(tinydraw::Point first, tinydraw::Point second, tinydraw::Point point) {
  return (second.x - first.x) * (point.y - first.y) - (second.y - first.y) * (point.x - first.x);
}

bool contains(const ParsedShape& shape, tinydraw::Point point) {
  if (shape.kind == tinydraw::RibbonPrimitiveKind::kCircle) {
    const float delta_x = point.x - shape.center.x;
    const float delta_y = point.y - shape.center.y;
    return delta_x * delta_x + delta_y * delta_y <= shape.radius * shape.radius;
  }
  if (shape.kind == tinydraw::RibbonPrimitiveKind::kTaperedSegment) {
    const tinydraw::RibbonPrimitive primitive = tinydraw::tapered_ribbon_segment(
        {.position = shape.center, .radius = shape.radius},
        {.position = shape.second_center, .radius = shape.second_radius});
    return tinydraw::tapered_ribbon_segment_covers(primitive, point);
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

}  // namespace

TEST_CASE("SVG export has stable exact output and painter-ordered eraser geometry") {
  LogFixture<4, 8> fixture;
  const std::array pen{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 512},
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 40, .radius_256 = 1'024},
  };
  const std::array eraser{
      vector_v2::CompactOperationSample{.x_quarter = 60, .y_quarter = 40, .radius_256 = 256},
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
        "<path d=\"M10 12L20 14A4 4 0 0 0 20 6L10 8A2 2 0 0 0 10 12Z\" "
        "data-tinydraw-segment=\"1\" x1=\"10\" y1=\"10\" r1=\"2\" x2=\"20\" "
        "y2=\"10\" r2=\"4\"/>\n"
        "</g>\n"
        "<g fill=\"#FFFFFF\">\n"
        "<circle cx=\"15\" cy=\"10\" r=\"1\"/>\n"
        "</g>\n"
        "</svg>\n");
  CHECK(sink.maximum_fragment <= 64U);
}

TEST_CASE("exported geometry exactly matches committed authority pixel coverage") {
  LogFixture<2, 8> fixture;
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 32, .y_quarter = 64, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 32, .radius_256 = 768},
      vector_v2::CompactOperationSample{.x_quarter = 144, .y_quarter = 80, .radius_256 = 1'536},
      vector_v2::CompactOperationSample{.x_quarter = 216, .y_quarter = 48, .radius_256 = 2'560},
  };
  REQUIRE(fixture.log.append({.color = 0U, .samples = samples}).has_value());
  StringSink sink;
  REQUIRE(vector_v2::export_svg(fixture.log, sink,
                                {.world_bounds = {0, 0, 64, 32}, .background = 0xFFFFU}));

  std::vector<ParsedShape> shapes;
  REQUIRE(parse_shapes(sink.text, shapes));
  REQUIRE_FALSE(shapes.empty());
  std::array<std::uint16_t, 64U * 32U> authority{};
  authority.fill(0xFFFFU);
  REQUIRE(vector_v2::apply_incremental_operation(
      {.tool = vector_v2::OperationTool::kPen, .color = 0U, .samples = samples},
      {.zoom = vector_v2::ZoomLevel::k100Percent,
       .level_bounds = {0, 0, 64, 32},
       .pixels = authority,
       .stride = 64}));

  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 64; ++x) {
      const tinydraw::Point center{.x = static_cast<float>(x) + 0.5F,
                                   .y = static_cast<float>(y) + 0.5F};
      const bool exported =
          std::any_of(shapes.begin(), shapes.end(),
                      [center](const ParsedShape& shape) { return contains(shape, center); });
      CAPTURE(x);
      CAPTURE(y);
      CHECK(exported == (authority[static_cast<std::size_t>(y * 64 + x)] == 0U));
    }
  }
}

TEST_CASE("SVG export handles empty and single-dot documents and sink failure") {
  LogFixture<2, 4> fixture;
  StringSink empty;
  REQUIRE(vector_v2::export_svg(fixture.log, empty));
  CHECK(well_formed_export(empty.text));
  CHECK(empty.text.find("<g ") == std::string::npos);

  const std::array dot{
      vector_v2::CompactOperationSample{.x_quarter = 4, .y_quarter = 8, .radius_256 = 128}};
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
    samples[index] = {.x_quarter = 100,
                      .y_quarter = 100,
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
