#include "usb_export.h"

#include <algorithm>
#include <array>
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

tinydraw::esp32::UsbExport* active_export = nullptr;
usb_phy_handle_t usb_phy = nullptr;
TaskHandle_t usb_task_handle = nullptr;
TaskHandle_t usb_start_waiter = nullptr;
bool usb_stack_ready = false;

void usb_device_task(void*) {
  const tusb_rhport_init_t config{
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_FULL,
  };
  usb_stack_ready = tusb_rhport_init(0, &config);
  xTaskNotifyGive(usb_start_waiter);
  if (!usb_stack_ready) {
    usb_task_handle = nullptr;
    vTaskDelete(nullptr);
    return;
  }
  while (true) {
    tud_task();
  }
}

}  // namespace

namespace tinydraw::esp32 {

UsbExport::UsbExport(const ReadOnlyFile& file, Fat83Name name) : disk_(file, name) {}

void UsbExport::prepare_export() {
  if (active_) {
    static_cast<void>(tud_disconnect());
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

bool UsbExport::finish_export(bool image_available) {
  if (active_) {
    vTaskDelay(pdMS_TO_TICKS(100));
    static_cast<void>(tud_connect());
    return true;
  }
  return image_available && start();
}

bool UsbExport::read(std::uint32_t lba, std::uint32_t offset,
                     std::span<std::uint8_t> output) const {
  return disk_.read(lba, offset, output);
}

bool UsbExport::start() {
  active_export = this;
  const usb_phy_config_t phy_config{
      .controller = USB_PHY_CTRL_OTG,
      .target = USB_PHY_TARGET_INT,
      .otg_mode = USB_OTG_MODE_DEVICE,
      .otg_speed = USB_PHY_SPEED_FULL,
      .ext_io_conf = nullptr,
      .otg_io_conf = nullptr,
  };
  if (usb_new_phy(&phy_config, &usb_phy) != ESP_OK) {
    active_export = nullptr;
    return false;
  }
  usb_start_waiter = xTaskGetCurrentTaskHandle();
  usb_stack_ready = false;
  if (xTaskCreatePinnedToCore(usb_device_task, "tinydraw_usb", 4096U, nullptr, 5U, &usb_task_handle,
                              1) != pdPASS ||
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2'000)) == 0U || !usb_stack_ready) {
    if (usb_task_handle != nullptr) {
      vTaskDelete(usb_task_handle);
      usb_task_handle = nullptr;
    }
    static_cast<void>(usb_del_phy(usb_phy));
    usb_phy = nullptr;
    active_export = nullptr;
    return false;
  }
  active_ = true;
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

bool tud_msc_test_unit_ready_cb(std::uint8_t) { return active_export != nullptr; }

void tud_msc_capacity_cb(std::uint8_t, std::uint32_t* block_count, std::uint16_t* block_size) {
  *block_count = tinydraw::Fat16ExportDisk::kBlockCount;
  *block_size = tinydraw::Fat16ExportDisk::kBlockSize;
}

bool tud_msc_is_writable_cb(std::uint8_t) { return false; }

bool tud_msc_start_stop_cb(std::uint8_t, std::uint8_t, bool, bool) { return true; }

std::int32_t tud_msc_read10_cb(std::uint8_t lun, std::uint32_t lba, std::uint32_t offset,
                               void* buffer, std::uint32_t buffer_size) {
  if (active_export == nullptr ||
      !active_export->read(lba, offset,
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
