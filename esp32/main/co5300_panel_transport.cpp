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
#ifndef TINYDRAW_CO5300_CLOCK_MHZ
#define TINYDRAW_CO5300_CLOCK_MHZ 50
#endif
constexpr int kPanelClockMHz = TINYDRAW_CO5300_CLOCK_MHZ;
static_assert(kPanelClockMHz == 40 || kPanelClockMHz == 50 || kPanelClockMHz == 60,
              "CO5300 clock must be one of the supported 40/50/60 MHz experiment points");
// Measured 2026-08-15 (benchmark-results/blockA-panel-limits): requests of 40,
// 50, and 60 MHz produce identical transfer walls. The GPSPI divider from the
// 80 MHz source clamps all three to 40 MHz actual; no overclock ever engaged.
// Four 32 KiB bounce buffers keep staging ahead of the measured 20 MB/s wire
// and let the edge-gated sweep pre-stage 176 full-width rows. The gate harness
// still has >200 KiB internal headroom after this allocation. Marginal color-
// transaction setup is ~44 us and a window pair is ~90 us; a one-window
// RAMWR/RAMWRC stream minimizes both.
constexpr int kTransferPixels = 16384;
constexpr int kTransferQueueDepth = 4;
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

using vector_v2::copy_ring_row;
using vector_v2::stage_pixels_swapped;
using vector_v2::stage_ring_row;
using vector_v2::swap_pixels_in_place;

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
    std::printf(
        "TINYDRAW_PANEL_TRANSPORT requested_clock_mhz=%d actual_clock=divider_limited_40mhz\n",
        kPanelClockMHz);

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
    io_config.pclk_hz = kPanelClockMHz * 1000 * 1000;
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
    acquire_wait_us_ = 0;
    ring_copy_us_ = 0;
    patch_us_ = 0;
    byte_swap_us_ = 0;
    transfer_us_ = 0;
    push_count_ = 0;
    rejected_push_count_ = 0;
  }
  [[nodiscard]] std::int64_t prepare_us() const { return prepare_us_; }
  [[nodiscard]] std::int64_t acquire_wait_us() const { return acquire_wait_us_; }
  [[nodiscard]] std::int64_t ring_copy_us() const { return ring_copy_us_; }
  [[nodiscard]] std::int64_t patch_us() const { return patch_us_; }
  [[nodiscard]] std::int64_t byte_swap_us() const { return byte_swap_us_; }
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

  [[nodiscard]] std::int64_t tear_age_us(TearSignalEdge edge) const {
    const auto& count = edge == TearSignalEdge::kRising ? tear_rising_edges_ : tear_falling_edges_;
    const auto& timestamp =
        edge == TearSignalEdge::kRising ? tear_last_rise_us_ : tear_last_fall_us_;
    std::uint32_t count_before = 0;
    std::uint32_t count_after = 0;
    std::uint32_t edge_us = 0;
    do {
      count_before = count.load(std::memory_order_acquire);
      edge_us = timestamp.load(std::memory_order_relaxed);
      count_after = count.load(std::memory_order_acquire);
    } while (count_before != count_after);
    if (count_after == 0U) {
      return -1;
    }
    return static_cast<std::int64_t>(
        static_cast<std::uint32_t>(static_cast<std::uint32_t>(esp_timer_get_time()) - edge_us));
  }

  [[nodiscard]] TearEdgeWaitResult wait_for_tear_edge(TearSignalEdge edge,
                                                      std::int64_t timeout_us) {
    const auto& count = edge == TearSignalEdge::kRising ? tear_rising_edges_ : tear_falling_edges_;
    return wait_for_tear_edge_after(edge, count.load(std::memory_order_acquire), timeout_us);
  }

  [[nodiscard]] TearEdgeWaitResult wait_for_tear_edge_after(TearSignalEdge edge,
                                                            std::uint32_t edge_count,
                                                            std::int64_t timeout_us) {
    TearEdgeWaitResult result{};
    result.selected_edge = edge;
    if (!ready_ || timeout_us <= 0) {
      return result;
    }

    struct EdgeSnapshot {
      std::uint32_t count;
      std::uint32_t timestamp_us;
    };
    const auto snapshot = [&]() {
      const auto& count =
          edge == TearSignalEdge::kRising ? tear_rising_edges_ : tear_falling_edges_;
      const auto& timestamp =
          edge == TearSignalEdge::kRising ? tear_last_rise_us_ : tear_last_fall_us_;
      EdgeSnapshot value{};
      std::uint32_t count_after = 0;
      do {
        value.count = count.load(std::memory_order_acquire);
        value.timestamp_us = timestamp.load(std::memory_order_relaxed);
        count_after = count.load(std::memory_order_acquire);
      } while (value.count != count_after);
      return value;
    };

    const EdgeSnapshot start{.count = edge_count, .timestamp_us = 0};
    // Discard an old semaphore token, then rely on the selected edge counter.
    // If an ISR races this drain, the counter check preserves the observation.
    static_cast<void>(xSemaphoreTake(tear_semaphore_, 0));
    const auto wait_phase = [&](std::int64_t phase_timeout_us) {
      const std::int64_t deadline_us = esp_timer_get_time() + phase_timeout_us;
      while (true) {
        const EdgeSnapshot observed = snapshot();
        // Signed modular distance is rollover-safe for this bounded wait.
        if (static_cast<std::int32_t>(observed.count - start.count) > 0) {
          result.observed = true;
          result.edge_count = observed.count;
          result.isr_timestamp_us = observed.timestamp_us;
          result.task_resume_timestamp_us = static_cast<std::uint32_t>(esp_timer_get_time());
          return true;
        }
        const std::int64_t remaining_us = deadline_us - esp_timer_get_time();
        if (remaining_us <= 0) {
          result.edge_count = observed.count;
          result.isr_timestamp_us = observed.timestamp_us;
          result.task_resume_timestamp_us = static_cast<std::uint32_t>(esp_timer_get_time());
          return false;
        }
        const TickType_t ticks = std::max<TickType_t>(
            1, pdMS_TO_TICKS(static_cast<std::uint64_t>(remaining_us + 999) / 1'000U));
        // Either edge posts this semaphore. A wake from the unselected edge is
        // ignored unless the selected counter also advanced.
        static_cast<void>(xSemaphoreTake(tear_semaphore_, ticks));
      }
    };

    if (wait_phase(timeout_us)) {
      return result;
    }

    // No selected edge for the requested interval. Attempt one rate-limited
    // TEON heal, report the attempt, and require a real selected edge during
    // the diagnostic recovery window rather than converting healing to success.
    result.heal_attempted = true;
    result.heal_command_sent = heal_tear_signal();
    // A selected edge may have arrived while healing sampled the pin. Only
    // wait an additional recovery window when TEON was actually transmitted.
    if (wait_phase(result.heal_command_sent ? 34'000 : 0)) {
      return result;
    }
    result.timed_out = true;
    return result;
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
    const bool drained = wait_for_all(100'000);
    const bool paged =
        drained && esp_lcd_panel_io_tx_param(io_, 0xFE, init_fe.data(), init_fe.size()) == ESP_OK;
    const bool sent =
        paged && esp_lcd_panel_io_tx_param(io_, 0x35, init_35.data(), init_35.size()) == ESP_OK;
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
        "TINYDRAW_PANEL_TE_HEAL drained=%u paged=%u sent=%u high=%d/%d edges_delta=%lu "
        "isr_reinstall=%u falling_edges=%lu\n",
        drained, paged, sent, high_samples, kProbes,
        static_cast<unsigned long>(edges_after - edges_before), isr_reinstalled,
        static_cast<unsigned long>(edges_after));
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

  bool stream_rect(int x, int y, int width, int height, const std::uint16_t* pixels, int stride,
                   int strip_rows, PanelStagePatch patch) {
    if (!ready_ || pixels == nullptr || width <= 0 || height <= 0) {
      return false;
    }
    const int source_stride = stride == 0 ? width : stride;
    if (source_stride < width || x < 0 || y < 0 || x + width > kCanvasWidth ||
        y + height > kCanvasHeight) {
      return false;
    }
    int rows_per_transfer = std::min(strip_rows, kTransferPixels / width);
    if (rows_per_transfer <= 0) {
      return false;
    }

    // One window for the whole stream. set_gap only applies inside
    // esp_lcd_panel_draw_bitmap, so the x offset is applied here.
    const int x0 = x + kPanelGapX;
    const int x1 = x + width - 1 + kPanelGapX;
    const int y1 = y + height - 1;
    const std::array<std::uint8_t, 4> caset{
        static_cast<std::uint8_t>(x0 >> 8), static_cast<std::uint8_t>(x0 & 0xFF),
        static_cast<std::uint8_t>(x1 >> 8), static_cast<std::uint8_t>(x1 & 0xFF)};
    const std::array<std::uint8_t, 4> raset{
        static_cast<std::uint8_t>(y >> 8), static_cast<std::uint8_t>(y & 0xFF),
        static_cast<std::uint8_t>(y1 >> 8), static_cast<std::uint8_t>(y1 & 0xFF)};
    if (esp_lcd_panel_io_tx_param(io_, kWriteCommandOpcode | (0x2AU << 8U), caset.data(),
                                  caset.size()) != ESP_OK ||
        esp_lcd_panel_io_tx_param(io_, kWriteCommandOpcode | (0x2BU << 8U), raset.data(),
                                  raset.size()) != ESP_OK) {
      return false;
    }

    bool first = true;
    for (int row = 0; row < height; row += rows_per_transfer) {
      const int rows = std::min(rows_per_transfer, height - row);
      const std::int64_t transfer_started = esp_timer_get_time();
      if (xSemaphoreTake(transfer_semaphore_, portMAX_DELAY) != pdTRUE) {
        return false;
      }
      auto* transfer =
          transfer_pixels_ + static_cast<std::ptrdiff_t>(transfer_index_ * kTransferPixels);
      transfer_index_ = (transfer_index_ + 1U) % kTransferQueueDepth;
      transfer_us_ += esp_timer_get_time() - transfer_started;

      const std::int64_t prepare_started = esp_timer_get_time();
      if (patch.paint == nullptr) {
        for (int strip_row = 0; strip_row < rows; ++strip_row) {
          const auto* source =
              pixels + static_cast<std::ptrdiff_t>((row + strip_row)) * source_stride;
          stage_pixels_swapped(source, transfer + static_cast<std::ptrdiff_t>(strip_row * width),
                               width);
        }
      } else {
        for (int strip_row = 0; strip_row < rows; ++strip_row) {
          const auto* source =
              pixels + static_cast<std::ptrdiff_t>((row + strip_row)) * source_stride;
          auto* destination = transfer + static_cast<std::ptrdiff_t>(strip_row * width);
          for (int column = 0; column < width; ++column) {
            destination[column] = source[column];
          }
        }
        const auto staged =
            std::span(transfer, static_cast<std::size_t>(rows) * static_cast<std::size_t>(width));
        if (!patch.apply({.panel_x = x,
                          .panel_y = y + row,
                          .width = width,
                          .height = rows,
                          .stride = width,
                          .pixels = staged})) {
          static_cast<void>(xSemaphoreGive(transfer_semaphore_));
          return false;
        }
        swap_pixels_in_place(staged);
      }
      prepare_us_ += esp_timer_get_time() - prepare_started;

      const std::uint32_t command = first ? 0x2CU : 0x3CU;
      first = false;
      const std::int64_t submit_started = esp_timer_get_time();
      transfer_submits_.fetch_add(1U, std::memory_order_release);
      if (esp_lcd_panel_io_tx_color(io_, kWriteColorOpcode | (command << 8U), transfer,
                                    static_cast<std::size_t>(rows) *
                                        static_cast<std::size_t>(width) * sizeof(std::uint16_t)) !=
          ESP_OK) {
        // The submit counter already advanced; roll it back so wait_for_all
        // does not deadlock on a transfer that never entered the queue.
        transfer_submits_.fetch_sub(1U, std::memory_order_release);
        static_cast<void>(xSemaphoreGive(transfer_semaphore_));
        return false;
      }
      transfer_us_ += esp_timer_get_time() - submit_started;
      ++push_count_;
    }
    return true;
  }

  bool stream_rect_ring(int x, int y, int width, int height, const std::uint16_t* area_pixels,
                        int stride, int shift_x, int shift_y, int area_width, int area_height,
                        int strip_rows, PanelStagePatch patch, PanelStreamGate gate) {
    if (!ready_ || area_pixels == nullptr || width <= 0 || height <= 0 || stride < area_width ||
        shift_x < 0 || shift_x >= area_width || shift_y < 0 || shift_y >= area_height || x < 0 ||
        y < 0 || x + width > area_width || y + height > area_height || x + width > kCanvasWidth ||
        y + height > kCanvasHeight) {
      return false;
    }
    const int rows_per_transfer = std::min(strip_rows, kTransferPixels / width);
    if (rows_per_transfer <= 0) {
      return false;
    }

    const int x0 = x + kPanelGapX;
    const int x1 = x + width - 1 + kPanelGapX;
    const int y1 = y + height - 1;
    const std::array<std::uint8_t, 4> caset{
        static_cast<std::uint8_t>(x0 >> 8), static_cast<std::uint8_t>(x0 & 0xFF),
        static_cast<std::uint8_t>(x1 >> 8), static_cast<std::uint8_t>(x1 & 0xFF)};
    const std::array<std::uint8_t, 4> raset{
        static_cast<std::uint8_t>(y >> 8), static_cast<std::uint8_t>(y & 0xFF),
        static_cast<std::uint8_t>(y1 >> 8), static_cast<std::uint8_t>(y1 & 0xFF)};
    if (esp_lcd_panel_io_tx_param(io_, kWriteCommandOpcode | (0x2AU << 8U), caset.data(),
                                  caset.size()) != ESP_OK ||
        esp_lcd_panel_io_tx_param(io_, kWriteCommandOpcode | (0x2BU << 8U), raset.data(),
                                  raset.size()) != ESP_OK) {
      return false;
    }

    struct PreparedStrip {
      std::uint16_t* pixels = nullptr;
      int row = 0;
      int rows = 0;
    };
    const auto prepare_strip = [&](int row, PreparedStrip& prepared) {
      const int rows = std::min(rows_per_transfer, height - row);
      const std::int64_t transfer_started = esp_timer_get_time();
      if (xSemaphoreTake(transfer_semaphore_, portMAX_DELAY) != pdTRUE) {
        return false;
      }
      auto* transfer =
          transfer_pixels_ + static_cast<std::ptrdiff_t>(transfer_index_ * kTransferPixels);
      transfer_index_ = (transfer_index_ + 1U) % kTransferQueueDepth;
      const std::int64_t acquired = esp_timer_get_time();
      acquire_wait_us_ += acquired - transfer_started;
      transfer_us_ += acquired - transfer_started;

      const std::int64_t prepare_started = acquired;
      for (int strip_row = 0; strip_row < rows; ++strip_row) {
        int source_row = y + row + strip_row + shift_y;
        if (source_row >= area_height) {
          source_row -= area_height;
        }
        const auto* source = area_pixels + static_cast<std::ptrdiff_t>(source_row) * stride;
        copy_ring_row(source, area_width, shift_x, x, width,
                      transfer + static_cast<std::ptrdiff_t>(strip_row * width));
      }
      const auto staged =
          std::span(transfer, static_cast<std::size_t>(rows) * static_cast<std::size_t>(width));
      const std::int64_t copy_completed = esp_timer_get_time();
      ring_copy_us_ += copy_completed - prepare_started;
      if (!patch.apply({.panel_x = x,
                        .panel_y = y + row,
                        .width = width,
                        .height = rows,
                        .stride = width,
                        .pixels = staged})) {
        static_cast<void>(xSemaphoreGive(transfer_semaphore_));
        return false;
      }
      const std::int64_t patch_completed = esp_timer_get_time();
      patch_us_ += patch_completed - copy_completed;
      swap_pixels_in_place(staged);
      const std::int64_t swap_completed = esp_timer_get_time();
      byte_swap_us_ += swap_completed - patch_completed;
      prepare_us_ += swap_completed - prepare_started;
      prepared = {.pixels = transfer, .row = row, .rows = rows};
      return true;
    };

    bool first = true;
    const auto submit_strip = [&](const PreparedStrip& prepared) {
      const std::uint32_t command = first ? 0x2CU : 0x3CU;
      const std::int64_t submit_started = esp_timer_get_time();
      transfer_submits_.fetch_add(1U, std::memory_order_release);
      if (esp_lcd_panel_io_tx_color(io_, kWriteColorOpcode | (command << 8U), prepared.pixels,
                                    static_cast<std::size_t>(prepared.rows) *
                                        static_cast<std::size_t>(width) * sizeof(std::uint16_t)) !=
          ESP_OK) {
        transfer_submits_.fetch_sub(1U, std::memory_order_release);
        static_cast<void>(xSemaphoreGive(transfer_semaphore_));
        return false;
      }
      first = false;
      transfer_us_ += esp_timer_get_time() - submit_started;
      ++push_count_;
      return true;
    };

    std::array<PreparedStrip, kTransferQueueDepth> prepared{};
    std::size_t prepared_count = 0;
    int next_row = 0;
    if (gate.wait != nullptr) {
      while (prepared_count < prepared.size() && next_row < height) {
        if (!prepare_strip(next_row, prepared[prepared_count])) {
          for (std::size_t index = 0; index < prepared_count; ++index) {
            static_cast<void>(xSemaphoreGive(transfer_semaphore_));
          }
          return false;
        }
        next_row += prepared[prepared_count].rows;
        ++prepared_count;
      }
      if (!gate.apply()) {
        for (std::size_t index = 0; index < prepared_count; ++index) {
          static_cast<void>(xSemaphoreGive(transfer_semaphore_));
        }
        return false;
      }
      for (std::size_t index = 0; index < prepared_count; ++index) {
        if (!submit_strip(prepared[index])) {
          for (++index; index < prepared_count; ++index) {
            static_cast<void>(xSemaphoreGive(transfer_semaphore_));
          }
          return false;
        }
      }
    }
    while (next_row < height) {
      PreparedStrip strip;
      if (!prepare_strip(next_row, strip) || !submit_strip(strip)) {
        return false;
      }
      next_row += strip.rows;
    }
    return true;
  }

  int read_scanline() {
    std::array<std::uint8_t, 2> value{};
    if (!read_register(0x45U, value.data(), value.size())) {
      return -1;
    }
    return (static_cast<int>(value[0]) << 8) | static_cast<int>(value[1]);
  }

  bool read_register(std::uint8_t command, std::uint8_t* value, std::size_t length) {
    if (!ready_ || value == nullptr || length == 0U) {
      return false;
    }
    return esp_lcd_panel_io_rx_param(
               io_, kReadCommandOpcode | (static_cast<std::uint32_t>(command) << 8U), value,
               length) == ESP_OK;
  }

 private:
  static constexpr std::uint32_t kWriteCommandOpcode = 0x02UL << 24U;
  static constexpr std::uint32_t kReadCommandOpcode = 0x03UL << 24U;
  static constexpr std::uint32_t kWriteColorOpcode = 0x32UL << 24U;

  static void on_tear_edge(void* context) {
    auto& self = *static_cast<Impl*>(context);
    const std::uint32_t now = static_cast<std::uint32_t>(esp_timer_get_time());
    if (gpio_get_level(kTearPin) != 0) {
      const std::uint32_t prior = self.tear_last_rise_us_.exchange(now, std::memory_order_relaxed);
      if (prior != 0U) {
        self.tear_period_us_.store(now - prior, std::memory_order_relaxed);
      }
      self.tear_rising_edges_.fetch_add(1U, std::memory_order_release);
    } else {
      const std::uint32_t rise = self.tear_last_rise_us_.load(std::memory_order_relaxed);
      if (rise != 0U) {
        self.tear_high_us_.store(now - rise, std::memory_order_relaxed);
      }
      self.tear_last_fall_us_.store(now, std::memory_order_relaxed);
      self.tear_falling_edges_.fetch_add(1U, std::memory_order_release);
    }
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
  std::int64_t acquire_wait_us_ = 0;
  std::int64_t ring_copy_us_ = 0;
  std::int64_t patch_us_ = 0;
  std::int64_t byte_swap_us_ = 0;
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
std::int64_t Co5300PanelTransport::acquire_wait_us() const { return impl_->acquire_wait_us(); }
std::int64_t Co5300PanelTransport::ring_copy_us() const { return impl_->ring_copy_us(); }
std::int64_t Co5300PanelTransport::patch_us() const { return impl_->patch_us(); }
std::int64_t Co5300PanelTransport::byte_swap_us() const { return impl_->byte_swap_us(); }
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
TearEdgeWaitResult Co5300PanelTransport::wait_for_tear_edge(TearSignalEdge edge,
                                                            std::int64_t timeout_us) {
  return impl_->wait_for_tear_edge(edge, timeout_us);
}
TearEdgeWaitResult Co5300PanelTransport::wait_for_tear_edge_after(TearSignalEdge edge,
                                                                  std::uint32_t edge_count,
                                                                  std::int64_t timeout_us) {
  return impl_->wait_for_tear_edge_after(edge, edge_count, timeout_us);
}
std::int64_t Co5300PanelTransport::tear_age_us(TearSignalEdge edge) const {
  return impl_->tear_age_us(edge);
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
bool Co5300PanelTransport::stream_rect(int x, int y, int width, int height,
                                       const std::uint16_t* pixels, int stride, int strip_rows,
                                       PanelStagePatch patch) {
  return impl_->stream_rect(x, y, width, height, pixels, stride, strip_rows, patch);
}
bool Co5300PanelTransport::stream_rect_ring(int x, int y, int width, int height,
                                            const std::uint16_t* area_pixels, int stride,
                                            int shift_x, int shift_y, int area_width,
                                            int area_height, int strip_rows, PanelStagePatch patch,
                                            PanelStreamGate gate) {
  return impl_->stream_rect_ring(x, y, width, height, area_pixels, stride, shift_x, shift_y,
                                 area_width, area_height, strip_rows, patch, gate);
}
int Co5300PanelTransport::read_scanline() { return impl_->read_scanline(); }
bool Co5300PanelTransport::read_register(std::uint8_t command, std::uint8_t* value,
                                         std::size_t length) {
  return impl_->read_register(command, value, length);
}

}  // namespace tinydraw::esp32
