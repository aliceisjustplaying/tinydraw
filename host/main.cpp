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
  std::vector<std::uint16_t> undo_pixels;
  tinydraw::ToolbarState toolbar;
  tinydraw::InkStream stream;
  tinydraw::RibbonStream ribbon;
  std::vector<tinydraw::RibbonPrimitive> geometry;
  std::size_t committed_count = 0U;
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
    geometry.clear();
    committed_count = 0U;
  };
  const auto start_new_drawing = [&] {
    reset_active_stroke();
    clear_canvas(committed_pixels, true);
    undo_pixels.clear();
    toolbar.can_undo = false;
    close_popups();
  };
  const auto undo = [&] {
    if (!toolbar.can_undo) {
      return;
    }
    reset_active_stroke();
    committed_pixels = undo_pixels;
    undo_pixels.clear();
    toolbar.can_undo = false;
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
        undo_pixels = committed_pixels;
        toolbar.can_undo = true;
        stroke_color = toolbar.tool == tinydraw::DrawingTool::kEraser
                           ? kBackground
                           : tinydraw::rgb565(toolbar.color);
        geometry.clear();
        committed_count = 0U;
        last_ink_point = stream.begin(
            {.x = point->x, .y = point->y, .timestamp_us = event.button.timestamp * 1'000U});
        apply_ribbon_update(geometry, committed_count, ribbon.append(last_ink_point));
      } else if (event.type == SDL_MOUSEMOTION && stream.active()) {
        const auto point = mouse_to_logical(event.motion.x, event.motion.y);
        if (point.has_value()) {
          last_ink_point = stream.update(
              {.x = point->x, .y = point->y, .timestamp_us = event.motion.timestamp * 1'000U});
          apply_ribbon_update(geometry, committed_count, ribbon.append(last_ink_point));
        }
      } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT &&
                 stream.active()) {
        const auto mapped = mouse_to_logical(event.button.x, event.button.y);
        const tinydraw::Point point = mapped.value_or(last_ink_point.position);
        last_ink_point = stream.finish(
            {.x = point.x, .y = point.y, .timestamp_us = event.button.timestamp * 1'000U});
        apply_ribbon_update(geometry, committed_count, ribbon.finish(last_ink_point));
        draw_ribbon(committed_pixels, geometry, stroke_color);
        geometry.clear();
        committed_count = 0U;
      }
    }

    pixels = committed_pixels;
    if (stream.active()) {
      draw_ribbon(pixels, geometry, stroke_color);
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
  if (argc != 1) {
    std::fprintf(stderr,
                 "usage: %s [--replay INPUT --output IMAGE.ppm | --ui-preview --output "
                 "IMAGE.ppm]\n",
                 argv[0]);
    return EXIT_FAILURE;
  }
  return interactive();
}
