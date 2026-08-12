#include "physical_display.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr int kPanelGapX = 0x10;
constexpr int kTransferPixels = 8192;
constexpr int kTransferQueueDepth = 3;
// Even-aligned transfer bounds around the three independently changing overlays.
constexpr int kDialogOverlayX = 26;
constexpr int kDialogOverlayTop = 124;
constexpr int kDialogOverlayWidth = 318;
constexpr int kDialogOverlayHeight = 168;
constexpr std::uint16_t kIoExpanderAddress = 0x20;
constexpr std::uint8_t kIoExpanderOutputRegister = 0x01;
constexpr std::uint8_t kIoExpanderConfigRegister = 0x03;
constexpr std::uint8_t kIoExpanderLcdReset = 1U << 0U;
constexpr std::uint8_t kIoExpanderDisplayPower = 1U << 1U;
constexpr std::uint8_t kIoExpanderTouchReset = 1U << 2U;
constexpr std::uint8_t kIoExpanderSdChipSelect = 1U << 7U;
constexpr std::uint8_t kIoExpanderOutputs =
    kIoExpanderLcdReset | kIoExpanderDisplayPower | kIoExpanderTouchReset | kIoExpanderSdChipSelect;

alignas(4) DMA_ATTR
    std::array<std::array<std::uint16_t, kTransferPixels>, kTransferQueueDepth> transfer_pixels;
StaticSemaphore_t transfer_semaphore_storage;
SemaphoreHandle_t transfer_semaphore = nullptr;

// Panel transfers complete strictly in submission order, so one monotonically
// increasing pair of counters plus a completion-timestamp ring provides honest
// "first/last physical strip" endpoints. The ISR is the only completion writer.
constexpr std::size_t kTransferHistory = 64U;
std::atomic<std::uint32_t> transfer_submits{0U};
std::atomic<std::uint32_t> transfer_completes{0U};
std::array<std::atomic<std::uint32_t>, kTransferHistory> transfer_complete_times{};

constexpr std::array<std::uint8_t, 1> init_fe{0x00};
constexpr std::array<std::uint8_t, 1> init_c4{0x80};
constexpr std::array<std::uint8_t, 1> init_3a{0x55};
constexpr std::array<std::uint8_t, 1> init_35{0x00};
constexpr std::array<std::uint8_t, 1> init_53{0x20};
constexpr std::array<std::uint8_t, 1> init_51{0xFF};
constexpr std::array<std::uint8_t, 1> init_63{0xFF};
constexpr std::array<std::uint8_t, 4> init_2a{0x00, 0x00, 0x01, 0x6F};
constexpr std::array<std::uint8_t, 4> init_2b{0x00, 0x00, 0x01, 0xBF};

const std::array<co5300_lcd_init_cmd_t, 11> panel_init{{
    {0xFE, init_fe.data(), init_fe.size(), 0},
    {0xC4, init_c4.data(), init_c4.size(), 0},
    {0x3A, init_3a.data(), init_3a.size(), 0},
    {0x35, init_35.data(), init_35.size(), 0},
    {0x53, init_53.data(), init_53.size(), 0},
    {0x51, init_51.data(), init_51.size(), 0},
    {0x63, init_63.data(), init_63.size(), 0},
    {0x2A, init_2a.data(), init_2a.size(), 0},
    {0x2B, init_2b.data(), init_2b.size(), 0},
    {0x11, nullptr, 0, 100},
    {0x29, nullptr, 0, 0},
}};

bool on_transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  const std::uint32_t sequence = transfer_completes.load(std::memory_order_relaxed);
  transfer_complete_times[sequence % kTransferHistory].store(
      static_cast<std::uint32_t>(esp_timer_get_time()), std::memory_order_relaxed);
  transfer_completes.store(sequence + 1U, std::memory_order_release);
  BaseType_t woke = pdFALSE;
  xSemaphoreGiveFromISR(transfer_semaphore, &woke);
  return woke == pdTRUE;
}

std::uint32_t transfer_submit_count(void*) {
  return transfer_submits.load(std::memory_order_acquire);
}

std::uint32_t transfer_complete_count(void*) {
  return transfer_completes.load(std::memory_order_acquire);
}

// Returns the completion time of 1-based transfer `sequence`, or -1 when the
// sequence has not completed or its slot has been overwritten by newer ones.
std::int64_t transfer_complete_time_us(void*, std::uint32_t sequence) {
  const std::uint32_t completed = transfer_completes.load(std::memory_order_acquire);
  if (sequence == 0U || sequence > completed || completed - sequence >= kTransferHistory) {
    return -1;
  }
  return static_cast<std::int64_t>(
      transfer_complete_times[(sequence - 1U) % kTransferHistory].load(std::memory_order_relaxed));
}

constexpr std::uint16_t swap_bytes(std::uint16_t pixel) {
  return static_cast<std::uint16_t>((pixel << 8U) | (pixel >> 8U));
}

constexpr std::uint32_t swap_pixel_pair(std::uint16_t first, std::uint16_t second) {
  const std::uint32_t pixels =
      static_cast<std::uint32_t>(first) | (static_cast<std::uint32_t>(second) << 16U);
  return ((pixels >> 8U) & 0x00FF00FFU) | ((pixels << 8U) & 0xFF00FF00U);
}

static_assert(swap_pixel_pair(0x1234U, 0xABCDU) == 0xCDAB3412U);

bool reset_panel_power() {
  i2c_master_bus_config_t bus_config{};
  bus_config.i2c_port = I2C_NUM_0;
  bus_config.sda_io_num = GPIO_NUM_15;
  bus_config.scl_io_num = GPIO_NUM_14;
  bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_config.glitch_ignore_cnt = 7;
  bus_config.flags.enable_internal_pullup = true;
  i2c_master_bus_handle_t bus = nullptr;
  if (i2c_new_master_bus(&bus_config, &bus) != ESP_OK) {
    return false;
  }

  i2c_device_config_t device_config{};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = kIoExpanderAddress;
  device_config.scl_speed_hz = 400000;
  i2c_master_dev_handle_t device = nullptr;
  if (i2c_master_bus_add_device(bus, &device_config, &device) != ESP_OK) {
    static_cast<void>(i2c_del_master_bus(bus));
    return false;
  }

  const auto write = [&](std::uint8_t address, std::uint8_t value) {
    const std::array payload{address, value};
    return i2c_master_transmit(device, payload.data(), payload.size(), 100) == ESP_OK;
  };
  const bool configured =
      write(kIoExpanderConfigRegister, static_cast<std::uint8_t>(~kIoExpanderOutputs));
  const bool powered_down = configured && write(kIoExpanderOutputRegister, kIoExpanderSdChipSelect);
  vTaskDelay(pdMS_TO_TICKS(20));
  const bool powered_up = powered_down && write(kIoExpanderOutputRegister, kIoExpanderOutputs);
  vTaskDelay(pdMS_TO_TICKS(150));

  const bool removed = i2c_master_bus_rm_device(device) == ESP_OK;
  const bool deleted = i2c_del_master_bus(bus) == ESP_OK;
  return powered_up && removed && deleted;
}

int palette_overlay_top(ToolbarState state) {
  state.confirm_new = false;
  return toolbar_overlay_top(state) & ~1;
}

}  // namespace

class PhysicalDisplay::Impl {
 public:
  explicit Impl(bool enable_overlays) : overlays_enabled_(enable_overlays) {
    std::printf("TINYDRAW_PANEL_HARD_RESET=%u\n", reset_panel_power());
    transfer_semaphore = xSemaphoreCreateCountingStatic(kTransferQueueDepth, kTransferQueueDepth,
                                                        &transfer_semaphore_storage);
    if (overlays_enabled_) {
      overlay_ = static_cast<std::uint16_t*>(heap_caps_malloc(
          static_cast<std::size_t>(kCanvasWidth * kCanvasHeight) * sizeof(std::uint16_t),
          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    if (transfer_semaphore == nullptr || (overlays_enabled_ && overlay_ == nullptr)) {
      return;
    }
    if (overlay_ != nullptr) {
      std::fill_n(overlay_, kCanvasWidth * kCanvasHeight, kBackground);
    }

    spi_bus_config_t bus_config{};
    bus_config.sclk_io_num = GPIO_NUM_11;
    bus_config.data0_io_num = GPIO_NUM_4;
    bus_config.data1_io_num = GPIO_NUM_5;
    bus_config.data2_io_num = GPIO_NUM_6;
    bus_config.data3_io_num = GPIO_NUM_7;
    bus_config.max_transfer_sz = kTransferPixels * sizeof(std::uint16_t);
    if (spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) {
      return;
    }

    esp_lcd_panel_io_spi_config_t io_config{};
    io_config.cs_gpio_num = GPIO_NUM_12;
    io_config.dc_gpio_num = GPIO_NUM_NC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 60 * 1000 * 1000;
    io_config.trans_queue_depth = kTransferQueueDepth;
    io_config.on_color_trans_done = on_transfer_done;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    if (esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_config,
                                 &io_) != ESP_OK) {
      return;
    }

    co5300_vendor_config_t vendor_config{};
    vendor_config.init_cmds = panel_init.data();
    vendor_config.init_cmds_size = static_cast<std::uint16_t>(panel_init.size());
    vendor_config.flags.use_qspi_interface = 1;
    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;
    if (esp_lcd_new_panel_co5300(io_, &panel_config, &panel_) != ESP_OK ||
        esp_lcd_panel_reset(panel_) != ESP_OK || esp_lcd_panel_init(panel_) != ESP_OK ||
        esp_lcd_panel_set_gap(panel_, kPanelGapX, 0) != ESP_OK ||
        esp_lcd_panel_disp_on_off(panel_, true) != ESP_OK) {
      return;
    }
    ready_ = true;
  }

  ~Impl() { heap_caps_free(overlay_); }

  [[nodiscard]] bool ready() const { return ready_; }

  void reset_timing() {
    prepare_us_ = 0;
    transfer_us_ = 0;
    push_count_ = 0;
  }

  [[nodiscard]] std::int64_t prepare_us() const { return prepare_us_; }
  [[nodiscard]] std::int64_t transfer_us() const { return transfer_us_; }
  [[nodiscard]] std::uint32_t push_count() const { return push_count_; }
  [[nodiscard]] std::uint32_t rejected_push_count() const { return rejected_push_count_; }

  void set_toolbar(const ToolbarState& toolbar) {
    if (!overlays_enabled_) {
      return;
    }
    const bool main_changed = toolbar_.tool != toolbar.tool || toolbar_.color != toolbar.color ||
                              toolbar_.size != toolbar.size ||
                              toolbar_.can_undo != toolbar.can_undo ||
                              toolbar_.recording != toolbar.recording;
    const bool battery_changed = toolbar_.battery_percentage != toolbar.battery_percentage ||
                                 toolbar_.battery_charging != toolbar.battery_charging ||
                                 toolbar_.external_power != toolbar.external_power;
    const bool toast_changed = toolbar_.export_toast != toolbar.export_toast ||
                               ((toolbar_.export_toast || toolbar.export_toast) &&
                                toolbar_.export_ready != toolbar.export_ready);
    const bool palette_changed =
        toolbar_.tools_open != toolbar.tools_open || toolbar_.colors_open != toolbar.colors_open ||
        toolbar_.sizes_open != toolbar.sizes_open || toolbar_.can_export != toolbar.can_export ||
        toolbar_.exporting != toolbar.exporting || toolbar_.export_ready != toolbar.export_ready ||
        ((toolbar_.tools_open || toolbar.tools_open) && toolbar_.tool != toolbar.tool) ||
        ((toolbar_.colors_open || toolbar.colors_open) && toolbar_.color != toolbar.color) ||
        ((toolbar_.sizes_open || toolbar.sizes_open) && toolbar_.size != toolbar.size);
    main_dirty_ = main_dirty_ || main_changed;
    if (battery_changed) {
      const auto old_rect = battery_overlay_rect(toolbar_);
      const auto new_rect = battery_overlay_rect(toolbar);
      battery_refresh_ = old_rect.value_or(new_rect.value_or(Rect{}));
      battery_dirty_ = old_rect.has_value() || new_rect.has_value();
    }
    if (toast_changed) {
      const auto old_rect = export_toast_rect(toolbar_);
      const auto new_rect = export_toast_rect(toolbar);
      toast_refresh_ = old_rect.value_or(new_rect.value_or(Rect{}));
      toast_dirty_ = old_rect.has_value() || new_rect.has_value();
    }
    if (palette_changed) {
      const int changed_top = std::min(palette_overlay_top(toolbar_), palette_overlay_top(toolbar));
      palette_refresh_top_ =
          palette_dirty_ ? std::min(palette_refresh_top_, changed_top) : changed_top;
      palette_dirty_ = true;
    }
    dialog_dirty_ = dialog_dirty_ || toolbar_.confirm_new != toolbar.confirm_new;
    toolbar_ = toolbar;
    toolbar_top_ = toolbar_overlay_top(toolbar);
    if (main_dirty_) {
      clear_overlay(0, kPhysicalMainOverlayTop, kCanvasWidth,
                    kCanvasHeight - kPhysicalMainOverlayTop);
    }
    if (battery_dirty_) {
      clear_overlay(battery_refresh_.x0, battery_refresh_.y0,
                    battery_refresh_.x1 - battery_refresh_.x0,
                    battery_refresh_.y1 - battery_refresh_.y0);
    }
    if (toast_dirty_) {
      clear_overlay(toast_refresh_.x0, toast_refresh_.y0, toast_refresh_.x1 - toast_refresh_.x0,
                    toast_refresh_.y1 - toast_refresh_.y0);
    }
    if (palette_dirty_) {
      clear_overlay(0, palette_refresh_top_, kCanvasWidth,
                    kPhysicalMainOverlayTop - palette_refresh_top_);
    }
    if (dialog_dirty_) {
      clear_overlay(kDialogOverlayX, kDialogOverlayTop, kDialogOverlayWidth, kDialogOverlayHeight);
    }
    draw_toolbar(std::span(overlay_, static_cast<std::size_t>(kCanvasWidth * kCanvasHeight)),
                 kCanvasWidth, kCanvasHeight, toolbar_);
  }

  void push_rect(int x, int y, int width, int height, const std::uint16_t* pixels, int stride = 0) {
    if (!ready_ || pixels == nullptr || width <= 0 || height <= 0) {
      ++rejected_push_count_;
      return;
    }
    const bool in_bounds = x >= 0 && y >= 0 && x < kCanvasWidth && y < kCanvasHeight &&
                           width <= kCanvasWidth - x && height <= kCanvasHeight - y;
    const bool valid_co5300_window = ((x | y | width | height) & 1) == 0;
    if (!in_bounds || !valid_co5300_window) {
      ++rejected_push_count_;
      std::printf(
          "TINYDRAW_PANEL_WINDOW_REJECT x=%d y=%d width=%d height=%d bounds=%u even_window=%u\n", x,
          y, width, height, in_bounds, valid_co5300_window);
      return;
    }
    const int source_stride = stride == 0 ? width : stride;
    if (source_stride < width) {
      ++rejected_push_count_;
      return;
    }
    if (width * height > kTransferPixels) {
      int rows_per_transfer = kTransferPixels / width;
      rows_per_transfer -= rows_per_transfer % 2;
      if (rows_per_transfer <= 0) {
        ++rejected_push_count_;
        return;
      }
      for (int row = 0; row < height; row += rows_per_transfer) {
        const int rows = std::min(rows_per_transfer, height - row);
        push_rect(x, y + row, width, rows,
                  pixels + static_cast<std::ptrdiff_t>(row * source_stride), source_stride);
      }
      return;
    }
    const auto transfer_started = esp_timer_get_time();
    ESP_ERROR_CHECK(xSemaphoreTake(transfer_semaphore, portMAX_DELAY) == pdTRUE ? ESP_OK
                                                                                : ESP_FAIL);
    auto& transfer = transfer_pixels[transfer_index_];
    transfer_index_ = (transfer_index_ + 1U) % transfer_pixels.size();
    transfer_us_ += esp_timer_get_time() - transfer_started;

    const auto prepare_started = esp_timer_get_time();
    const auto battery_rect = battery_overlay_rect(toolbar_);
    const bool intersects_battery = battery_rect.has_value() && x < battery_rect->x1 &&
                                    x + width > battery_rect->x0 && y < battery_rect->y1 &&
                                    y + height > battery_rect->y0;
    const auto toast_rect = export_toast_rect(toolbar_);
    const bool intersects_toast = toast_rect.has_value() && x < toast_rect->x1 &&
                                  x + width > toast_rect->x0 && y < toast_rect->y1 &&
                                  y + height > toast_rect->y0;
    if (!overlays_enabled_ || (y + height <= toolbar_top_ && !intersects_battery &&
                               !intersects_toast && width % 2 == 0)) {
      for (int row = 0; row < height; ++row) {
        const auto source = pixels + static_cast<std::ptrdiff_t>(row * source_stride);
        auto* destination = reinterpret_cast<std::uint32_t*>(
            transfer.data() + static_cast<std::ptrdiff_t>(row * width));
        int column = 0;
        for (; column + 1 < width; column += 2) {
          destination[column / 2] = swap_pixel_pair(source[column], source[column + 1]);
        }
        if (column < width) {
          transfer[static_cast<std::size_t>(row * width + column)] = swap_bytes(source[column]);
        }
      }
    } else {
      for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
          const int panel_x = x + column;
          const int panel_y = y + row;
          const std::size_t source = static_cast<std::size_t>(row * source_stride + column);
          const std::size_t destination = static_cast<std::size_t>(row * width + column);
          const std::size_t canvas = static_cast<std::size_t>(panel_y * kCanvasWidth + panel_x);
          const auto point =
              Point{static_cast<float>(panel_x) + 0.5F, static_cast<float>(panel_y) + 0.5F};
          const bool toolbar_pixel = toolbar_overlay_contains(point, toolbar_);
          const std::uint16_t pixel = toolbar_pixel ? overlay_[canvas] : pixels[source];
          transfer[destination] = swap_bytes(pixel);
        }
      }
    }
    prepare_us_ += esp_timer_get_time() - prepare_started;
    const auto submit_started = esp_timer_get_time();
    transfer_submits.fetch_add(1U, std::memory_order_release);
    ESP_ERROR_CHECK(
        esp_lcd_panel_draw_bitmap(panel_, x, y, x + width, y + height, transfer.data()));
    transfer_us_ += esp_timer_get_time() - submit_started;
    ++push_count_;
  }

  void push_canvas(std::span<const std::uint16_t> canvas, int top = 0, int bottom = kCanvasHeight) {
    // The CO5300 requires even starts and even column/row counts.
    constexpr int rows_per_transfer = (kTransferPixels / kCanvasWidth) & ~1;
    for (int y = top; y < bottom; y += rows_per_transfer) {
      const int height = std::min(rows_per_transfer, bottom - y);
      push_rect(0, y, kCanvasWidth, height,
                canvas.data() + static_cast<std::size_t>(y * kCanvasWidth));
    }
    if (top == 0 && bottom == kCanvasHeight) {
      main_dirty_ = false;
      battery_dirty_ = false;
      toast_dirty_ = false;
      palette_dirty_ = false;
      palette_refresh_top_ = kPhysicalMainOverlayTop;
      dialog_dirty_ = false;
    }
  }

  void push_world(std::span<const std::uint16_t> world, ViewOrigin origin, int bottom) {
    constexpr int rows_per_transfer = (kTransferPixels / kCanvasWidth) & ~1;
    for (int y = 0; y < bottom; y += rows_per_transfer) {
      const int height = std::min(rows_per_transfer, bottom - y);
      const auto offset = static_cast<std::size_t>((origin.y + y) * WorldCanvas::kWidth + origin.x);
      push_rect(0, y, kCanvasWidth, height, world.data() + offset, WorldCanvas::kWidth);
    }
  }

  void refresh_toolbar(std::span<const std::uint16_t> canvas) {
    refresh_toolbar_source(canvas, kCanvasWidth, 0, 0);
  }

  void refresh_toolbar_world(std::span<const std::uint16_t> world, ViewOrigin origin) {
    refresh_toolbar_source(world, WorldCanvas::kWidth, origin.x, origin.y);
  }

 private:
  void refresh_toolbar_source(std::span<const std::uint16_t> source, int stride, int source_x,
                              int source_y) {
    const auto offset_at = [&](int x, int y) {
      return static_cast<std::size_t>((source_y + y) * stride + source_x + x);
    };
    if (battery_dirty_) {
      const auto offset = offset_at(battery_refresh_.x0, battery_refresh_.y0);
      push_rect(battery_refresh_.x0, battery_refresh_.y0, battery_refresh_.x1 - battery_refresh_.x0,
                battery_refresh_.y1 - battery_refresh_.y0, source.data() + offset, stride);
    }
    if (toast_dirty_) {
      const auto offset = offset_at(toast_refresh_.x0, toast_refresh_.y0);
      push_rect(toast_refresh_.x0, toast_refresh_.y0, toast_refresh_.x1 - toast_refresh_.x0,
                toast_refresh_.y1 - toast_refresh_.y0, source.data() + offset, stride);
    }
    if (dialog_dirty_) {
      const auto offset = offset_at(kDialogOverlayX, kDialogOverlayTop);
      push_rect(kDialogOverlayX, kDialogOverlayTop, kDialogOverlayWidth, kDialogOverlayHeight,
                source.data() + offset, stride);
    }
    if (palette_dirty_) {
      const auto offset = offset_at(0, palette_refresh_top_);
      push_rect(0, palette_refresh_top_, kCanvasWidth,
                kPhysicalMainOverlayTop - palette_refresh_top_, source.data() + offset, stride);
    }
    if (main_dirty_) {
      const auto offset = offset_at(0, kPhysicalMainOverlayTop);
      push_rect(0, kPhysicalMainOverlayTop, kCanvasWidth, kCanvasHeight - kPhysicalMainOverlayTop,
                source.data() + offset, stride);
    }
    main_dirty_ = false;
    battery_dirty_ = false;
    toast_dirty_ = false;
    palette_dirty_ = false;
    palette_refresh_top_ = kPhysicalMainOverlayTop;
    dialog_dirty_ = false;
  }

  void clear_overlay(int x, int y, int width, int height) {
    for (int row = 0; row < height; ++row) {
      auto* start = overlay_ + static_cast<std::ptrdiff_t>((y + row) * kCanvasWidth + x);
      std::fill_n(start, width, kBackground);
    }
  }

  esp_lcd_panel_io_handle_t io_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;
  std::uint16_t* overlay_ = nullptr;
  ToolbarState toolbar_{};
  int toolbar_top_ = toolbar_overlay_top(toolbar_);
  bool main_dirty_ = false;
  bool battery_dirty_ = false;
  Rect battery_refresh_{};
  bool toast_dirty_ = false;
  Rect toast_refresh_{};
  bool palette_dirty_ = false;
  int palette_refresh_top_ = kPhysicalMainOverlayTop;
  bool dialog_dirty_ = false;
  std::int64_t prepare_us_ = 0;
  std::int64_t transfer_us_ = 0;
  std::uint32_t push_count_ = 0;
  std::uint32_t rejected_push_count_ = 0;
  std::size_t transfer_index_ = 0;
  bool ready_ = false;
  bool overlays_enabled_ = true;
};

PhysicalDisplay::PhysicalDisplay(bool enable_overlays)
    : impl_(std::make_unique<Impl>(enable_overlays)) {}
PhysicalDisplay::~PhysicalDisplay() = default;

bool PhysicalDisplay::ready() const { return impl_->ready(); }
void PhysicalDisplay::reset_timing() { impl_->reset_timing(); }
std::int64_t PhysicalDisplay::prepare_us() const { return impl_->prepare_us(); }
std::int64_t PhysicalDisplay::transfer_us() const { return impl_->transfer_us(); }
std::uint32_t PhysicalDisplay::push_count() const { return impl_->push_count(); }
std::uint32_t PhysicalDisplay::rejected_push_count() const { return impl_->rejected_push_count(); }
void PhysicalDisplay::set_toolbar(const ToolbarState& toolbar) { impl_->set_toolbar(toolbar); }
void PhysicalDisplay::push_rect(int x, int y, int width, int height, const std::uint16_t* pixels,
                                int stride) {
  impl_->push_rect(x, y, width, height, pixels, stride);
}
void PhysicalDisplay::push_canvas(std::span<const std::uint16_t> canvas, int top, int bottom) {
  impl_->push_canvas(canvas, top, bottom);
}
void PhysicalDisplay::push_world(std::span<const std::uint16_t> world, ViewOrigin origin,
                                 int bottom) {
  impl_->push_world(world, origin, bottom);
}
void PhysicalDisplay::refresh_toolbar(std::span<const std::uint16_t> canvas) {
  impl_->refresh_toolbar(canvas);
}
void PhysicalDisplay::refresh_toolbar_world(std::span<const std::uint16_t> world,
                                            ViewOrigin origin) {
  impl_->refresh_toolbar_world(world, origin);
}

std::uint32_t physical_display_submit_count(void*) { return transfer_submit_count(nullptr); }
std::uint32_t physical_display_complete_count(void*) { return transfer_complete_count(nullptr); }
std::int64_t physical_display_complete_time_us(void*, std::uint32_t sequence) {
  return transfer_complete_time_us(nullptr, sequence);
}

}  // namespace tinydraw::esp32
