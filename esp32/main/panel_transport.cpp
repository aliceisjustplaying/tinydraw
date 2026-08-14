#include "panel_transport.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinydraw/graphics/world_canvas.h"

namespace tinydraw::esp32 {
namespace {

constexpr gpio_num_t kTearPin = GPIO_NUM_13;
constexpr int kTransferPixels = 8192;
constexpr int kTransferQueueDepth = 3;
constexpr std::size_t kTransferHistory = 64U;
constexpr std::uint32_t kDmaCaps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;

constexpr std::array<std::uint8_t, 1> init_zero{0x00};
constexpr std::array<std::uint8_t, 1> init_c4{0x80};
constexpr std::array<std::uint8_t, 1> init_55{0x55};
constexpr std::array<std::uint8_t, 1> init_20{0x20};
constexpr std::array<std::uint8_t, 1> init_ff{0xFF};
constexpr std::array<std::uint8_t, 2> init_te_scanline{0x01, 0xD1};
constexpr std::array<std::uint8_t, 4> init_2a{0x00, 0x00, 0x01, 0x6F};
constexpr std::array<std::uint8_t, 4> init_2b{0x00, 0x00, 0x01, 0xBF};

const std::array<co5300_lcd_init_cmd_t, 11> co5300_panel_init{{
    {0xFE, init_zero.data(), init_zero.size(), 0},
    {0xC4, init_c4.data(), init_c4.size(), 0},
    {0x3A, init_55.data(), init_55.size(), 0},
    {0x35, init_zero.data(), init_zero.size(), 0},
    {0x53, init_20.data(), init_20.size(), 0},
    {0x51, init_ff.data(), init_ff.size(), 0},
    {0x63, init_ff.data(), init_ff.size(), 0},
    {0x2A, init_2a.data(), init_2a.size(), 0},
    {0x2B, init_2b.data(), init_2b.size(), 0},
    {0x11, nullptr, 0, 100},
    {0x29, nullptr, 0, 0},
}};

// Official Waveshare SH8601 sequence used by its revision-detecting 1.8-inch
// BSP. The 0x01D1 TE scanline is retained until V1 hardware timing is measured.
const std::array<sh8601_lcd_init_cmd_t, 9> sh8601_panel_init{{
    {0x11, nullptr, 0, 120},
    {0x44, init_te_scanline.data(), init_te_scanline.size(), 0},
    {0x35, init_zero.data(), init_zero.size(), 0},
    {0x53, init_20.data(), init_20.size(), 10},
    {0x2A, init_2a.data(), init_2a.size(), 0},
    {0x2B, init_2b.data(), init_2b.size(), 0},
    {0x51, init_zero.data(), init_zero.size(), 10},
    {0x29, nullptr, 0, 10},
    {0x51, init_ff.data(), init_ff.size(), 0},
}};

constexpr std::uint32_t swap_pixel_pair(std::uint16_t first, std::uint16_t second) {
  const std::uint32_t pixels =
      static_cast<std::uint32_t>(first) | (static_cast<std::uint32_t>(second) << 16U);
  return ((pixels >> 8U) & 0x00FF00FFU) | ((pixels << 8U) & 0xFF00FF00U);
}

static_assert(swap_pixel_pair(0x1234U, 0xABCDU) == 0xCDAB3412U);

}  // namespace

class PanelTransport::Impl {
 public:
  explicit Impl(BoardHardware& hardware) : revision_(hardware.profile().revision) {
    if (!hardware.ready()) {
      return;
    }
    transfer_pixels_ = static_cast<std::uint16_t*>(heap_caps_malloc(
        static_cast<std::size_t>(kTransferQueueDepth * kTransferPixels) * sizeof(std::uint16_t),
        kDmaCaps));
    transfer_semaphore_ = xSemaphoreCreateCountingStatic(kTransferQueueDepth, kTransferQueueDepth,
                                                         &transfer_semaphore_storage_);
    tear_semaphore_ = xSemaphoreCreateBinaryStatic(&tear_semaphore_storage_);
    if (transfer_pixels_ == nullptr || transfer_semaphore_ == nullptr ||
        tear_semaphore_ == nullptr) {
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
    bus_initialized_ = true;

    esp_lcd_panel_io_spi_config_t io_config{};
    io_config.cs_gpio_num = GPIO_NUM_12;
    io_config.dc_gpio_num = GPIO_NUM_NC;
    io_config.spi_mode = 0;
    const BoardDriverProfile driver = board_driver_profile(revision_);
    io_config.pclk_hz = driver.panel_clock_hz;
    io_config.trans_queue_depth = kTransferQueueDepth;
    io_config.on_color_trans_done = on_transfer_done;
    io_config.user_ctx = this;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    if (esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_config,
                                 &io_) != ESP_OK) {
      return;
    }

    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.data_endian = LCD_RGB_DATA_ENDIAN_BIG;
    panel_config.bits_per_pixel = 16;
    esp_err_t panel_created = ESP_FAIL;
    if (revision_ == BoardRevision::kV1) {
      sh8601_vendor_config_t vendor_config{};
      vendor_config.init_cmds = sh8601_panel_init.data();
      vendor_config.init_cmds_size = static_cast<std::uint16_t>(sh8601_panel_init.size());
      vendor_config.flags.use_qspi_interface = 1;
      panel_config.vendor_config = &vendor_config;
      panel_created = esp_lcd_new_panel_sh8601(io_, &panel_config, &panel_);
    } else {
      co5300_vendor_config_t vendor_config{};
      vendor_config.init_cmds = co5300_panel_init.data();
      vendor_config.init_cmds_size = static_cast<std::uint16_t>(co5300_panel_init.size());
      vendor_config.flags.use_qspi_interface = 1;
      panel_config.vendor_config = &vendor_config;
      panel_created = esp_lcd_new_panel_co5300(io_, &panel_config, &panel_);
    }
    if (panel_created != ESP_OK || esp_lcd_panel_reset(panel_) != ESP_OK ||
        esp_lcd_panel_init(panel_) != ESP_OK ||
        esp_lcd_panel_set_gap(panel_, driver.panel_x_gap, 0) != ESP_OK ||
        esp_lcd_panel_disp_on_off(panel_, true) != ESP_OK) {
      return;
    }
    gpio_config_t tear_config{};
    tear_config.pin_bit_mask = 1ULL << static_cast<unsigned>(kTearPin);
    tear_config.mode = GPIO_MODE_INPUT;
    tear_config.pull_up_en = GPIO_PULLUP_DISABLE;
    tear_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    tear_config.intr_type = GPIO_INTR_ANYEDGE;
    const esp_err_t isr_service = gpio_install_isr_service(0);
    if (gpio_config(&tear_config) != ESP_OK ||
        (isr_service != ESP_OK && isr_service != ESP_ERR_INVALID_STATE) ||
        gpio_isr_handler_add(kTearPin, on_tear_edge, this) != ESP_OK) {
      return;
    }
    tear_isr_installed_ = true;
    ready_ = true;
  }

  ~Impl() {
    static_cast<void>(wait_for_all(2'000'000));
    ready_ = false;
    if (tear_isr_installed_) {
      static_cast<void>(gpio_isr_handler_remove(kTearPin));
    }
    if (panel_ != nullptr) {
      static_cast<void>(esp_lcd_panel_del(panel_));
    }
    if (io_ != nullptr) {
      static_cast<void>(esp_lcd_panel_io_del(io_));
    }
    if (bus_initialized_) {
      static_cast<void>(spi_bus_free(SPI2_HOST));
    }
    heap_caps_free(transfer_pixels_);
  }

  [[nodiscard]] bool ready() const { return ready_; }
  void reset_timing() {
    prepare_us_ = 0;
    transfer_us_ = 0;
    push_count_ = 0;
    rejected_push_count_ = 0;
  }
  [[nodiscard]] std::int64_t prepare_us() const { return prepare_us_; }
  [[nodiscard]] std::int64_t transfer_us() const { return transfer_us_; }
  [[nodiscard]] std::uint32_t push_count() const { return push_count_; }
  [[nodiscard]] std::uint32_t rejected_push_count() const { return rejected_push_count_; }
  [[nodiscard]] std::uint32_t submit_count() const {
    return transfer_submits_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::uint32_t complete_count() const {
    return transfer_completes_.load(std::memory_order_acquire);
  }

  [[nodiscard]] TearSignalTiming tear_signal_timing() const {
    const std::uint32_t rising = tear_rising_edges_.load(std::memory_order_acquire);
    const std::uint32_t period = tear_period_us_.load(std::memory_order_relaxed);
    const std::uint32_t high = tear_high_us_.load(std::memory_order_relaxed);
    return {.rising_edges = rising,
            .period_us = rising < 2U ? -1 : static_cast<std::int64_t>(period),
            .high_us = high == 0U ? -1 : static_cast<std::int64_t>(high),
            .level = gpio_get_level(kTearPin) != 0};
  }

  [[nodiscard]] bool wait_for_safe_frame_start(std::int64_t timeout_us) {
    if (!ready_ || !board_driver_profile(revision_).safe_frame_sync || timeout_us <= 0) {
      return false;
    }
    const std::uint32_t start = tear_falling_edges_.load(std::memory_order_acquire);
    static_cast<void>(xSemaphoreTake(tear_semaphore_, 0));
    if (tear_falling_edges_.load(std::memory_order_acquire) != start) {
      return true;
    }
    const TickType_t timeout_ticks = pdMS_TO_TICKS((timeout_us + 999) / 1'000);
    return xSemaphoreTake(tear_semaphore_, timeout_ticks) == pdTRUE;
  }

  [[nodiscard]] std::int64_t complete_time_us(std::uint32_t sequence) const {
    const std::uint32_t completed = complete_count();
    if (sequence == 0U || sequence > completed || completed - sequence >= kTransferHistory) {
      return -1;
    }
    return static_cast<std::int64_t>(
        transfer_complete_times_[(sequence - 1U) % kTransferHistory].load(
            std::memory_order_relaxed));
  }

  [[nodiscard]] bool wait_for_all(std::int64_t timeout_us) {
    const std::uint32_t target = submit_count();
    const std::int64_t started = esp_timer_get_time();
    while (complete_count() < target) {
      if (esp_timer_get_time() - started >= timeout_us) {
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
  }

  void push_rect(int x, int y, int width, int height, const std::uint16_t* pixels, int stride) {
    if (!ready_ || pixels == nullptr || width <= 0 || height <= 0) {
      ++rejected_push_count_;
      return;
    }
    const bool in_bounds = x >= 0 && y >= 0 && x < kCanvasWidth && y < kCanvasHeight &&
                           width <= kCanvasWidth - x && height <= kCanvasHeight - y;
    const bool valid_window = ((x | y | width | height) & 1) == 0;
    if (!in_bounds || !valid_window) {
      ++rejected_push_count_;
      std::printf(
          "TINYDRAW_PANEL_WINDOW_REJECT x=%d y=%d width=%d height=%d bounds=%u even_window=%u\n", x,
          y, width, height, in_bounds, valid_window);
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

    const std::int64_t transfer_started = esp_timer_get_time();
    ESP_ERROR_CHECK(xSemaphoreTake(transfer_semaphore_, portMAX_DELAY) == pdTRUE ? ESP_OK
                                                                                 : ESP_FAIL);
    auto* transfer =
        transfer_pixels_ + static_cast<std::ptrdiff_t>(transfer_index_ * kTransferPixels);
    transfer_index_ = (transfer_index_ + 1U) % kTransferQueueDepth;
    transfer_us_ += esp_timer_get_time() - transfer_started;

    const std::int64_t prepare_started = esp_timer_get_time();
    for (int row = 0; row < height; ++row) {
      const auto* source = pixels + static_cast<std::ptrdiff_t>(row * source_stride);
      auto* destination =
          reinterpret_cast<std::uint32_t*>(transfer + static_cast<std::ptrdiff_t>(row * width));
      for (int column = 0; column < width; column += 2) {
        destination[column / 2] = swap_pixel_pair(source[column], source[column + 1]);
      }
    }
    prepare_us_ += esp_timer_get_time() - prepare_started;

    const std::int64_t submit_started = esp_timer_get_time();
    transfer_submits_.fetch_add(1U, std::memory_order_release);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_, x, y, x + width, y + height, transfer));
    transfer_us_ += esp_timer_get_time() - submit_started;
    ++push_count_;
  }

 private:
  static void on_tear_edge(void* context) {
    auto& self = *static_cast<Impl*>(context);
    const std::uint32_t now = static_cast<std::uint32_t>(esp_timer_get_time());
    if (gpio_get_level(kTearPin) != 0) {
      const std::uint32_t prior = self.tear_last_rise_us_.exchange(now, std::memory_order_relaxed);
      if (prior != 0U) {
        self.tear_period_us_.store(now - prior, std::memory_order_relaxed);
      }
      self.tear_rising_edges_.fetch_add(1U, std::memory_order_release);
      return;
    }
    const std::uint32_t rise = self.tear_last_rise_us_.load(std::memory_order_relaxed);
    if (rise != 0U) {
      self.tear_high_us_.store(now - rise, std::memory_order_relaxed);
    }
    self.tear_falling_edges_.fetch_add(1U, std::memory_order_release);
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(self.tear_semaphore_, &woke);
    if (woke == pdTRUE) {
      portYIELD_FROM_ISR();
    }
  }

  static bool on_transfer_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*,
                               void* context) {
    auto& self = *static_cast<Impl*>(context);
    const std::uint32_t sequence = self.transfer_completes_.load(std::memory_order_relaxed);
    self.transfer_complete_times_[sequence % kTransferHistory].store(
        static_cast<std::uint32_t>(esp_timer_get_time()), std::memory_order_relaxed);
    self.transfer_completes_.store(sequence + 1U, std::memory_order_release);
    BaseType_t woke = pdFALSE;
    xSemaphoreGiveFromISR(self.transfer_semaphore_, &woke);
    return woke == pdTRUE;
  }

  std::uint16_t* transfer_pixels_ = nullptr;
  StaticSemaphore_t transfer_semaphore_storage_{};
  SemaphoreHandle_t transfer_semaphore_ = nullptr;
  StaticSemaphore_t tear_semaphore_storage_{};
  SemaphoreHandle_t tear_semaphore_ = nullptr;
  esp_lcd_panel_io_handle_t io_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;
  std::array<std::atomic<std::uint32_t>, kTransferHistory> transfer_complete_times_{};
  std::atomic<std::uint32_t> transfer_submits_{0U};
  std::atomic<std::uint32_t> transfer_completes_{0U};
  std::atomic<std::uint32_t> tear_rising_edges_{0U};
  std::atomic<std::uint32_t> tear_falling_edges_{0U};
  std::atomic<std::uint32_t> tear_last_rise_us_{0U};
  std::atomic<std::uint32_t> tear_period_us_{0U};
  std::atomic<std::uint32_t> tear_high_us_{0U};
  std::int64_t prepare_us_ = 0;
  std::int64_t transfer_us_ = 0;
  std::uint32_t push_count_ = 0;
  std::uint32_t rejected_push_count_ = 0;
  std::size_t transfer_index_ = 0;
  BoardRevision revision_ = BoardRevision::kUnknown;
  bool bus_initialized_ = false;
  bool tear_isr_installed_ = false;
  bool ready_ = false;
};

PanelTransport::PanelTransport(BoardHardware& hardware) : impl_(std::make_unique<Impl>(hardware)) {}
PanelTransport::~PanelTransport() = default;
bool PanelTransport::ready() const { return impl_->ready(); }
void PanelTransport::reset_timing() { impl_->reset_timing(); }
std::int64_t PanelTransport::prepare_us() const { return impl_->prepare_us(); }
std::int64_t PanelTransport::transfer_us() const { return impl_->transfer_us(); }
std::uint32_t PanelTransport::push_count() const { return impl_->push_count(); }
std::uint32_t PanelTransport::rejected_push_count() const { return impl_->rejected_push_count(); }
std::uint32_t PanelTransport::submit_count() const { return impl_->submit_count(); }
std::uint32_t PanelTransport::complete_count() const { return impl_->complete_count(); }
std::int64_t PanelTransport::complete_time_us(std::uint32_t sequence) const {
  return impl_->complete_time_us(sequence);
}
TearSignalTiming PanelTransport::tear_signal_timing() const { return impl_->tear_signal_timing(); }
bool PanelTransport::wait_for_safe_frame_start(std::int64_t timeout_us) {
  return impl_->wait_for_safe_frame_start(timeout_us);
}
bool PanelTransport::wait_for_all(std::int64_t timeout_us) {
  return impl_->wait_for_all(timeout_us);
}
void PanelTransport::push_rect(int x, int y, int width, int height, const std::uint16_t* pixels,
                               int stride) {
  impl_->push_rect(x, y, width, height, pixels, stride);
}

}  // namespace tinydraw::esp32
