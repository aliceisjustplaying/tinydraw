set(TINYDRAW_CORE "${CMAKE_CURRENT_LIST_DIR}/../../core")
include("${CMAKE_CURRENT_LIST_DIR}/../../vector_v2/sources.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/vector_v2/sources.cmake")
set(TINYDRAW_PNGENC "${CMAKE_CURRENT_LIST_DIR}/../../third_party/pngenc/src")
set(TINYDRAW_PNGENC_SRCS
  "${TINYDRAW_PNGENC}/PNGenc.cpp"
  "${TINYDRAW_PNGENC}/adler32.c"
  "${TINYDRAW_PNGENC}/crc32.c"
  "${TINYDRAW_PNGENC}/deflate.c"
  "${TINYDRAW_PNGENC}/trees.c"
  "${TINYDRAW_PNGENC}/zutil.c"
)

set(TINYDRAW_APP_SRCS
  "firmware_canvas.cpp"
  "main.cpp"
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
)
if(TINYDRAW_QEMU)
  set(TINYDRAW_PLATFORM_SRCS "qemu_display.cpp")
  set(TINYDRAW_PLATFORM_REQUIRES esp_lcd_qemu_rgb)
elseif(TINYDRAW_PANEL_PROBE)
  set(TINYDRAW_APP_SRCS
    "main.cpp"
    "co5300_panel_transport.cpp"
    "physical_touch.cpp"
    "panel_probe.cpp"
    "${TINYDRAW_CORE}/src/coverage_tile.cpp"
  )
  set(TINYDRAW_PLATFORM_SRCS "")
  set(TINYDRAW_PLATFORM_REQUIRES esp_lcd_co5300 esp_lcd_touch_cst816s)
elseif(TINYDRAW_RASTER_V1)
  list(APPEND TINYDRAW_APP_SRCS
    "co5300_panel_transport.cpp"
    "drawing_store.cpp"
    "hardware_app.cpp"
    "image_export_store.cpp"
    "physical_display.cpp"
    "physical_touch.cpp"
    "power_manager.cpp"
    "rtc_clock.cpp"
    "time_sync.cpp"
    "usb_export.cpp"
    "${TINYDRAW_CORE}/src/fat16_disk.cpp"
    "${TINYDRAW_CORE}/src/png_encoder.cpp"
    ${TINYDRAW_PNGENC_SRCS}
  )
  set(TINYDRAW_PLATFORM_SRCS "")
  set(TINYDRAW_PLATFORM_REQUIRES
      esp_event esp_hw_support esp_lcd_co5300 esp_lcd_touch_cst816s esp_netif esp_wifi nvs_flash pcf85063a tinyusb)
else()
  set(TINYDRAW_APP_SRCS
    "co5300_panel_transport.cpp"
    "physical_touch.cpp"
    "power_manager.cpp"
    "rtc_clock.cpp"
    "time_sync.cpp"
    "usb_export.cpp"
    "vector_v2/svg_export_store.cpp"
    "vector_v2/vector_v2_autosave_store.cpp"
    "vector_v2/vector_v2_export.cpp"
    ${TINYDRAW_VECTOR_V2_FIRMWARE_SRCS}
    "${TINYDRAW_CORE}/src/coverage_tile.cpp"
    "${TINYDRAW_CORE}/src/fat16_disk.cpp"
    "${TINYDRAW_CORE}/src/ink_stream.cpp"
    "${TINYDRAW_CORE}/src/png_encoder.cpp"
    "${TINYDRAW_CORE}/src/ribbon_geometry.cpp"
    "${TINYDRAW_CORE}/src/ribbon_renderer.cpp"
    "${TINYDRAW_CORE}/src/vector_document.cpp"
    ${TINYDRAW_PNGENC_SRCS}
    ${TINYDRAW_VECTOR_V2_SRCS}
  )
  if(TINYDRAW_VECTOR_V2_GATE_HARNESS)
    list(APPEND TINYDRAW_APP_SRCS
      "vector_v2/vector_v2_gate_harness.cpp"
      "vector_v2/vector_v2_gate_harness_history.cpp"
      "vector_v2/vector_v2_gate_harness_ink.cpp"
      "vector_v2/vector_v2_gate_harness_mixed.cpp"
      "vector_v2/vector_v2_gate_harness_owner.cpp"
      "vector_v2/vector_v2_gate_harness_pan.cpp"
      "vector_v2/vector_v2_gate_harness_render.cpp"
      "vector_v2/vector_v2_gate_harness_stress.cpp"
      "vector_v2/vector_v2_gate_harness_trace.cpp"
      "${TINYDRAW_CORE}/src/realistic_workload.cpp"
      ${TINYDRAW_VECTOR_V2_DIAGNOSTIC_SRCS}
    )
  endif()
  if(TINYDRAW_VECTOR_V2_TILE_CENSUS)
    list(APPEND TINYDRAW_APP_SRCS "vector_v2/vector_v2_tile_census.cpp")
  endif()
  if(TINYDRAW_VECTOR_V2_DEMO)
    list(APPEND TINYDRAW_APP_SRCS ${TINYDRAW_VECTOR_V2_DEMO_SRCS})
  endif()
  set(TINYDRAW_PLATFORM_SRCS "")
  set(TINYDRAW_PLATFORM_REQUIRES esp_event esp_lcd_co5300 esp_lcd_touch_cst816s esp_netif esp_wifi nvs_flash pcf85063a)
endif()

idf_component_register(
  SRCS
    ${TINYDRAW_APP_SRCS}
    ${TINYDRAW_PLATFORM_SRCS}
  INCLUDE_DIRS "." "vector_v2" "${TINYDRAW_CORE}/include" "${CMAKE_CURRENT_LIST_DIR}/../../vector_v2/include"
  PRIV_INCLUDE_DIRS "${TINYDRAW_PNGENC}"
  PRIV_REQUIRES ${TINYDRAW_PLATFORM_REQUIRES} esp_partition esp_psram esp_timer espcoredump
  LDFRAGMENTS "linker.lf"
)

# ESP-IDF disables builtin memcpy component-wide. These three translation
# units make frequent fixed 20-36 byte value copies; expanding only those
# copies removes ROM call overhead without bloating the IRAM raster loops.
set_source_files_properties(
  "${CMAKE_CURRENT_LIST_DIR}/../../vector_v2/src/materialized_canvas_composition.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/../../vector_v2/src/operation_log.cpp"
  PROPERTIES COMPILE_OPTIONS "-finline-stringops=memcpy"
)
set_source_files_properties(
  "${CMAKE_CURRENT_LIST_DIR}/../../vector_v2/src/incremental_document_replay.cpp"
  PROPERTIES COMPILE_OPTIONS "-finline-stringops=memcpy"
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
idf_component_get_property(TINYUSB_LIB espressif__tinyusb COMPONENT_LIB)
target_include_directories(${TINYUSB_LIB} PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
if(NOT TINYDRAW_QEMU)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MEM_SHRINK=3
    # Widest exported scanline: Vector V2 world (1472 * 3 + 1). Raster V1
    # needs 3313; png_encoder.cpp rejects anything wider than this buffer.
    PNG_MAX_BUFFERED_PIXELS=4417
  )
endif()
if(TINYDRAW_PANEL_PROBE)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_PANEL_PROBE=1)
  set(TINYDRAW_PANEL_PROBE_CELL "0" CACHE STRING
      "Panel probe cell: 0=software suite, 1=boundary-rising-full, 2=boundary-falling-full, 3=midframe-wrap-rising, 4=freerun-unsynced, 5=boundary-rising-canvas368, 6=cycle all optical cells")
  if(NOT TINYDRAW_PANEL_PROBE_CELL MATCHES "^[0-6]$")
    message(FATAL_ERROR "TINYDRAW_PANEL_PROBE_CELL must be 0..6")
  endif()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TINYDRAW_PANEL_PROBE_CELL=${TINYDRAW_PANEL_PROBE_CELL})
endif()
if(TINYDRAW_RASTER_V1)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_RASTER_V1=1)
endif()
if(NOT TINYDRAW_VECTOR_V2_TILE_SLOTS STREQUAL "604")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TINYDRAW_VECTOR_V2_TILE_SLOTS=${TINYDRAW_VECTOR_V2_TILE_SLOTS}
  )
endif()
if(TINYDRAW_VECTOR_V2_GATE_HARNESS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TINYDRAW_VECTOR_V2_GATE_HARNESS=1
    TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS=1
  )
  target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../vector_v2/test_support/include"
  )
  # Recorded owner ink traces for the replay gate. under-overlay stays out
  # (streamed delivery, ~190 KiB); the rest total ~95 KiB of app flash.
  foreach(TINYDRAW_INK_TRACE
      fast-curve-dense-25 fast-curve-400 fast-curve-400-xl slow-precise-100
      scribble-multistroke)
    target_add_binary_data(${COMPONENT_LIB}
      "${CMAKE_CURRENT_LIST_DIR}/../../testdata/ink-traces/${TINYDRAW_INK_TRACE}.csv" TEXT)
  endforeach()
  # Compact authority captured from the owner's real release-stress document.
  target_add_binary_data(${COMPONENT_LIB}
    "${CMAKE_CURRENT_LIST_DIR}/../../testdata/documents/captured-drawing-2026-08-19.tdoc" BINARY)
endif()
if(TINYDRAW_VECTOR_V2_TILE_CENSUS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_V2_TILE_CENSUS=1)
endif()
if(TINYDRAW_VECTOR_V2_TEARING_PROBE)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_V2_TEARING_PROBE=1)
endif()
if(TINYDRAW_VECTOR_V2_DEMO)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_V2_DEMO=1)
endif()
if(TINYDRAW_QEMU)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_QEMU=1)
endif()
if(TINYDRAW_QEMU_GRAPHICS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_QEMU_GRAPHICS=1)
endif()
