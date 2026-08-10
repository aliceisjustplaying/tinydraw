#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>

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
#include "esp_timer.h"
#include "firmware_canvas.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinydraw/ink/ink_stream.h"
#include "tinydraw/ink/ribbon_geometry.h"
#include "tinydraw/ui/toolbar.h"

namespace {

constexpr std::uint16_t kBackground = 0xFFFFU;
constexpr int kPanelGapX = 0x10;
constexpr int kTransferPixels = 4096;
constexpr int kTransferQueueDepth = 3;
constexpr int kToolbarTop = 374;
constexpr int kPopupTop = 296;

DMA_ATTR std::array<std::array<std::uint16_t, kTransferPixels>, kTransferQueueDepth>
    transfer_pixels;
StaticSemaphore_t transfer_semaphore_storage;
SemaphoreHandle_t transfer_semaphore = nullptr;

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
  BaseType_t woke = pdFALSE;
  xSemaphoreGiveFromISR(transfer_semaphore, &woke);
  return woke == pdTRUE;
}

std::uint16_t swap_bytes(std::uint16_t pixel) {
  return static_cast<std::uint16_t>((pixel << 8U) | (pixel >> 8U));
}

class PhysicalDisplay final : public tinydraw::DisplayBackend {
 public:
  PhysicalDisplay() {
    transfer_semaphore = xSemaphoreCreateCountingStatic(kTransferQueueDepth, kTransferQueueDepth,
                                                        &transfer_semaphore_storage);
    overlay_ = static_cast<std::uint16_t*>(heap_caps_malloc(
        static_cast<std::size_t>(tinydraw::kCanvasWidth * tinydraw::kCanvasHeight) *
            sizeof(std::uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (transfer_semaphore == nullptr || overlay_ == nullptr) {
      return;
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
    io_config.pclk_hz = 40 * 1000 * 1000;
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
    toolbar_ = toolbar;
    const int new_top = (toolbar.colors_open || toolbar.sizes_open) ? kPopupTop : kToolbarTop;
    toolbar_refresh_top_ = std::min(toolbar_top_, new_top);
    toolbar_top_ = new_top;
    std::fill_n(overlay_, tinydraw::kCanvasWidth * tinydraw::kCanvasHeight, kBackground);
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
    for (int row = 0; row < height; ++row) {
      for (int column = 0; column < width; ++column) {
        const int panel_x = x + column;
        const int panel_y = y + row;
        const std::size_t source = static_cast<std::size_t>(row * source_stride + column);
        const std::size_t destination = static_cast<std::size_t>(row * width + column);
        const std::size_t canvas =
            static_cast<std::size_t>(panel_y * tinydraw::kCanvasWidth + panel_x);
        const auto point =
            tinydraw::Point{static_cast<float>(panel_x) + 0.5F, static_cast<float>(panel_y) + 0.5F};
        const bool toolbar_pixel =
            panel_y >= toolbar_top_ && tinydraw::toolbar_contains(point, toolbar_);
        const std::uint16_t pixel = toolbar_pixel ? overlay_[canvas] : pixels[source];
        transfer[destination] = swap_bytes(pixel);
      }
    }
    prepare_us_ += esp_timer_get_time() - prepare_started;
    const auto submit_started = esp_timer_get_time();
    ESP_ERROR_CHECK(
        esp_lcd_panel_draw_bitmap(panel_, x, y, x + width, y + height, transfer.data()));
    transfer_us_ += esp_timer_get_time() - submit_started;
    ++push_count_;
  }

  void push_canvas(std::span<const std::uint16_t> canvas, int top = 0) {
    // The CO5300 requires even transfer-window boundaries.
    constexpr int rows_per_transfer = 10;
    for (int y = top; y < tinydraw::kCanvasHeight; y += rows_per_transfer) {
      const int height = std::min(rows_per_transfer, tinydraw::kCanvasHeight - y);
      push_rect(0, y, tinydraw::kCanvasWidth, height,
                canvas.data() + static_cast<std::size_t>(y * tinydraw::kCanvasWidth));
    }
  }

  void refresh_toolbar(std::span<const std::uint16_t> canvas) {
    push_canvas(canvas, toolbar_refresh_top_);
    toolbar_refresh_top_ = toolbar_top_;
  }

 private:
  esp_lcd_panel_io_handle_t io_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;
  std::uint16_t* overlay_ = nullptr;
  tinydraw::ToolbarState toolbar_{};
  int toolbar_top_ = kToolbarTop;
  int toolbar_refresh_top_ = kToolbarTop;
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
      std::printf("[DEBUG-touch-rate] registers=%u id=0x%02x firmware=0x%02x "
                  "scan_period=0x%02x irq=0x%02x\n",
                  registers_read, chip_id, firmware, scan_period, interrupt_mode);
    }
  }

  [[nodiscard]] bool ready() const { return ready_; }

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

struct TouchEvent {
  tinydraw::Point point;
  std::uint32_t timestamp_us = 0;
  bool touching = false;
};

struct TouchTaskContext {
  PhysicalTouch* touch = nullptr;
  QueueHandle_t queue = nullptr;
};

void enqueue_latest(QueueHandle_t queue, const TouchEvent& event) {
  if (xQueueSend(queue, &event, 0) == pdTRUE) {
    return;
  }
  TouchEvent discarded;
  static_cast<void>(xQueueReceive(queue, &discarded, 0));
  static_cast<void>(xQueueSend(queue, &event, 0));
}

void touch_task(void* argument) {
  auto& context = *static_cast<TouchTaskContext*>(argument);
  tinydraw::Point last_point{};
  bool touching = false;
  std::uint32_t no_touch_started_us = 0;
  while (true) {
    tinydraw::Point point{};
    const TouchRead read = context.touch->read(point);
    const std::uint32_t now = timestamp_us();
    if (read == TouchRead::kPoint) {
      no_touch_started_us = 0;
      if (!touching || point.x != last_point.x || point.y != last_point.y) {
        enqueue_latest(context.queue, {.point = point, .timestamp_us = now, .touching = true});
        last_point = point;
      }
      touching = true;
    } else if (read == TouchRead::kNoTouch && touching) {
      if (no_touch_started_us == 0U) {
        no_touch_started_us = now;
      } else if (now - no_touch_started_us >= 20'000U) {
        enqueue_latest(context.queue,
                       {.point = last_point, .timestamp_us = now, .touching = false});
        touching = false;
        no_touch_started_us = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

}  // namespace

void run_hardware_app() {
  PhysicalDisplay display;
  PhysicalTouch touch;
  if (!display.ready() || !touch.ready()) {
    std::printf("TINYDRAW_HARDWARE_FAIL display=%u touch=%u\n", display.ready(), touch.ready());
    return;
  }

  tinydraw::esp32::FirmwareCanvas canvas(display);
  if (!canvas.ready() || !canvas.capabilities_valid()) {
    std::printf("TINYDRAW_HARDWARE_FAIL canvas=0\n");
    return;
  }

  tinydraw::ToolbarState toolbar;
  display.set_toolbar(toolbar);
  display.push_canvas(canvas.committed());

  tinydraw::InkConfig brush;
  brush.size = tinydraw::brush_size(toolbar.size);
  tinydraw::InkStream ink(brush);
  tinydraw::CurvedRibbonStream ribbon;
  tinydraw::InkPoint last_ink{};
  tinydraw::Point last_touch{};
  std::uint16_t stroke_color = tinydraw::rgb565(toolbar.color);
  bool pressed = false;
  std::uint32_t stroke_samples = 0;
  std::uint32_t stroke_started_us = 0;
  std::uint32_t previous_touch_us = 0;
  std::uint64_t touch_intervals_us = 0;
  std::uint32_t maximum_touch_interval_us = 0;
  std::uint32_t maximum_input_lag_us = 0;
  UBaseType_t maximum_queue_depth = 0;
  std::int64_t stroke_render_us = 0;
  std::int64_t maximum_render_us = 0;

  const auto close_popups = [&] {
    toolbar.colors_open = false;
    toolbar.sizes_open = false;
  };
  const auto reset_stroke = [&] {
    ink.end();
    ribbon.reset();
    canvas.raster().cancel();
  };
  const auto update_toolbar = [&] {
    toolbar.can_undo = canvas.undo_history().can_undo();
    display.set_toolbar(toolbar);
    display.refresh_toolbar(canvas.committed());
  };
  const auto select_size = [&](tinydraw::PenSize size) {
    toolbar.size = size;
    auto config = ink.config();
    config.size = tinydraw::brush_size(size);
    ink.set_config(config);
    close_popups();
  };
  const auto new_drawing = [&] {
    reset_stroke();
    canvas.undo_history().begin_entry();
    canvas.undo_history().capture_canvas(canvas.committed());
    static_cast<void>(canvas.undo_history().commit_entry());
    std::fill(canvas.committed().begin(), canvas.committed().end(), kBackground);
    std::fill(canvas.visible().begin(), canvas.visible().end(), kBackground);
    close_popups();
    toolbar.can_undo = canvas.undo_history().can_undo();
    display.set_toolbar(toolbar);
    display.push_canvas(canvas.committed());
  };
  const auto undo = [&] {
    if (!canvas.undo_history().can_undo()) {
      return;
    }
    reset_stroke();
    static_cast<void>(canvas.undo_history().undo(canvas.committed(), canvas.visible(), &display));
    close_popups();
    update_toolbar();
  };
  const auto toolbar_action = [&](tinydraw::Point point) {
    switch (tinydraw::toolbar_action_at(point, toolbar)) {
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
        return;
      case tinydraw::ToolbarAction::kNewDrawing:
        new_drawing();
        return;
      case tinydraw::ToolbarAction::kNone:
        break;
    }
    update_toolbar();
  };

  QueueHandle_t touch_queue = xQueueCreate(32U, sizeof(TouchEvent));
  TouchTaskContext touch_context{.touch = &touch, .queue = touch_queue};
  if (touch_queue == nullptr || xTaskCreatePinnedToCore(touch_task, "tinydraw_touch", 4096U,
                                                        &touch_context, 5U, nullptr, 1) != pdPASS) {
    std::printf("TINYDRAW_HARDWARE_FAIL touch_task=0\n");
    return;
  }

  std::printf("TINYDRAW_HARDWARE_OK display=CO5300 touch=CST820\n");
  while (true) {
    TouchEvent event;
    if (xQueueReceive(touch_queue, &event, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    const tinydraw::Point point = event.point;
    const bool touching = event.touching;
    const std::uint32_t input_lag_us = timestamp_us() - event.timestamp_us;
    const UBaseType_t queue_depth = uxQueueMessagesWaiting(touch_queue);
    if (touching && !pressed) {
      std::printf("[DEBUG-hw1] down x=%.0f y=%.0f\n", static_cast<double>(point.x),
                  static_cast<double>(point.y));
      pressed = true;
      last_touch = point;
      if (tinydraw::toolbar_contains(point, toolbar)) {
        toolbar_action(point);
      } else {
        close_popups();
        stroke_color = toolbar.tool == tinydraw::DrawingTool::kEraser
                           ? kBackground
                           : tinydraw::rgb565(toolbar.color);
        last_ink = ink.begin({.x = point.x, .y = point.y, .timestamp_us = event.timestamp_us});
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
        static_cast<void>(canvas.raster().update(ribbon.append(last_ink), stroke_color));
        const auto elapsed = esp_timer_get_time() - started;
        stroke_render_us += elapsed;
        maximum_render_us = std::max(maximum_render_us, elapsed);
      }
    } else if (touching && pressed && ink.active() &&
               (point.x != last_touch.x || point.y != last_touch.y)) {
      last_touch = point;
      last_ink = ink.update({.x = point.x, .y = point.y, .timestamp_us = event.timestamp_us});
      const std::uint32_t touch_interval_us = event.timestamp_us - previous_touch_us;
      previous_touch_us = event.timestamp_us;
      touch_intervals_us += touch_interval_us;
      maximum_touch_interval_us = std::max(maximum_touch_interval_us, touch_interval_us);
      ++stroke_samples;
      maximum_input_lag_us = std::max(maximum_input_lag_us, input_lag_us);
      maximum_queue_depth = std::max(maximum_queue_depth, queue_depth);
      const auto started = esp_timer_get_time();
      static_cast<void>(canvas.raster().update(ribbon.append(last_ink), stroke_color));
      const auto elapsed = esp_timer_get_time() - started;
      stroke_render_us += elapsed;
      maximum_render_us = std::max(maximum_render_us, elapsed);
    } else if (!touching && pressed) {
      std::printf("[DEBUG-hw1] up x=%.0f y=%.0f active=%u\n", static_cast<double>(last_touch.x),
                  static_cast<double>(last_touch.y), ink.active());
      pressed = false;
      if (ink.active()) {
        last_ink =
            ink.finish({.x = last_touch.x, .y = last_touch.y, .timestamp_us = event.timestamp_us});
        const auto started = esp_timer_get_time();
        static_cast<void>(
            canvas.raster().finish(ribbon.finish(last_ink), stroke_color, &canvas.undo_history()));
        const auto finish_us = esp_timer_get_time() - started;
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
            static_cast<unsigned long long>(stroke_samples <= 1
                                                ? 0
                                                : touch_intervals_us / (stroke_samples - 1U)),
            static_cast<unsigned long>(maximum_touch_interval_us),
            static_cast<unsigned long>(maximum_input_lag_us),
            static_cast<unsigned long>(maximum_queue_depth));
        update_toolbar();
      }
    }
  }
}
