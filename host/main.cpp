#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "input_coordinates.h"
#include "tinydraw/geometry.h"
#include "tinydraw/graphics/ribbon_renderer.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"

namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr std::uint16_t kGrid = 0xDEDBU;
constexpr std::uint16_t kInk = 0x001FU;

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

void draw_ribbon(std::vector<std::uint16_t>& pixels,
                 const std::vector<tinydraw::InkPoint>& points) {
  const auto primitives = tinydraw::build_pf_ribbon(points);
  static_cast<void>(tinydraw::render_ribbon(primitives, pixels, tinydraw::kCanvasWidth,
                                            tinydraw::kCanvasHeight, kInk));
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
  std::vector<tinydraw::InkPoint> stroke_points;
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
      stroke_points.clear();
      stroke_points.push_back(stream.begin(touch));
    } else if ((action == "move" || action == "up") && stream.active()) {
      stroke_points.push_back(action == "up" ? stream.finish(touch) : stream.update(touch));
      if (action == "up") {
        draw_ribbon(pixels, stroke_points);
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

int interactive() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  SDL_Window* window = SDL_CreateWindow("TinyDraw host — drag to test ink input",
                                        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                        tinydraw::kCanvasWidth * 2, tinydraw::kCanvasHeight * 2,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
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
  tinydraw::InkStream stream;
  std::vector<tinydraw::InkPoint> stroke_points;

  bool running = true;
  while (running) {
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT ||
          (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
        running = false;
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_c) {
        stream.end();
        stroke_points.clear();
        clear_canvas(committed_pixels, true);
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        const auto point = mouse_to_logical(event.button.x, event.button.y);
        if (!point.has_value()) {
          continue;
        }
        stroke_points.clear();
        stroke_points.push_back(stream.begin(
            {.x = point->x, .y = point->y, .timestamp_us = event.button.timestamp * 1'000U}));
      } else if (event.type == SDL_MOUSEMOTION && stream.active()) {
        const auto point = mouse_to_logical(event.motion.x, event.motion.y);
        if (point.has_value()) {
          stroke_points.push_back(stream.update(
              {.x = point->x, .y = point->y, .timestamp_us = event.motion.timestamp * 1'000U}));
        }
      } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT &&
                 stream.active()) {
        const auto mapped = mouse_to_logical(event.button.x, event.button.y);
        const tinydraw::Point point = mapped.value_or(
            stroke_points.empty() ? tinydraw::Point{} : stroke_points.back().position);
        stroke_points.push_back(stream.finish(
            {.x = point.x, .y = point.y, .timestamp_us = event.button.timestamp * 1'000U}));
        draw_ribbon(committed_pixels, stroke_points);
        stroke_points.clear();
      }
    }

    pixels = committed_pixels;
    if (stream.active()) {
      draw_ribbon(pixels, stroke_points);
    }
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
  if (argc != 1) {
    std::fprintf(stderr, "usage: %s [--replay INPUT --output IMAGE.ppm]\n", argv[0]);
    return EXIT_FAILURE;
  }
  return interactive();
}
