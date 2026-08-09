#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <vector>

#include "input_coordinates.h"
#include "tinydraw/geometry.h"
#include "tinydraw/graphics/ribbon_renderer.h"
#include "tinydraw/graphics/stroke_raster.h"
#include "tinydraw/graphics/tile_undo_history.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/ui/toolbar.h"

namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr std::uint16_t kGrid = 0xDEDBU;
constexpr std::uint16_t kReplayInk = 0x001FU;
// A 1.8-inch 368x448 panel is about 1.14 x 1.39 inches. On the 254-PPI Retina
// panel of a 14-inch 2021 MacBook Pro at default scaling, SDL uses 127 points per inch.
constexpr int kPhysicalWindowWidth = 145;
constexpr int kPhysicalWindowHeight = 177;

std::optional<tinydraw::Point> mouse_to_logical(int x, int y) {
  return tinydraw::host::event_to_logical({.x = static_cast<float>(x), .y = static_cast<float>(y)});
}

void set_pixel(std::vector<std::uint16_t>& pixels, int x, int y, std::uint16_t color) {
  if (x < 0 || x >= tinydraw::kCanvasWidth || y < 0 || y >= tinydraw::kCanvasHeight) {
    return;
  }
  const auto index = static_cast<std::size_t>(y * tinydraw::kCanvasWidth + x);
  pixels[index] = color;
}

void apply_ribbon_update(std::vector<tinydraw::RibbonPrimitive>& geometry,
                         std::size_t& committed_count, const tinydraw::RibbonUpdate& update) {
  geometry.resize(committed_count);
  geometry.insert(geometry.end(), update.committed.begin(), update.committed.end());
  committed_count = geometry.size();
  geometry.insert(geometry.end(), update.provisional.begin(), update.provisional.end());
}

void draw_ribbon(std::vector<std::uint16_t>& pixels,
                 std::span<const tinydraw::RibbonPrimitive> primitives, std::uint16_t color) {
  static tinydraw::RibbonRenderer renderer;
  static_cast<void>(
      renderer.render(primitives, pixels, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight, color));
}

void clear_canvas(std::vector<std::uint16_t>& pixels, bool show_grid) {
  std::fill(pixels.begin(), pixels.end(), kBackground);
  if (!show_grid) {
    return;
  }
  for (int y = 0; y < tinydraw::kCanvasHeight; y += 64) {
    for (int x = 0; x < tinydraw::kCanvasWidth; ++x) {
      set_pixel(pixels, x, y, kGrid);
    }
  }
  for (int x = 0; x < tinydraw::kCanvasWidth; x += 64) {
    for (int y = 0; y < tinydraw::kCanvasHeight; ++y) {
      set_pixel(pixels, x, y, kGrid);
    }
  }
}

bool write_ppm(const std::string& path, const std::vector<std::uint16_t>& pixels) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    return false;
  }
  output << "P6\n" << tinydraw::kCanvasWidth << ' ' << tinydraw::kCanvasHeight << "\n255\n";
  std::vector<char> rgb;
  rgb.reserve(pixels.size() * 3U);
  for (const std::uint16_t pixel : pixels) {
    const auto red = static_cast<unsigned char>(((pixel >> 11U) & 0x1FU) * 255U / 31U);
    const auto green = static_cast<unsigned char>(((pixel >> 5U) & 0x3FU) * 255U / 63U);
    const auto blue = static_cast<unsigned char>((pixel & 0x1FU) * 255U / 31U);
    rgb.push_back(static_cast<char>(red));
    rgb.push_back(static_cast<char>(green));
    rgb.push_back(static_cast<char>(blue));
  }
  output.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
  return output.good();
}

int replay(const std::string& input_path, const std::string& output_path) {
  std::ifstream input(input_path);
  if (!input) {
    std::fprintf(stderr, "cannot open replay: %s\n", input_path.c_str());
    return EXIT_FAILURE;
  }

  std::vector<std::uint16_t> pixels(
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight));
  clear_canvas(pixels, false);
  tinydraw::InkStream stream;
  tinydraw::RibbonStream ribbon;
  std::vector<tinydraw::RibbonPrimitive> geometry;
  std::size_t committed_count = 0U;
  std::size_t point_count = 0;

  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) {
      continue;
    }

    std::istringstream fields(line);
    std::string action;
    std::string trailing;
    tinydraw::TouchPoint touch{};
    if (!(fields >> action >> touch.x >> touch.y >> touch.timestamp_us) || (fields >> trailing)) {
      std::fprintf(stderr, "invalid replay syntax on line %zu\n", line_number);
      return EXIT_FAILURE;
    }

    if (action == "down" && !stream.active()) {
      geometry.clear();
      committed_count = 0U;
      apply_ribbon_update(geometry, committed_count, ribbon.append(stream.begin(touch)));
    } else if ((action == "move" || action == "up") && stream.active()) {
      const auto update = action == "up" ? ribbon.finish(stream.finish(touch))
                                         : ribbon.append(stream.update(touch));
      apply_ribbon_update(geometry, committed_count, update);
      if (action == "up") {
        draw_ribbon(pixels, geometry, kReplayInk);
      }
    } else {
      std::fprintf(stderr, "invalid replay lifecycle on line %zu\n", line_number);
      return EXIT_FAILURE;
    }
    ++point_count;
  }

  if (stream.active() || point_count == 0U) {
    std::fprintf(stderr, "incomplete replay: %s\n", input_path.c_str());
    return EXIT_FAILURE;
  }
  if (!write_ppm(output_path, pixels)) {
    std::fprintf(stderr, "cannot write replay image: %s\n", output_path.c_str());
    return EXIT_FAILURE;
  }
  std::printf("replayed %zu points to %s\n", point_count, output_path.c_str());
  return EXIT_SUCCESS;
}

int ui_preview(const std::string& output_path) {
  std::vector<std::uint16_t> pixels(
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight));
  clear_canvas(pixels, true);
  tinydraw::draw_toolbar(pixels, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight, {});
  if (!write_ppm(output_path, pixels)) {
    std::fprintf(stderr, "cannot write UI preview: %s\n", output_path.c_str());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

bool draw_test_stroke(tinydraw::StrokeRaster& raster, tinydraw::TileUndoHistory& history,
                      tinydraw::Point start, tinydraw::Point end, std::uint16_t color,
                      std::uint32_t timestamp_us) {
  tinydraw::InkStream ink;
  tinydraw::RibbonStream ribbon;
  static_cast<void>(raster.update(
      ribbon.append(ink.begin({.x = start.x, .y = start.y, .timestamp_us = timestamp_us})), color));
  static_cast<void>(
      raster.update(ribbon.append(ink.update({.x = (start.x + end.x) * 0.5F,
                                              .y = (start.y + end.y) * 0.5F,
                                              .timestamp_us = timestamp_us + 8'000U})),
                    color));
  static_cast<void>(raster.finish(
      ribbon.finish(ink.finish({.x = end.x, .y = end.y, .timestamp_us = timestamp_us + 16'000U})),
      color, &history));
  return history.can_undo();
}

int undo_e2e() {
  std::vector<std::uint16_t> committed(
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight));
  clear_canvas(committed, true);
  const auto blank = committed;
  std::vector<std::uint16_t> visible = committed;
  std::vector<std::uint8_t> coverage(committed.size(), 0U);
  std::vector<std::uint16_t> undo_storage(tinydraw::TileUndoHistory::kRequiredPixels);
  tinydraw::TileUndoHistory history(undo_storage);
  tinydraw::StrokeRaster raster(committed, visible, coverage);
  tinydraw::ToolbarState toolbar;

  const auto undo = [&] {
    static_cast<void>(history.undo(committed, visible));
    toolbar.can_undo = history.can_undo();
  };
  const auto draw = [&](tinydraw::Point start, tinydraw::Point end, std::uint16_t color,
                        std::uint32_t timestamp_us) {
    toolbar.can_undo = draw_test_stroke(raster, history, start, end, color, timestamp_us);
  };

  draw({30.0F, 40.0F}, {140.0F, 110.0F}, kReplayInk, 1'000U);
  const auto first_stroke = committed;
  draw({220.0F, 80.0F}, {320.0F, 180.0F}, 0xF800U, 20'000U);
  const auto second_stroke = committed;
  undo();
  const bool undid_second = committed == first_stroke && visible == committed && toolbar.can_undo;
  undo();
  const bool undid_first = committed == blank && visible == committed && !toolbar.can_undo;

  draw({30.0F, 40.0F}, {140.0F, 110.0F}, kReplayInk, 40'000U);
  draw({30.0F, 40.0F}, {140.0F, 110.0F}, kBackground, 60'000U);
  const bool erased = committed != first_stroke;
  undo();
  const bool undid_eraser = committed == first_stroke && toolbar.can_undo;

  history.begin_entry();
  history.capture_canvas(committed);
  static_cast<void>(history.commit_entry());
  clear_canvas(committed, true);
  visible = committed;
  toolbar.can_undo = history.can_undo();
  const bool started_new = committed == blank && toolbar.can_undo;
  undo();
  const bool undid_new = committed == first_stroke && visible == committed && toolbar.can_undo;

  if (first_stroke == blank || second_stroke == first_stroke || !undid_second || !undid_first ||
      !erased || !undid_eraser || !started_new || !undid_new) {
    std::fprintf(stderr, "dirty-tile undo E2E failed\n");
    return EXIT_FAILURE;
  }
  std::printf("TINYDRAW_UNDO_OK depth=10 draw=1 erase=1 new=1 exact=1\n");
  return EXIT_SUCCESS;
}

int interactive() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  SDL_Window* window = SDL_CreateWindow(
      "TinyDraw host — drag to test ink input", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      kPhysicalWindowWidth, kPhysicalWindowHeight, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* renderer = window == nullptr ? nullptr : SDL_CreateRenderer(window, -1, 0);
  SDL_Texture* texture =
      renderer == nullptr
          ? nullptr
          : SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                              tinydraw::kCanvasWidth, tinydraw::kCanvasHeight);
  if (window == nullptr || renderer == nullptr || texture == nullptr) {
    std::fprintf(stderr, "SDL setup failed: %s\n", SDL_GetError());
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_FAILURE;
  }

  SDL_RenderSetLogicalSize(renderer, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight);
  SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);

  std::vector<std::uint16_t> committed_pixels(
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight));
  clear_canvas(committed_pixels, true);
  std::vector<std::uint16_t> pixels = committed_pixels;
  std::vector<std::uint16_t> undo_storage(tinydraw::TileUndoHistory::kRequiredPixels);
  tinydraw::TileUndoHistory undo_history(undo_storage);
  std::vector<std::uint8_t> active_coverage(
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight), 0U);
  tinydraw::StrokeRaster stroke_raster(committed_pixels, pixels, active_coverage);
  tinydraw::ToolbarState toolbar;
  tinydraw::InkConfig initial_brush;
  initial_brush.size = tinydraw::brush_size(toolbar.size);
  tinydraw::InkStream stream(initial_brush);
  tinydraw::RibbonStream ribbon;
  tinydraw::InkPoint last_ink_point{};
  std::uint16_t stroke_color = tinydraw::rgb565(toolbar.color);

  const auto close_popups = [&] {
    toolbar.colors_open = false;
    toolbar.sizes_open = false;
  };
  const auto select_size = [&](tinydraw::PenSize size) {
    toolbar.size = size;
    tinydraw::InkConfig config = stream.config();
    config.size = tinydraw::brush_size(size);
    stream.set_config(config);
    close_popups();
  };
  const auto reset_active_stroke = [&] {
    stream.end();
    ribbon.reset();
    stroke_raster.cancel();
  };
  const auto start_new_drawing = [&] {
    reset_active_stroke();
    undo_history.begin_entry();
    undo_history.capture_canvas(committed_pixels);
    static_cast<void>(undo_history.commit_entry());
    clear_canvas(committed_pixels, true);
    pixels = committed_pixels;
    toolbar.can_undo = undo_history.can_undo();
    close_popups();
  };
  const auto undo = [&] {
    if (!undo_history.can_undo()) {
      return;
    }
    reset_active_stroke();
    static_cast<void>(undo_history.undo(committed_pixels, pixels));
    toolbar.can_undo = undo_history.can_undo();
    close_popups();
  };

  bool running = true;
  while (running) {
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT ||
          (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
        running = false;
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_c) {
        start_new_drawing();
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_z &&
                 (event.key.keysym.mod & KMOD_GUI) != 0) {
        undo();
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        const auto point = mouse_to_logical(event.button.x, event.button.y);
        if (!point.has_value()) {
          continue;
        }
        if (tinydraw::toolbar_contains(*point, toolbar)) {
          pixels = committed_pixels;
          switch (tinydraw::toolbar_action_at(*point, toolbar)) {
            case tinydraw::ToolbarAction::kSelectPen:
              toolbar.tool = tinydraw::DrawingTool::kPen;
              close_popups();
              break;
            case tinydraw::ToolbarAction::kSelectEraser:
              toolbar.tool = tinydraw::DrawingTool::kEraser;
              close_popups();
              break;
            case tinydraw::ToolbarAction::kSelectBlack:
              toolbar.color = tinydraw::InkColor::kBlack;
              toolbar.tool = tinydraw::DrawingTool::kPen;
              toolbar.colors_open = false;
              break;
            case tinydraw::ToolbarAction::kSelectBlue:
              toolbar.color = tinydraw::InkColor::kBlue;
              toolbar.tool = tinydraw::DrawingTool::kPen;
              toolbar.colors_open = false;
              break;
            case tinydraw::ToolbarAction::kSelectRed:
              toolbar.color = tinydraw::InkColor::kRed;
              toolbar.tool = tinydraw::DrawingTool::kPen;
              toolbar.colors_open = false;
              break;
            case tinydraw::ToolbarAction::kSelectGreen:
              toolbar.color = tinydraw::InkColor::kGreen;
              toolbar.tool = tinydraw::DrawingTool::kPen;
              toolbar.colors_open = false;
              break;
            case tinydraw::ToolbarAction::kToggleColors:
              toolbar.colors_open = !toolbar.colors_open;
              toolbar.sizes_open = false;
              break;
            case tinydraw::ToolbarAction::kToggleSizes:
              toolbar.sizes_open = !toolbar.sizes_open;
              toolbar.colors_open = false;
              break;
            case tinydraw::ToolbarAction::kSelectSmall:
              select_size(tinydraw::PenSize::kSmall);
              break;
            case tinydraw::ToolbarAction::kSelectMedium:
              select_size(tinydraw::PenSize::kMedium);
              break;
            case tinydraw::ToolbarAction::kSelectLarge:
              select_size(tinydraw::PenSize::kLarge);
              break;
            case tinydraw::ToolbarAction::kSelectExtraLarge:
              select_size(tinydraw::PenSize::kExtraLarge);
              break;
            case tinydraw::ToolbarAction::kUndo:
              undo();
              break;
            case tinydraw::ToolbarAction::kNewDrawing:
              start_new_drawing();
              break;
            case tinydraw::ToolbarAction::kNone:
              break;
          }
          continue;
        }
        close_popups();
        stroke_color = toolbar.tool == tinydraw::DrawingTool::kEraser
                           ? kBackground
                           : tinydraw::rgb565(toolbar.color);
        last_ink_point = stream.begin(
            {.x = point->x, .y = point->y, .timestamp_us = event.button.timestamp * 1'000U});
        static_cast<void>(stroke_raster.update(ribbon.append(last_ink_point), stroke_color));
      } else if (event.type == SDL_MOUSEMOTION && stream.active()) {
        const auto point = mouse_to_logical(event.motion.x, event.motion.y);
        if (point.has_value()) {
          last_ink_point = stream.update(
              {.x = point->x, .y = point->y, .timestamp_us = event.motion.timestamp * 1'000U});
          static_cast<void>(stroke_raster.update(ribbon.append(last_ink_point), stroke_color));
        }
      } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT &&
                 stream.active()) {
        const auto mapped = mouse_to_logical(event.button.x, event.button.y);
        const tinydraw::Point point = mapped.value_or(last_ink_point.position);
        last_ink_point = stream.finish(
            {.x = point.x, .y = point.y, .timestamp_us = event.button.timestamp * 1'000U});
        static_cast<void>(
            stroke_raster.finish(ribbon.finish(last_ink_point), stroke_color, &undo_history));
        toolbar.can_undo = undo_history.can_undo();
      }
    }

    tinydraw::draw_toolbar(pixels, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight, toolbar);
    SDL_UpdateTexture(texture, nullptr, pixels.data(),
                      tinydraw::kCanvasWidth * static_cast<int>(sizeof(std::uint16_t)));
    SDL_SetRenderDrawColor(renderer, 24, 24, 24, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    SDL_Delay(8);
  }

  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 5 && std::string(argv[1]) == "--replay" && std::string(argv[3]) == "--output") {
    return replay(argv[2], argv[4]);
  }
  if (argc == 4 && std::string(argv[1]) == "--ui-preview" && std::string(argv[2]) == "--output") {
    return ui_preview(argv[3]);
  }
  if (argc == 2 && std::string(argv[1]) == "--undo-e2e") {
    return undo_e2e();
  }
  if (argc != 1) {
    std::fprintf(stderr,
                 "usage: %s [--replay INPUT --output IMAGE.ppm | --ui-preview --output "
                 "IMAGE.ppm | --undo-e2e]\n",
                 argv[0]);
    return EXIT_FAILURE;
  }
  return interactive();
}
