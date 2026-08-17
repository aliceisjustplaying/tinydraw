#include "usb_export.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "esp_err.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tusb.h"

namespace {

constexpr std::uint8_t kMscInterface = 0;
constexpr std::uint8_t kInterfaceCount = 1;
constexpr std::uint8_t kMscOutEndpoint = 0x01;
constexpr std::uint8_t kMscInEndpoint = 0x81;
constexpr std::uint16_t kConfigurationLength = TUD_CONFIG_DESC_LEN + TUD_MSC_DESC_LEN;

const tusb_desc_device_t device_descriptor{
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4010,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

const std::uint8_t configuration_descriptor[]{
    TUD_CONFIG_DESCRIPTOR(1, kInterfaceCount, 0, kConfigurationLength, 0, 500),
    TUD_MSC_DESCRIPTOR(kMscInterface, 4, kMscOutEndpoint, kMscInEndpoint, 64),
};

constexpr std::array<std::string_view, 5> string_descriptors{
    "", "TinyDraw", "TinyDraw Export", "TINYDRAW01", "Drawing",
};

std::atomic<tinydraw::esp32::UsbExport*> active_export{nullptr};
std::atomic<usb_phy_handle_t> usb_phy{nullptr};
std::atomic<TaskHandle_t> usb_task_handle{nullptr};
std::atomic<TaskHandle_t> usb_start_waiter{nullptr};
std::atomic<TaskHandle_t> usb_stop_waiter{nullptr};
std::atomic<bool> usb_stack_ready{false};
std::atomic<bool> usb_stop_requested{false};
std::atomic<bool> usb_stop_succeeded{false};

void usb_device_task(void*) {
  const tusb_rhport_init_t config{
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_FULL,
  };
  const bool initialized = tusb_rhport_init(0, &config);
  usb_stack_ready.store(initialized);
  if (const TaskHandle_t waiter = usb_start_waiter.load(); waiter != nullptr) {
    xTaskNotifyGive(waiter);
  }

  for (;;) {
    while (!usb_stop_requested.load()) {
      if (initialized) {
        // A bounded wait lets the same task that initialized TinyUSB also own
        // deinitialization when the application asks to leave export mode.
        tud_task_ext(10U, false);
      } else {
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }

    // TinyUSB must be deinitialized by its owning task. If it refuses, keep
    // servicing the stack and let the application retry instead of abandoning
    // a live controller while claiming that drawing mode resumed.
    if (initialized && !tud_deinit(0)) {
      usb_stop_succeeded.store(false);
      usb_stop_requested.store(false);
      if (const TaskHandle_t waiter = usb_stop_waiter.load(); waiter != nullptr) {
        xTaskNotifyGive(waiter);
      }
      continue;
    }

    usb_stack_ready.store(false);
    bool phy_released = true;
    if (const usb_phy_handle_t phy = usb_phy.load(); phy != nullptr) {
      phy_released = usb_del_phy(phy) == ESP_OK;
      if (phy_released) {
        usb_phy.store(nullptr);
      }
    }
    usb_stop_succeeded.store(phy_released);
    usb_task_handle.store(nullptr);
    if (const TaskHandle_t waiter = usb_stop_waiter.load(); waiter != nullptr) {
      xTaskNotifyGive(waiter);
    }
    vTaskDelete(nullptr);
  }
}

}  // namespace

namespace tinydraw::esp32 {

UsbExport::UsbExport(const ReadOnlyFile& file, Fat83Name name) : disk_(file, name) {}

UsbExport::UsbExport(const ReadOnlyFile& first_file, Fat83Name first_name,
                     const ReadOnlyFile& second_file, Fat83Name second_name)
    : disk_(first_file, first_name, second_file, second_name) {}

bool UsbExport::prepare_export() { return stop(); }

bool UsbExport::finish_export(bool image_available) { return image_available && start(); }

bool UsbExport::stop() {
  if (const UsbExport* owner = active_export.load(); owner != nullptr && owner != this) {
    return false;
  }

  const bool owns_session = active_export.load() == this || session_.active();
  if (owns_session && !session_.begin_stop()) {
    return false;
  }

  const auto finish_stop = [&]() {
    UsbExport* expected = this;
    static_cast<void>(active_export.compare_exchange_strong(expected, nullptr));
    session_.end();
    usb_stop_requested.store(false);
    return true;
  };

  if (usb_task_handle.load() == nullptr) {
    if (const usb_phy_handle_t phy = usb_phy.load(); phy != nullptr) {
      if (usb_del_phy(phy) != ESP_OK) {
        return false;
      }
      usb_phy.store(nullptr);
    }
    return finish_stop();
  }

  usb_stop_succeeded.store(false);
  usb_stop_waiter.store(xTaskGetCurrentTaskHandle());
  usb_stop_requested.store(true);
  const bool notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2'000)) != 0U;
  usb_stop_waiter.store(nullptr);
  if (!notified || !usb_stop_succeeded.load()) {
    return false;
  }
  return finish_stop();
}

bool UsbExport::read(std::uint32_t lba, std::uint32_t offset,
                     std::span<std::uint8_t> output) const {
  return session_.media_present() && disk_.read(lba, offset, output);
}

bool UsbExport::start() {
  if (session_.media_present()) {
    return true;
  }
  if (session_.active() || usb_task_handle.load() != nullptr || usb_phy.load() != nullptr ||
      !session_.begin()) {
    return false;
  }
  UsbExport* expected = nullptr;
  if (!active_export.compare_exchange_strong(expected, this)) {
    session_.end();
    return false;
  }

  const usb_phy_config_t phy_config{
      .controller = USB_PHY_CTRL_OTG,
      .target = USB_PHY_TARGET_INT,
      .otg_mode = USB_OTG_MODE_DEVICE,
      .otg_speed = USB_PHY_SPEED_FULL,
      .ext_io_conf = nullptr,
      .otg_io_conf = nullptr,
  };
  usb_phy_handle_t created_phy = nullptr;
  if (usb_new_phy(&phy_config, &created_phy) != ESP_OK) {
    active_export.store(nullptr);
    session_.end();
    return false;
  }
  usb_phy.store(created_phy);
  usb_stop_requested.store(false);
  usb_stop_succeeded.store(false);
  usb_stack_ready.store(false);
  usb_start_waiter.store(xTaskGetCurrentTaskHandle());
  TaskHandle_t created_task = nullptr;
  if (xTaskCreatePinnedToCore(usb_device_task, "tinydraw_usb", 4096U, nullptr, 5U, &created_task,
                              1) != pdPASS) {
    usb_start_waiter.store(nullptr);
    static_cast<void>(usb_del_phy(usb_phy.exchange(nullptr)));
    active_export.store(nullptr);
    session_.end();
    return false;
  }
  usb_task_handle.store(created_task);
  const bool notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2'000)) != 0U;
  usb_start_waiter.store(nullptr);
  if (!notified || !usb_stack_ready.load()) {
    static_cast<void>(stop());
    return false;
  }
  return true;
}

}  // namespace tinydraw::esp32

extern "C" {

const std::uint8_t* tud_descriptor_device_cb() {
  return reinterpret_cast<const std::uint8_t*>(&device_descriptor);
}

const std::uint8_t* tud_descriptor_configuration_cb(std::uint8_t) {
  return configuration_descriptor;
}

const std::uint16_t* tud_descriptor_string_cb(std::uint8_t index, std::uint16_t) {
  static std::array<std::uint16_t, 32> descriptor{};
  if (index >= string_descriptors.size()) {
    return nullptr;
  }
  if (index == 0U) {
    descriptor[1] = 0x0409U;
    descriptor[0] = static_cast<std::uint16_t>((TUSB_DESC_STRING << 8U) | 4U);
    return descriptor.data();
  }
  const auto text = string_descriptors[index];
  const std::size_t length = std::min(text.size(), descriptor.size() - 1U);
  for (std::size_t character = 0; character < length; ++character) {
    descriptor[character + 1U] = static_cast<std::uint8_t>(text[character]);
  }
  descriptor[0] = static_cast<std::uint16_t>((TUSB_DESC_STRING << 8U) | (length * 2U + 2U));
  return descriptor.data();
}

void tud_msc_inquiry_cb(std::uint8_t, std::uint8_t vendor_id[8], std::uint8_t product_id[16],
                        std::uint8_t product_rev[4]) {
  std::memcpy(vendor_id, "TINYDRAW", 8);
  std::memcpy(product_id, "Drawing Export  ", 16);
  std::memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(std::uint8_t lun) {
  const auto* export_volume = active_export.load();
  if (export_volume == nullptr || !export_volume->media_present()) {
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
    return false;
  }
  return true;
}

void tud_msc_capacity_cb(std::uint8_t, std::uint32_t* block_count, std::uint16_t* block_size) {
  *block_count = tinydraw::Fat16ExportDisk::kBlockCount;
  *block_size = tinydraw::Fat16ExportDisk::kBlockSize;
}

bool tud_msc_is_writable_cb(std::uint8_t) { return false; }

bool tud_msc_start_stop_cb(std::uint8_t, std::uint8_t, bool start, bool load_eject) {
  if (load_eject && !start) {
    if (auto* export_volume = active_export.load(); export_volume != nullptr) {
      export_volume->note_host_ejected();
    }
  }
  return true;
}

void tud_umount_cb() {
  if (auto* export_volume = active_export.load(); export_volume != nullptr) {
    export_volume->note_host_ejected();
  }
}

std::int32_t tud_msc_read10_cb(std::uint8_t lun, std::uint32_t lba, std::uint32_t offset,
                               void* buffer, std::uint32_t buffer_size) {
  const auto* export_volume = active_export.load();
  if (export_volume == nullptr || !export_volume->media_present()) {
    tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3A, 0x00);
    return -1;
  }
  if (!export_volume->read(lba, offset,
                           std::span(static_cast<std::uint8_t*>(buffer), buffer_size))) {
    tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
    return -1;
  }
  return static_cast<std::int32_t>(buffer_size);
}

std::int32_t tud_msc_write10_cb(std::uint8_t lun, std::uint32_t, std::uint32_t, std::uint8_t*,
                                std::uint32_t) {
  tud_msc_set_sense(lun, SCSI_SENSE_DATA_PROTECT, 0x27, 0x00);
  return -1;
}

std::int32_t tud_msc_scsi_cb(std::uint8_t lun, const std::uint8_t command[16], void*,
                             std::uint16_t) {
  if (command[0] == SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL) {
    return 0;
  }
  tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
  return -1;
}

}  // extern "C"
