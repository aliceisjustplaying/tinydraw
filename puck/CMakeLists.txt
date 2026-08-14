if(NOT CMAKE_CXX_COMPILER_TARGET STREQUAL "wasm32-wasip1")
  message(FATAL_ERROR "TINYDRAW_BUILD_PUCK requires CMAKE_CXX_COMPILER_TARGET=wasm32-wasip1")
endif()

add_executable(tinydraw_puck puck_abi.cpp)
target_link_libraries(tinydraw_puck PRIVATE tinydraw::core tinydraw_project_options)
foreach(target IN ITEMS tinydraw_puck tinydraw_core tinydraw_pngenc)
  target_compile_options(${target} PRIVATE -fno-exceptions -fno-rtti)
endforeach()
target_compile_options(tinydraw_puck PRIVATE
  -ffunction-sections
  -fdata-sections
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
  -mexec-model=reactor
  -Wl,--export-memory
  -Wl,--gc-sections
  -Wl,--initial-memory=16777216
  -Wl,--max-memory=67108864
  -Wl,-z,stack-size=1048576
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
