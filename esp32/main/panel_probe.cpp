#include "panel_probe.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "co5300_panel_transport.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinydraw/graphics/world_canvas.h"
#include "tinydraw/vector_v2/panel_staging.h"

namespace tinydraw::esp32 {
namespace {

constexpr int kPanelWidth = 368;
constexpr int kPanelHeight = 448;
constexpr std::size_t kFramePixels =
    static_cast<std::size_t>(kPanelWidth) * static_cast<std::size_t>(kPanelHeight);
constexpr std::size_t kScratchPixels = 16384;

std::int64_t percentile(std::vector<std::int64_t>& sorted, int percent) {
  if (sorted.empty()) {
    return -1;
  }
  const std::size_t rank = (sorted.size() * static_cast<std::size_t>(percent) + 99U) / 100U;
  return sorted[std::min(sorted.size() - 1U, rank == 0U ? 0U : rank - 1U)];
}

void report_distribution(const char* tag, const char* detail, std::vector<std::int64_t> samples) {
  std::sort(samples.begin(), samples.end());
  std::printf("%s %s samples=%lu min_us=%lld p50_us=%lld p95_us=%lld max_us=%lld\n", tag, detail,
              static_cast<unsigned long>(samples.size()),
              static_cast<long long>(samples.empty() ? -1 : samples.front()),
              static_cast<long long>(percentile(samples, 50)),
              static_cast<long long>(percentile(samples, 95)),
              static_cast<long long>(samples.empty() ? -1 : samples.back()));
  std::fflush(stdout);
}

const char* edge_name(TearSignalEdge edge) {
  return edge == TearSignalEdge::kRising ? "rising" : "falling";
}

// P1: TE edge timing from ISR timestamps. Deltas between consecutive selected
// edges give the period distribution; the ISR-to-task-resume delta bounds the
// scheduling latency any edge-synchronized policy inherits.
void probe_tear_signal(Co5300PanelTransport& display, TearSignalEdge edge, int samples) {
  std::vector<std::int64_t> periods;
  std::vector<std::int64_t> resume_latencies;
  std::vector<std::int64_t> highs;
  periods.reserve(static_cast<std::size_t>(samples));
  resume_latencies.reserve(static_cast<std::size_t>(samples));
  highs.reserve(static_cast<std::size_t>(samples));
  std::uint32_t previous_edge_us = 0;
  bool have_previous = false;
  int timeouts = 0;
  for (int index = 0; index < samples; ++index) {
    const auto wait = display.wait_for_tear_edge(edge, 40'000);
    if (!wait.observed) {
      ++timeouts;
      have_previous = false;
      continue;
    }
    if (have_previous) {
      periods.push_back(static_cast<std::int64_t>(wait.isr_timestamp_us - previous_edge_us));
    }
    previous_edge_us = wait.isr_timestamp_us;
    have_previous = true;
    resume_latencies.push_back(
        static_cast<std::int64_t>(wait.task_resume_timestamp_us - wait.isr_timestamp_us));
    const auto timing = display.tear_signal_timing();
    if (timing.high_us > 0) {
      highs.push_back(timing.high_us);
    }
  }
  std::printf("TINYDRAW_PROBE_TE edge=%s requested=%d timeouts=%d\n", edge_name(edge), samples,
              timeouts);
  report_distribution("TINYDRAW_PROBE_TE_PERIOD", edge_name(edge), std::move(periods));
  report_distribution("TINYDRAW_PROBE_TE_RESUME", edge_name(edge), std::move(resume_latencies));
  report_distribution("TINYDRAW_PROBE_TE_HIGH", edge_name(edge), std::move(highs));
}

// P2: staging bandwidth. Every outgoing pixel crosses PSRAM -> internal DMA
// memory with a byte swap; this measures that floor without any panel work.
void probe_staging_bandwidth(const std::uint16_t* frame, std::uint16_t* scratch) {
  constexpr int kRepetitions = 20;
  const auto measure = [&](const char* kind, auto&& stage_chunk) {
    const std::int64_t started = esp_timer_get_time();
    for (int repetition = 0; repetition < kRepetitions; ++repetition) {
      std::size_t offset = 0;
      while (offset < kFramePixels) {
        const std::size_t pixels = std::min(kScratchPixels, kFramePixels - offset);
        stage_chunk(frame + offset, pixels);
        offset += pixels;
      }
    }
    const std::int64_t elapsed = esp_timer_get_time() - started;
    const double frame_us = static_cast<double>(elapsed) / kRepetitions;
    const double bytes = static_cast<double>(kFramePixels) * 2.0;
    std::printf("TINYDRAW_PROBE_STAGING kind=%s frame_us=%.0f mb_per_s=%.1f\n", kind, frame_us,
                bytes / frame_us);
    std::fflush(stdout);
  };
  measure("memcpy", [&](const std::uint16_t* source, std::size_t pixels) {
    std::copy_n(source, pixels, scratch);
  });
  measure("swap", [&](const std::uint16_t* source, std::size_t pixels) {
    vector_v2::stage_pixels_swapped(source, scratch, static_cast<int>(pixels));
  });
  // Ring staging visits whole rows with a rotation; measure a full frame of
  // row-shaped work with a nonzero shift so the wrap path is exercised.
  const std::int64_t started = esp_timer_get_time();
  for (int repetition = 0; repetition < kRepetitions; ++repetition) {
    for (int row = 0; row < kPanelHeight; ++row) {
      vector_v2::stage_ring_row(frame + static_cast<std::ptrdiff_t>(row) * kPanelWidth, kPanelWidth,
                                120, 0, kPanelWidth, scratch);
    }
  }
  const std::int64_t elapsed = esp_timer_get_time() - started;
  const double frame_us = static_cast<double>(elapsed) / kRepetitions;
  std::printf("TINYDRAW_PROBE_STAGING kind=ring_swap frame_us=%.0f mb_per_s=%.1f\n", frame_us,
              static_cast<double>(kFramePixels) * 2.0 / frame_us);
  std::fflush(stdout);
}

// P3/P4: full-frame throughput, windowed pushes versus one continuation
// stream. The strip-size sweep separates fixed per-transaction cost from
// per-pixel cost; the two paths differ only in window setup per strip.
void probe_frame_throughput(Co5300PanelTransport& display, const std::uint16_t* frame) {
  constexpr std::array kStripRows{8, 16, 32, 44};
  constexpr int kRepetitions = 10;
  for (const bool streamed : {false, true}) {
    for (const int strip_rows : kStripRows) {
      std::vector<std::int64_t> walls;
      walls.reserve(kRepetitions);
      bool passed = true;
      for (int repetition = 0; repetition < kRepetitions && passed; ++repetition) {
        passed = display.wait_for_all(100'000);
        const std::int64_t started = esp_timer_get_time();
        if (streamed) {
          passed =
              passed && display.stream_rect(0, 0, kPanelWidth, kPanelHeight, frame, 0, strip_rows);
        } else {
          for (int row = 0; row < kPanelHeight && passed; row += strip_rows) {
            const int rows = std::min(strip_rows, kPanelHeight - row);
            display.push_rect(0, row, kPanelWidth, rows,
                              frame + static_cast<std::ptrdiff_t>(row) * kPanelWidth, kPanelWidth);
          }
        }
        passed = passed && display.wait_for_all(200'000);
        walls.push_back(esp_timer_get_time() - started);
      }
      const int transactions = (kPanelHeight + strip_rows - 1) / strip_rows;
      std::printf("TINYDRAW_PROBE_FRAME path=%s strip_rows=%d transactions=%d pass=%u\n",
                  streamed ? "stream_continuation" : "windowed_push", strip_rows, transactions,
                  passed);
      report_distribution("TINYDRAW_PROBE_FRAME_WALL",
                          streamed ? "stream_continuation" : "windowed_push", std::move(walls));
    }
  }
}

// P5: TE-synchronized full-frame cadence. Alternating solid frames start at
// the selected edge; edge-to-DMA-complete against the measured period answers
// whether whole-frame-per-period presentation is mechanically available.
// DMA completion is not optical visibility; tear correctness of this exact
// mode is a Block B camera cell.
void probe_te_synced_stream(Co5300PanelTransport& display, TearSignalEdge edge,
                            const std::uint16_t* frame_a, const std::uint16_t* frame_b,
                            int region_height) {
  constexpr int kFrames = 120;
  std::vector<std::int64_t> edge_to_complete;
  std::vector<std::int64_t> frame_intervals;
  edge_to_complete.reserve(kFrames);
  frame_intervals.reserve(kFrames);
  int edge_failures = 0;
  int stream_failures = 0;
  std::int64_t previous_complete = 0;
  for (int index = 0; index < kFrames; ++index) {
    const auto wait = display.wait_for_tear_edge(edge, 40'000);
    if (!wait.observed) {
      ++edge_failures;
      continue;
    }
    const std::uint32_t edge_us = wait.isr_timestamp_us;
    const bool streamed = display.stream_rect(0, 0, kPanelWidth, region_height,
                                              (index & 1) == 0 ? frame_a : frame_b, 0, 44);
    const bool completed = streamed && display.wait_for_all(100'000);
    const std::int64_t complete_us = esp_timer_get_time();
    if (!completed) {
      ++stream_failures;
      continue;
    }
    edge_to_complete.push_back(
        static_cast<std::int64_t>(static_cast<std::uint32_t>(complete_us) - edge_us));
    if (previous_complete != 0) {
      frame_intervals.push_back(complete_us - previous_complete);
    }
    previous_complete = complete_us;
  }
  std::printf(
      "TINYDRAW_PROBE_TE_SYNC edge=%s rows=%d frames=%d edge_failures=%d stream_failures=%d\n",
      edge_name(edge), region_height, kFrames, edge_failures, stream_failures);
  report_distribution("TINYDRAW_PROBE_TE_SYNC_COMPLETE", edge_name(edge),
                      std::move(edge_to_complete));
  report_distribution("TINYDRAW_PROBE_TE_SYNC_INTERVAL", edge_name(edge),
                      std::move(frame_intervals));
}

// P6a: read-path control. Registers with known nonzero values (brightness is
// initialized to 0xFF, power mode reports display-on bits, ID should be
// non-blank) separate "QSPI reads are broken" from "scanline reports zero".
void probe_read_controls(Co5300PanelTransport& display) {
  struct Control {
    std::uint8_t command;
    std::size_t length;
    const char* name;
  };
  constexpr std::array<Control, 6> kControls{{
      {0x04, 3, "rddid"},
      {0x09, 4, "rddst"},
      {0x0A, 1, "rddpm"},
      {0x0B, 1, "rddmadctl"},
      {0x0C, 1, "rddcolmod"},
      {0x52, 1, "brightness"},
  }};
  for (const auto& control : kControls) {
    std::array<std::uint8_t, 4> value{};
    const bool read = display.read_register(control.command, value.data(), control.length);
    std::printf("TINYDRAW_PROBE_READ register=%s command=0x%02X ok=%u value=%02X%02X%02X%02X\n",
                control.name, control.command, read, value[0], value[1], value[2], value[3]);
  }
  std::fflush(stdout);
}

// P6b: GETSCANLINE (0x45) sweep. Values that advance monotonically with delay
// after the edge would provide a software beam-position oracle; a failed or
// constant read is reported as-is.
void probe_scanline(Co5300PanelTransport& display, TearSignalEdge edge) {
  constexpr std::array kDelaysUs{0,     1'000,  2'000,  4'000,  6'000,
                                 8'000, 10'000, 12'000, 14'000, 16'000};
  for (int round = 0; round < 3; ++round) {
    for (const int delay_us : kDelaysUs) {
      const auto wait = display.wait_for_tear_edge(edge, 40'000);
      if (!wait.observed) {
        std::printf("TINYDRAW_PROBE_SCANLINE round=%d delay_us=%d edge=timeout\n", round, delay_us);
        continue;
      }
      if (delay_us > 0) {
        esp_rom_delay_us(static_cast<std::uint32_t>(delay_us));
      }
      const std::int64_t read_started = esp_timer_get_time();
      const int scanline = display.read_scanline();
      const std::int64_t read_us = esp_timer_get_time() - read_started;
      std::printf("TINYDRAW_PROBE_SCANLINE round=%d delay_us=%d value=%d read_us=%lld\n", round,
                  delay_us, scanline, static_cast<long long>(read_us));
    }
  }
  std::fflush(stdout);
}

#ifndef TINYDRAW_PANEL_PROBE_CELL
#define TINYDRAW_PANEL_PROBE_CELL 0
#endif

// ---- Block B optical cells -------------------------------------------------
//
// Camera-facing patterns. The classifier registers each video frame via the
// four corner fiducials, reads the cell identity strip, then classifies the
// central field per row as frame color A or B. Normal scan-in appears as one
// downward-moving A/B boundary; a tear is a static split, an upward-moving
// boundary, or multiple simultaneous boundaries. Guard columns expose white
// edge notches against solid blue.

void fill_rect(std::uint16_t* frame, int x, int y, int width, int height, std::uint16_t color) {
  for (int row = y; row < y + height; ++row) {
    std::fill_n(frame + static_cast<std::ptrdiff_t>(row) * kPanelWidth + x, width, color);
  }
}

void paint_cell_pattern(std::uint16_t* frame, std::uint16_t field_color, int cell) {
  constexpr std::uint16_t kWhite = 0xFFFF;
  constexpr std::uint16_t kBlack = 0x0000;
  constexpr std::uint16_t kGuardBlue = 0x001F;
  std::fill_n(frame, kFramePixels, field_color);
  // Guard columns: white notches are unmistakable against saturated blue.
  fill_rect(frame, 0, 0, 8, kPanelHeight, kGuardBlue);
  fill_rect(frame, kPanelWidth - 8, 0, 8, kPanelHeight, kGuardBlue);
  // Corner fiducials: 40x40 white with 24x24 black core, inset 12 px.
  for (const auto& [corner_x, corner_y] :
       {std::pair{12, 12}, std::pair{kPanelWidth - 52, 12}, std::pair{12, kPanelHeight - 52},
        std::pair{kPanelWidth - 52, kPanelHeight - 52}}) {
    fill_rect(frame, corner_x, corner_y, 40, 40, kWhite);
    fill_rect(frame, corner_x + 8, corner_y + 8, 24, 24, kBlack);
  }
  // Cell identity strip: five 24 px blocks, MSB first, white=1 on black.
  fill_rect(frame, 120, 8, 5 * 24 + 8, 32, kBlack);
  for (int bit = 0; bit < 5; ++bit) {
    if ((cell >> (4 - bit)) & 1) {
      fill_rect(frame, 124 + bit * 24, 12, 20, 24, kWhite);
    }
  }
}

struct OpticalCellSpec {
  int cell = 0;
  const char* name = "";
  const char* expected = "";
};

constexpr std::array<OpticalCellSpec, 6> kOpticalCells{{
    {0, "software-suite", "n/a"},
    {1, "boundary-rising-full", "unknown_under_test"},
    {2, "boundary-falling-full", "unknown_under_test"},
    {3, "midframe-wrap-rising", "tear_mechanism_control"},
    {4, "freerun-unsynced", "must_tear_positive_control"},
    {5, "boundary-rising-canvas368", "unknown_under_test"},
}};

void run_optical_cell(Co5300PanelTransport& display, std::uint16_t* frame_a, std::uint16_t* frame_b,
                      int cell, std::int64_t duration_us) {
  const auto& spec = kOpticalCells[static_cast<std::size_t>(cell)];
  paint_cell_pattern(frame_a, 0xF800, cell);  // A = red
  paint_cell_pattern(frame_b, 0x07E0, cell);  // B = green
  const int region_rows = cell == 5 ? 368 : kPanelHeight;
  const std::int64_t kCellDurationUs = duration_us;
  std::printf("TINYDRAW_PROBE_CELL_START cell=%d name=%s expected=%s rows=%d duration_s=%lld\n",
              cell, spec.name, spec.expected, region_rows,
              static_cast<long long>(duration_us / 1'000'000));
  std::fflush(stdout);

  std::vector<std::int64_t> intervals;
  intervals.reserve(4096);
  int frames = 0;
  int edge_failures = 0;
  int stream_failures = 0;
  std::int64_t previous_complete = 0;
  const std::int64_t cell_started = esp_timer_get_time();
  std::int64_t next_progress = cell_started + 5'000'000;
  while (esp_timer_get_time() - cell_started < kCellDurationUs) {
    const std::uint16_t* frame = (frames & 1) == 0 ? frame_a : frame_b;
    bool streamed = false;
    switch (cell) {
      case 1:
      case 5: {
        const auto wait = display.wait_for_tear_edge(TearSignalEdge::kRising, 40'000);
        if (!wait.observed) {
          ++edge_failures;
          continue;
        }
        streamed = display.stream_rect(0, 0, kPanelWidth, region_rows, frame, 0, 44);
        break;
      }
      case 2: {
        const auto wait = display.wait_for_tear_edge(TearSignalEdge::kFalling, 40'000);
        if (!wait.observed) {
          ++edge_failures;
          continue;
        }
        streamed = display.stream_rect(0, 0, kPanelWidth, region_rows, frame, 0, 44);
        break;
      }
      case 3: {
        // Mechanism control: start mid-frame behind the modeled beam with a
        // wrapped second band, at the real 40 MHz rates. Not a claim of exact
        // V2 replication; it reproduces the mid-frame start + wrap shape.
        const auto wait = display.wait_for_tear_edge(TearSignalEdge::kRising, 40'000);
        if (!wait.observed) {
          ++edge_failures;
          continue;
        }
        esp_rom_delay_us(8'000);        // beam ~row 213 by the 26.7 rows/ms model
        constexpr int kStartRow = 166;  // modeled beam minus 48-row margin
        streamed = display.stream_rect(0, kStartRow, kPanelWidth, kPanelHeight - kStartRow,
                                       frame + static_cast<std::ptrdiff_t>(kStartRow) * kPanelWidth,
                                       0, 44) &&
                   display.stream_rect(0, 0, kPanelWidth, kStartRow, frame, 0, 44);
        break;
      }
      case 4:
        // Positive control: free-running, deliberately unsynchronized.
        streamed = display.stream_rect(0, 0, kPanelWidth, region_rows, frame, 0, 44);
        break;
      default:
        return;
    }
    const bool completed = streamed && display.wait_for_all(100'000);
    const std::int64_t complete_us = esp_timer_get_time();
    if (!completed) {
      ++stream_failures;
      continue;
    }
    ++frames;
    if (previous_complete != 0) {
      intervals.push_back(complete_us - previous_complete);
    }
    previous_complete = complete_us;
    if (complete_us >= next_progress) {
      std::printf("TINYDRAW_PROBE_CELL_PROGRESS cell=%d frames=%d edge_failures=%d\n", cell, frames,
                  edge_failures);
      std::fflush(stdout);
      next_progress += 5'000'000;
    }
  }
  std::printf(
      "TINYDRAW_PROBE_CELL_DONE cell=%d name=%s frames=%d edge_failures=%d "
      "stream_failures=%d\n",
      cell, spec.name, frames, edge_failures, stream_failures);
  report_distribution("TINYDRAW_PROBE_CELL_INTERVAL", spec.name, std::move(intervals));
}

}  // namespace

void run_panel_probe() {
  std::printf("TINYDRAW_PANEL_PROBE_START clock_mhz=%d\n", kCo5300ClockMHz);
  Co5300PanelTransport display;
  if (!display.ready()) {
    std::printf("TINYDRAW_PANEL_PROBE_DONE pass=0 reason=display\n");
    return;
  }

  auto* frame_a = static_cast<std::uint16_t*>(
      heap_caps_malloc(kFramePixels * sizeof(std::uint16_t), MALLOC_CAP_SPIRAM));
  auto* frame_b = static_cast<std::uint16_t*>(
      heap_caps_malloc(kFramePixels * sizeof(std::uint16_t), MALLOC_CAP_SPIRAM));
  auto* scratch = static_cast<std::uint16_t*>(heap_caps_malloc(
      kScratchPixels * sizeof(std::uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (frame_a == nullptr || frame_b == nullptr || scratch == nullptr) {
    std::printf("TINYDRAW_PANEL_PROBE_DONE pass=0 reason=memory\n");
    return;
  }
  // Solid red/green RGB565: unambiguous on glass and cheap to verify.
  std::fill_n(frame_a, kFramePixels, static_cast<std::uint16_t>(0xF800));
  std::fill_n(frame_b, kFramePixels, static_cast<std::uint16_t>(0x07E0));

  // Let the panel settle and the TE ISR accumulate edges before sampling.
  vTaskDelay(pdMS_TO_TICKS(200));

#if TINYDRAW_PANEL_PROBE_CELL == 6
  // One-take cycle for a single continuous handheld video. Positive control
  // first; blue interstitials mark segment boundaries for the classifier.
  // Loops forever so the camera operator can film any complete pass.
  constexpr std::array kCycleOrder{4, 1, 5, 2, 3};
  for (int pass = 1;; ++pass) {
    std::printf("TINYDRAW_PROBE_CYCLE_PASS pass=%d\n", pass);
    std::fflush(stdout);
    for (const int cell : kCycleOrder) {
      std::fill_n(frame_a, kFramePixels, static_cast<std::uint16_t>(0x001F));
      static_cast<void>(display.stream_rect(0, 0, kPanelWidth, kPanelHeight, frame_a, 0, 44));
      static_cast<void>(display.wait_for_all(100'000));
      vTaskDelay(pdMS_TO_TICKS(1'000));
      // 12 s ~= 350 panel updates and ~2,900 video frames per cell: enough
      // for gross verdicts. Rare-tear sensitivity comes from a targeted soak
      // of the winning cell, not from long cells in every pass.
      run_optical_cell(display, frame_a, frame_b, cell, 12'000'000);
    }
  }
#elif TINYDRAW_PANEL_PROBE_CELL != 0
  run_optical_cell(display, frame_a, frame_b, TINYDRAW_PANEL_PROBE_CELL, 45'000'000);
  heap_caps_free(scratch);
  heap_caps_free(frame_b);
  heap_caps_free(frame_a);
  std::printf("TINYDRAW_PANEL_PROBE_DONE pass=1\n");
  std::fflush(stdout);
  return;
#endif

  probe_tear_signal(display, TearSignalEdge::kRising, 350);
  probe_tear_signal(display, TearSignalEdge::kFalling, 350);
  probe_staging_bandwidth(frame_a, scratch);
  probe_frame_throughput(display, frame_a);
  for (const int rows : {448, 424, 400, 368}) {
    probe_te_synced_stream(display, TearSignalEdge::kRising, frame_a, frame_b, rows);
  }
  probe_read_controls(display);
  probe_scanline(display, TearSignalEdge::kRising);

  heap_caps_free(scratch);
  heap_caps_free(frame_b);
  heap_caps_free(frame_a);
  std::printf("TINYDRAW_PANEL_PROBE_DONE pass=1\n");
  std::fflush(stdout);
}

}  // namespace tinydraw::esp32
