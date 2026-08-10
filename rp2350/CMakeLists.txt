set(PICO_BOARD pico2 CACHE STRING "RP2350 board")

cmake_minimum_required(VERSION 3.27)

if(NOT DEFINED ENV{PICO_SDK_PATH})
  message(FATAL_ERROR "PICO_SDK_PATH is unset; run ./scripts/rp2350 bootstrap")
endif()
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(tinydraw_rp2350 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

pico_sdk_init()

add_executable(tinydraw_rp2350
  src/main.cpp
  ../core/src/ink_stream.cpp
  ../core/src/toolbar.cpp
  vendor/amoled/AMOLED_1in8.c
  vendor/config/DEV_Config.c
  vendor/qspi/qspi_pio.c
  vendor/touch/FT3168.c
)

pico_generate_pio_header(tinydraw_rp2350
  ${CMAKE_CURRENT_LIST_DIR}/vendor/qspi/qspi.pio
)

target_include_directories(tinydraw_rp2350 PRIVATE
  ../core/include
  vendor/amoled
  vendor/config
  vendor/qspi
  vendor/touch
)

target_compile_options(tinydraw_rp2350 PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion;-fno-exceptions;-fno-rtti>
)

target_link_libraries(tinydraw_rp2350 PRIVATE
  pico_stdlib
  hardware_adc
  hardware_clocks
  hardware_dma
  hardware_gpio
  hardware_i2c
  hardware_pio
  hardware_pwm
  hardware_spi
  hardware_watchdog
)

pico_enable_stdio_usb(tinydraw_rp2350 1)
pico_enable_stdio_uart(tinydraw_rp2350 0)
pico_add_extra_outputs(tinydraw_rp2350)
