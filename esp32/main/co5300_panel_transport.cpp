#include "co5300_panel_transport.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "tinydraw/graphics/world_canvas.h"
#include "tinydraw/vector_v2/panel_staging.h"

namespace tinydraw::esp32 {
namespace {

constexpr int kPanelGapX = 0x10;
constexpr gpio_num_t kTearPin = GPIO_NUM_13;
// 32 KiB bounce buffers: per-transaction setup costs ~0.4 ms, so a full
// panel at 8192-pixel strips paid ~8 ms of pure overhead across 21 pushes;
// 16384-pixel strips halve that for 96 KiB of internal DMA memory.
constexpr int kTransferPixels = 16384;
constexpr int kTransferQueueDepth = 3;
constexpr std::size_t kTransferHistory = 64U;
constexpr std::uint16_t kIoExpanderAddress = 0x20;
constexpr std::uint8_t kIoExpanderOutputRegister = 0x01;
constexpr std::uint8_t kIoExpanderConfigRegister = 0x03;
constexpr std::uint8_t kIoExpanderLcdReset = 1U << 0U;
constexpr std::uint8_t kIoExpanderDisplayPower = 1U << 1U;
constexpr std::uint8_t kIoExpanderTouchReset = 1U << 2U;
constexpr std::uint8_t kIoExpanderSdChipSelect = 1U << 7U;
constexpr std::uint8_t kIoExpanderOutputs =
    kIoExpanderLcdReset | kIoExpanderDisplayPower | kIoExpanderTouchReset | kIoExpanderSdChipSelect;
constexpr std::uint32_t kDmaCaps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;

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

using vector_v2::stage_pixels_swapped;
using vector_v2::stage_ring_row;

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

}  // namespace

class Co5300PanelTransport::Impl {
 public:
  Impl() {
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
    std::printf("TINYDRAW_PANEL_HARD_RESET=%u\n", reset_panel_power());

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
    io_config.pclk_hz = 60 * 1000 * 1000;
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
    // TEON sits in the init list before sleep-out and is ignored there on
    // marginal boots (observed as whole sessions without a single TE edge).
    // Re-issue it now that the panel is awake and displaying.
    static_cast<void>(esp_lcd_panel_io_tx_param(io_, 0x35, init_35.data(), init_35.size()));
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
    const std::uint32_t falling = tear_falling_edges_.load(std::memory_order_acquire);
    const std::uint32_t period = tear_period_us_.load(std::memory_order_relaxed);
    const std::uint32_t high = tear_high_us_.load(std::memory_order_relaxed);
    return {.rising_edges = rising,
            .falling_edges = falling,
            .period_us = rising < 2U ? -1 : static_cast<std::int64_t>(period),
            .high_us = high == 0U ? -1 : static_cast<std::int64_t>(high),
            .level = gpio_get_level(kTearPin) != 0};
  }

  [[nodiscard]] std::uint32_t tear_falling_edge_count() const {
    return tear_falling_edges_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::int64_t tear_age_us() const {
    const std::uint32_t fall = tear_last_fall_us_.load(std::memory_order_acquire);
    if (fall == 0U) {
      return -1;
    }
    return static_cast<std::int64_t>(
        static_cast<std::uint32_t>(static_cast<std::uint32_t>(esp_timer_get_time()) - fall));
  }

  [[nodiscard]] bool wait_for_safe_frame_start(std::int64_t timeout_us) {
    if (!ready_ || timeout_us <= 0) {
      return false;
    }
    const std::uint32_t start = tear_falling_edges_.load(std::memory_order_acquire);
    static_cast<void>(xSemaphoreTake(tear_semaphore_, 0));
    if (tear_falling_edges_.load(std::memory_order_acquire) != start) {
      return true;
    }
    const TickType_t timeout_ticks = pdMS_TO_TICKS((timeout_us + 999) / 1'000);
    if (xSemaphoreTake(tear_semaphore_, timeout_ticks) == pdTRUE) {
      return true;
    }
    // Not one TE edge for the whole wait: the panel stopped emitting (or a
    // marginal boot never started it). Heal and give the signal one more
    // frame period to appear.
    if (heal_tear_signal()) {
      return xSemaphoreTake(tear_semaphore_, pdMS_TO_TICKS(34)) == pdTRUE;
    }
    return false;
  }

  // Re-issues TEON, rate-limited, after draining in-flight color transfers
  // so the command cannot interleave with pixel data. Re-selects the user
  // command page first (a controller stuck on a vendor page would apply
  // 0x35 to the wrong register), samples the raw TE pin as a diagnostic,
  // and re-installs the GPIO ISR when the pin toggles without any counted
  // edge. Called from the single presenting task.
  bool heal_tear_signal() {
    const auto now = static_cast<std::uint32_t>(esp_timer_get_time());
    if (last_te_heal_us_ != 0U && now - last_te_heal_us_ < 1'000'000U) {
      return false;
    }
    last_te_heal_us_ = now;
    static_cast<void>(wait_for_all(100'000));
    const bool paged =
        esp_lcd_panel_io_tx_param(io_, 0xFE, init_fe.data(), init_fe.size()) == ESP_OK;
    const bool sent =
        esp_lcd_panel_io_tx_param(io_, 0x35, init_35.data(), init_35.size()) == ESP_OK;
    // Sample the pin across roughly two frame periods: a toggling level
    // means the panel emits TE and the interrupt side lost it.
    const std::uint32_t edges_before = tear_falling_edges_.load(std::memory_order_acquire);
    int high_samples = 0;
    constexpr int kProbes = 34;
    for (int probe = 0; probe < kProbes; ++probe) {
      high_samples += gpio_get_level(kTearPin) != 0;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    const std::uint32_t edges_after = tear_falling_edges_.load(std::memory_order_acquire);
    const bool pin_toggles = high_samples != 0 && high_samples != kProbes;
    bool isr_reinstalled = false;
    if (pin_toggles && edges_after == edges_before) {
      static_cast<void>(gpio_isr_handler_remove(kTearPin));
      isr_reinstalled = gpio_isr_handler_add(kTearPin, on_tear_edge, this) == ESP_OK;
    }
    std::printf(
        "TINYDRAW_PANEL_TE_HEAL paged=%u sent=%u high=%d/%d edges_delta=%lu isr_reinstall=%u "
        "falling_edges=%lu\n",
        paged, sent, high_samples, kProbes, static_cast<unsigned long>(edges_after - edges_before),
        isr_reinstalled, static_cast<unsigned long>(edges_after));
    return sent;
  }

  [[nodiscard]] std::int64_t complete_time_us(std::uint32_t sequence) const {
    const std::uint32_t completed = complete_count();
    // Wraparound-safe: signed distance stays correct across the 32-bit
    // counter rollover.
    const auto behind = static_cast<std::int32_t>(completed - sequence);
    if (sequence == 0U || behind < 0 || behind >= static_cast<std::int32_t>(kTransferHistory)) {
      return -1;
    }
    return static_cast<std::int64_t>(
        transfer_complete_times_[(sequence - 1U) % kTransferHistory].load(
            std::memory_order_relaxed));
  }

  [[nodiscard]] bool wait_for_all(std::int64_t timeout_us) {
    const std::uint32_t target = submit_count();
    const std::int64_t started = esp_timer_get_time();
    // Wraparound-safe: signed distance stays correct across the 32-bit
    // counter rollover.
    while (static_cast<std::int32_t>(complete_count() - target) < 0) {
      if (esp_timer_get_time() - started >= timeout_us) {
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
  }

  void push_rect_ring(int x, int y, int width, int height, const std::uint16_t* area_pixels,
                      int stride, int shift_x, int shift_y, int area_width, int area_height) {
    if (!ready_ || area_pixels == nullptr || width <= 0 || height <= 0 || stride < area_width ||
        shift_x < 0 || shift_x >= area_width || shift_y < 0 || shift_y >= area_height ||
        x + width > area_width || y + height > area_height) {
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
    if (width * height > kTransferPixels) {
      int rows_per_transfer = kTransferPixels / width;
      rows_per_transfer -= rows_per_transfer % 2;
      if (rows_per_transfer <= 0) {
        ++rejected_push_count_;
        return;
      }
      for (int row = 0; row < height; row += rows_per_transfer) {
        const int rows = std::min(rows_per_transfer, height - row);
        push_rect_ring(x, y + row, width, rows, area_pixels, stride, shift_x, shift_y, area_width,
                       area_height);
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
      int source_row = y + row + shift_y;
      if (source_row >= area_height) {
        source_row -= area_height;
      }
      const auto* source = area_pixels + static_cast<std::ptrdiff_t>(source_row) * stride;
      stage_ring_row(source, area_width, shift_x, x, width,
                     transfer + static_cast<std::ptrdiff_t>(row * width));
    }
    prepare_us_ += esp_timer_get_time() - prepare_started;

    const std::int64_t submit_started = esp_timer_get_time();
    transfer_submits_.fetch_add(1U, std::memory_order_release);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_, x, y, x + width, y + height, transfer));
    transfer_us_ += esp_timer_get_time() - submit_started;
    ++push_count_;
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
      stage_pixels_swapped(source, transfer + static_cast<std::ptrdiff_t>(row * width), width);
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
    self.tear_last_fall_us_.store(now == 0U ? 1U : now, std::memory_order_release);
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
  std::uint32_t last_te_heal_us_ = 0;
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
  std::atomic<std::uint32_t> tear_last_fall_us_{0U};
  std::atomic<std::uint32_t> tear_period_us_{0U};
  std::atomic<std::uint32_t> tear_high_us_{0U};
  std::int64_t prepare_us_ = 0;
  std::int64_t transfer_us_ = 0;
  std::uint32_t push_count_ = 0;
  std::uint32_t rejected_push_count_ = 0;
  std::size_t transfer_index_ = 0;
  bool bus_initialized_ = false;
  bool tear_isr_installed_ = false;
  bool ready_ = false;
};

Co5300PanelTransport::Co5300PanelTransport() : impl_(std::make_unique<Impl>()) {}
Co5300PanelTransport::~Co5300PanelTransport() = default;
bool Co5300PanelTransport::ready() const { return impl_->ready(); }
void Co5300PanelTransport::reset_timing() { impl_->reset_timing(); }
std::int64_t Co5300PanelTransport::prepare_us() const { return impl_->prepare_us(); }
std::int64_t Co5300PanelTransport::transfer_us() const { return impl_->transfer_us(); }
std::uint32_t Co5300PanelTransport::push_count() const { return impl_->push_count(); }
std::uint32_t Co5300PanelTransport::rejected_push_count() const {
  return impl_->rejected_push_count();
}
std::uint32_t Co5300PanelTransport::submit_count() const { return impl_->submit_count(); }
std::uint32_t Co5300PanelTransport::complete_count() const { return impl_->complete_count(); }
std::int64_t Co5300PanelTransport::complete_time_us(std::uint32_t sequence) const {
  return impl_->complete_time_us(sequence);
}
TearSignalTiming Co5300PanelTransport::tear_signal_timing() const {
  return impl_->tear_signal_timing();
}
bool Co5300PanelTransport::wait_for_safe_frame_start(std::int64_t timeout_us) {
  return impl_->wait_for_safe_frame_start(timeout_us);
}
std::int64_t Co5300PanelTransport::tear_age_us() const { return impl_->tear_age_us(); }
std::uint32_t Co5300PanelTransport::tear_falling_edges() const {
  return impl_->tear_falling_edge_count();
}
bool Co5300PanelTransport::wait_for_all(std::int64_t timeout_us) {
  return impl_->wait_for_all(timeout_us);
}
void Co5300PanelTransport::push_rect(int x, int y, int width, int height,
                                     const std::uint16_t* pixels, int stride) {
  impl_->push_rect(x, y, width, height, pixels, stride);
}
void Co5300PanelTransport::push_rect_ring(int x, int y, int width, int height,
                                          const std::uint16_t* area_pixels, int stride, int shift_x,
                                          int shift_y, int area_width, int area_height) {
  impl_->push_rect_ring(x, y, width, height, area_pixels, stride, shift_x, shift_y, area_width,
                        area_height);
}

}  // namespace tinydraw::esp32
