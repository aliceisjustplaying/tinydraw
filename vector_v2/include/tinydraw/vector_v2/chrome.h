#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace tinydraw::vector_v2 {

struct ChromePoint {
  float x = 0.0F;
  float y = 0.0F;
};

inline constexpr int kChromeCanvasBottom = 372;
inline constexpr std::size_t kPaletteColorCount = 16;

enum class ChromeTool { kDraw, kErase, kPan };
enum class ChromeSize { kSmall, kMedium, kLarge, kExtraLarge };
enum class ChromePopup { kNone, kTools, kColors, kSizes, kDocument };
enum class ChromeAction {
  kNone,
  kUndo,
  kRedo,
  kToggleTools,
  kToggleColors,
  kToggleSizes,
  kToggleDocument,
  kSelectDraw,
  kSelectErase,
  kSelectPan,
  kSelectSmall,
  kSelectMedium,
  kSelectLarge,
  kSelectExtraLarge,
  kPreviousPalette,
  kNextPalette,
  kSelectColor,
  kNewDrawing,
  kExport,
};

struct ChromeState {
  ChromeTool tool = ChromeTool::kDraw;
  ChromeSize size = ChromeSize::kLarge;
  ChromePopup popup = ChromePopup::kNone;
  std::uint8_t palette_page = 0;
  std::uint8_t color_index = 12;
  bool can_undo = false;
  bool can_redo = false;
  bool can_export = false;
};

[[nodiscard]] constexpr std::uint16_t rgb565(std::uint32_t rgb888) {
  return static_cast<std::uint16_t>(((rgb888 >> 19U) & 0x1FU) << 11U |
                                    ((rgb888 >> 10U) & 0x3FU) << 5U | ((rgb888 >> 3U) & 0x1FU));
}

inline constexpr std::array<std::array<std::uint16_t, kPaletteColorCount>, 2> kPico8Palettes{{
    {rgb565(0x000000U), rgb565(0x1D2B53U), rgb565(0x7E2553U), rgb565(0x008751U), rgb565(0xAB5236U),
     rgb565(0x5F574FU), rgb565(0xC2C3C7U), rgb565(0xFFF1E8U), rgb565(0xFF004DU), rgb565(0xFFA300U),
     rgb565(0xFFEC27U), rgb565(0x00E436U), rgb565(0x29ADFFU), rgb565(0x83769CU), rgb565(0xFF77A8U),
     rgb565(0xFFCCAAU)},
    {rgb565(0x291814U), rgb565(0x111D35U), rgb565(0x422136U), rgb565(0x125359U), rgb565(0x742F29U),
     rgb565(0x49333BU), rgb565(0xA28879U), rgb565(0xF3EF7DU), rgb565(0xBE1250U), rgb565(0xFF6C24U),
     rgb565(0xA8E72EU), rgb565(0x00B543U), rgb565(0x065AB5U), rgb565(0x754665U), rgb565(0xFF6E59U),
     rgb565(0xFF9D81U)},
}};

[[nodiscard]] std::uint16_t selected_color(const ChromeState& state);
[[nodiscard]] float brush_size(ChromeSize size);
[[nodiscard]] bool chrome_contains(ChromePoint point, const ChromeState& state);
[[nodiscard]] ChromeAction chrome_action_at(ChromePoint point, const ChromeState& state);
[[nodiscard]] std::optional<std::uint8_t> chrome_color_at(ChromePoint point,
                                                          const ChromeState& state);
void draw_chrome(std::span<std::uint16_t> pixels, int width, int height, const ChromeState& state);

}  // namespace tinydraw::vector_v2
