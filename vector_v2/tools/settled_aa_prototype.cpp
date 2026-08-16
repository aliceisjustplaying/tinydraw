// Settled-AA + arc-length-resampling host prototype (owner decisions #3/#4,
// 2026-08-16; design: VECTOR_V2_COMMITTED_OVERLAY_DESIGN.md sibling work,
// review_findings_2026_08_16_cold_campaign/REVIEW.md "Settled AA").
//
// Renders committed authority from a recorded trace two ways over the same
// window and writes PNGs for owner judgment:
//   baseline: the production hard-edged rasterizer (apply_incremental_operation)
//   aa:       analytic capsule coverage - the quality reference the device
//             boundary-pixel implementation approximates
// Optionally re-runs both with deterministic arc-length resampling of the
// InkStream output (review §9.4) so the smoothness lever is visible alone
// and combined.
//
// Frozen RGB565 blend model (proposal for owner review): RGB565 expands to
// 8-bit per channel by bit replication; compositing accumulates in float,
// front-to-back newest-first with per-operation UNION coverage (self-overlap
// inside one operation never darkens); erasers composite opaque white; the
// final pixel rounds once to RGB565 over a white background.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "tinydraw/export/png_encoder.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/vector_v2/chained_operation_builder.h"
#include "tinydraw/vector_v2/incremental_rasterizer.h"
#include "tinydraw/vector_v2/ink_trace.h"
#include "tinydraw/vector_v2/materialized_canvas.h"
#include "tinydraw/vector_v2/operation.h"

namespace vector_v2 = tinydraw::vector_v2;

namespace {

constexpr std::size_t kProductChunkSampleLimit = 32;  // kInteractiveChunkSampleLimit

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

// Deterministic arc-length resampling of the adjusted InkStream output
// (review §9.4): interpolate position, radius, and timestamp at fixed
// screen-space spacing, carrying residual distance across raw segments. The
// final raw endpoint always emits so lift geometry is preserved.
std::vector<tinydraw::InkPoint> resample_arc_length(std::span<const tinydraw::InkPoint> points,
                                                    float spacing_px) {
  if (points.size() < 2U || spacing_px <= 0.0F) {
    return {points.begin(), points.end()};
  }
  std::vector<tinydraw::InkPoint> out;
  out.push_back(points.front());
  float carry = 0.0F;
  for (std::size_t index = 1; index < points.size(); ++index) {
    const tinydraw::InkPoint& a = points[index - 1U];
    const tinydraw::InkPoint& b = points[index];
    const float dx = b.position.x - a.position.x;
    const float dy = b.position.y - a.position.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0F) {
      continue;
    }
    float distance = spacing_px - carry;
    while (distance <= length) {
      const float t = distance / length;
      tinydraw::InkPoint emitted = a;
      emitted.position.x = a.position.x + dx * t;
      emitted.position.y = a.position.y + dy * t;
      emitted.radius = a.radius + (b.radius - a.radius) * t;
      emitted.timestamp_us =
          a.timestamp_us +
          static_cast<std::uint32_t>(static_cast<float>(b.timestamp_us - a.timestamp_us) * t);
      out.push_back(emitted);
      distance += spacing_px;
    }
    carry = length - (distance - spacing_px);
  }
  if (out.size() < 2U || out.back().position.x != points.back().position.x ||
      out.back().position.y != points.back().position.y) {
    out.push_back(points.back());
  }
  return out;
}

struct CommittedOperation {
  vector_v2::OperationTool tool = vector_v2::OperationTool::kPen;
  std::uint16_t color = 0;
  std::vector<vector_v2::CompactOperationSample> samples;
};

// One tapered capsule segment in level space; the unit both renderers
// consume so committed (quantized) and float-reference geometry go through
// the identical compositor.
struct CapsuleChord {
  float ax = 0;
  float ay = 0;
  float ar = 0;
  float bx = 0;
  float by = 0;
  float br = 0;
};

struct ChordOperation {
  std::uint16_t color = 0;
  bool eraser = false;
  std::vector<CapsuleChord> chords;
};

// The V1-equivalent path: midpoint quadratics over the unquantized float
// InkPoints (screen space == level space at the drawing zoom), densely
// subdivided. Same smoothing model as CurvedRibbonStream / the prepared
// curve units — the only variable versus the committed path is the
// quarter-world sample quantization (and chunk boundaries).
std::vector<ChordOperation> float_reference_operations(
    std::span<const vector_v2::TraceEvent> events, float brush_size, float resample_spacing_px,
    std::uint16_t color) {
  tinydraw::InkConfig config;
  config.size = brush_size;
  tinydraw::InkStream ink(config);
  std::vector<ChordOperation> operations;
  std::vector<tinydraw::InkPoint> stroke;
  const auto commit_stroke = [&](std::span<const tinydraw::InkPoint> raw) {
    const auto points = resample_arc_length(raw, resample_spacing_px);
    if (points.size() < 2U) {
      return;
    }
    ChordOperation operation;
    operation.color = color;
    constexpr int kSubdivisions = 8;
    const auto emit_quadratic = [&](const tinydraw::InkPoint& start,
                                    const tinydraw::InkPoint& control,
                                    const tinydraw::InkPoint& end) {
      float px = start.position.x;
      float py = start.position.y;
      float pr = start.radius;
      for (int step = 1; step <= kSubdivisions; ++step) {
        const float t = static_cast<float>(step) / kSubdivisions;
        const float u = 1.0F - t;
        const float x =
            u * u * start.position.x + 2.0F * u * t * control.position.x + t * t * end.position.x;
        const float y =
            u * u * start.position.y + 2.0F * u * t * control.position.y + t * t * end.position.y;
        const float r = u * u * start.radius + 2.0F * u * t * control.radius + t * t * end.radius;
        operation.chords.push_back({px, py, pr, x, y, r});
        px = x;
        py = y;
        pr = r;
      }
    };
    const auto midpoint = [](const tinydraw::InkPoint& a, const tinydraw::InkPoint& b) {
      tinydraw::InkPoint result = b;
      result.position.x = (a.position.x + b.position.x) * 0.5F;
      result.position.y = (a.position.y + b.position.y) * 0.5F;
      result.radius = (a.radius + b.radius) * 0.5F;
      return result;
    };
    if (points.size() == 2U) {
      operation.chords.push_back({points[0].position.x, points[0].position.y, points[0].radius,
                                  points[1].position.x, points[1].position.y, points[1].radius});
    } else {
      emit_quadratic(points[0], points[0], midpoint(points[0], points[1]));
      for (std::size_t index = 1; index + 1U < points.size(); ++index) {
        emit_quadratic(midpoint(points[index - 1U], points[index]), points[index],
                       midpoint(points[index], points[index + 1U]));
      }
      emit_quadratic(midpoint(points[points.size() - 2U], points.back()), points.back(),
                     points.back());
    }
    operations.push_back(std::move(operation));
  };
  for (const vector_v2::TraceEvent& event : events) {
    const tinydraw::TouchPoint touch{
        .x = static_cast<float>(event.x),
        .y = static_cast<float>(event.y),
        .timestamp_us = static_cast<std::uint32_t>(event.t_us),
    };
    if (event.kind == vector_v2::TraceEventKind::kDown) {
      stroke.clear();
      stroke.push_back(ink.begin(touch));
      continue;
    }
    if (stroke.empty()) {
      continue;
    }
    if (event.kind == vector_v2::TraceEventKind::kMove) {
      stroke.push_back(ink.update(touch));
      continue;
    }
    stroke.push_back(ink.finish(touch));
    commit_stroke(stroke);
    stroke.clear();
  }
  return operations;
}

std::vector<ChordOperation> chords_from_committed(const std::vector<CommittedOperation>& committed,
                                                  vector_v2::ZoomLevel zoom) {
  std::vector<ChordOperation> operations;
  for (const CommittedOperation& operation : committed) {
    ChordOperation out;
    out.color = operation.color;
    out.eraser = operation.tool == vector_v2::OperationTool::kEraser;
    const auto samples = std::span<const vector_v2::CompactOperationSample>(operation.samples);
    for (std::size_t endpoint = 1; endpoint < samples.size(); ++endpoint) {
      const auto unit = vector_v2::prepare_incremental_curve_unit(samples, endpoint, zoom);
      if (!unit.has_value()) {
        continue;
      }
      for (std::size_t step = 0; step < unit->step_count; ++step) {
        const auto& chord = unit->steps[step];
        out.chords.push_back({chord.first_x, chord.first_y, chord.first_radius, chord.second_x,
                              chord.second_y, chord.second_radius});
      }
    }
    operations.push_back(std::move(out));
  }
  return operations;
}

// Trace -> InkStream -> (optional resample) -> product chunking. Mirrors the
// angularity tool's pipeline, with the resampler inserted between the ink
// stream and the builder exactly where review §9.4 puts it.
std::vector<CommittedOperation> committed_operations(std::span<const vector_v2::TraceEvent> events,
                                                     float brush_size, vector_v2::ZoomLevel zoom,
                                                     float resample_spacing_px,
                                                     std::uint16_t color) {
  const float inverse_scale = 1.0F / zoom_scale_of(zoom);
  tinydraw::InkConfig config;
  config.size = brush_size;
  tinydraw::InkStream ink(config);
  std::vector<vector_v2::CompactOperationSample> storage(65'536);
  vector_v2::ChainedOperationBuilder builder(storage, kProductChunkSampleLimit);
  std::vector<CommittedOperation> operations;

  const auto operation_point = [&](const tinydraw::InkPoint& point) {
    return vector_v2::OperationPoint{
        .world_x = std::clamp(point.position.x * inverse_scale, 0.0F,
                              static_cast<float>(vector_v2::kWorldWidth)),
        .world_y = std::clamp(point.position.y * inverse_scale, 0.0F,
                              static_cast<float>(vector_v2::kWorldHeight)),
        .radius = point.radius * inverse_scale,
        .timestamp_us = point.timestamp_us,
    };
  };
  const auto capture_chunk = [&]() {
    const auto pending = builder.pending_append();
    if (pending.has_value()) {
      operations.push_back(
          {pending->tool, pending->color, {pending->samples.begin(), pending->samples.end()}});
    }
    static_cast<void>(builder.acknowledge_commit());
  };
  const auto commit_stroke = [&](std::span<const tinydraw::InkPoint> stroke) {
    if (stroke.size() < 2U) {
      return;
    }
    if (!builder.begin(vector_v2::OperationTool::kPen, color, 1U, operation_point(stroke[0]))) {
      return;
    }
    for (std::size_t index = 1; index + 1U < stroke.size(); ++index) {
      auto status = builder.add(operation_point(stroke[index]));
      if (status == vector_v2::ChainedOperationStatus::kChunkReady) {
        capture_chunk();
        static_cast<void>(builder.add(operation_point(stroke[index])));
      }
    }
    auto status = builder.finish(operation_point(stroke.back()));
    while (status == vector_v2::ChainedOperationStatus::kChunkReady ||
           status == vector_v2::ChainedOperationStatus::kFinalChunkReady) {
      capture_chunk();
      status = builder.acknowledge_commit();
      if (status == vector_v2::ChainedOperationStatus::kComplete ||
          status == vector_v2::ChainedOperationStatus::kAccepted) {
        break;
      }
    }
  };

  std::vector<tinydraw::InkPoint> stroke;
  for (const vector_v2::TraceEvent& event : events) {
    const tinydraw::TouchPoint touch{
        .x = static_cast<float>(event.x),
        .y = static_cast<float>(event.y),
        .timestamp_us = static_cast<std::uint32_t>(event.t_us),
    };
    if (event.kind == vector_v2::TraceEventKind::kDown) {
      stroke.clear();
      stroke.push_back(ink.begin(touch));
      continue;
    }
    if (stroke.empty()) {
      continue;
    }
    if (event.kind == vector_v2::TraceEventKind::kMove) {
      stroke.push_back(ink.update(touch));
      continue;
    }
    stroke.push_back(ink.finish(touch));
    const auto resampled = resample_arc_length(stroke, resample_spacing_px);
    commit_stroke(resampled);
    stroke.clear();
  }
  return operations;
}

struct Window {
  int x0 = 0;
  int y0 = 0;
  int width = 0;
  int height = 0;
};

// Analytic tapered-capsule coverage renderer: per operation, UNION coverage
// over that operation's exact prepared level-space chords; across operations,
// front-to-back newest-first compositing. This is the ideal the device
// boundary-pixel pass approximates.
struct AaStats {
  std::size_t boundary_pixels = 0;
  std::size_t interior_pixels = 0;
};

AaStats render_analytic(const std::vector<ChordOperation>& operations, const Window& window,
                        std::span<std::uint16_t> out_pixels) {
  const std::size_t pixel_count =
      static_cast<std::size_t>(window.width) * static_cast<std::size_t>(window.height);
  std::vector<float> alpha_acc(pixel_count, 0.0F);
  std::vector<float> red(pixel_count, 0.0F);
  std::vector<float> green(pixel_count, 0.0F);
  std::vector<float> blue(pixel_count, 0.0F);
  std::vector<float> op_alpha(pixel_count, 0.0F);
  AaStats stats;

  for (std::size_t op_index = operations.size(); op_index-- > 0U;) {
    const ChordOperation& operation = operations[op_index];
    std::fill(op_alpha.begin(), op_alpha.end(), 0.0F);
    bool touched = false;
    {
      for (const CapsuleChord& chord : operation.chords) {
        const float ax = chord.ax - static_cast<float>(window.x0);
        const float ay = chord.ay - static_cast<float>(window.y0);
        const float bx = chord.bx - static_cast<float>(window.x0);
        const float by = chord.by - static_cast<float>(window.y0);
        const float r_max = std::max(chord.ar, chord.br);
        const int px0 = std::max(0, static_cast<int>(std::floor(std::min(ax, bx) - r_max - 1.5F)));
        const int py0 = std::max(0, static_cast<int>(std::floor(std::min(ay, by) - r_max - 1.5F)));
        const int px1 =
            std::min(window.width, static_cast<int>(std::ceil(std::max(ax, bx) + r_max + 1.5F)));
        const int py1 =
            std::min(window.height, static_cast<int>(std::ceil(std::max(ay, by) + r_max + 1.5F)));
        const float abx = bx - ax;
        const float aby = by - ay;
        const float length_squared = abx * abx + aby * aby;
        for (int y = py0; y < py1; ++y) {
          for (int x = px0; x < px1; ++x) {
            const float px = static_cast<float>(x) + 0.5F;
            const float py = static_cast<float>(y) + 0.5F;
            const float apx = px - ax;
            const float apy = py - ay;
            const float t = length_squared <= 0.0F
                                ? 0.0F
                                : std::clamp((apx * abx + apy * aby) / length_squared, 0.0F, 1.0F);
            const float dx = apx - t * abx;
            const float dy = apy - t * aby;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float r = chord.ar + (chord.br - chord.ar) * t;
            const float alpha = std::clamp(0.5F + (r - d), 0.0F, 1.0F);
            if (alpha > 0.0F) {
              const std::size_t at =
                  static_cast<std::size_t>(y) * static_cast<std::size_t>(window.width) +
                  static_cast<std::size_t>(x);
              op_alpha[at] = std::max(op_alpha[at], alpha);
              touched = true;
            }
          }
        }
      }
    }
    if (!touched) {
      continue;
    }
    const std::uint16_t rgb565 = operation.eraser ? 0xFFFFU : operation.color;
    const float cr = static_cast<float>(((rgb565 >> 11U) & 0x1FU) * 255U / 31U);
    const float cg = static_cast<float>(((rgb565 >> 5U) & 0x3FU) * 255U / 63U);
    const float cb = static_cast<float>((rgb565 & 0x1FU) * 255U / 31U);
    for (std::size_t at = 0; at < pixel_count; ++at) {
      const float alpha = op_alpha[at];
      if (alpha <= 0.0F) {
        continue;
      }
      stats.boundary_pixels += alpha < 1.0F ? 1U : 0U;
      stats.interior_pixels += alpha >= 1.0F ? 1U : 0U;
      const float contribution = alpha * (1.0F - alpha_acc[at]);
      red[at] += cr * contribution;
      green[at] += cg * contribution;
      blue[at] += cb * contribution;
      alpha_acc[at] += contribution;
    }
  }
  for (std::size_t at = 0; at < pixel_count; ++at) {
    const float remaining = 1.0F - alpha_acc[at];
    const float r = red[at] + 255.0F * remaining;
    const float g = green[at] + 255.0F * remaining;
    const float b = blue[at] + 255.0F * remaining;
    const auto r5 =
        static_cast<std::uint16_t>(std::lround(std::clamp(r, 0.0F, 255.0F) * 31.0F / 255.0F));
    const auto g6 =
        static_cast<std::uint16_t>(std::lround(std::clamp(g, 0.0F, 255.0F) * 63.0F / 255.0F));
    const auto b5 =
        static_cast<std::uint16_t>(std::lround(std::clamp(b, 0.0F, 255.0F) * 31.0F / 255.0F));
    out_pixels[at] = static_cast<std::uint16_t>((r5 << 11U) | (g6 << 5U) | b5);
  }
  return stats;
}

bool render_baseline(const std::vector<CommittedOperation>& operations, vector_v2::ZoomLevel zoom,
                     const Window& window, std::span<std::uint16_t> out_pixels) {
  std::fill(out_pixels.begin(), out_pixels.end(), 0xFFFFU);
  const vector_v2::RasterSurface surface{
      .zoom = zoom,
      .level_bounds = {window.x0, window.y0, window.x0 + window.width, window.y0 + window.height},
      .pixels = out_pixels,
      .stride = window.width,
  };
  for (const CommittedOperation& operation : operations) {
    if (!vector_v2::apply_incremental_operation(
            {.tool = operation.tool, .color = operation.color, .samples = operation.samples},
            surface)) {
      return false;
    }
  }
  return true;
}

class BufferPngOutput final : public tinydraw::PngOutput {
 public:
  bool write(std::size_t offset, std::span<const std::uint8_t> bytes) override {
    if (offset + bytes.size() > buffer_.size()) {
      buffer_.resize(offset + bytes.size());
    }
    std::copy(bytes.begin(), bytes.end(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset));
    return true;
  }
  bool read(std::size_t offset, std::span<std::uint8_t> bytes) override {
    if (offset + bytes.size() > buffer_.size()) {
      return false;
    }
    std::copy_n(buffer_.begin() + static_cast<std::ptrdiff_t>(offset), bytes.size(), bytes.begin());
    return true;
  }
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const { return buffer_; }

 private:
  std::vector<std::uint8_t> buffer_;
};

bool write_png(const std::string& path, std::span<const std::uint16_t> pixels, int width,
               int height) {
  BufferPngOutput output;
  const std::size_t workspace_bytes = tinydraw::png_encoder_workspace_bytes();
  std::vector<std::uint8_t> workspace(workspace_bytes + 64U);
  void* workspace_pointer = workspace.data();
  std::size_t space = workspace.size();
  workspace_pointer = std::align(tinydraw::png_encoder_workspace_alignment(), workspace_bytes,
                                 workspace_pointer, space);
  std::vector<std::uint8_t> row(tinydraw::png_encoder_row_bytes(width));
  const auto result = tinydraw::encode_png_rgb565(pixels, width, height, output, workspace_pointer,
                                                  workspace_bytes, row);
  if (!result.success()) {
    std::fprintf(stderr, "png encode failed: %d\n", result.error);
    return false;
  }
  std::ofstream file(path, std::ios::binary);
  file.write(reinterpret_cast<const char*>(output.bytes().data()),
             static_cast<std::streamsize>(result.bytes_written));
  return file.good();
}

// Nearest-neighbor magnification so hard-edge vs AA differences survive
// image viewers' own smoothing.
std::vector<std::uint16_t> magnify(std::span<const std::uint16_t> pixels, int width, int height,
                                   int factor) {
  const auto wide_width = static_cast<std::size_t>(width) * static_cast<std::size_t>(factor);
  std::vector<std::uint16_t> out(wide_width * static_cast<std::size_t>(height) *
                                 static_cast<std::size_t>(factor));
  for (int y = 0; y < height * factor; ++y) {
    for (int x = 0; x < width * factor; ++x) {
      out[static_cast<std::size_t>(y) * wide_width + static_cast<std::size_t>(x)] =
          pixels[static_cast<std::size_t>(y / factor) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x / factor)];
    }
  }
  return out;
}

Window ink_window(const std::vector<CommittedOperation>& operations, vector_v2::ZoomLevel zoom,
                  int width, int height) {
  float min_x = 1e9F;
  float min_y = 1e9F;
  float max_x = -1e9F;
  float max_y = -1e9F;
  const float scale = zoom_scale_of(zoom);
  for (const CommittedOperation& operation : operations) {
    for (const vector_v2::CompactOperationSample& sample : operation.samples) {
      min_x = std::min(min_x, static_cast<float>(sample.x_quarter) * 0.0625F * scale);
      min_y = std::min(min_y, static_cast<float>(sample.y_quarter) * 0.0625F * scale);
      max_x = std::max(max_x, static_cast<float>(sample.x_quarter) * 0.0625F * scale);
      max_y = std::max(max_y, static_cast<float>(sample.y_quarter) * 0.0625F * scale);
    }
  }
  const int center_x = static_cast<int>((min_x + max_x) * 0.5F);
  const int center_y = static_cast<int>((min_y + max_y) * 0.5F);
  return {std::max(0, center_x - width / 2), std::max(0, center_y - height / 2), width, height};
}

std::optional<std::vector<vector_v2::TraceEvent>> load_trace(const std::string& path) {
  std::ifstream file(path);
  if (!file.good()) {
    return std::nullopt;
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  const std::string text = contents.str();
  std::vector<vector_v2::TraceEvent> events(16'384);
  const auto parsed = vector_v2::parse_ink_trace_csv(text, events);
  if (!parsed.ok()) {
    std::fprintf(stderr, "trace parse failed at line %u\n", static_cast<unsigned>(parsed.line));
    return std::nullopt;
  }
  events.resize(parsed.event_count);
  return events;
}

}  // namespace

int main(int argc, char** argv) {
  std::string trace_path;
  std::string out_prefix = "aa-prototype";
  int zoom_percent = 400;
  float brush_size = 5.0F;
  float resample_px = 0.0F;
  std::uint16_t color = 0x0000U;
  int window_width = 368;
  int window_height = 448;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    const auto next_value = [&]() -> const char* { return index + 1 < argc ? argv[++index] : ""; };
    if (argument == "--zoom") {
      zoom_percent = std::atoi(next_value());
    } else if (argument == "--size") {
      brush_size = static_cast<float>(std::atof(next_value()));
    } else if (argument == "--resample") {
      resample_px = static_cast<float>(std::atof(next_value()));
    } else if (argument == "--out") {
      out_prefix = next_value();
    } else if (argument == "--color") {
      color = static_cast<std::uint16_t>(std::strtoul(next_value(), nullptr, 0));
    } else if (argument == "--window") {
      window_width = std::atoi(next_value());
      window_height = std::atoi(next_value());
    } else {
      trace_path = argument;
    }
  }
  if (trace_path.empty()) {
    std::fprintf(stderr,
                 "usage: %s trace.csv [--zoom 25|50|100|200|400] [--size px] [--resample px] "
                 "[--color rgb565] [--window w h] [--out prefix]\n",
                 argv[0]);
    return 2;
  }
  const auto zoom = zoom_from_percent(zoom_percent);
  if (!zoom.has_value()) {
    std::fprintf(stderr, "invalid zoom %d\n", zoom_percent);
    return 2;
  }
  const auto events = load_trace(trace_path);
  if (!events.has_value()) {
    return 1;
  }

  const auto operations = committed_operations(*events, brush_size, *zoom, resample_px, color);
  if (operations.empty()) {
    std::fprintf(stderr, "trace produced no committed operations\n");
    return 1;
  }
  std::size_t samples = 0;
  for (const CommittedOperation& operation : operations) {
    samples += operation.samples.size();
  }
  const Window window = ink_window(operations, *zoom, window_width, window_height);
  const std::size_t pixel_count =
      static_cast<std::size_t>(window.width) * static_cast<std::size_t>(window.height);
  std::vector<std::uint16_t> baseline(pixel_count);
  std::vector<std::uint16_t> analytic(pixel_count);
  if (!render_baseline(operations, *zoom, window, baseline)) {
    std::fprintf(stderr, "baseline render failed\n");
    return 1;
  }
  const AaStats stats = render_analytic(chords_from_committed(operations, *zoom), window, analytic);
  // The V1-equivalent float-geometry reference: same compositor, unquantized
  // centerline. Any shape difference against the committed AA render is the
  // quarter-world sample quantization (plus chunk-boundary joints).
  std::vector<std::uint16_t> reference(pixel_count);
  static_cast<void>(render_analytic(
      float_reference_operations(*events, brush_size, resample_px, color), window, reference));

  bool ok = write_png(out_prefix + "-baseline.png", baseline, window.width, window.height) &&
            write_png(out_prefix + "-aa.png", analytic, window.width, window.height);
  constexpr int kMagnify = 4;
  const int crop_width = window.width / kMagnify;
  const int crop_height = window.height / kMagnify;
  const int crop_x = (window.width - crop_width) / 2;
  const int crop_y = (window.height - crop_height) / 2;
  std::vector<std::uint16_t> crop(static_cast<std::size_t>(crop_width) *
                                  static_cast<std::size_t>(crop_height));
  const auto write_crop = [&](std::span<const std::uint16_t> source, const std::string& path) {
    for (int y = 0; y < crop_height; ++y) {
      std::copy_n(
          source.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(crop_y + y) *
                                                           static_cast<std::size_t>(window.width) +
                                                       static_cast<std::size_t>(crop_x)),
          crop_width,
          crop.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) *
                                                     static_cast<std::size_t>(crop_width)));
    }
    const auto magnified = magnify(crop, crop_width, crop_height, kMagnify);
    return write_png(path, magnified, crop_width * kMagnify, crop_height * kMagnify);
  };
  ok = ok && write_crop(baseline, out_prefix + "-baseline-x4.png") &&
       write_crop(analytic, out_prefix + "-aa-x4.png") &&
       write_png(out_prefix + "-ref-aa.png", reference, window.width, window.height) &&
       write_crop(reference, out_prefix + "-ref-aa-x4.png");
  if (!ok) {
    return 1;
  }
  std::printf(
      "TINYDRAW_AA_PROTOTYPE trace=%s zoom=%d size=%.1f resample_px=%.2f operations=%zu "
      "samples=%zu window=%d,%d,%dx%d interior_px=%zu boundary_px=%zu boundary_share=%.3f\n",
      trace_path.c_str(), zoom_percent, static_cast<double>(brush_size),
      static_cast<double>(resample_px), operations.size(), samples, window.x0, window.y0,
      window.width, window.height, stats.interior_pixels, stats.boundary_pixels,
      stats.interior_pixels + stats.boundary_pixels == 0U
          ? 0.0
          : static_cast<double>(stats.boundary_pixels) /
                static_cast<double>(stats.interior_pixels + stats.boundary_pixels));
  return 0;
}
