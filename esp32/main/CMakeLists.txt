set(TINYDRAW_CORE "${CMAKE_CURRENT_LIST_DIR}/../../core")

set(TINYDRAW_PLATFORM_SRCS "qemu_display.cpp")
set(TINYDRAW_PLATFORM_REQUIRES esp_lcd_qemu_rgb)
if(NOT TINYDRAW_QEMU)
  list(APPEND TINYDRAW_PLATFORM_SRCS "drawing_store.cpp" "hardware_app.cpp" "power_manager.cpp")
  list(APPEND TINYDRAW_PLATFORM_REQUIRES esp_lcd_co5300 esp_lcd_touch_cst816s)
endif()

idf_component_register(
  SRCS
    "firmware_canvas.cpp"
    "main.cpp"
    ${TINYDRAW_PLATFORM_SRCS}
    "${TINYDRAW_CORE}/src/coordinate_transform.cpp"
    "${TINYDRAW_CORE}/src/coverage_tile.cpp"
    "${TINYDRAW_CORE}/src/demo_tape.cpp"
    "${TINYDRAW_CORE}/src/drawing_snapshot.cpp"
    "${TINYDRAW_CORE}/src/ink_stream.cpp"
    "${TINYDRAW_CORE}/src/perfect_freehand.cpp"
    "${TINYDRAW_CORE}/src/ribbon_geometry.cpp"
    "${TINYDRAW_CORE}/src/ribbon_renderer.cpp"
    "${TINYDRAW_CORE}/src/stroke_raster.cpp"
    "${TINYDRAW_CORE}/src/tile_undo_history.cpp"
    "${TINYDRAW_CORE}/src/toolbar.cpp"
    "${TINYDRAW_CORE}/src/world_canvas.cpp"
  INCLUDE_DIRS "${TINYDRAW_CORE}/include"
  PRIV_REQUIRES ${TINYDRAW_PLATFORM_REQUIRES} esp_partition esp_psram esp_timer
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
if(TINYDRAW_QEMU)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_QEMU=1)
endif()
if(TINYDRAW_QEMU_GRAPHICS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_QEMU_GRAPHICS=1)
endif()
