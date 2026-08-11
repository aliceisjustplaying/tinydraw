#include "rtc_clock.h"

#include <array>
#include <cstdio>
#include <string_view>

#include "esp_err.h"

namespace tinydraw::esp32 {
namespace {

constexpr std::uint8_t kSecondsRegister = 0x04;
constexpr std::uint8_t kOscillatorStopped = 0x80;

constexpr bool leap_year(std::uint16_t year) {
  return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
}

constexpr std::uint8_t days_in_month(std::uint16_t year, std::uint8_t month) {
  constexpr std::array<std::uint8_t, 12> days{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return month == 2U && leap_year(year) ? 29U : days[month - 1U];
}

constexpr bool valid_time(const FatDateTime& time) {
  return time.year >= 1980U && time.year <= 2069U && time.month >= 1U && time.month <= 12U &&
         time.day >= 1U && time.day <= days_in_month(time.year, time.month) && time.hour <= 23U &&
         time.minute <= 59U && time.second <= 59U;
}

constexpr bool before(const FatDateTime& left, const FatDateTime& right) {
  if (left.year != right.year) return left.year < right.year;
  if (left.month != right.month) return left.month < right.month;
  if (left.day != right.day) return left.day < right.day;
  if (left.hour != right.hour) return left.hour < right.hour;
  if (left.minute != right.minute) return left.minute < right.minute;
  return left.second < right.second;
}

constexpr std::uint8_t weekday(std::uint16_t year, std::uint8_t month, std::uint8_t day) {
  constexpr std::array<int, 12> offsets{0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  int adjusted_year = year;
  if (month < 3U) --adjusted_year;
  return static_cast<std::uint8_t>((adjusted_year + adjusted_year / 4 - adjusted_year / 100 +
                                    adjusted_year / 400 + offsets[month - 1U] + day) %
                                   7);
}

std::uint8_t decimal(std::string_view text) {
  std::uint8_t value = 0;
  for (char character : text) {
    if (character >= '0' && character <= '9') {
      value = static_cast<std::uint8_t>(value * 10U + character - '0');
    }
  }
  return value;
}

FatDateTime build_time() {
  constexpr std::array<std::string_view, 12> months{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  constexpr std::string_view date = __DATE__;
  constexpr std::string_view time = __TIME__;
  FatDateTime result;
  result.year = static_cast<std::uint16_t>(decimal(date.substr(7, 4)));
  result.day = decimal(date.substr(4, 2));
  for (std::size_t index = 0; index < months.size(); ++index) {
    if (date.substr(0, 3) == months[index]) {
      result.month = static_cast<std::uint8_t>(index + 1U);
      break;
    }
  }
  result.hour = decimal(time.substr(0, 2));
  result.minute = decimal(time.substr(3, 2));
  result.second = decimal(time.substr(6, 2));
  return result;
}

}  // namespace

RtcClock::RtcClock(i2c_master_bus_handle_t bus) {
  if (bus == nullptr || pcf85063a_init(&device_, bus, PCF85063A_ADDRESS) != ESP_OK) {
    return;
  }
  ready_ = true;

  FatDateTime current;
  const FatDateTime compiled = build_time();
  const bool retained = read(current);
  if ((!retained || before(current, compiled)) && set(compiled)) {
    current = compiled;
  }
  std::printf("TINYDRAW_RTC ready=1 retained=%u utc=%04u-%02u-%02uT%02u:%02u:%02uZ\n", retained,
              current.year, current.month, current.day, current.hour, current.minute,
              current.second);
}

RtcClock::~RtcClock() {
  if (device_.dev_handle != nullptr) {
    static_cast<void>(i2c_master_bus_rm_device(device_.dev_handle));
  }
}

bool RtcClock::read(FatDateTime& time) {
  if (!ready_) {
    return false;
  }
  std::uint8_t seconds = 0;
  if (pcf85063a_read_register(&device_, kSecondsRegister, &seconds, 1) != ESP_OK ||
      (seconds & kOscillatorStopped) != 0U) {
    return false;
  }
  pcf85063a_datetime_t value{};
  if (pcf85063a_get_time_date(&device_, &value) != ESP_OK) {
    return false;
  }
  const FatDateTime candidate{.year = value.year,
                              .month = value.month,
                              .day = value.day,
                              .hour = value.hour,
                              .minute = value.min,
                              .second = value.sec};
  if (!valid_time(candidate)) {
    return false;
  }
  time = candidate;
  return true;
}

bool RtcClock::set(const FatDateTime& time) {
  if (!ready_ || !valid_time(time)) {
    return false;
  }
  const pcf85063a_datetime_t value{.year = time.year,
                                   .month = time.month,
                                   .day = time.day,
                                   .dotw = weekday(time.year, time.month, time.day),
                                   .hour = time.hour,
                                   .min = time.minute,
                                   .sec = time.second};
  return pcf85063a_set_time_date(&device_, value) == ESP_OK;
}

}  // namespace tinydraw::esp32
