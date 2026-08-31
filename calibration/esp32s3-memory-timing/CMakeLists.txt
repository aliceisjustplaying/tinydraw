cmake_minimum_required(VERSION 3.22)

set(IDF_TARGET esp32s3 CACHE STRING "Calibration target")
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(esp32s3_memory_timing_calibration)
