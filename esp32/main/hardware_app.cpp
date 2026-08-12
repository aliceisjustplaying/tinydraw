#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

#include "demo_recording.h"
#include "drawing_store.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "firmware_canvas.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "image_export_store.h"
#include "power_manager.h"
#include "rtc_clock.h"
#include "time_sync.h"
#include "tinydraw/demo/demo_tape.h"
#include "tinydraw/document/vector_document.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/ui/toolbar.h"
#include "usb_export.h"
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
#include "interactive_pan_benchmark.h"
#endif
#ifdef TINYDRAW_PHASE2_PROTOTYPE
#include "phase2_prototype_runner.h"
#endif
#ifdef TINYDRAW_RASTER_PAN_BENCHMARK
#include "raster_pan_benchmark.h"
#endif
#ifdef TINYDRAW_VECTOR_BENCHMARK
#include "vector_benchmark_runner.h"
#endif

namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr int kPanelGapX = 0x10;
constexpr int kTransferPixels = 8192;
constexpr int kTransferQueueDepth = 3;
// Even-aligned transfer bounds around the three independently changing overlays.
constexpr int kMainOverlayTop = 372;
constexpr int kDialogOverlayX = 26;
constexpr int kDialogOverlayTop = 124;
constexpr int kDialogOverlayWidth = 318;
constexpr int kDialogOverlayHeight = 168;
constexpr gpio_num_t kDemoButton = GPIO_NUM_0;
constexpr std::uint32_t kDemoLongPressUs = 800'000U;
constexpr std::uint32_t kPowerRefreshUs = 1'000'000U;
constexpr std::size_t kDemoCapacity = 8192U;
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
constexpr std::size_t kVectorStrokeCapacity = 1'100U;
constexpr std::size_t kVectorSampleCapacity = 16'384U;
#else
constexpr std::size_t kVectorStrokeCapacity = 512U;
constexpr std::size_t kVectorSampleCapacity = 8192U;
#endif
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
std::array<std::atomic<std::int64_t>, kTransferHistory> transfer_complete_times{};

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
  transfer_complete_times[sequence % kTransferHistory].store(esp_timer_get_time(),
                                                             std::memory_order_relaxed);
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
  return transfer_complete_times[(sequence - 1U) % kTransferHistory].load(
      std::memory_order_relaxed);
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

int palette_overlay_top(tinydraw::ToolbarState state) {
  state.confirm_new = false;
  return tinydraw::toolbar_overlay_top(state) & ~1;
}

class PhysicalDisplay final : public tinydraw::DisplayBackend {
 public:
  PhysicalDisplay() {
    std::printf("TINYDRAW_PANEL_HARD_RESET=%u\n", reset_panel_power());
    transfer_semaphore = xSemaphoreCreateCountingStatic(kTransferQueueDepth, kTransferQueueDepth,
                                                        &transfer_semaphore_storage);
    overlay_ = static_cast<std::uint16_t*>(heap_caps_malloc(
        static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight) *
            sizeof(std::uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (transfer_semaphore == nullptr || overlay_ == nullptr) {
      return;
    }
    std::fill_n(overlay_, tinydraw::kCanvasWidth * tinydraw::kCanvasHeight, kBackground);

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

  ~PhysicalDisplay() override { heap_caps_free(overlay_); }

  [[nodiscard]] bool ready() const { return ready_; }

  void reset_timing() {
    prepare_us_ = 0;
    transfer_us_ = 0;
    push_count_ = 0;
  }

  [[nodiscard]] std::int64_t prepare_us() const { return prepare_us_; }
  [[nodiscard]] std::int64_t transfer_us() const { return transfer_us_; }
  [[nodiscard]] std::uint32_t push_count() const { return push_count_; }

  void set_toolbar(const tinydraw::ToolbarState& toolbar) {
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
      const auto old_rect = tinydraw::battery_overlay_rect(toolbar_);
      const auto new_rect = tinydraw::battery_overlay_rect(toolbar);
      battery_refresh_ = old_rect.value_or(new_rect.value_or(tinydraw::Rect{}));
      battery_dirty_ = old_rect.has_value() || new_rect.has_value();
    }
    if (toast_changed) {
      const auto old_rect = tinydraw::export_toast_rect(toolbar_);
      const auto new_rect = tinydraw::export_toast_rect(toolbar);
      toast_refresh_ = old_rect.value_or(new_rect.value_or(tinydraw::Rect{}));
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
    toolbar_top_ = tinydraw::toolbar_overlay_top(toolbar);
    if (main_dirty_) {
      clear_overlay(0, kMainOverlayTop, tinydraw::kCanvasWidth,
                    tinydraw::kCanvasHeight - kMainOverlayTop);
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
      clear_overlay(0, palette_refresh_top_, tinydraw::kCanvasWidth,
                    kMainOverlayTop - palette_refresh_top_);
    }
    if (dialog_dirty_) {
      clear_overlay(kDialogOverlayX, kDialogOverlayTop, kDialogOverlayWidth, kDialogOverlayHeight);
    }
    tinydraw::draw_toolbar(std::span(overlay_, static_cast<std::size_t>(tinydraw::kCanvasWidth *
                                                                        tinydraw::kCanvasHeight)),
                           tinydraw::kCanvasWidth, tinydraw::kCanvasHeight, toolbar_);
  }

  void push_rect(int x, int y, int width, int height, const std::uint16_t* pixels,
                 int stride = 0) override {
    if (!ready_ || pixels == nullptr || width <= 0 || height <= 0) {
      return;
    }
    const int source_stride = stride == 0 ? width : stride;
    if (source_stride < width) {
      return;
    }
    if (width * height > kTransferPixels) {
      int rows_per_transfer = kTransferPixels / width;
      rows_per_transfer -= rows_per_transfer % 2;
      if (rows_per_transfer <= 0) {
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
    const auto battery_rect = tinydraw::battery_overlay_rect(toolbar_);
    const bool intersects_battery = battery_rect.has_value() && x < battery_rect->x1 &&
                                    x + width > battery_rect->x0 && y < battery_rect->y1 &&
                                    y + height > battery_rect->y0;
    const auto toast_rect = tinydraw::export_toast_rect(toolbar_);
    const bool intersects_toast = toast_rect.has_value() && x < toast_rect->x1 &&
                                  x + width > toast_rect->x0 && y < toast_rect->y1 &&
                                  y + height > toast_rect->y0;
    if (y + height <= toolbar_top_ && !intersects_battery && !intersects_toast && width % 2 == 0) {
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
          const std::size_t canvas =
              static_cast<std::size_t>(panel_y * tinydraw::kCanvasWidth + panel_x);
          const auto point = tinydraw::Point{static_cast<float>(panel_x) + 0.5F,
                                             static_cast<float>(panel_y) + 0.5F};
          const bool toolbar_pixel = tinydraw::toolbar_overlay_contains(point, toolbar_);
          const std::uint16_t pixel = toolbar_pixel ? overlay_[canvas] : pixels[source];
          transfer[destination] = swap_bytes(pixel);
        }
      }
    }
    prepare_us_ += esp_timer_get_time() - prepare_started;
    const auto submit_started = esp_timer_get_time();
    ESP_ERROR_CHECK(
        esp_lcd_panel_draw_bitmap(panel_, x, y, x + width, y + height, transfer.data()));
    transfer_submits.fetch_add(1U, std::memory_order_release);
    transfer_us_ += esp_timer_get_time() - submit_started;
    ++push_count_;
  }

  void push_canvas(std::span<const std::uint16_t> canvas, int top = 0,
                   int bottom = tinydraw::kCanvasHeight) {
    // The CO5300 requires even transfer-window boundaries.
    constexpr int rows_per_transfer = (kTransferPixels / tinydraw::kCanvasWidth) & ~1;
    for (int y = top; y < bottom; y += rows_per_transfer) {
      const int height = std::min(rows_per_transfer, bottom - y);
      push_rect(0, y, tinydraw::kCanvasWidth, height,
                canvas.data() + static_cast<std::size_t>(y * tinydraw::kCanvasWidth));
    }
    if (top == 0 && bottom == tinydraw::kCanvasHeight) {
      main_dirty_ = false;
      battery_dirty_ = false;
      toast_dirty_ = false;
      palette_dirty_ = false;
      palette_refresh_top_ = kMainOverlayTop;
      dialog_dirty_ = false;
    }
  }

  void push_world(std::span<const std::uint16_t> world, tinydraw::ViewOrigin origin, int bottom) {
    constexpr int rows_per_transfer = (kTransferPixels / tinydraw::kCanvasWidth) & ~1;
    for (int y = 0; y < bottom; y += rows_per_transfer) {
      const int height = std::min(rows_per_transfer, bottom - y);
      const auto offset =
          static_cast<std::size_t>((origin.y + y) * tinydraw::WorldCanvas::kWidth + origin.x);
      push_rect(0, y, tinydraw::kCanvasWidth, height, world.data() + offset,
                tinydraw::WorldCanvas::kWidth);
    }
  }

  void refresh_toolbar(std::span<const std::uint16_t> canvas) {
    refresh_toolbar_source(canvas, tinydraw::kCanvasWidth, 0, 0);
  }

  void refresh_toolbar_world(std::span<const std::uint16_t> world, tinydraw::ViewOrigin origin) {
    refresh_toolbar_source(world, tinydraw::WorldCanvas::kWidth, origin.x, origin.y);
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
      push_rect(0, palette_refresh_top_, tinydraw::kCanvasWidth,
                kMainOverlayTop - palette_refresh_top_, source.data() + offset, stride);
    }
    if (main_dirty_) {
      const auto offset = offset_at(0, kMainOverlayTop);
      push_rect(0, kMainOverlayTop, tinydraw::kCanvasWidth,
                tinydraw::kCanvasHeight - kMainOverlayTop, source.data() + offset, stride);
    }
    main_dirty_ = false;
    battery_dirty_ = false;
    toast_dirty_ = false;
    palette_dirty_ = false;
    palette_refresh_top_ = kMainOverlayTop;
    dialog_dirty_ = false;
  }

  void clear_overlay(int x, int y, int width, int height) {
    for (int row = 0; row < height; ++row) {
      auto* start = overlay_ + static_cast<std::ptrdiff_t>((y + row) * tinydraw::kCanvasWidth + x);
      std::fill_n(start, width, kBackground);
    }
  }

  esp_lcd_panel_io_handle_t io_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;
  std::uint16_t* overlay_ = nullptr;
  tinydraw::ToolbarState toolbar_{};
  int toolbar_top_ = tinydraw::toolbar_overlay_top(toolbar_);
  bool main_dirty_ = false;
  bool battery_dirty_ = false;
  tinydraw::Rect battery_refresh_{};
  bool toast_dirty_ = false;
  tinydraw::Rect toast_refresh_{};
  bool palette_dirty_ = false;
  int palette_refresh_top_ = kMainOverlayTop;
  bool dialog_dirty_ = false;
  std::int64_t prepare_us_ = 0;
  std::int64_t transfer_us_ = 0;
  std::uint32_t push_count_ = 0;
  std::size_t transfer_index_ = 0;
  bool ready_ = false;
};

enum class TouchRead { kPoint, kNoTouch, kError };

class PhysicalTouch {
 public:
  PhysicalTouch() {
    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = I2C_NUM_0;
    bus_config.sda_io_num = GPIO_NUM_15;
    bus_config.scl_io_num = GPIO_NUM_14;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bus_config, &bus_) != ESP_OK) {
      return;
    }
    esp_lcd_panel_io_i2c_config_t io_config{};
    io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_CST816S_ADDRESS;
    io_config.scl_speed_hz = 400000;
    io_config.control_phase_bytes = 1;
    io_config.lcd_cmd_bits = 8;
    io_config.flags.disable_control_phase = true;
    if (esp_lcd_new_panel_io_i2c(bus_, &io_config, &io_) != ESP_OK) {
      return;
    }
    esp_lcd_touch_config_t touch_config{};
    touch_config.x_max = tinydraw::kCanvasWidth;
    touch_config.y_max = tinydraw::kCanvasHeight;
    touch_config.rst_gpio_num = GPIO_NUM_NC;
    touch_config.int_gpio_num = GPIO_NUM_21;
    touch_config.levels.reset = 0;
    touch_config.levels.interrupt = 0;
    ready_ = esp_lcd_touch_new_i2c_cst816s(io_, &touch_config, &touch_) == ESP_OK;
    if (ready_) {
      std::uint8_t chip_id = 0;
      std::uint8_t firmware = 0;
      std::uint8_t scan_period = 0;
      std::uint8_t interrupt_mode = 0;
      const bool registers_read =
          esp_lcd_panel_io_rx_param(io_, 0xA7, &chip_id, 1) == ESP_OK &&
          esp_lcd_panel_io_rx_param(io_, 0xA9, &firmware, 1) == ESP_OK &&
          esp_lcd_panel_io_rx_param(io_, 0xEE, &scan_period, 1) == ESP_OK &&
          esp_lcd_panel_io_rx_param(io_, 0xFA, &interrupt_mode, 1) == ESP_OK;
      std::printf(
          "[DEBUG-touch-rate] registers=%u id=0x%02x firmware=0x%02x "
          "scan_period=0x%02x irq=0x%02x\n",
          registers_read, chip_id, firmware, scan_period, interrupt_mode);
    }
  }

  [[nodiscard]] bool ready() const { return ready_; }
  [[nodiscard]] i2c_master_bus_handle_t bus() const { return bus_; }

  TouchRead read(tinydraw::Point& point) {
    if (!ready_ || esp_lcd_touch_read_data(touch_) != ESP_OK) {
      return TouchRead::kError;
    }
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t strength = 0;
    std::uint8_t count = 0;
    if (!esp_lcd_touch_get_coordinates(touch_, &x, &y, &strength, &count, 1) || count == 0) {
      return TouchRead::kNoTouch;
    }
    point = {.x = static_cast<float>(x), .y = static_cast<float>(y)};
    return TouchRead::kPoint;
  }

 private:
  i2c_master_bus_handle_t bus_ = nullptr;
  esp_lcd_panel_io_handle_t io_ = nullptr;
  esp_lcd_touch_handle_t touch_ = nullptr;
  bool ready_ = false;
};

std::uint32_t timestamp_us() { return static_cast<std::uint32_t>(esp_timer_get_time()); }

using TouchEvent = tinydraw::DemoInputEvent;

enum class AppEventKind : std::uint8_t {
  kTouch,
  kResetForDemo,
  kDemoRecordingStarted,
  kDemoRecordingStopped,
  kDemoReplayStarted,
  kDemoReplayStopped,
  kPowerStatusChanged,
  kRefinementPublished,
};

struct AppEvent {
  TouchEvent touch{};
  tinydraw::esp32::PowerStatus power{};
  AppEventKind kind = AppEventKind::kTouch;
};

#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
void enqueue_refinement_published(void* raw) {
  auto queue = static_cast<QueueHandle_t>(raw);
  const AppEvent event{.kind = AppEventKind::kRefinementPublished};
  static_cast<void>(xQueueSend(queue, &event, 0));
}
#endif

struct TouchTaskContext {
  PhysicalTouch* touch = nullptr;
  QueueHandle_t queue = nullptr;
  tinydraw::DemoTape* tape = nullptr;
  std::span<const tinydraw::DemoSample> built_in_demo;
  tinydraw::esp32::PowerManager* power = nullptr;
  tinydraw::esp32::PowerStatus power_status{};
#ifdef TINYDRAW_PHASE2_PROTOTYPE
  tinydraw::esp32::Phase2TouchProbe* phase2_probe = nullptr;
#endif
};

void enqueue_latest(QueueHandle_t queue, const AppEvent& event) {
  if (xQueueSend(queue, &event, 0) == pdTRUE) {
    return;
  }
  AppEvent discarded;
  static_cast<void>(xQueueReceive(queue, &discarded, 0));
  static_cast<void>(xQueueSend(queue, &event, 0));
}

void enqueue_control(QueueHandle_t queue, AppEventKind kind) {
  const AppEvent event{.kind = kind};
  ESP_ERROR_CHECK(xQueueSend(queue, &event, portMAX_DELAY) == pdTRUE ? ESP_OK : ESP_FAIL);
}

void emit_touch(TouchTaskContext& context, const TouchEvent& event) {
  if (context.tape->recording()) {
    static_cast<void>(context.tape->record(event));
  }
  enqueue_latest(context.queue, {.touch = event});
}

void dump_demo(std::span<const tinydraw::DemoSample> samples, bool overflowed) {
  std::printf("TINYDRAW_DEMO_BEGIN count=%lu overflow=%u\n",
              static_cast<unsigned long>(samples.size()), overflowed);
  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto& sample = samples[index];
    std::printf("TINYDRAW_DEMO %lu %u %u %u\n", static_cast<unsigned long>(sample.offset_us),
                sample.x, sample.y, sample.touching);
    if (index % 64U == 63U) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  std::printf("TINYDRAW_DEMO_END\n");
}

void wait_for_demo_offset(std::uint32_t replay_started_us, std::uint32_t offset_us) {
  while (timestamp_us() - replay_started_us < offset_us) {
    const std::uint32_t elapsed = timestamp_us() - replay_started_us;
    const std::uint32_t remaining = offset_us - elapsed;
    if (remaining > 2'000U) {
      vTaskDelay(pdMS_TO_TICKS((remaining - 1'000U) / 1'000U));
    } else {
      esp_rom_delay_us(remaining);
    }
  }
}

void replay_demo(TouchTaskContext& context, std::span<const tinydraw::DemoSample> samples) {
  if (samples.empty()) {
    std::printf("TINYDRAW_DEMO_EMPTY\n");
    return;
  }
  xQueueReset(context.queue);
  enqueue_control(context.queue, AppEventKind::kDemoReplayStarted);
  enqueue_control(context.queue, AppEventKind::kResetForDemo);

  std::printf("TINYDRAW_DEMO_REPLAY_BEGIN count=%lu\n", static_cast<unsigned long>(samples.size()));
  const std::uint32_t replay_started_us = timestamp_us();
  for (const auto& sample : samples) {
    wait_for_demo_offset(replay_started_us, sample.offset_us);
    enqueue_latest(context.queue,
                   {.touch = tinydraw::replay_demo_sample(sample, replay_started_us)});
  }
  enqueue_control(context.queue, AppEventKind::kDemoReplayStopped);
  std::printf("TINYDRAW_DEMO_REPLAY_END\n");
}

void touch_task(void* argument) {
  auto& context = *static_cast<TouchTaskContext*>(argument);
  tinydraw::Point last_point{};
  bool touching = false;
  std::uint32_t no_touch_started_us = 0;
  bool raw_button_down = false;
  bool button_down = false;
  bool long_press_handled = false;
  std::uint32_t raw_button_changed_us = timestamp_us();
  std::uint32_t button_pressed_us = 0;
  std::uint32_t power_sampled_us = timestamp_us();
  while (true) {
    tinydraw::Point point{};
    const TouchRead read = context.touch->read(point);
    const std::uint32_t now = timestamp_us();
#ifdef TINYDRAW_PHASE2_PROTOTYPE
    if (context.phase2_probe != nullptr) {
      context.phase2_probe->record(now);
    }
#endif
    if (read == TouchRead::kPoint) {
      no_touch_started_us = 0;
      if (!touching || point.x != last_point.x || point.y != last_point.y) {
        emit_touch(context, {.point = point, .timestamp_us = now, .touching = true});
        last_point = point;
      }
      touching = true;
    } else if (read == TouchRead::kNoTouch && touching) {
      if (no_touch_started_us == 0U) {
        no_touch_started_us = now;
      } else if (now - no_touch_started_us >= 20'000U) {
        emit_touch(context, {.point = last_point, .timestamp_us = now, .touching = false});
        touching = false;
        no_touch_started_us = 0;
      }
    }

#ifndef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
    const bool next_raw_button_down = gpio_get_level(kDemoButton) == 0;
    if (next_raw_button_down != raw_button_down) {
      raw_button_down = next_raw_button_down;
      raw_button_changed_us = now;
    }
    if (raw_button_down != button_down && now - raw_button_changed_us >= 25'000U) {
      button_down = raw_button_down;
      if (button_down) {
        button_pressed_us = now;
        long_press_handled = false;
      } else if (context.tape->recording()) {
        context.tape->stop_recording();
        enqueue_control(context.queue, AppEventKind::kDemoRecordingStopped);
        std::printf("TINYDRAW_DEMO_RECORDING_END count=%lu overflow=%u\n",
                    static_cast<unsigned long>(context.tape->size()), context.tape->overflowed());
        dump_demo(context.tape->samples(), context.tape->overflowed());
      } else if (!long_press_handled) {
        context.tape->begin_recording(now);
        enqueue_control(context.queue, AppEventKind::kDemoRecordingStarted);
        std::printf("TINYDRAW_DEMO_RECORDING_BEGIN capacity=%lu\n",
                    static_cast<unsigned long>(kDemoCapacity));
      }
    }
    if (button_down && !context.tape->recording() && !long_press_handled &&
        now - button_pressed_us >= kDemoLongPressUs) {
      const auto recorded = context.tape->samples();
      replay_demo(context, recorded.empty() ? context.built_in_demo : recorded);
      long_press_handled = true;
    }
#endif
    if (!touching && context.power != nullptr && context.power->ready() &&
        now - power_sampled_us >= kPowerRefreshUs) {
      const auto power_status = context.power->read();
      power_sampled_us = now;
      if (power_status.valid && power_status != context.power_status) {
        const AppEvent event{.power = power_status, .kind = AppEventKind::kPowerStatusChanged};
        if (xQueueSend(context.queue, &event, 0) == pdTRUE) {
          context.power_status = power_status;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

}  // namespace

void run_hardware_app() {
  PhysicalDisplay display;
  PhysicalTouch touch;
  tinydraw::esp32::PowerManager power(touch.bus());
  tinydraw::esp32::RtcClock clock(touch.bus());
  if (!display.ready() || !touch.ready()) {
    std::printf("TINYDRAW_HARDWARE_FAIL display=%u touch=%u\n", display.ready(), touch.ready());
    return;
  }

  tinydraw::esp32::FirmwareCanvas canvas(display);
  if (!canvas.ready() || !canvas.capabilities_valid()) {
    std::printf("TINYDRAW_HARDWARE_FAIL canvas=0\n");
    return;
  }

  auto* vector_strokes = static_cast<tinydraw::VectorStroke*>(heap_caps_malloc(
      kVectorStrokeCapacity * sizeof(tinydraw::VectorStroke), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  auto* vector_samples = static_cast<tinydraw::StrokeSample*>(heap_caps_malloc(
      kVectorSampleCapacity * sizeof(tinydraw::StrokeSample), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (vector_strokes == nullptr || vector_samples == nullptr) {
    std::printf("TINYDRAW_HARDWARE_FAIL vector_strokes=%u vector_samples=%u\n",
                vector_strokes != nullptr, vector_samples != nullptr);
    return;
  }
  tinydraw::VectorDocument vector_document(std::span(vector_strokes, kVectorStrokeCapacity),
                                           std::span(vector_samples, kVectorSampleCapacity));
  std::printf("TINYDRAW_VECTOR_READY strokes=%lu samples=%lu bytes=%lu free_psram=%lu\n",
              static_cast<unsigned long>(kVectorStrokeCapacity),
              static_cast<unsigned long>(kVectorSampleCapacity),
              static_cast<unsigned long>(kVectorStrokeCapacity * sizeof(tinydraw::VectorStroke) +
                                         kVectorSampleCapacity * sizeof(tinydraw::StrokeSample)),
              static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  tinydraw::esp32::DrawingStore drawing_store;
  tinydraw::esp32::ImageExportStore image_export_store;
  tinydraw::esp32::UsbExport usb_export(image_export_store);
  if (drawing_store.ready()) {
    if (!drawing_store.restore(canvas.world(), canvas.committed(), canvas.visible())) {
      std::printf("TINYDRAW_AUTOSAVE_RESTORE_FAIL\n");
    } else {
      std::printf("TINYDRAW_AUTOSAVE_READY\n");
    }
  } else {
    std::printf("TINYDRAW_AUTOSAVE_DISABLED\n");
  }
  std::printf(
      "TINYDRAW_PSRAM world_bytes=%lu free=%lu largest=%lu minimum=%lu\n",
      static_cast<unsigned long>(tinydraw::WorldCanvas::kRequiredPixels * sizeof(std::uint16_t)),
      static_cast<unsigned long>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned long>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
      static_cast<unsigned long>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM)));

  const auto initial_power_status = power.read();
  tinydraw::esp32::PowerStatus current_power_status = initial_power_status;
  tinydraw::ToolbarState toolbar;
  toolbar.can_export = image_export_store.ready();
  toolbar.battery_percentage = initial_power_status.percentage;
  toolbar.battery_charging = initial_power_status.charging;
  toolbar.external_power = initial_power_status.external_power;
  std::printf(
      "TINYDRAW_POWER_READY ready=%u button=%u valid=%u battery=%d voltage_mv=%u "
      "charging=%u vbus=%u\n",
      power.ready(), power.power_button_ready(), initial_power_status.valid,
      initial_power_status.percentage, initial_power_status.battery_mv,
      initial_power_status.charging, initial_power_status.external_power);
  display.set_toolbar(toolbar);
  display.push_canvas(canvas.committed());
#ifdef TINYDRAW_RASTER_PAN_BENCHMARK
  vTaskDelay(pdMS_TO_TICKS(500));
  tinydraw::esp32::run_raster_pan_benchmark(canvas.world(), display, kMainOverlayTop);
#endif
#ifdef TINYDRAW_VECTOR_BENCHMARK
  toolbar.recording = true;
  display.set_toolbar(toolbar);
  display.refresh_toolbar(canvas.committed());
  std::printf("TINYDRAW_VECTOR_BENCH_PENDING delay_ms=1000 stack_bytes=16384\n");
  std::fflush(stdout);
  vTaskDelay(pdMS_TO_TICKS(1'000));
  // The renderer's ribbon path recurses through ~1 KiB RibbonUpdate frames and
  // overflows the 6 KiB main-task stack; give the benchmark its own task.
  {
    struct BenchmarkTask {
      std::span<std::uint16_t> destination;
      std::atomic<bool> done{false};
    } benchmark_task{.destination = canvas.visible()};
    const auto entry = [](void* raw) {
      auto* task = static_cast<BenchmarkTask*>(raw);
      tinydraw::esp32::run_vector_benchmarks(task->destination);
      task->done.store(true);
      vTaskDelete(nullptr);
    };
    if (xTaskCreatePinnedToCore(entry, "vector_bench", 16'384U, &benchmark_task, 2U, nullptr, 0) ==
        pdPASS) {
      while (!benchmark_task.done.load()) {
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    } else {
      std::printf("TINYDRAW_VECTOR_BENCH_FAIL task=0\n");
    }
  }
  std::copy(canvas.committed().begin(), canvas.committed().end(), canvas.visible().begin());
  toolbar.recording = false;
  display.set_toolbar(toolbar);
  display.refresh_toolbar(canvas.committed());
#endif
#if !defined(TINYDRAW_PHASE2_PROTOTYPE) && !defined(TINYDRAW_INTERACTIVE_PAN_BENCHMARK)
  static_cast<void>(tinydraw::esp32::start_time_sync(clock));
#endif

  tinydraw::InkConfig brush;
  brush.size = tinydraw::brush_size(toolbar.size);
  tinydraw::InkStream ink(brush);
  tinydraw::CurvedRibbonStream ribbon;
  tinydraw::InkPoint last_ink{};
  tinydraw::Point last_touch{};
  std::uint16_t stroke_color = tinydraw::rgb565(toolbar.color);
  bool pressed = false;
  bool panning = false;
  bool demo_replaying = false;
  bool persistence_resync_needed = false;
  tinydraw::Point previous_saved_touch{};
  tinydraw::Point pan_start_touch{};
  tinydraw::ViewOrigin pan_start_origin{};
  tinydraw::ViewOrigin vector_stroke_origin{};
  bool vector_stroke_recording = false;
  bool vector_stroke_committed = false;
  bool vector_stroke_refused = false;
  bool toolbar_pressed = false;
  tinydraw::Point toolbar_sum{};
  std::uint32_t toolbar_samples = 0;
  std::uint32_t stroke_samples = 0;
  std::uint32_t stroke_started_us = 0;
  std::uint32_t previous_touch_us = 0;
  std::uint64_t touch_intervals_us = 0;
  std::uint32_t maximum_touch_interval_us = 0;
  std::uint32_t maximum_input_lag_us = 0;
  UBaseType_t maximum_queue_depth = 0;
  std::int64_t stroke_render_us = 0;
  std::int64_t maximum_render_us = 0;
  std::uint32_t pan_frames = 0;
  std::int64_t pan_render_us = 0;
  std::int64_t maximum_pan_render_us = 0;
  std::uint32_t export_toast_until_us = 0;
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
  tinydraw::esp32::InteractivePanBenchmark* interactive_pan_benchmark = nullptr;
#endif

  const auto close_popups = [&] {
    toolbar.tools_open = false;
    toolbar.colors_open = false;
    toolbar.sizes_open = false;
  };
  const auto reset_stroke = [&] {
    ink.end();
    ribbon.reset();
    canvas.raster().cancel();
    vector_document.cancel_stroke();
    vector_stroke_recording = false;
    vector_stroke_committed = false;
    vector_stroke_refused = false;
  };
  const auto update_toolbar = [&] {
    toolbar.can_undo = canvas.undo_history().can_undo();
    display.set_toolbar(toolbar);
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
    if (interactive_pan_benchmark != nullptr) {
      tinydraw::esp32::interactive_pan_benchmark_lock_cache(*interactive_pan_benchmark);
      display.refresh_toolbar_world(canvas.world().pixels(), canvas.world().origin());
      tinydraw::esp32::interactive_pan_benchmark_unlock_cache(*interactive_pan_benchmark);
    } else {
      display.refresh_toolbar(canvas.committed());
    }
#else
    display.refresh_toolbar(canvas.committed());
#endif
  };
  const auto select_size = [&](tinydraw::PenSize size) {
    toolbar.size = size;
    auto config = ink.config();
    config.size = tinydraw::brush_size(size);
    ink.set_config(config);
    close_popups();
  };
  const auto pan_to = [&](tinydraw::Point point, std::uint32_t event_us) {
    const int delta_x = static_cast<int>(std::lround(point.x - pan_start_touch.x));
    const int delta_y = static_cast<int>(std::lround(point.y - pan_start_touch.y));
    const auto started = esp_timer_get_time();
    const tinydraw::ViewOrigin requested_origin{pan_start_origin.x - delta_x,
                                                pan_start_origin.y - delta_y};
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
    tinydraw::esp32::interactive_pan_benchmark_lock_cache(*interactive_pan_benchmark);
    const tinydraw::ViewOrigin previous_origin = canvas.world().origin();
    if (!canvas.world().move_to(requested_origin)) {
      tinydraw::esp32::interactive_pan_benchmark_unlock_cache(*interactive_pan_benchmark);
      return;
    }
    const tinydraw::ViewOrigin actual_origin = canvas.world().origin();
    if (!tinydraw::esp32::interactive_pan_benchmark_view_changed(*interactive_pan_benchmark,
                                                                 actual_origin)) {
      static_cast<void>(canvas.world().move_to(previous_origin));
      tinydraw::esp32::interactive_pan_benchmark_unlock_cache(*interactive_pan_benchmark);
      return;
    }
#else
    if (!canvas.world().move_to(requested_origin)) {
      return;
    }
#endif
    display.push_world(canvas.world().pixels(), canvas.world().origin(), kMainOverlayTop);
    const auto finished = esp_timer_get_time();
    const auto elapsed = finished - started;
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
    // Snapshot readiness before allowing the renderer to publish another band,
    // so miss metrics describe the pixels that were actually presented.
    tinydraw::esp32::interactive_pan_benchmark_record_frame(
        *interactive_pan_benchmark, canvas.world().origin(), event_us,
        static_cast<std::uint32_t>(elapsed), static_cast<std::uint32_t>(finished) - event_us);
    tinydraw::esp32::interactive_pan_benchmark_unlock_cache(*interactive_pan_benchmark);
#endif
    ++pan_frames;
    pan_render_us += elapsed;
    maximum_pan_render_us = std::max(maximum_pan_render_us, elapsed);
  };
  const auto new_drawing = [&] {
    reset_stroke();
    vector_document.clear();
    vector_stroke_recording = false;
    canvas.undo_history().begin_entry(canvas.world().origin());
    canvas.undo_history().capture_canvas(canvas.committed());
    static_cast<void>(canvas.undo_history().commit_entry());
    static_cast<void>(canvas.world().clear(canvas.committed(), canvas.visible()));
    toolbar.export_ready = false;
    toolbar.confirm_new = false;
    close_popups();
    toolbar.can_undo = canvas.undo_history().can_undo();
    display.set_toolbar(toolbar);
    display.push_canvas(canvas.committed());
    if (!demo_replaying) {
      drawing_store.save_all(canvas.world());
      persistence_resync_needed = false;
    }
  };
  const auto reset_for_demo = [&] {
    reset_stroke();
    pressed = false;
    panning = false;
    toolbar_pressed = false;
    toolbar_samples = 0;
    toolbar = {};
    toolbar.can_export = image_export_store.ready();
    toolbar.battery_percentage = current_power_status.percentage;
    toolbar.battery_charging = current_power_status.charging;
    toolbar.external_power = current_power_status.external_power;
    auto config = ink.config();
    config.size = tinydraw::brush_size(toolbar.size);
    ink.set_config(config);
    stroke_color = tinydraw::rgb565(toolbar.color);
    canvas.undo_history().clear();
    static_cast<void>(canvas.world().clear(canvas.committed(), canvas.visible()));
    display.set_toolbar(toolbar);
    display.push_canvas(canvas.committed());
  };
  const auto undo = [&] {
    if (!canvas.undo_history().can_undo()) {
      return;
    }
    reset_stroke();
    display.reset_timing();
    const auto started = esp_timer_get_time();
    static_cast<void>(canvas.world().capture(canvas.committed()));
    const auto undo_origin = canvas.undo_history().next_undo_origin();
    const bool view_changed = undo_origin.has_value() && *undo_origin != canvas.world().origin();
    if (view_changed) {
      static_cast<void>(canvas.world().show(*undo_origin, canvas.committed(), canvas.visible()));
    }
    const auto stats = canvas.undo_history().undo(canvas.committed(), canvas.visible(),
                                                  view_changed ? nullptr : &display);
    static_cast<void>(canvas.world().capture(canvas.committed()));
    close_popups();
    toolbar.export_ready = false;
    toolbar.can_undo = canvas.undo_history().can_undo();
    display.set_toolbar(toolbar);
    if (view_changed) {
      display.push_canvas(canvas.committed());
    } else {
      display.refresh_toolbar(canvas.committed());
    }
    if (!demo_replaying) {
      if (persistence_resync_needed) {
        drawing_store.save_all(canvas.world());
      } else {
        drawing_store.save_viewport(canvas.world(), canvas.committed());
      }
      persistence_resync_needed = false;
    }
    std::printf(
        "[DEBUG-undo1] tiles=%lu bytes=%lu view_changed=%u elapsed_us=%lld prepare_us=%lld "
        "transfer_us=%lld pushes=%lu\n",
        static_cast<unsigned long>(stats.tiles_restored),
        static_cast<unsigned long>(stats.display_bytes), view_changed,
        static_cast<long long>(esp_timer_get_time() - started),
        static_cast<long long>(display.prepare_us()), static_cast<long long>(display.transfer_us()),
        static_cast<unsigned long>(display.push_count()));
  };
  const auto export_image = [&] {
    reset_stroke();
    usb_export.prepare_export();
    tinydraw::FatDateTime modified_time;
    if (clock.read(modified_time)) {
      usb_export.set_modified_time(modified_time);
    }
    static_cast<void>(canvas.world().capture(canvas.committed()));
    toolbar.exporting = true;
    toolbar.export_ready = false;
    toolbar.export_toast = true;
    close_popups();
    update_toolbar();
    vTaskDelay(pdMS_TO_TICKS(20));
    const auto stats = image_export_store.encode(canvas.world().pixels());
    std::printf("TINYDRAW_EXPORT success=%u bytes=%lu elapsed_us=%lld free_psram=%lu\n",
                stats.success, static_cast<unsigned long>(stats.bytes),
                static_cast<long long>(stats.elapsed_us),
                static_cast<unsigned long>(stats.free_psram));
    std::fflush(stdout);
    const bool usb_ready = usb_export.finish_export(stats.success);
    toolbar.exporting = false;
    toolbar.export_ready = stats.success && usb_ready;
    toolbar.export_toast = true;
    export_toast_until_us = timestamp_us() + 3'000'000U;
    close_popups();
    update_toolbar();
  };
  const auto toolbar_action = [&](tinydraw::Point point) {
    const auto action = tinydraw::toolbar_action_at(point, toolbar);
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
    if (action == tinydraw::ToolbarAction::kSelectSmall ||
        action == tinydraw::ToolbarAction::kSelectMedium ||
        action == tinydraw::ToolbarAction::kSelectLarge) {
      const int zoom_percent = action == tinydraw::ToolbarAction::kSelectSmall    ? 50
                               : action == tinydraw::ToolbarAction::kSelectMedium ? 100
                                                                                  : 200;
      toolbar.size = action == tinydraw::ToolbarAction::kSelectSmall    ? tinydraw::PenSize::kSmall
                     : action == tinydraw::ToolbarAction::kSelectMedium ? tinydraw::PenSize::kMedium
                                                                        : tinydraw::PenSize::kLarge;
      close_popups();
      const bool zoom_changed = tinydraw::esp32::interactive_pan_benchmark_set_zoom(
          *interactive_pan_benchmark, zoom_percent);
      if (!zoom_changed) {
        std::printf("TINYDRAW_INTERACTIVE_PAN_FAIL zoom=%d\n", zoom_percent);
      }
      // set_zoom presents the transition itself as center-out strips.
      update_toolbar();
      return;
    }
    if (action == tinydraw::ToolbarAction::kSelectExtraLarge) {
      close_popups();
      toolbar.recording = false;
      static_cast<void>(
          tinydraw::esp32::finish_interactive_pan_benchmark(*interactive_pan_benchmark));
      update_toolbar();
      return;
    }
    // Pen and pan remain available so the same materialized document can be
    // mutated and exercised. Other production actions stay outside this
    // throwaway prototype.
    if (action != tinydraw::ToolbarAction::kToggleSizes &&
        action != tinydraw::ToolbarAction::kToggleTools &&
        action != tinydraw::ToolbarAction::kSelectPen &&
        action != tinydraw::ToolbarAction::kSelectPan &&
        action != tinydraw::ToolbarAction::kSelectEraser &&
        action != tinydraw::ToolbarAction::kToggleColors &&
        action != tinydraw::ToolbarAction::kSelectColor &&
        action != tinydraw::ToolbarAction::kNone) {
      close_popups();
      update_toolbar();
      return;
    }
#endif
    switch (action) {
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
        toolbar.color = tinydraw::toolbar_color_at(point, toolbar).value_or(toolbar.color);
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
        export_image();
        return;
      case tinydraw::ToolbarAction::kUndo:
        undo();
        return;
      case tinydraw::ToolbarAction::kNewDrawing:
        close_popups();
        toolbar.confirm_new = true;
        break;
      case tinydraw::ToolbarAction::kCancelNewDrawing:
        toolbar.confirm_new = false;
        break;
      case tinydraw::ToolbarAction::kConfirmNewDrawing:
        new_drawing();
        return;
      case tinydraw::ToolbarAction::kNone:
        break;
    }
    update_toolbar();
  };

  auto* demo_storage = static_cast<tinydraw::DemoSample*>(heap_caps_malloc(
      kDemoCapacity * sizeof(tinydraw::DemoSample), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  QueueHandle_t touch_queue = xQueueCreate(32U, sizeof(AppEvent));
  if (demo_storage == nullptr || touch_queue == nullptr) {
    std::printf("TINYDRAW_HARDWARE_FAIL demo_storage=%u touch_queue=%u\n", demo_storage != nullptr,
                touch_queue != nullptr);
    return;
  }
  tinydraw::DemoTape demo_tape(std::span(demo_storage, kDemoCapacity));
  gpio_config_t demo_button_config{};
  demo_button_config.pin_bit_mask = 1ULL << static_cast<unsigned>(kDemoButton);
  demo_button_config.mode = GPIO_MODE_INPUT;
  demo_button_config.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK(gpio_config(&demo_button_config));

#ifdef TINYDRAW_PHASE2_PROTOTYPE
  tinydraw::esp32::Phase2TouchProbe phase2_touch_probe;
#endif
  TouchTaskContext touch_context{
      .touch = &touch,
      .queue = touch_queue,
      .tape = &demo_tape,
      .built_in_demo = tinydraw::esp32::demo::kBuiltInDemo,
      .power = &power,
      .power_status = initial_power_status,
#ifdef TINYDRAW_PHASE2_PROTOTYPE
      .phase2_probe = &phase2_touch_probe,
#endif
  };
  if (xTaskCreatePinnedToCore(touch_task, "tinydraw_touch", 4096U, &touch_context, 5U, nullptr,
                              1) != pdPASS) {
    std::printf("TINYDRAW_HARDWARE_FAIL touch_task=0\n");
    return;
  }

#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
  drawing_store.suspend();
  toolbar.tool = tinydraw::DrawingTool::kPan;
  toolbar.size = tinydraw::PenSize::kMedium;
  toolbar.recording = true;
  close_popups();
  display.set_toolbar(toolbar);
  display.refresh_toolbar(canvas.committed());
  const std::int64_t benchmark_init_started = esp_timer_get_time();
  interactive_pan_benchmark = tinydraw::esp32::start_interactive_pan_benchmark(
      vector_document, canvas.world(), canvas.prototype_materialization_storage(), canvas.visible(),
      kMainOverlayTop, display, enqueue_refinement_published, touch_queue,
      {.submit_count = transfer_submit_count,
       .complete_count = transfer_complete_count,
       .complete_time_us = transfer_complete_time_us,
       .context = nullptr});
  if (interactive_pan_benchmark == nullptr) {
    std::printf("TINYDRAW_HARDWARE_FAIL interactive_pan_benchmark=0\n");
    return;
  }
  xQueueReset(touch_queue);
  tinydraw::esp32::interactive_pan_benchmark_lock_cache(*interactive_pan_benchmark);
  display.push_world(canvas.world().pixels(), canvas.world().origin(), kMainOverlayTop);
  tinydraw::esp32::interactive_pan_benchmark_record_zoom_present(*interactive_pan_benchmark);
  tinydraw::esp32::interactive_pan_benchmark_unlock_cache(*interactive_pan_benchmark);
  std::printf("TINYDRAW_INTERACTIVE_PAN_READY zoom=100 controls=S:50 M:100 L:200 XL:finish\n");
  std::printf("TINYDRAW_AUTO_INIT initial_atlas_us=%lld\n",
              static_cast<long long>(esp_timer_get_time() - benchmark_init_started));
  // Hands-free A/B zoom driver: exercises the same set_zoom -> push_world ->
  // record_zoom_present path the toolbar uses, without touch input, and prints
  // driver-side wall times so baseline and patched firmware are comparable
  // regardless of internal metric definitions.
  {
    constexpr std::array<int, 12> kAutoZoomSequence{50,  100, 200, 100, 50,  100,
                                                    200, 100, 50,  100, 200, 100};
    std::fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(2'000));
    for (std::size_t cycle = 0; cycle < kAutoZoomSequence.size(); ++cycle) {
      const int percent = kAutoZoomSequence[cycle];
      const std::int64_t auto_zoom_started = esp_timer_get_time();
      const bool changed = tinydraw::esp32::interactive_pan_benchmark_set_zoom(
          *interactive_pan_benchmark, percent);
      const std::int64_t auto_zoom_returned = esp_timer_get_time();
      tinydraw::esp32::ZoomTransitionTiming timing;
      if (changed && tinydraw::esp32::interactive_pan_benchmark_last_zoom_timing(
                         *interactive_pan_benchmark, percent, timing)) {
        std::printf(
            "TINYDRAW_AUTO_ZOOM cycle=%u zoom=%d changed=1 total_us=%lld cancel_us=%lu "
            "first_ready_us=%lu first_submit_us=%lu first_complete_us=%lu last_submit_us=%lu "
            "last_complete_us=%lu fallback_us=%lu settled_us=%lu\n",
            static_cast<unsigned>(cycle), percent,
            static_cast<long long>(auto_zoom_returned - auto_zoom_started),
            static_cast<unsigned long>(timing.cancel_done_us),
            static_cast<unsigned long>(timing.first_strip_ready_us),
            static_cast<unsigned long>(timing.first_strip_submit_us),
            static_cast<unsigned long>(timing.first_strip_complete_us),
            static_cast<unsigned long>(timing.last_visible_submit_us),
            static_cast<unsigned long>(timing.last_visible_complete_us),
            static_cast<unsigned long>(timing.fallback_ready_us),
            static_cast<unsigned long>(timing.settled_us));
      } else {
        std::printf("TINYDRAW_AUTO_ZOOM cycle=%u zoom=%d changed=0 total_us=%lld\n",
                    static_cast<unsigned>(cycle), percent,
                    static_cast<long long>(auto_zoom_returned - auto_zoom_started));
      }
      std::fflush(stdout);
      vTaskDelay(pdMS_TO_TICKS(5'000));
    }
    std::printf("TINYDRAW_AUTO_ZOOM_DONE\n");
    std::fflush(stdout);
  }
#endif

#ifdef TINYDRAW_PHASE2_PROTOTYPE
  toolbar.recording = true;
  display.set_toolbar(toolbar);
  display.refresh_toolbar(canvas.committed());
  std::printf("TINYDRAW_PHASE2_PENDING delay_ms=1000 stack_bytes=16384\n");
  std::fflush(stdout);
  vTaskDelay(pdMS_TO_TICKS(1'000));
  {
    struct PrototypeTask {
      std::span<std::uint16_t> cache;
      std::span<std::uint16_t> reference;
      tinydraw::DisplayBackend* display = nullptr;
      tinydraw::esp32::Phase2TouchProbe* touch_probe = nullptr;
      std::atomic<bool> done{false};
    } prototype_task{
        .cache = canvas.visible(),
        .reference = canvas.committed(),
        .display = &display,
        .touch_probe = &phase2_touch_probe,
    };
    const auto entry = [](void* raw) {
      auto* task = static_cast<PrototypeTask*>(raw);
      tinydraw::esp32::run_phase2_prototype(task->cache, task->reference, *task->display,
                                            *task->touch_probe);
      task->done.store(true);
      vTaskDelete(nullptr);
    };
    if (xTaskCreatePinnedToCore(entry, "phase2_proto", 16'384U, &prototype_task, 2U, nullptr, 0) ==
        pdPASS) {
      while (!prototype_task.done.load()) {
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    } else {
      std::printf("TINYDRAW_PHASE2_FAIL task=0\n");
    }
  }
  xQueueReset(touch_queue);
  static_cast<void>(
      canvas.world().show(canvas.world().origin(), canvas.committed(), canvas.visible()));
  toolbar.recording = false;
  display.set_toolbar(toolbar);
  display.push_canvas(canvas.committed());
  static_cast<void>(tinydraw::esp32::start_time_sync(clock));
#endif

  std::printf("TINYDRAW_HARDWARE_OK display=CO5300 touch=CST820 demo_capacity=%lu\n",
              static_cast<unsigned long>(kDemoCapacity));
  while (true) {
    if (toolbar.export_toast &&
        static_cast<std::int32_t>(timestamp_us() - export_toast_until_us) >= 0) {
      toolbar.export_toast = false;
      update_toolbar();
    }
    AppEvent app_event;
    const TickType_t wait = toolbar.export_toast ? pdMS_TO_TICKS(50) : portMAX_DELAY;
    if (xQueueReceive(touch_queue, &app_event, wait) != pdTRUE) {
      continue;
    }
    if (app_event.kind == AppEventKind::kDemoReplayStarted ||
        app_event.kind == AppEventKind::kDemoReplayStopped) {
      demo_replaying = app_event.kind == AppEventKind::kDemoReplayStarted;
      if (demo_replaying) {
        drawing_store.suspend();
      } else {
        persistence_resync_needed = true;
      }
      continue;
    }
    if (app_event.kind == AppEventKind::kResetForDemo) {
      reset_for_demo();
      continue;
    }
    if (app_event.kind == AppEventKind::kRefinementPublished) {
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
      update_toolbar();
#endif
      continue;
    }
    if (app_event.kind == AppEventKind::kPowerStatusChanged) {
      current_power_status = app_event.power;
      toolbar.battery_percentage = current_power_status.percentage;
      toolbar.battery_charging = current_power_status.charging;
      toolbar.external_power = current_power_status.external_power;
      update_toolbar();
      std::printf("TINYDRAW_POWER battery=%d voltage_mv=%u charging=%u vbus=%u\n",
                  current_power_status.percentage, current_power_status.battery_mv,
                  current_power_status.charging, current_power_status.external_power);
      continue;
    }
    if (app_event.kind == AppEventKind::kDemoRecordingStarted ||
        app_event.kind == AppEventKind::kDemoRecordingStopped) {
      toolbar.recording = app_event.kind == AppEventKind::kDemoRecordingStarted;
      update_toolbar();
      continue;
    }
    TouchEvent event = app_event.touch;
    if (toolbar.export_toast) {
      toolbar.export_toast = false;
      update_toolbar();
    }
    if (panning && event.touching) {
      AppEvent latest;
      while (xQueuePeek(touch_queue, &latest, 0) == pdTRUE && latest.kind == AppEventKind::kTouch) {
        static_cast<void>(xQueueReceive(touch_queue, &latest, 0));
        event = latest.touch;
        if (!event.touching) {
          break;
        }
      }
    }
    const tinydraw::Point point = event.point;
    const bool touching = event.touching;
    if (!demo_replaying) {
      drawing_store.activity();
    }
    const std::uint32_t input_lag_us = timestamp_us() - event.timestamp_us;
    const UBaseType_t queue_depth = uxQueueMessagesWaiting(touch_queue);
    if (touching && !pressed) {
      std::printf("[DEBUG-hw1] down x=%.0f y=%.0f\n", static_cast<double>(point.x),
                  static_cast<double>(point.y));
      pressed = true;
      last_touch = point;
      if (toolbar.confirm_new) {
        const auto action = tinydraw::toolbar_action_at(point, toolbar);
        if (action == tinydraw::ToolbarAction::kCancelNewDrawing ||
            action == tinydraw::ToolbarAction::kConfirmNewDrawing) {
          toolbar_action(point);
          continue;
        }
      }
      if (tinydraw::toolbar_contains(point, toolbar)) {
        toolbar_pressed = true;
        toolbar_sum = point;
        toolbar_samples = 1;
      } else {
        close_popups();
        if (toolbar.tool == tinydraw::DrawingTool::kPan) {
#ifndef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
          static_cast<void>(canvas.world().capture(canvas.committed()));
#endif
          pan_start_touch = point;
          pan_start_origin = canvas.world().origin();
          pan_frames = 0;
          pan_render_us = 0;
          maximum_pan_render_us = 0;
          display.reset_timing();
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
          tinydraw::esp32::interactive_pan_benchmark_begin_pan(
              *interactive_pan_benchmark, pan_start_origin, event.timestamp_us);
#endif
          panning = true;
          continue;
        }
        stroke_color = toolbar.tool == tinydraw::DrawingTool::kEraser
                           ? kBackground
                           : tinydraw::rgb565(toolbar.color);
        previous_saved_touch = point;
        if (!demo_replaying) {
          drawing_store.include_segment(point, point, ink.config().size + 4.0F,
                                        canvas.world().origin());
        }
        last_ink = ink.begin({.x = point.x, .y = point.y, .timestamp_us = event.timestamp_us});
        vector_stroke_origin = canvas.world().origin();
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
        const bool mutation_ready =
            tinydraw::esp32::interactive_pan_benchmark_begin_stroke(*interactive_pan_benchmark);
        const auto first_vector_sample = tinydraw::esp32::interactive_pan_benchmark_map_sample(
            *interactive_pan_benchmark, last_ink.position, last_ink.radius);
#else
        const bool mutation_ready = true;
        const tinydraw::StrokeSample first_vector_sample{
            .x = last_ink.position.x + static_cast<float>(vector_stroke_origin.x),
            .y = last_ink.position.y + static_cast<float>(vector_stroke_origin.y),
            .radius = last_ink.radius,
        };
#endif
        vector_stroke_recording =
            !demo_replaying && mutation_ready &&
            vector_document.begin_stroke(stroke_color,
                                         toolbar.tool == tinydraw::DrawingTool::kEraser
                                             ? tinydraw::VectorTool::kEraser
                                             : tinydraw::VectorTool::kPen,
                                         first_vector_sample);
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
        if (mutation_ready && !vector_stroke_recording) {
          tinydraw::esp32::interactive_pan_benchmark_cancel_stroke(*interactive_pan_benchmark);
        }
#endif
        vector_stroke_committed = false;
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
        vector_stroke_refused = !vector_stroke_recording;
        if (vector_stroke_recording) {
          tinydraw::esp32::interactive_pan_benchmark_lock_cache(*interactive_pan_benchmark);
          static_cast<void>(
              canvas.world().show(canvas.world().origin(), canvas.committed(), canvas.visible()));
          tinydraw::esp32::interactive_pan_benchmark_unlock_cache(*interactive_pan_benchmark);
        }
#else
        vector_stroke_refused = false;
#endif
        stroke_samples = 1;
        stroke_started_us = event.timestamp_us;
        previous_touch_us = event.timestamp_us;
        touch_intervals_us = 0;
        maximum_touch_interval_us = 0;
        maximum_input_lag_us = input_lag_us;
        maximum_queue_depth = queue_depth;
        stroke_render_us = 0;
        maximum_render_us = 0;
        display.reset_timing();
        const auto started = esp_timer_get_time();
        if (!vector_stroke_refused) {
          static_cast<void>(canvas.raster().update(ribbon.append(last_ink), stroke_color));
        }
        const auto elapsed = esp_timer_get_time() - started;
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
        tinydraw::esp32::interactive_pan_benchmark_record_draw_update(
            *interactive_pan_benchmark, static_cast<std::uint32_t>(elapsed));
#endif
        stroke_render_us += elapsed;
        maximum_render_us = std::max(maximum_render_us, elapsed);
      }
    } else if (touching && pressed && toolbar_pressed &&
               (point.x != last_touch.x || point.y != last_touch.y)) {
      last_touch = point;
      if (tinydraw::toolbar_contains(point, toolbar)) {
        toolbar_sum.x += point.x;
        toolbar_sum.y += point.y;
        ++toolbar_samples;
      }
    } else if (touching && pressed && panning &&
               (point.x != last_touch.x || point.y != last_touch.y)) {
      last_touch = point;
      pan_to(point, event.timestamp_us);
    } else if (touching && pressed && ink.active() &&
               (point.x != last_touch.x || point.y != last_touch.y)) {
      last_touch = point;
      if (!demo_replaying) {
        drawing_store.include_segment(previous_saved_touch, point, ink.config().size + 4.0F,
                                      canvas.world().origin());
      }
      previous_saved_touch = point;
      last_ink = ink.update({.x = point.x, .y = point.y, .timestamp_us = event.timestamp_us});
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
      const auto vector_sample = tinydraw::esp32::interactive_pan_benchmark_map_sample(
          *interactive_pan_benchmark, last_ink.position, last_ink.radius);
#else
      const tinydraw::StrokeSample vector_sample{
          .x = last_ink.position.x + static_cast<float>(vector_stroke_origin.x),
          .y = last_ink.position.y + static_cast<float>(vector_stroke_origin.y),
          .radius = last_ink.radius,
      };
#endif
      if (vector_stroke_recording && !vector_document.append(vector_sample)) {
        vector_document.cancel_stroke();
        vector_stroke_recording = false;
        vector_stroke_refused = true;
        std::printf("TINYDRAW_VECTOR_FULL strokes=%lu samples=%lu\n",
                    static_cast<unsigned long>(vector_document.stroke_count()),
                    static_cast<unsigned long>(vector_document.sample_count()));
      }
      const std::uint32_t touch_interval_us = event.timestamp_us - previous_touch_us;
      previous_touch_us = event.timestamp_us;
      touch_intervals_us += touch_interval_us;
      maximum_touch_interval_us = std::max(maximum_touch_interval_us, touch_interval_us);
      ++stroke_samples;
      maximum_input_lag_us = std::max(maximum_input_lag_us, input_lag_us);
      maximum_queue_depth = std::max(maximum_queue_depth, queue_depth);
      const auto started = esp_timer_get_time();
      if (!vector_stroke_refused) {
        static_cast<void>(canvas.raster().update(ribbon.append(last_ink), stroke_color));
      }
      const auto elapsed = esp_timer_get_time() - started;
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
      tinydraw::esp32::interactive_pan_benchmark_record_draw_update(
          *interactive_pan_benchmark, static_cast<std::uint32_t>(elapsed));
#endif
      stroke_render_us += elapsed;
      maximum_render_us = std::max(maximum_render_us, elapsed);
    } else if (!touching && pressed) {
      std::printf("[DEBUG-hw1] up x=%.0f y=%.0f active=%u\n", static_cast<double>(last_touch.x),
                  static_cast<double>(last_touch.y), ink.active());
      pressed = false;
      if (toolbar_pressed) {
        toolbar_pressed = false;
        const float divisor = static_cast<float>(toolbar_samples == 0 ? 1 : toolbar_samples);
        const tinydraw::Point tap{.x = toolbar_sum.x / divisor, .y = toolbar_sum.y / divisor};
        toolbar_samples = 0;
        if (tinydraw::toolbar_contains(tap, toolbar)) {
          toolbar_action(tap);
        }
        continue;
      }
      if (panning) {
        pan_to(point, event.timestamp_us);
        panning = false;
        std::int64_t settle_us = 0;
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
        tinydraw::esp32::interactive_pan_benchmark_end_pan(*interactive_pan_benchmark);
#else
        const auto settle_started = esp_timer_get_time();
        static_cast<void>(
            canvas.world().show(canvas.world().origin(), canvas.committed(), canvas.visible()));
        settle_us = esp_timer_get_time() - settle_started;
        if (!demo_replaying && !persistence_resync_needed) {
          drawing_store.save_origin(canvas.world());
        }
#endif
        const auto bytes = static_cast<std::uint64_t>(pan_frames) * tinydraw::kCanvasWidth *
                           kMainOverlayTop * sizeof(std::uint16_t);
        std::printf(
            "TINYDRAW_PAN_PERF frames=%lu bytes=%llu average_us=%lld max_us=%lld "
            "settle_us=%lld prepare_us=%lld transfer_us=%lld pushes=%lu\n",
            static_cast<unsigned long>(pan_frames), static_cast<unsigned long long>(bytes),
            static_cast<long long>(pan_frames == 0 ? 0 : pan_render_us / pan_frames),
            static_cast<long long>(maximum_pan_render_us), static_cast<long long>(settle_us),
            static_cast<long long>(display.prepare_us()),
            static_cast<long long>(display.transfer_us()),
            static_cast<unsigned long>(display.push_count()));
        continue;
      }
      if (ink.active()) {
        last_ink =
            ink.finish({.x = last_touch.x, .y = last_touch.y, .timestamp_us = event.timestamp_us});
        if (vector_stroke_recording) {
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
          const auto final_vector_sample = tinydraw::esp32::interactive_pan_benchmark_map_sample(
              *interactive_pan_benchmark, last_ink.position, last_ink.radius);
#else
          const tinydraw::StrokeSample final_vector_sample{
              .x = last_ink.position.x + static_cast<float>(vector_stroke_origin.x),
              .y = last_ink.position.y + static_cast<float>(vector_stroke_origin.y),
              .radius = last_ink.radius,
          };
#endif
          if (vector_document.append(final_vector_sample) && vector_document.finish_stroke()) {
            vector_stroke_committed = true;
            std::printf("TINYDRAW_VECTOR_STROKE strokes=%lu samples=%lu\n",
                        static_cast<unsigned long>(vector_document.stroke_count()),
                        static_cast<unsigned long>(vector_document.sample_count()));
          } else {
            vector_document.cancel_stroke();
            std::printf("TINYDRAW_VECTOR_FULL strokes=%lu samples=%lu\n",
                        static_cast<unsigned long>(vector_document.stroke_count()),
                        static_cast<unsigned long>(vector_document.sample_count()));
          }
          vector_stroke_recording = false;
        }
        const auto started = esp_timer_get_time();
#ifdef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
        if (vector_stroke_refused || !vector_stroke_committed) {
          canvas.raster().cancel();
          tinydraw::esp32::interactive_pan_benchmark_cancel_stroke(*interactive_pan_benchmark);
        } else {
          // Raster Undo owns the prototype's inactive atlas arena, so it must
          // not participate in this deliberately history-free benchmark.
          static_cast<void>(canvas.raster().finish(ribbon.finish(last_ink), stroke_color, nullptr,
                                                   canvas.world().origin()));
          tinydraw::esp32::interactive_pan_benchmark_lock_cache(*interactive_pan_benchmark);
          static_cast<void>(canvas.world().capture(canvas.committed()));
          tinydraw::esp32::interactive_pan_benchmark_unlock_cache(*interactive_pan_benchmark);
          tinydraw::esp32::interactive_pan_benchmark_commit_stroke(*interactive_pan_benchmark);
        }
#else
        static_cast<void>(canvas.raster().finish(ribbon.finish(last_ink), stroke_color,
                                                 &canvas.undo_history(), canvas.world().origin()));
#endif
        const auto finish_us = esp_timer_get_time() - started;
        toolbar.export_ready = false;
#ifndef TINYDRAW_INTERACTIVE_PAN_BENCHMARK
        if (!demo_replaying) {
          if (persistence_resync_needed) {
            static_cast<void>(canvas.world().capture(canvas.committed()));
            drawing_store.save_all(canvas.world());
          } else {
            drawing_store.save_stroke(canvas.world(), canvas.committed());
          }
          persistence_resync_needed = false;
        }
#endif
        std::printf(
            "[DEBUG-perf1] samples=%lu updates_us=%lld average_us=%lld max_us=%lld "
            "finish_us=%lld display_prepare_us=%lld display_transfer_us=%lld pushes=%lu "
            "stroke_us=%lu touch_average_us=%llu touch_max_us=%lu "
            "max_input_lag_us=%lu max_queue=%lu\n",
            static_cast<unsigned long>(stroke_samples), static_cast<long long>(stroke_render_us),
            static_cast<long long>(stroke_samples == 0 ? 0 : stroke_render_us / stroke_samples),
            static_cast<long long>(maximum_render_us), static_cast<long long>(finish_us),
            static_cast<long long>(display.prepare_us()),
            static_cast<long long>(display.transfer_us()),
            static_cast<unsigned long>(display.push_count()),
            static_cast<unsigned long>(event.timestamp_us - stroke_started_us),
            static_cast<unsigned long long>(
                stroke_samples <= 1 ? 0 : touch_intervals_us / (stroke_samples - 1U)),
            static_cast<unsigned long>(maximum_touch_interval_us),
            static_cast<unsigned long>(maximum_input_lag_us),
            static_cast<unsigned long>(maximum_queue_depth));
        update_toolbar();
      }
    }
  }
}
