// Measures committed-authority centerline angularity on recorded ink traces.
//
// Pipeline mirrors the product exactly:
//   trace CSV -> InkStream (speed-derived pressure; esp32 vector_v2_app.cpp
//   uses a default InkConfig with size = brush_size(chrome.size)) ->
//   screen-to-world mapping (VectorV2Presenter::operation_point, camera at
//   origin) -> ChainedOperationBuilder with the product's 32-sample chunk
//   limit (vector_v2_presenter.h kInteractiveChunkSampleLimit) ->
//   prepare_incremental_curve_unit, whose PreparedCurveStep chords are the
//   exact level-space segments cold replay and warm appends paint.
//
// The true curve per unit is the midpoint quadratic; its control point is
// recovered exactly from the emitted chords (B(0.5) is the shared joint, so
// control = (4*mid - start - end) / 2), so no rasterizer math is duplicated.
//
// --chords 1|2|4 previews the flatness experiment by re-subdividing the same
// quadratic; 2 is the current authority behavior.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/vector_v2/chained_operation_builder.h"
#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/ink_trace.h"
#include "tinydraw/vector_v2/operation.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

struct Point {
  float x = 0;
  float y = 0;
};

struct Chord {
  Point first{};
  Point second{};
  float mean_radius = 0;
};

struct TraceMetrics {
  std::size_t operations = 0;
  std::size_t units = 0;
  std::size_t chords = 0;
  std::vector<float> deviations;     // per curved unit, px
  std::vector<float> joint_degrees;  // per interior polyline joint
  double chord_length_total = 0;
  double radius_total = 0;
  std::size_t radius_samples = 0;
};

float distance_point_segment(Point point, Point a, Point b) {
  const float abx = b.x - a.x;
  const float aby = b.y - a.y;
  const float apx = point.x - a.x;
  const float apy = point.y - a.y;
  const float length_squared = abx * abx + aby * aby;
  const float t = length_squared <= 0.0F
                      ? 0.0F
                      : std::clamp((apx * abx + apy * aby) / length_squared, 0.0F, 1.0F);
  const float dx = apx - t * abx;
  const float dy = apy - t * aby;
  return std::sqrt(dx * dx + dy * dy);
}

Point quadratic_at(Point start, Point control, Point end, float t) {
  const float u = 1.0F - t;
  return {
      .x = u * u * start.x + 2.0F * u * t * control.x + t * t * end.x,
      .y = u * u * start.y + 2.0F * u * t * control.y + t * t * end.y,
  };
}

std::optional<float> percentile(std::vector<float> values, double rank) {
  if (values.empty()) {
    return std::nullopt;
  }
  std::sort(values.begin(), values.end());
  const std::size_t index = std::min(
      values.size() - 1U,
      static_cast<std::size_t>(std::ceil(rank * static_cast<double>(values.size())) - 1.0));
  return values[index];
}

float zoom_scale_of(vector_v2::ZoomLevel zoom) {
  switch (zoom) {
    case vector_v2::ZoomLevel::k25Percent:
      return 0.25F;
    case vector_v2::ZoomLevel::k50Percent:
      return 0.5F;
    case vector_v2::ZoomLevel::k100Percent:
      return 1.0F;
    case vector_v2::ZoomLevel::k200Percent:
      return 2.0F;
    case vector_v2::ZoomLevel::k400Percent:
      return 4.0F;
  }
  return 1.0F;
}

std::optional<vector_v2::ZoomLevel> zoom_from_percent(int percent) {
  switch (percent) {
    case 25:
      return vector_v2::ZoomLevel::k25Percent;
    case 50:
      return vector_v2::ZoomLevel::k50Percent;
    case 100:
      return vector_v2::ZoomLevel::k100Percent;
    case 200:
      return vector_v2::ZoomLevel::k200Percent;
    case 400:
      return vector_v2::ZoomLevel::k400Percent;
  }
  return std::nullopt;
}

// Collects the committed operations (chunked exactly like the product) for
// one trace at one brush size and zoom.
std::vector<std::vector<vector_v2::CompactOperationSample>> committed_operations(
    std::span<const vector_v2::TraceEvent> events, float brush_size, vector_v2::ZoomLevel zoom) {
  const float inverse_scale = 1.0F / zoom_scale_of(zoom);
  tinydraw::InkConfig config;
  config.size = brush_size;
  tinydraw::InkStream ink(config);

  std::vector<vector_v2::CompactOperationSample> storage(65'536);
  constexpr std::size_t kProductChunkSampleLimit = 32;  // kInteractiveChunkSampleLimit
  vector_v2::ChainedOperationBuilder builder(storage, kProductChunkSampleLimit);

  std::vector<std::vector<vector_v2::CompactOperationSample>> operations;
  const auto capture_chunk = [&]() {
    const auto pending = builder.pending_append();
    if (pending.has_value()) {
      operations.emplace_back(pending->samples.begin(), pending->samples.end());
    }
    static_cast<void>(builder.acknowledge_commit());
  };
  const auto operation_point = [&](tinydraw::InkPoint point) {
    return vector_v2::OperationPoint{
        .world_x = std::clamp(point.position.x * inverse_scale, 0.0F,
                              static_cast<float>(vector_v2::kWorldWidth)),
        .world_y = std::clamp(point.position.y * inverse_scale, 0.0F,
                              static_cast<float>(vector_v2::kWorldHeight)),
        .radius = point.radius * inverse_scale,
        .timestamp_us = point.timestamp_us,
    };
  };

  bool stroke_active = false;
  for (const vector_v2::TraceEvent& event : events) {
    const tinydraw::TouchPoint touch{
        .x = static_cast<float>(event.x),
        .y = static_cast<float>(event.y),
        .timestamp_us = static_cast<std::uint32_t>(event.t_us),
    };
    if (event.kind == vector_v2::TraceEventKind::kDown) {
      const tinydraw::InkPoint begun = ink.begin(touch);
      stroke_active =
          builder.begin(vector_v2::OperationTool::kPen, 0x0000U, 1U, operation_point(begun));
      continue;
    }
    if (!stroke_active) {
      continue;
    }
    if (event.kind == vector_v2::TraceEventKind::kMove) {
      auto status = builder.add(operation_point(ink.update(touch)));
      while (status == vector_v2::ChainedOperationStatus::kChunkReady) {
        capture_chunk();
        status = builder.add(operation_point(ink.update(touch)));
        break;  // The product re-offers the rejected point once; mirror that.
      }
      continue;
    }
    auto status = builder.finish(operation_point(ink.finish(touch)));
    while (status == vector_v2::ChainedOperationStatus::kChunkReady ||
           status == vector_v2::ChainedOperationStatus::kFinalChunkReady) {
      capture_chunk();
      status = builder.acknowledge_commit();
      if (status == vector_v2::ChainedOperationStatus::kComplete ||
          status == vector_v2::ChainedOperationStatus::kAccepted) {
        break;
      }
    }
    stroke_active = false;
  }
  return operations;
}

// Rebuilds the polyline the authority paints for one operation and folds the
// unit deviations/joint angles into the metrics.
void measure_operation(std::span<const vector_v2::CompactOperationSample> samples,
                       vector_v2::ZoomLevel zoom, int chord_override, TraceMetrics& metrics) {
  if (samples.empty()) {
    return;
  }
  ++metrics.operations;
  std::vector<Chord> chain;

  const auto add_unit = [&](std::size_t endpoint) {
    const auto unit = vector_v2::prepare_incremental_curve_unit(samples, endpoint, zoom);
    if (!unit.has_value() || unit->step_count == 0U) {
      return;
    }
    ++metrics.units;
    const auto& steps = unit->steps;
    const bool curved = unit->step_count >= 2U;
    if (!curved) {
      chain.push_back({{steps[0].first_x, steps[0].first_y},
                       {steps[0].second_x, steps[0].second_y},
                       (steps[0].first_radius + steps[0].second_radius) * 0.5F});
      return;
    }
    const Point start{steps[0].first_x, steps[0].first_y};
    const Point mid{steps[0].second_x, steps[0].second_y};
    const Point end{steps[1].second_x, steps[1].second_y};
    // B(0.5) == mid exactly (curved_unit subdivides at the parametric
    // midpoint), so the control point is recoverable without rasterizer math.
    const Point control{(4.0F * mid.x - start.x - end.x) * 0.5F,
                        (4.0F * mid.y - start.y - end.y) * 0.5F};
    const float mean_radius = (steps[0].first_radius + steps[1].second_radius) * 0.5F;

    std::vector<Point> vertices;
    if (chord_override == 1) {
      vertices = {start, end};
    } else if (chord_override == 4) {
      vertices = {start, quadratic_at(start, control, end, 0.25F), mid,
                  quadratic_at(start, control, end, 0.75F), end};
    } else {
      vertices = {start, mid, end};
    }
    float worst = 0.0F;
    for (int index = 1; index < 64; ++index) {
      const Point on_curve = quadratic_at(start, control, end, static_cast<float>(index) / 64.0F);
      float nearest = std::numeric_limits<float>::max();
      for (std::size_t segment = 0; segment + 1U < vertices.size(); ++segment) {
        nearest = std::min(
            nearest, distance_point_segment(on_curve, vertices[segment], vertices[segment + 1U]));
      }
      worst = std::max(worst, nearest);
    }
    metrics.deviations.push_back(worst);
    for (std::size_t segment = 0; segment + 1U < vertices.size(); ++segment) {
      chain.push_back({vertices[segment], vertices[segment + 1U], mean_radius});
    }
    if (unit->step_count == 3U) {  // Final-endpoint straight tail segment.
      chain.push_back({{steps[2].first_x, steps[2].first_y},
                       {steps[2].second_x, steps[2].second_y},
                       (steps[2].first_radius + steps[2].second_radius) * 0.5F});
    }
  };

  if (samples.size() <= 2U) {
    add_unit(samples.size() - 1U);
  } else {
    for (std::size_t endpoint = 2; endpoint < samples.size(); ++endpoint) {
      add_unit(endpoint);
    }
  }

  metrics.chords += chain.size();
  for (const Chord& chord : chain) {
    const float dx = chord.second.x - chord.first.x;
    const float dy = chord.second.y - chord.first.y;
    metrics.chord_length_total += std::sqrt(dx * dx + dy * dy);
    metrics.radius_total += chord.mean_radius;
    ++metrics.radius_samples;
  }
  for (std::size_t index = 0; index + 1U < chain.size(); ++index) {
    const Point a{chain[index].second.x - chain[index].first.x,
                  chain[index].second.y - chain[index].first.y};
    const Point b{chain[index + 1U].second.x - chain[index + 1U].first.x,
                  chain[index + 1U].second.y - chain[index + 1U].first.y};
    const float la = std::sqrt(a.x * a.x + a.y * a.y);
    const float lb = std::sqrt(b.x * b.x + b.y * b.y);
    if (la <= 1e-6F || lb <= 1e-6F) {
      continue;
    }
    const float cosine = std::clamp((a.x * b.x + a.y * b.y) / (la * lb), -1.0F, 1.0F);
    metrics.joint_degrees.push_back(std::acos(cosine) * 180.0F / 3.14159265F);
  }
}

struct TraceRun {
  const char* path;
  float brush_size;
};

int run_trace(const char* path, float brush_size, int zoom_percent, int chord_override) {
  std::ifstream file(path);
  if (!file) {
    std::fprintf(stderr, "cannot open %s\n", path);
    return 1;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  const std::string csv = buffer.str();
  std::vector<vector_v2::TraceEvent> events(32'768);
  const auto parsed = vector_v2::parse_ink_trace_csv(csv, events);
  if (!parsed.ok()) {
    std::fprintf(stderr, "invalid trace %s (line %zu)\n", path, parsed.line);
    return 1;
  }
  const auto zoom = zoom_from_percent(zoom_percent);
  if (!zoom.has_value()) {
    std::fprintf(stderr, "unsupported zoom %d\n", zoom_percent);
    return 1;
  }

  const auto operations =
      committed_operations(std::span(events.data(), parsed.event_count), brush_size, *zoom);
  TraceMetrics metrics;
  for (const auto& operation : operations) {
    measure_operation(operation, *zoom, chord_override, metrics);
  }

  const float deviation_max =
      metrics.deviations.empty()
          ? 0.0F
          : *std::max_element(metrics.deviations.begin(), metrics.deviations.end());
  const float deviation_p95 = percentile(metrics.deviations, 0.95).value_or(0.0F);
  const float joint_p50 = percentile(metrics.joint_degrees, 0.50).value_or(0.0F);
  const float joint_p95 = percentile(metrics.joint_degrees, 0.95).value_or(0.0F);
  const float joint_max =
      metrics.joint_degrees.empty()
          ? 0.0F
          : *std::max_element(metrics.joint_degrees.begin(), metrics.joint_degrees.end());
  const std::size_t joints_over_10 = static_cast<std::size_t>(
      std::count_if(metrics.joint_degrees.begin(), metrics.joint_degrees.end(),
                    [](float degree) { return degree > 10.0F; }));
  const std::size_t joints_over_20 = static_cast<std::size_t>(
      std::count_if(metrics.joint_degrees.begin(), metrics.joint_degrees.end(),
                    [](float degree) { return degree > 20.0F; }));
  const double mean_chord =
      metrics.chords == 0U ? 0.0 : metrics.chord_length_total / static_cast<double>(metrics.chords);
  const double mean_radius =
      metrics.radius_samples == 0U
          ? 0.0
          : metrics.radius_total / static_cast<double>(metrics.radius_samples);
  const double deviation_ratio =
      mean_radius <= 0.0 ? 0.0 : static_cast<double>(deviation_max) / (2.0 * mean_radius);

  const char* name = std::strrchr(path, '/') != nullptr ? std::strrchr(path, '/') + 1 : path;
  std::printf(
      "%-26s zoom=%-3d size=%-4.1f chords=%d ops=%-3zu units=%-4zu chords_n=%-5zu "
      "dev_max=%-6.2f dev_p95=%-6.2f joint_p50=%-5.1f joint_p95=%-5.1f joint_max=%-6.1f "
      "j>10=%-4zu j>20=%-4zu chord_len=%-5.1f mean_r=%-5.2f dev/width=%.3f\n",
      name, zoom_percent, static_cast<double>(brush_size), chord_override, metrics.operations,
      metrics.units, metrics.chords, static_cast<double>(deviation_max),
      static_cast<double>(deviation_p95), static_cast<double>(joint_p50),
      static_cast<double>(joint_p95), static_cast<double>(joint_max), joints_over_10,
      joints_over_20, mean_chord, mean_radius, deviation_ratio);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  int zoom_override = 0;
  float size_override = 0.0F;
  int chords = 2;
  std::vector<const char*> paths;
  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "--zoom") == 0 && index + 1 < argc) {
      zoom_override = std::atoi(argv[++index]);
    } else if (std::strcmp(argv[index], "--size") == 0 && index + 1 < argc) {
      size_override = static_cast<float>(std::atof(argv[++index]));
    } else if (std::strcmp(argv[index], "--chords") == 0 && index + 1 < argc) {
      chords = std::atoi(argv[++index]);
    } else {
      paths.push_back(argv[index]);
    }
  }
  if (chords != 1 && chords != 2 && chords != 4) {
    std::fprintf(stderr, "--chords must be 1, 2, or 4\n");
    return 2;
  }

  if (!paths.empty()) {
    const int zoom = zoom_override != 0 ? zoom_override : 100;
    const float size = size_override != 0.0F ? size_override : 5.0F;
    int failures = 0;
    for (const char* path : paths) {
      failures += run_trace(path, size, zoom, chords);
    }
    return failures == 0 ? 0 : 1;
  }

  // Default suite: the six canonical recorded traces at the brush sizes the
  // owner actually used (smallest = 5.0, XL = 20.0), at 100% and 400%.
  const std::array<TraceRun, 6> runs{{
      {"testdata/ink-traces/fast-curve-dense-25.csv", 5.0F},
      {"testdata/ink-traces/fast-curve-400.csv", 5.0F},
      {"testdata/ink-traces/fast-curve-400-xl.csv", 20.0F},
      {"testdata/ink-traces/slow-precise-100.csv", 5.0F},
      {"testdata/ink-traces/scribble-multistroke.csv", 5.0F},
      {"testdata/ink-traces/under-overlay.csv", 5.0F},
  }};
  int failures = 0;
  for (int zoom : {100, 400}) {
    for (const TraceRun& run : runs) {
      const float size = size_override != 0.0F ? size_override : run.brush_size;
      failures += run_trace(run.path, size, zoom_override != 0 ? zoom_override : zoom, chords);
    }
    std::printf("\n");
  }
  return failures == 0 ? 0 : 1;
}
