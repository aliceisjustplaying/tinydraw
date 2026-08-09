#include <SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "tinydraw/geometry.h"

namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr std::uint16_t kGrid = 0xDEDBU;
constexpr std::uint16_t kInk = 0x001FU;

void set_pixel(std::vector<std::uint16_t>& pixels, int x, int y, std::uint16_t color) {
  if (x < 0 || x >= tinydraw::kCanvasWidth || y < 0 || y >= tinydraw::kCanvasHeight) {
    return;
  }
  const auto index = static_cast<std::size_t>(y * tinydraw::kCanvasWidth + x);
  pixels[index] = color;
}

void draw_dot(std::vector<std::uint16_t>& pixels, int x, int y) {
  for (int offset_y = -2; offset_y <= 2; ++offset_y) {
    for (int offset_x = -2; offset_x <= 2; ++offset_x) {
      if ((offset_x * offset_x) + (offset_y * offset_y) <= 4) {
        set_pixel(pixels, x + offset_x, y + offset_y, kInk);
      }
    }
  }
}

void clear_canvas(std::vector<std::uint16_t>& pixels) {
  std::fill(pixels.begin(), pixels.end(), kBackground);
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

}  // namespace

int main() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  SDL_Window* window = SDL_CreateWindow("TinyDraw host — drag to test logical input",
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

  std::vector<std::uint16_t> pixels(
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight));
  clear_canvas(pixels);

  bool running = true;
  while (running) {
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT ||
          (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
        running = false;
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_c) {
        clear_canvas(pixels);
      } else if (event.type == SDL_MOUSEMOTION && (event.motion.state & SDL_BUTTON_LMASK) != 0U) {
        float logical_x = 0.0F;
        float logical_y = 0.0F;
        SDL_RenderWindowToLogical(renderer, event.motion.x, event.motion.y, &logical_x, &logical_y);
        draw_dot(pixels, static_cast<int>(logical_x), static_cast<int>(logical_y));
      }
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
