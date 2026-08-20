if(NOT CMAKE_CXX_COMPILER_TARGET STREQUAL "wasm32-wasip1")
  message(FATAL_ERROR "TINYDRAW_BUILD_PUCK requires CMAKE_CXX_COMPILER_TARGET=wasm32-wasip1")
endif()

add_executable(tinydraw_puck puck_abi.cpp)
target_link_libraries(tinydraw_puck PRIVATE
  tinydraw::vector_v2
  tinydraw::core
  tinydraw_project_options
)

# WebAssembly never unwinds and the application uses no runtime type queries.
# Apply the same C++ runtime contract to every static library that can reach the
# final module, so an unused exception path cannot pull a larger runtime in.
foreach(target IN ITEMS tinydraw_puck tinydraw_vector_v2 tinydraw_core tinydraw_pngenc)
  target_compile_options(${target} PRIVATE -fno-exceptions -fno-rtti)
endforeach()
# PNGenc is unused and garbage-collected, but its static target must still
# compile before the final link. Its upstream platform test predates WASI;
# select the same standard-library branch used by TinyDraw's macOS host.
target_compile_definitions(tinydraw_pngenc PRIVATE __MACH__=1)
target_compile_definitions(tinydraw_core PRIVATE __MACH__=1)
target_compile_options(tinydraw_puck PRIVATE -ffunction-sections -fdata-sections)

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
  tinydraw_owner_buffer
  tinydraw_owner_capacity
  tinydraw_owner_load
  tinydraw_diag_production_enabled
  tinydraw_diag_operation_count
  tinydraw_diag_sample_count
  tinydraw_diag_active_stroke_point_capacity
  tinydraw_diag_stroke_active
  tinydraw_diag_slot_capacity
  tinydraw_diag_resident_raw_tiles
  tinydraw_diag_visible_tiles_remaining
  tinydraw_diag_recent_view_count
  tinydraw_diag_maintenance_pending
  tinydraw_diag_last_fallback_pixels
  tinydraw_diag_last_fallback_tiles
  tinydraw_diag_current_raw
  tinydraw_diag_current_uniform
  tinydraw_diag_current_fallback
  tinydraw_diag_zoom100_raw
  tinydraw_diag_zoom100_uniform
  tinydraw_diag_zoom100_fallback
  tinydraw_diag_rerender_renders
  tinydraw_diag_rerender_unique
  tinydraw_diag_rerender_cold
  tinydraw_diag_rerender_damage
  tinydraw_diag_rerender_eviction
  tinydraw_diag_rerender_stale
  tinydraw_diag_rerender_unexplained
)

set(TINYDRAW_PUCK_LINK_OPTIONS
  -mexec-model=reactor
  -Wl,--export-memory
  -Wl,--gc-sections
  -Wl,--initial-memory=33554432
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
