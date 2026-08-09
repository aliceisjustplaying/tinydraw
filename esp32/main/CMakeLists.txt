set(TINYDRAW_CORE "${CMAKE_CURRENT_LIST_DIR}/../../core")

idf_component_register(
  SRCS
    "firmware_canvas.cpp"
    "main.cpp"
    "qemu_display.cpp"
    "${TINYDRAW_CORE}/src/coordinate_transform.cpp"
    "${TINYDRAW_CORE}/src/coverage_tile.cpp"
    "${TINYDRAW_CORE}/src/ink_stream.cpp"
    "${TINYDRAW_CORE}/src/perfect_freehand.cpp"
    "${TINYDRAW_CORE}/src/ribbon_geometry.cpp"
    "${TINYDRAW_CORE}/src/ribbon_renderer.cpp"
    "${TINYDRAW_CORE}/src/stroke_raster.cpp"
    "${TINYDRAW_CORE}/src/tile_undo_history.cpp"
    "${TINYDRAW_CORE}/src/toolbar.cpp"
  INCLUDE_DIRS "${TINYDRAW_CORE}/include"
  PRIV_REQUIRES esp_lcd_qemu_rgb esp_psram
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
if(TINYDRAW_QEMU_GRAPHICS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_QEMU_GRAPHICS=1)
endif()
