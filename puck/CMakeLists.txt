if(NOT CMAKE_CXX_COMPILER_TARGET STREQUAL "wasm32-wasip1")
  message(FATAL_ERROR "TINYDRAW_BUILD_PUCK requires CMAKE_CXX_COMPILER_TARGET=wasm32-wasip1")
endif()

set(TINYDRAW_PUCK_FIRMWARE_DIR "${PROJECT_SOURCE_DIR}/esp32/main")
set(TINYDRAW_PUCK_APP_DIR "${TINYDRAW_PUCK_FIRMWARE_DIR}/vector_v2")
include("${TINYDRAW_PUCK_APP_DIR}/sources.cmake")
option(TINYDRAW_PUCK_DEMO "Include TinyDraw's record/replay demo controller in Puck" ON)
set(TINYDRAW_PUCK_APP_SRCS ${TINYDRAW_VECTOR_V2_FIRMWARE_SRCS})
if(TINYDRAW_PUCK_DEMO)
  list(APPEND TINYDRAW_PUCK_APP_SRCS ${TINYDRAW_VECTOR_V2_DEMO_SRCS})
endif()

add_executable(tinydraw_puck
  src/tinydraw_emu.cpp
  src/wasi_support.cpp
  platform/board_stubs.cpp
  platform/co5300_panel_transport.cpp
  platform/esp_shim.cpp
  platform/export_stubs.cpp
  ${TINYDRAW_PUCK_APP_SRCS}
)
target_link_libraries(tinydraw_puck PRIVATE
  tinydraw::vector_v2
  tinydraw::core
  tinydraw_project_options
)
target_include_directories(tinydraw_puck BEFORE PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/shim"
  "${CMAKE_CURRENT_SOURCE_DIR}/platform"
  "${TINYDRAW_PUCK_FIRMWARE_DIR}"
  "${TINYDRAW_PUCK_APP_DIR}"
)
set(TINYDRAW_PUCK_ABI_INCLUDE_DIR "" CACHE PATH "Directory containing Puck emu_abi.h")
if(EXISTS "${TINYDRAW_PUCK_ABI_INCLUDE_DIR}/emu_abi.h")
  target_include_directories(tinydraw_puck PRIVATE "${TINYDRAW_PUCK_ABI_INCLUDE_DIR}")
  target_compile_definitions(tinydraw_puck PRIVATE TINYDRAW_PUCK_HAVE_ABI_HEADER=1)
endif()
target_compile_definitions(tinydraw_puck PRIVATE TINYDRAW_SINGLE_THREADED_TOUCH=1)
if(TINYDRAW_PUCK_DEMO)
  target_compile_definitions(tinydraw_puck PRIVATE TINYDRAW_VECTOR_V2_DEMO=1)
endif()

foreach(target IN ITEMS tinydraw_puck tinydraw_vector_v2 tinydraw_core)
  target_compile_options(${target} PRIVATE -Oz -fno-exceptions -fno-rtti)
endforeach()
target_compile_options(tinydraw_puck PRIVATE
  -ffunction-sections
  -fdata-sections
)
set_source_files_properties(
  platform/board_stubs.cpp
  platform/export_stubs.cpp
  PROPERTIES COMPILE_OPTIONS "-Wno-unused-private-field"
)
set_source_files_properties(
  "${TINYDRAW_PUCK_APP_DIR}/vector_v2_presenter.cpp"
  "${TINYDRAW_PUCK_APP_DIR}/vector_v2_presenter_staging.cpp"
  PROPERTIES COMPILE_OPTIONS "-Wno-sign-conversion"
)

set(TINYDRAW_PUCK_EXPORTS
  emu_device
  emu_init
  emu_tick
  emu_fb
  emu_push_count
  emu_push_x
  emu_push_y
  emu_push_w
  emu_push_h
  emu_touch
  emu_button
  emu_button_verdict
  emu_sensor_event
)

set(TINYDRAW_PUCK_LINK_OPTIONS
  -Oz
  -mexec-model=reactor
  -Wl,--export-memory
  -Wl,--gc-sections
  -Wl,--strip-all
  -Wl,--initial-memory=50331648
  -Wl,--max-memory=67108864
  -Wl,-z,stack-size=2097152
)
foreach(symbol IN LISTS TINYDRAW_PUCK_EXPORTS)
  list(APPEND TINYDRAW_PUCK_LINK_OPTIONS "-Wl,--export=${symbol}")
endforeach()
target_link_options(tinydraw_puck PRIVATE ${TINYDRAW_PUCK_LINK_OPTIONS})

set_target_properties(tinydraw_puck PROPERTIES
  OUTPUT_NAME emu
  SUFFIX .wasm
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/puck"
)
