#include "time_sync.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string_view>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "time_sync_config.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::string_view kSsid = TINYDRAW_WIFI_SSID;
constexpr std::string_view kPassword = TINYDRAW_WIFI_PASSWORD;
constexpr std::string_view kTimezone = TINYDRAW_TIMEZONE;
constexpr TickType_t kConnectPoll = pdMS_TO_TICKS(100);
constexpr int kConnectPolls = 100;
constexpr TickType_t kNtpTimeout = pdMS_TO_TICKS(10'000);

void stop_network(esp_netif_t* station, bool wifi_initialized, bool sntp_initialized,
                  bool nvs_initialized) {
  if (sntp_initialized) {
    esp_netif_sntp_deinit();
  }
  if (wifi_initialized) {
    static_cast<void>(esp_wifi_disconnect());
    static_cast<void>(esp_wifi_stop());
    static_cast<void>(esp_wifi_deinit());
  }
  if (station != nullptr) {
    esp_netif_destroy_default_wifi(station);
  }
  if (nvs_initialized) {
    static_cast<void>(nvs_flash_deinit());
  }
}

bool connect_to_hotspot(esp_netif_t*& station, bool& wifi_initialized, bool& nvs_initialized) {
  const esp_err_t netif_result = esp_netif_init();
  if (netif_result != ESP_OK && netif_result != ESP_ERR_INVALID_STATE) {
    return false;
  }
  const esp_err_t event_result = esp_event_loop_create_default();
  if (event_result != ESP_OK && event_result != ESP_ERR_INVALID_STATE) {
    return false;
  }
  station = esp_netif_create_default_wifi_sta();
  if (station == nullptr) {
    return false;
  }

  esp_err_t nvs_result = nvs_flash_init();
  if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES || nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_result = nvs_flash_erase();
    if (nvs_result == ESP_OK) {
      nvs_result = nvs_flash_init();
    }
  }
  if (nvs_result != ESP_OK) {
    return false;
  }
  nvs_initialized = true;

  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  if (esp_wifi_init(&init) != ESP_OK) {
    return false;
  }
  wifi_initialized = true;
  if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
    return false;
  }

  wifi_config_t config{};
  std::copy_n(kSsid.begin(), kSsid.size(), config.sta.ssid);
  std::copy_n(kPassword.begin(), kPassword.size(), config.sta.password);
  config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
  config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
  config.sta.threshold.authmode = kPassword.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  config.sta.pmf_cfg.capable = true;
  config.sta.pmf_cfg.required = false;
  if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
      esp_wifi_set_config(WIFI_IF_STA, &config) != ESP_OK || esp_wifi_start() != ESP_OK ||
      esp_wifi_connect() != ESP_OK) {
    return false;
  }

  for (int poll = 0; poll < kConnectPolls; ++poll) {
    esp_netif_ip_info_t ip{};
    if (esp_netif_get_ip_info(station, &ip) == ESP_OK && ip.ip.addr != 0U) {
      return true;
    }
    if (poll > 0 && poll % 20 == 0) {
      static_cast<void>(esp_wifi_connect());
    }
    vTaskDelay(kConnectPoll);
  }
  return false;
}

bool sync_clock(RtcClock& clock) {
  esp_netif_t* station = nullptr;
  bool wifi_initialized = false;
  bool sntp_initialized = false;
  bool nvs_initialized = false;
  bool success = false;

  if (connect_to_hotspot(station, wifi_initialized, nvs_initialized)) {
    const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&config) == ESP_OK) {
      sntp_initialized = true;
      if (esp_netif_sntp_sync_wait(kNtpTimeout) == ESP_OK) {
        std::time_t epoch = 0;
        static_cast<void>(std::time(&epoch));
        static_cast<void>(setenv("TZ", kTimezone.data(), 1));
        tzset();
        std::tm local{};
        if (localtime_r(&epoch, &local) != nullptr) {
          const FatDateTime time{.year = static_cast<std::uint16_t>(local.tm_year + 1900),
                                 .month = static_cast<std::uint8_t>(local.tm_mon + 1),
                                 .day = static_cast<std::uint8_t>(local.tm_mday),
                                 .hour = static_cast<std::uint8_t>(local.tm_hour),
                                 .minute = static_cast<std::uint8_t>(local.tm_min),
                                 .second = static_cast<std::uint8_t>(local.tm_sec)};
          success = clock.set(time);
          if (success) {
            std::printf("TINYDRAW_NTP_OK local=%04u-%02u-%02uT%02u:%02u:%02u\n", time.year,
                        time.month, time.day, time.hour, time.minute, time.second);
          }
        }
      }
    }
  }

  stop_network(station, wifi_initialized, sntp_initialized, nvs_initialized);
  std::printf("TINYDRAW_NTP_DONE success=%u wifi_stopped=1\n", success);
  return success;
}

void time_sync_task(void* argument) {
  auto& clock = *static_cast<RtcClock*>(argument);
  static_cast<void>(sync_clock(clock));
  vTaskDelete(nullptr);
}

}  // namespace

bool start_time_sync(RtcClock& clock) {
  if (!clock.ready() || kSsid.empty() || kSsid.size() >= sizeof(wifi_sta_config_t{}.ssid) ||
      kPassword.size() >= sizeof(wifi_sta_config_t{}.password)) {
    std::printf("TINYDRAW_NTP_DISABLED rtc=%u credentials=%u\n", clock.ready(), !kSsid.empty());
    return false;
  }
  return xTaskCreatePinnedToCore(time_sync_task, "tinydraw_ntp", 4096U, &clock, 1U, nullptr, 1) ==
         pdPASS;
}

}  // namespace tinydraw::esp32
