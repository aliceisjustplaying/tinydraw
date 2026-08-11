#include <SDL.h>

#include <algorithm>
#include <cmath>
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
#include "tinydraw/graphics/world_canvas.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/ui/toolbar.h"

namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr std::uint16_t kReplayInk = 0x001FU;
// A 1.8-inch 368x448 panel is about 1.14 x 1.39 inches. On the 254-PPI Retina
// panel of a 14-inch 2021 MacBook Pro at default scaling, SDL uses 127 points per inch.
constexpr int kPhysicalWindowWidth = 145;
constexpr int kPhysicalWindowHeight = 177;

std::optional<tinydraw::Point> mouse_to_logical(int x, int y) {
  return tinydraw::host::event_to_logical({.x = static_cast<float>(x), .y = static_cast<float>(y)});
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

void clear_canvas(std::vector<std::uint16_t>& pixels) {
  std::fill(pixels.begin(), pixels.end(), kBackground);
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
  clear_canvas(pixels);
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

int ui_preview(const std::string& output_path, const tinydraw::ToolbarState& toolbar = {}) {
  std::vector<std::uint16_t> pixels(
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight));
  clear_canvas(pixels);
  tinydraw::draw_toolbar(pixels, tinydraw::kCanvasWidth, tinydraw::kCanvasHeight, toolbar);
  if (!write_ppm(output_path, pixels)) {
    std::fprintf(stderr, "cannot write UI preview: %s\n", output_path.c_str());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}

bool draw_test_stroke(tinydraw::StrokeRaster& raster, tinydraw::TileUndoHistory& history,
                      tinydraw::Point start, tinydraw::Point end, std::uint16_t color,
                      std::uint32_t timestamp_us, tinydraw::ViewOrigin origin = {}) {
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
      color, &history, origin));
  return history.can_undo();
}

int undo_e2e() {
  std::vector<std::uint16_t> committed(
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight));
  clear_canvas(committed);
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
  clear_canvas(committed);
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

int pan_e2e() {
  std::vector<std::uint16_t> committed(tinydraw::WorldCanvas::kViewportPixels, kBackground);
  std::vector<std::uint16_t> visible = committed;
  std::vector<std::uint16_t> world_storage(tinydraw::WorldCanvas::kRequiredPixels);
  std::vector<std::uint16_t> undo_storage(tinydraw::TileUndoHistory::kRequiredPixels);
  std::vector<std::uint8_t> coverage(committed.size(), 0U);
  tinydraw::WorldCanvas world(world_storage);
  tinydraw::TileUndoHistory history(undo_storage);
  tinydraw::StrokeRaster raster(committed, visible, coverage);

  const auto first_origin = world.origin();
  draw_test_stroke(raster, history, {30.0F, 40.0F}, {140.0F, 110.0F}, kReplayInk, 1'000U,
                   first_origin);
  const auto first_stroke = committed;
  static_cast<void>(world.capture(committed));

  const tinydraw::ViewOrigin second_origin{first_origin.x + 200, first_origin.y};
  static_cast<void>(world.show(second_origin, committed, visible));
  const auto blank_second_view = committed;
  static_cast<void>(world.show(first_origin, committed, visible));
  const bool first_survived_pan = committed == first_stroke;
  static_cast<void>(world.show(second_origin, committed, visible));

  draw_test_stroke(raster, history, {220.0F, 80.0F}, {320.0F, 180.0F}, 0xF800U, 20'000U,
                   second_origin);
  static_cast<void>(history.undo(committed, visible));
  static_cast<void>(world.capture(committed));
  const bool undid_second = committed == blank_second_view;

  const auto undo_origin = history.next_undo_origin();
  if (undo_origin.has_value()) {
    static_cast<void>(world.show(*undo_origin, committed, visible));
  }
  static_cast<void>(history.undo(committed, visible));
  const bool returned_to_first_view = world.origin() == first_origin;
  const bool undid_first =
      std::ranges::all_of(committed, [](std::uint16_t pixel) { return pixel == kBackground; });

  if (!first_survived_pan || !undid_second || !returned_to_first_view || !undid_first) {
    std::fprintf(stderr, "pannable canvas E2E failed\n");
    return EXIT_FAILURE;
  }
  std::printf("TINYDRAW_PAN_OK world=3x3 draw=1 undo_across_views=1 exact=1\n");
  return EXIT_SUCCESS;
}

int interactive(int window_scale) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  SDL_Window* window = SDL_CreateWindow(
      "TinyDraw host — drag to test ink input", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      kPhysicalWindowWidth * window_scale, kPhysicalWindowHeight * window_scale,
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
  clear_canvas(committed_pixels);
  std::vector<std::uint16_t> pixels = committed_pixels;
  std::vector<std::uint16_t> world_storage(tinydraw::WorldCanvas::kRequiredPixels);
  tinydraw::WorldCanvas world(world_storage);
  std::vector<std::uint16_t> undo_storage(tinydraw::TileUndoHistory::kRequiredPixels);
  tinydraw::TileUndoHistory undo_history(undo_storage);
  std::vector<std::uint8_t> active_coverage(
      static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight), 0U);
  tinydraw::StrokeRaster stroke_raster(committed_pixels, pixels, active_coverage);
  tinydraw::ToolbarState toolbar;
  tinydraw::InkConfig initial_brush;
  initial_brush.size = tinydraw::brush_size(toolbar.size);
  tinydraw::InkStream stream(initial_brush);
  tinydraw::CurvedRibbonStream ribbon;
  tinydraw::InkPoint last_ink_point{};
  std::uint16_t stroke_color = tinydraw::rgb565(toolbar.color);

  const auto close_popups = [&] {
    toolbar.tools_open = false;
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
  const auto request_new_drawing = [&] {
    reset_active_stroke();
    pixels = committed_pixels;
    close_popups();
    toolbar.confirm_new = true;
  };
  const auto start_new_drawing = [&] {
    reset_active_stroke();
    undo_history.begin_entry(world.origin());
    undo_history.capture_canvas(committed_pixels);
    static_cast<void>(undo_history.commit_entry());
    static_cast<void>(world.clear(committed_pixels, pixels));
    toolbar.can_undo = undo_history.can_undo();
    toolbar.confirm_new = false;
    close_popups();
  };
  const auto undo = [&] {
    if (!undo_history.can_undo()) {
      return;
    }
    reset_active_stroke();
    static_cast<void>(world.capture(committed_pixels));
    if (const auto origin = undo_history.next_undo_origin(); origin.has_value()) {
      static_cast<void>(world.show(*origin, committed_pixels, pixels));
    }
    static_cast<void>(undo_history.undo(committed_pixels, pixels));
    static_cast<void>(world.capture(committed_pixels));
    toolbar.can_undo = undo_history.can_undo();
    close_popups();
  };

  bool panning = false;
  tinydraw::Point pan_start_touch{};
  tinydraw::ViewOrigin pan_start_origin{};
  const auto pan_to = [&](tinydraw::Point point) {
    const int delta_x = static_cast<int>(std::lround(point.x - pan_start_touch.x));
    const int delta_y = static_cast<int>(std::lround(point.y - pan_start_touch.y));
    static_cast<void>(world.show({pan_start_origin.x - delta_x, pan_start_origin.y - delta_y},
                                 committed_pixels, pixels));
  };

  bool running = true;
  while (running) {
    SDL_Event event{};
    while (SDL_PollEvent(&event) != 0) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
        if (toolbar.confirm_new) {
          toolbar.confirm_new = false;
          pixels = committed_pixels;
        } else {
          running = false;
        }
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_c) {
        request_new_drawing();
      } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_z &&
                 (event.key.keysym.mod & KMOD_GUI) != 0) {
        undo();
      } else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        const auto point = mouse_to_logical(event.button.x, event.button.y);
        if (!point.has_value()) {
          continue;
        }
        if (stream.active()) {
          reset_active_stroke();
        }
        if (tinydraw::toolbar_contains(*point, toolbar)) {
          pixels = committed_pixels;
          switch (tinydraw::toolbar_action_at(*point, toolbar)) {
            case tinydraw::ToolbarAction::kSelectPen:
              toolbar.tool = tinydraw::DrawingTool::kPen;
              close_popups();
              break;
            case tinydraw::ToolbarAction::kSelectPan:
              toolbar.tool = tinydraw::DrawingTool::kPan;
              close_popups();
              break;
            case tinydraw::ToolbarAction::kSelectEraser:
              toolbar.tool = tinydraw::DrawingTool::kEraser;
              close_popups();
              break;
            case tinydraw::ToolbarAction::kSelectColor:
              toolbar.color = tinydraw::toolbar_color_at(*point, toolbar).value_or(toolbar.color);
              toolbar.tool = tinydraw::DrawingTool::kPen;
              close_popups();
              break;
            case tinydraw::ToolbarAction::kToggleTools:
              toolbar.tools_open = !toolbar.tools_open;
              toolbar.colors_open = false;
              toolbar.sizes_open = false;
              break;
            case tinydraw::ToolbarAction::kToggleColors:
              toolbar.colors_open = !toolbar.colors_open;
              toolbar.tools_open = false;
              toolbar.sizes_open = false;
              break;
            case tinydraw::ToolbarAction::kToggleSizes:
              toolbar.sizes_open = !toolbar.sizes_open;
              toolbar.tools_open = false;
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
            case tinydraw::ToolbarAction::kExport:
              break;
            case tinydraw::ToolbarAction::kUndo:
              undo();
              break;
            case tinydraw::ToolbarAction::kNewDrawing:
              request_new_drawing();
              break;
            case tinydraw::ToolbarAction::kCancelNewDrawing:
              toolbar.confirm_new = false;
              break;
            case tinydraw::ToolbarAction::kConfirmNewDrawing:
              start_new_drawing();
              break;
            case tinydraw::ToolbarAction::kNone:
              break;
          }
          continue;
        }
        close_popups();
        if (toolbar.tool == tinydraw::DrawingTool::kPan) {
          static_cast<void>(world.capture(committed_pixels));
          pan_start_touch = *point;
          pan_start_origin = world.origin();
          panning = true;
          continue;
        }
        stroke_color = toolbar.tool == tinydraw::DrawingTool::kEraser
                           ? kBackground
                           : tinydraw::rgb565(toolbar.color);
        last_ink_point = stream.begin(
            {.x = point->x, .y = point->y, .timestamp_us = event.button.timestamp * 1'000U});
        static_cast<void>(stroke_raster.update(ribbon.append(last_ink_point), stroke_color));
      } else if (event.type == SDL_MOUSEMOTION && panning) {
        if (const auto point = mouse_to_logical(event.motion.x, event.motion.y);
            point.has_value()) {
          pan_to(*point);
        }
      } else if (event.type == SDL_MOUSEMOTION && stream.active()) {
        const auto point = mouse_to_logical(event.motion.x, event.motion.y);
        if (point.has_value()) {
          last_ink_point = stream.update(
              {.x = point->x, .y = point->y, .timestamp_us = event.motion.timestamp * 1'000U});
          static_cast<void>(stroke_raster.update(ribbon.append(last_ink_point), stroke_color));
        }
      } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT &&
                 panning) {
        if (const auto point = mouse_to_logical(event.button.x, event.button.y);
            point.has_value()) {
          pan_to(*point);
        }
        panning = false;
      } else if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT &&
                 stream.active()) {
        const auto mapped = mouse_to_logical(event.button.x, event.button.y);
        const tinydraw::Point point = mapped.value_or(last_ink_point.position);
        last_ink_point = stream.finish(
            {.x = point.x, .y = point.y, .timestamp_us = event.button.timestamp * 1'000U});
        static_cast<void>(stroke_raster.finish(ribbon.finish(last_ink_point), stroke_color,
                                               &undo_history, world.origin()));
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
  if (argc == 4 && std::string(argv[2]) == "--output") {
    if (std::string(argv[1]) == "--ui-preview") {
      return ui_preview(argv[3]);
    }
    if (std::string(argv[1]) == "--color-palette-preview") {
      return ui_preview(argv[3], {.colors_open = true});
    }
    if (std::string(argv[1]) == "--tool-palette-preview") {
      return ui_preview(argv[3], {.tools_open = true});
    }
    if (std::string(argv[1]) == "--new-dialog-preview") {
      return ui_preview(argv[3], {.confirm_new = true});
    }
  }
  if (argc == 2 && std::string(argv[1]) == "--undo-e2e") {
    return undo_e2e();
  }
  if (argc == 2 && std::string(argv[1]) == "--pan-e2e") {
    return pan_e2e();
  }
  if (argc == 3 && std::string(argv[1]) == "--scale") {
    char* end = nullptr;
    const long scale = std::strtol(argv[2], &end, 10);
    if (end != argv[2] && *end == '\0' && scale >= 1 && scale <= 3) {
      return interactive(static_cast<int>(scale));
    }
  }
  if (argc != 1) {
    std::fprintf(stderr,
                 "usage: %s [--scale {1|2|3} | --replay INPUT --output IMAGE.ppm | "
                 "--ui-preview --output IMAGE.ppm | --color-palette-preview --output IMAGE.ppm | "
                 "--tool-palette-preview --output IMAGE.ppm | "
                 "--new-dialog-preview --output IMAGE.ppm | --undo-e2e | --pan-e2e]\n",
                 argv[0]);
    return EXIT_FAILURE;
  }
  return interactive(1);
}
