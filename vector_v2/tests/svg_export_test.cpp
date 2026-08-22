#include "tinydraw/vector_v2/svg_export.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
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
    maximum_fragment = std::max(maximum_fragment, bytes.size());
    return true;
  }

  std::size_t bytes_written = 0;
  std::size_t calls = 0;
  std::size_t maximum_fragment = 0;
};

struct ProgressTrace {
  std::vector<std::size_t> completed;
  std::vector<std::size_t> totals;
};

void record_progress(std::size_t completed, std::size_t total, void* context) {
  auto& trace = *static_cast<ProgressTrace*>(context);
  trace.completed.push_back(completed);
  trace.totals.push_back(total);
}

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

bool consume(std::string_view text, std::size_t& cursor, std::string_view expected) {
  if (text.substr(cursor, expected.size()) != expected) {
    return false;
  }
  cursor += expected.size();
  return true;
}

bool parse_shapes(std::string_view svg, std::vector<ParsedShape>& shapes) {
  std::size_t path_start = 0;
  while ((path_start = svg.find("<path fill=\"", path_start)) != std::string_view::npos) {
    std::size_t cursor = svg.find(" d=\"", path_start);
    const std::size_t path_end =
        cursor == std::string_view::npos ? cursor : svg.find("\"/>", cursor);
    if (cursor == std::string_view::npos || path_end == std::string_view::npos) {
      return false;
    }
    cursor += std::string_view(" d=\"").size();
    while (cursor < path_end) {
      if (!consume(svg, cursor, "M")) {
        return false;
      }
      tinydraw::Point start{};
      if (!parse_float(svg, cursor, start.x) || !consume(svg, cursor, " ") ||
          !parse_float(svg, cursor, start.y)) {
        return false;
      }
      if (cursor < path_end && svg[cursor] == 'A') {
        ParsedShape shape{.kind = tinydraw::RibbonPrimitiveKind::kCircle};
        float second_radius = 0.0F;
        tinydraw::Point opposite{};
        tinydraw::Point finish{};
        if (!consume(svg, cursor, "A") || !parse_float(svg, cursor, shape.radius) ||
            !consume(svg, cursor, " ") || !parse_float(svg, cursor, second_radius) ||
            !consume(svg, cursor, " 0 1 1 ") || !parse_float(svg, cursor, opposite.x) ||
            !consume(svg, cursor, " ") || !parse_float(svg, cursor, opposite.y) ||
            !consume(svg, cursor, "A") || !parse_float(svg, cursor, second_radius) ||
            !consume(svg, cursor, " ") || !parse_float(svg, cursor, second_radius) ||
            !consume(svg, cursor, " 0 1 1 ") || !parse_float(svg, cursor, finish.x) ||
            !consume(svg, cursor, " ") || !parse_float(svg, cursor, finish.y) ||
            !consume(svg, cursor, "Z")) {
          return false;
        }
        shape.center = {.x = (start.x + opposite.x) * 0.5F, .y = (start.y + opposite.y) * 0.5F};
        shapes.push_back(shape);
      } else {
        ParsedShape shape;
        shape.points[shape.point_count++] = start;
        while (cursor < path_end && svg[cursor] == 'L') {
          ++cursor;
          if (shape.point_count == shape.points.size() ||
              !parse_float(svg, cursor, shape.points[shape.point_count].x) ||
              !consume(svg, cursor, " ") ||
              !parse_float(svg, cursor, shape.points[shape.point_count].y)) {
            return false;
          }
          ++shape.point_count;
        }
        if (shape.point_count < 3U || !consume(svg, cursor, "Z")) {
          return false;
        }
        shapes.push_back(shape);
      }
    }
    path_start = path_end + 3U;
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
      .distance = 0.0F,
      .running_length = 0.0F,
      .timestamp_us = 0U,
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

float squared_distance_to_segment(tinydraw::Point point, tinydraw::Point first,
                                  tinydraw::Point second) {
  const float delta_x = second.x - first.x;
  const float delta_y = second.y - first.y;
  const float length_squared = delta_x * delta_x + delta_y * delta_y;
  if (length_squared == 0.0F) {
    const float x = point.x - first.x;
    const float y = point.y - first.y;
    return x * x + y * y;
  }
  const float projection = std::clamp(
      ((point.x - first.x) * delta_x + (point.y - first.y) * delta_y) / length_squared, 0.0F, 1.0F);
  const float x = point.x - (first.x + projection * delta_x);
  const float y = point.y - (first.y + projection * delta_y);
  return x * x + y * y;
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

bool circle_intersects_convex(const ParsedShape& circle, const ParsedShape& convex) {
  if (circle.kind != tinydraw::RibbonPrimitiveKind::kCircle ||
      convex.kind != tinydraw::RibbonPrimitiveKind::kConvex || convex.point_count < 3U) {
    return false;
  }
  if (contains(convex, circle.center)) {
    return true;
  }
  const float radius_squared = circle.radius * circle.radius;
  for (std::uint8_t index = 0; index < convex.point_count; ++index) {
    if (squared_distance_to_segment(circle.center, convex.points[index],
                                    convex.points[(index + 1U) % convex.point_count]) <=
        radius_squared) {
      return true;
    }
  }
  return false;
}

std::size_t shared_vertices(const ParsedShape& first, const ParsedShape& second) {
  std::size_t shared = 0U;
  for (std::uint8_t first_index = 0; first_index < first.point_count; ++first_index) {
    const tinydraw::Point left = first.points[first_index];
    const bool found =
        std::any_of(second.points.begin(), second.points.begin() + second.point_count,
                    [left](tinydraw::Point right) {
                      return left.x == doctest::Approx(right.x).epsilon(0.0001) &&
                             left.y == doctest::Approx(right.y).epsilon(0.0001);
                    });
    shared += found ? 1U : 0U;
  }
  return shared;
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
  ProgressTrace progress;
  REQUIRE(vector_v2::export_svg(fixture.log, sink,
                                {.world_bounds = {0, 0, 30, 20},
                                 .background = 0xFFFFU,
                                 .progress = record_progress,
                                 .progress_context = &progress}));
  CHECK(sink.text ==
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"30\" height=\"20\" "
        "viewBox=\"0 0 30 20\">\n"
        "<defs>\n"
        "<mask id=\"erase0\" maskUnits=\"userSpaceOnUse\" x=\"0\" y=\"0\" width=\"30\" "
        "height=\"20\">\n"
        "<rect x=\"0\" y=\"0\" width=\"30\" height=\"20\" fill=\"#FFFFFF\"/>\n"
        "<path fill=\"#000000\" fill-rule=\"nonzero\" "
        "d=\"M16 10A1 1 0 1 1 14 10A1 1 0 1 1 16 10Z\"/>\n"
        "</mask>\n"
        "</defs>\n"
        "<g mask=\"url(#erase0)\">\n"
        "<path fill=\"#FF0000\" fill-rule=\"nonzero\" "
        "d=\"M12 10A2 2 0 1 1 8 10A2 2 0 1 1 12 10Z"
        "M10 8L20 6L20 14L10 12Z"
        "M24 10A4 4 0 1 1 16 10A4 4 0 1 1 24 10Z\"/>\n"
        "</g>\n"
        "</svg>\n");
  CHECK(sink.text.find("<path fill=\"#FFFFFF\"") == std::string::npos);
  CHECK(sink.maximum_fragment <= 1'024U);
  CHECK(progress.completed == std::vector<std::size_t>{0U, 1U, 2U});
  CHECK(progress.totals == std::vector<std::size_t>{2U, 2U, 2U});
}

TEST_CASE("SVG eraser masks preserve interleaved painter order and transparency") {
  LogFixture<5, 5> fixture;
  const auto append_dot = [&](vector_v2::OperationTool tool, std::uint16_t color, std::uint16_t x) {
    const std::array sample{
        vector_v2::CompactOperationSample{.x_quarter = x, .y_quarter = 160U, .radius_256 = 256U}};
    return fixture.log.append({.tool = tool, .color = color, .samples = sample}).has_value();
  };
  REQUIRE(append_dot(vector_v2::OperationTool::kPen, 0xF800U, 80U));
  REQUIRE(append_dot(vector_v2::OperationTool::kEraser, 0U, 96U));
  REQUIRE(append_dot(vector_v2::OperationTool::kPen, 0x001FU, 112U));
  REQUIRE(append_dot(vector_v2::OperationTool::kEraser, 0U, 128U));
  REQUIRE(append_dot(vector_v2::OperationTool::kPen, 0x07E0U, 144U));

  StringSink sink;
  REQUIRE(vector_v2::export_svg(fixture.log, sink, {.world_bounds = {0, 0, 20, 20}}));
  REQUIRE(well_formed_export(sink.text));
  const std::size_t content = sink.text.find("</defs>\n");
  REQUIRE(content != std::string::npos);
  const std::string_view ordered(sink.text.data() + static_cast<std::ptrdiff_t>(content),
                                 sink.text.size() - content);
  const std::size_t outer = ordered.find("<g mask=\"url(#erase1)\">");
  const std::size_t inner = ordered.find("<g mask=\"url(#erase0)\">");
  const std::size_t red = ordered.find("<path fill=\"#FF0000\"");
  const std::size_t close_inner = ordered.find("</g>", red);
  const std::size_t blue = ordered.find("<path fill=\"#0000FF\"", close_inner);
  const std::size_t close_outer = ordered.find("</g>", blue);
  const std::size_t green = ordered.find("<path fill=\"#00FF00\"", close_outer);
  CHECK(outer < inner);
  CHECK(inner < red);
  CHECK(red < close_inner);
  CHECK(close_inner < blue);
  CHECK(blue < close_outer);
  CHECK(close_outer < green);
  CHECK(sink.text.find("<path fill=\"#FFFFFF\"") == std::string::npos);
  CHECK(vector_v2::svg_path_count(fixture.log) == 5U);
}

TEST_CASE("operation chunks from one physical gesture export as one path") {
  LogFixture<4, 8> fixture;
  const std::array first{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 40, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 80, .y_quarter = 40, .radius_256 = 256},
  };
  const std::array second{
      first.back(),
      vector_v2::CompactOperationSample{.x_quarter = 120, .y_quarter = 40, .radius_256 = 256},
      vector_v2::CompactOperationSample{.x_quarter = 160, .y_quarter = 40, .radius_256 = 256},
  };
  const std::array next_gesture{
      vector_v2::CompactOperationSample{.x_quarter = 40, .y_quarter = 80, .radius_256 = 256},
  };
  REQUIRE(fixture.log.append({.color = 0U, .gesture_id = 7U, .samples = first}).has_value());
  REQUIRE(fixture.log.append({.color = 0U, .gesture_id = 7U, .samples = second}).has_value());
  REQUIRE(fixture.log.append({.color = 0U, .gesture_id = 8U, .samples = next_gesture}).has_value());

  StringSink sink;
  REQUIRE(vector_v2::export_svg(fixture.log, sink));
  const auto occurrences = [&sink](std::string_view needle) {
    std::size_t count = 0;
    std::size_t cursor = 0;
    while ((cursor = sink.text.find(needle, cursor)) != std::string::npos) {
      ++count;
      cursor += needle.size();
    }
    return count;
  };
  CHECK(occurrences("<path ") == 2U);
  CHECK(vector_v2::svg_path_count(fixture.log) == 2U);
}

TEST_CASE("SVG geometry is continuous across overlapping physical-gesture chunks") {
  LogFixture<2, 8> chunked;
  LogFixture<1, 8> contiguous;
  const vector_v2::CompactOperationSample a{.x_quarter = 40U, .y_quarter = 80U, .radius_256 = 256U};
  const vector_v2::CompactOperationSample b{.x_quarter = 80U, .y_quarter = 60U, .radius_256 = 320U};
  const vector_v2::CompactOperationSample c{
      .x_quarter = 120U, .y_quarter = 80U, .radius_256 = 384U};
  const vector_v2::CompactOperationSample d{
      .x_quarter = 160U, .y_quarter = 40U, .radius_256 = 448U};
  const std::array first{a, b, c};
  const std::array second{c, d};
  const std::array all{a, b, c, d};
  REQUIRE(chunked.log.append({.color = 0x001FU, .gesture_id = 9U, .samples = first}).has_value());
  REQUIRE(chunked.log.append({.color = 0x001FU, .gesture_id = 9U, .samples = second}).has_value());
  REQUIRE(contiguous.log.append({.color = 0x001FU, .gesture_id = 9U, .samples = all}).has_value());

  StringSink chunked_svg;
  StringSink contiguous_svg;
  REQUIRE(vector_v2::export_svg(chunked.log, chunked_svg));
  REQUIRE(vector_v2::export_svg(contiguous.log, contiguous_svg));
  CHECK(chunked_svg.text == contiguous_svg.text);
}

TEST_CASE("SVG quadratic subspans meet without outward overlap teeth") {
  LogFixture<1, 3> fixture;
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 160U, .y_quarter = 320U, .radius_256 = 2'560U},
      vector_v2::CompactOperationSample{.x_quarter = 320U, .y_quarter = 160U, .radius_256 = 2'560U},
      vector_v2::CompactOperationSample{.x_quarter = 480U, .y_quarter = 320U, .radius_256 = 2'560U},
  };
  REQUIRE(fixture.log.append({.color = 0x001FU, .samples = samples}).has_value());

  StringSink sink;
  REQUIRE(vector_v2::export_svg(fixture.log, sink));
  std::vector<ParsedShape> shapes;
  REQUIRE(parse_shapes(sink.text, shapes));

  std::vector<ParsedShape> convexes;
  std::copy_if(shapes.begin(), shapes.end(), std::back_inserter(convexes),
               [](const ParsedShape& shape) {
                 return shape.kind == tinydraw::RibbonPrimitiveKind::kConvex;
               });
  REQUIRE(convexes.size() >= 3U);
  CHECK(shared_vertices(convexes[0], convexes[1]) == 2U);
  CHECK(shared_vertices(convexes[1], convexes[2]) == 2U);
}

TEST_CASE("SVG sharp reversals keep round joints attached to the stroke") {
  LogFixture<1, 3> fixture;
  const std::array samples{
      vector_v2::CompactOperationSample{.x_quarter = 320U, .y_quarter = 320U, .radius_256 = 1'024U},
      vector_v2::CompactOperationSample{.x_quarter = 320U, .y_quarter = 640U, .radius_256 = 1'024U},
      vector_v2::CompactOperationSample{.x_quarter = 320U, .y_quarter = 0U, .radius_256 = 1'024U},
  };
  REQUIRE(fixture.log.append({.color = 0x001FU, .samples = samples}).has_value());

  StringSink sink;
  REQUIRE(vector_v2::export_svg(fixture.log, sink));
  std::vector<ParsedShape> shapes;
  REQUIRE(parse_shapes(sink.text, shapes));
  const auto joint = std::find_if(shapes.begin(), shapes.end(), [](const ParsedShape& shape) {
    return shape.kind == tinydraw::RibbonPrimitiveKind::kCircle &&
           shape.center.x == doctest::Approx(20.0F) && shape.center.y == doctest::Approx(40.0F);
  });
  REQUIRE(joint != shapes.end());
  CHECK(std::any_of(shapes.begin(), shapes.end(), [&](const ParsedShape& shape) {
    return circle_intersects_convex(*joint, shape);
  }));
}

TEST_CASE("SVG export omits a synthetic background rectangle") {
  LogFixture<1, 1> fixture;
  StringSink sink;
  REQUIRE(vector_v2::export_svg(fixture.log, sink));
  CHECK(sink.text.find("<rect ") == std::string::npos);
}

TEST_CASE("shared-boundary SVG differs from overlap raster only at antialiased seams") {
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
  std::size_t hard_disagreements = 0;
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
      hard_disagreements += (rendered == 0x0000U && mathematical == 0xFFFFU) ||
                                    (rendered == 0xFFFFU && mathematical == 0x0000U)
                                ? 1U
                                : 0U;
      // Device raster primitives overlap by 0.75 px to prevent fixed-grid
      // cracks. SVG subspans share exact boundaries to avoid vector-scale
      // corner teeth. Their difference must stay within AA edge coverage;
      // neither representation may add or remove a fully covered pixel.
      CHECK(std::abs(static_cast<int>((rendered >> 11U) & 31U) -
                     static_cast<int>((mathematical >> 11U) & 31U)) <= 12);
      CHECK(std::abs(static_cast<int>((rendered >> 5U) & 63U) -
                     static_cast<int>((mathematical >> 5U) & 63U)) <= 24);
      CHECK(std::abs(static_cast<int>(rendered & 31U) - static_cast<int>(mathematical & 31U)) <=
            12);
    }
  }
  CHECK(differing_pixels < 96U);
  CHECK(hard_disagreements == 0U);
  CHECK(contains(shapes.back(), {.x = 54.0F, .y = 20.0F}));
  CHECK_FALSE(contains(shapes.front(), {.x = 8.0F, .y = 20.0F}));
}

TEST_CASE("SVG export handles empty and single-dot documents and sink failure") {
  LogFixture<2, 4> fixture;
  StringSink empty;
  REQUIRE(vector_v2::export_svg(fixture.log, empty));
  CHECK(well_formed_export(empty.text));
  CHECK(empty.text.find("<path fill=") == std::string::npos);

  const std::array dot{
      vector_v2::CompactOperationSample{.x_quarter = 16, .y_quarter = 32, .radius_256 = 128}};
  REQUIRE(fixture.log.append({.color = 0x001FU, .samples = dot}).has_value());
  StringSink single;
  REQUIRE(vector_v2::export_svg(fixture.log, single));
  CHECK(single.text.find("<path fill=\"#0000FF\" fill-rule=\"nonzero\" d=\"M1.5 2A0.5 0.5") !=
        std::string::npos);
  CHECK(single.text.find("<circle") == std::string::npos);
  CHECK(single.text.find("stroke-width") == std::string::npos);
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
    CHECK(sink.maximum_fragment <= 1'024U);
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
  CHECK(sink.maximum_fragment <= 1'024U);
  CHECK(sink.calls <= (sink.bytes_written + 1'023U) / 1'024U + 8U);
}
