set(TINYDRAW_CORE "${CMAKE_CURRENT_LIST_DIR}/../../core")
set(TINYDRAW_PNGENC "${CMAKE_CURRENT_LIST_DIR}/../../third_party/pngenc/src")
set(TINYDRAW_PNGENC_SRCS
  "${TINYDRAW_PNGENC}/PNGenc.cpp"
  "${TINYDRAW_PNGENC}/adler32.c"
  "${TINYDRAW_PNGENC}/crc32.c"
  "${TINYDRAW_PNGENC}/deflate.c"
  "${TINYDRAW_PNGENC}/trees.c"
  "${TINYDRAW_PNGENC}/zutil.c"
)

set(TINYDRAW_PLATFORM_SRCS "qemu_display.cpp")
set(TINYDRAW_PLATFORM_REQUIRES esp_lcd_qemu_rgb)
if(NOT TINYDRAW_QEMU)
  list(APPEND TINYDRAW_PLATFORM_SRCS
    "drawing_store.cpp"
    "hardware_app.cpp"
    "image_export_store.cpp"
    "power_manager.cpp"
    "rtc_clock.cpp"
    "time_sync.cpp"
    "usb_export.cpp"
    "${TINYDRAW_CORE}/src/fat16_disk.cpp"
    "${TINYDRAW_CORE}/src/png_encoder.cpp"
    ${TINYDRAW_PNGENC_SRCS}
  )
  list(APPEND TINYDRAW_PLATFORM_REQUIRES esp_event esp_lcd_co5300 esp_lcd_touch_cst816s esp_netif esp_wifi nvs_flash pcf85063a tinyusb esp_hw_support)
  if(TINYDRAW_VECTOR_BENCHMARK OR TINYDRAW_PHASE2_PROTOTYPE)
    list(APPEND TINYDRAW_PLATFORM_SRCS
      "${TINYDRAW_CORE}/src/camera.cpp"
      "${TINYDRAW_CORE}/src/vector_benchmark.cpp"
      "${TINYDRAW_CORE}/src/viewport_renderer.cpp"
    )
  endif()
  if(TINYDRAW_VECTOR_BENCHMARK)
    list(APPEND TINYDRAW_PLATFORM_SRCS "vector_benchmark_runner.cpp")
  endif()
  if(TINYDRAW_PHASE2_PROTOTYPE)
    list(APPEND TINYDRAW_PLATFORM_SRCS "phase2_prototype_runner.cpp")
  endif()
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
    "${TINYDRAW_CORE}/src/vector_document.cpp"
    "${TINYDRAW_CORE}/src/world_canvas.cpp"
  INCLUDE_DIRS "." "${TINYDRAW_CORE}/include"
  PRIV_INCLUDE_DIRS "${TINYDRAW_PNGENC}"
  PRIV_REQUIRES ${TINYDRAW_PLATFORM_REQUIRES} esp_partition esp_psram esp_timer espcoredump
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
idf_component_get_property(TINYUSB_LIB espressif__tinyusb COMPONENT_LIB)
target_include_directories(${TINYUSB_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
if(NOT TINYDRAW_QEMU)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MEM_SHRINK=3
    PNG_MAX_BUFFERED_PIXELS=3313
  )
endif()
if(TINYDRAW_VECTOR_BENCHMARK)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_BENCHMARK=1)
endif()
if(TINYDRAW_PHASE2_PROTOTYPE)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_PHASE2_PROTOTYPE=1)
endif()
if(TINYDRAW_QEMU)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_QEMU=1)
endif()
if(TINYDRAW_QEMU_GRAPHICS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_QEMU_GRAPHICS=1)
endif()
