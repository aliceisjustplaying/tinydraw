cmake_minimum_required(VERSION 3.22)

set(IDF_TARGET esp32s3 CACHE STRING "TinyDraw firmware target")
option(TINYDRAW_QEMU_GRAPHICS "Enable the QEMU-only virtual display" OFF)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(tinydraw_esp32)
