set(TINYDRAW_CORE "${CMAKE_CURRENT_LIST_DIR}/../../core")
include("${CMAKE_CURRENT_LIST_DIR}/../../vector_v2/sources.cmake")
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
if(TINYDRAW_VECTOR_V2_OVERVIEW_WALK OR TINYDRAW_VECTOR_V2_APP)
  set(TINYDRAW_VECTOR_V2_HARDWARE_SRCS "vector_v2/vector_v2_overview_walk.cpp")
  set(TINYDRAW_VECTOR_V2_CORE_SRCS "")
  if(TINYDRAW_VECTOR_V2_APP)
    set(TINYDRAW_VECTOR_V2_HARDWARE_SRCS
      "vector_v2/vector_v2_app.cpp"
      "vector_v2/vector_v2_export.cpp"
      "vector_v2/vector_v2_presenter.cpp"
      "vector_v2/vector_v2_touch_sampler.cpp"
      "image_export_store.cpp"
      "power_manager.cpp"
      "usb_export.cpp"
    )
    if(TINYDRAW_VECTOR_V2_GATE_HARNESS)
      list(APPEND TINYDRAW_VECTOR_V2_HARDWARE_SRCS
        "vector_v2/vector_v2_gate_harness.cpp"
      )
    endif()
    if(TINYDRAW_VECTOR_V2_TILE_CENSUS)
      list(APPEND TINYDRAW_VECTOR_V2_HARDWARE_SRCS
        "vector_v2/vector_v2_tile_census.cpp"
      )
    endif()
    set(TINYDRAW_VECTOR_V2_CORE_SRCS
      "${TINYDRAW_CORE}/src/coverage_tile.cpp"
      "${TINYDRAW_CORE}/src/fat16_disk.cpp"
      "${TINYDRAW_CORE}/src/ink_stream.cpp"
      "${TINYDRAW_CORE}/src/png_encoder.cpp"
      "${TINYDRAW_CORE}/src/realistic_workload.cpp"
      "${TINYDRAW_CORE}/src/ribbon_geometry.cpp"
      "${TINYDRAW_CORE}/src/ribbon_renderer.cpp"
      "${TINYDRAW_CORE}/src/vector_document.cpp"
      ${TINYDRAW_PNGENC_SRCS}
    )
  endif()
  set(TINYDRAW_APP_SRCS
    "co5300_panel_transport.cpp"
    "physical_touch.cpp"
    ${TINYDRAW_VECTOR_V2_HARDWARE_SRCS}
    ${TINYDRAW_VECTOR_V2_CORE_SRCS}
    ${TINYDRAW_VECTOR_V2_SRCS}
  )
  set(TINYDRAW_PLATFORM_SRCS "")
  set(TINYDRAW_PLATFORM_REQUIRES esp_lcd_co5300 esp_lcd_touch_cst816s)
elseif(NOT TINYDRAW_QEMU)
  list(APPEND TINYDRAW_PLATFORM_SRCS
    "co5300_panel_transport.cpp"
    "drawing_store.cpp"
    "hardware_app.cpp"
    "image_export_store.cpp"
    "physical_touch.cpp"
    "physical_display.cpp"
    "power_manager.cpp"
    "rtc_clock.cpp"
    "time_sync.cpp"
    "usb_export.cpp"
    "${TINYDRAW_CORE}/src/fat16_disk.cpp"
    "${TINYDRAW_CORE}/src/png_encoder.cpp"
    ${TINYDRAW_PNGENC_SRCS}
  )
  list(APPEND TINYDRAW_PLATFORM_REQUIRES esp_event esp_lcd_co5300 esp_lcd_touch_cst816s esp_netif esp_wifi nvs_flash pcf85063a tinyusb esp_hw_support)
  if(TINYDRAW_VECTOR_BENCHMARK OR TINYDRAW_PHASE2_PROTOTYPE OR TINYDRAW_INTERACTIVE_PAN_BENCHMARK)
    list(APPEND TINYDRAW_PLATFORM_SRCS
      "${TINYDRAW_CORE}/src/camera.cpp"
      "${TINYDRAW_CORE}/src/raster_materializer.cpp"
      "${TINYDRAW_CORE}/src/realistic_workload.cpp"
      "${TINYDRAW_CORE}/src/settled_renderer.cpp"
      "${TINYDRAW_CORE}/src/stroke_lod.cpp"
      "${TINYDRAW_CORE}/src/stroke_macrogrid.cpp"
      "${TINYDRAW_CORE}/src/vector_benchmark.cpp"
      "${TINYDRAW_CORE}/src/vector_document.cpp"
      "${TINYDRAW_CORE}/src/viewport_renderer.cpp"
    )
  endif()
  if(TINYDRAW_VECTOR_BENCHMARK)
    list(APPEND TINYDRAW_PLATFORM_SRCS "vector_benchmark_runner.cpp")
  endif()
  if(TINYDRAW_PHASE2_PROTOTYPE)
    list(APPEND TINYDRAW_PLATFORM_SRCS "phase2_prototype_runner.cpp")
  endif()
  if(TINYDRAW_RASTER_PAN_BENCHMARK)
    list(APPEND TINYDRAW_PLATFORM_SRCS "raster_pan_benchmark.cpp")
  endif()
  if(TINYDRAW_INTERACTIVE_PAN_BENCHMARK)
    list(APPEND TINYDRAW_PLATFORM_SRCS "interactive_pan_benchmark.cpp")
  endif()
  if(TINYDRAW_VECTOR_V2_MEMORY_PROBE)
    list(APPEND TINYDRAW_PLATFORM_SRCS "vector_v2/vector_v2_memory_probe.cpp")
  endif()
  if(TINYDRAW_PANEL_PROBE)
    list(APPEND TINYDRAW_PLATFORM_SRCS "panel_probe.cpp")
  endif()
endif()

idf_component_register(
  SRCS
    ${TINYDRAW_APP_SRCS}
    ${TINYDRAW_PLATFORM_SRCS}
  INCLUDE_DIRS "." "vector_v2" "${TINYDRAW_CORE}/include" "${CMAKE_CURRENT_LIST_DIR}/../../vector_v2/include"
  PRIV_INCLUDE_DIRS "${TINYDRAW_PNGENC}"
  PRIV_REQUIRES ${TINYDRAW_PLATFORM_REQUIRES} esp_partition esp_psram esp_timer espcoredump
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
set(TINYDRAW_CO5300_CLOCK_MHZ "50" CACHE STRING
    "CO5300 QSPI clock in MHz (40/50 experiments; 60 diagnostic overclock only)")
set_property(CACHE TINYDRAW_CO5300_CLOCK_MHZ PROPERTY STRINGS 40 50 60)
if(NOT TINYDRAW_CO5300_CLOCK_MHZ MATCHES "^(40|50|60)$")
  message(FATAL_ERROR "TINYDRAW_CO5300_CLOCK_MHZ must be 40, 50, or 60")
endif()
if(TINYDRAW_CO5300_CLOCK_MHZ STREQUAL "60")
  message(WARNING
          "CO5300 60 MHz is a diagnostic overclock outside the published 50 MHz envelope")
endif()
message(STATUS
        "TinyDraw panel experiment=${TINYDRAW_VECTOR_V2_PRESENTATION_EXPERIMENT}, TE edge=${TINYDRAW_VECTOR_V2_TE_EDGE}, clock=${TINYDRAW_CO5300_CLOCK_MHZ} MHz")
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  TINYDRAW_CO5300_CLOCK_MHZ=${TINYDRAW_CO5300_CLOCK_MHZ}
)
if(TINYDRAW_VECTOR_V2_PRESENTATION_EXPERIMENT STREQUAL "boundary-top-sweep")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TINYDRAW_VECTOR_V2_PRESENTATION_BOUNDARY_TOP_SWEEP=1)
else()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TINYDRAW_VECTOR_V2_PRESENTATION_BEAM_RACE_CONTROL=1)
endif()
if(TINYDRAW_VECTOR_V2_TE_EDGE STREQUAL "rising")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_V2_TE_EDGE_RISING=1)
else()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_V2_TE_EDGE_FALLING=1)
endif()
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
if(TINYDRAW_VECTOR_BENCHMARK)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_BENCHMARK=1)
endif()
if(TINYDRAW_PHASE2_PROTOTYPE)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_PHASE2_PROTOTYPE=1)
endif()
if(TINYDRAW_RASTER_PAN_BENCHMARK)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_RASTER_PAN_BENCHMARK=1)
endif()
if(TINYDRAW_INTERACTIVE_PAN_BENCHMARK)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_INTERACTIVE_PAN_BENCHMARK=1)
endif()
if(TINYDRAW_VECTOR_V2_MEMORY_PROBE)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_V2_MEMORY_PROBE=1)
endif()
if(TINYDRAW_PANEL_PROBE)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_PANEL_PROBE=1)
  set(TINYDRAW_PANEL_PROBE_CELL "0" CACHE STRING
      "Panel probe cell: 0=software suite, 1=boundary-rising-full, 2=boundary-falling-full, 3=midframe-wrap-rising, 4=freerun-unsynced, 5=boundary-rising-canvas368")
  if(NOT TINYDRAW_PANEL_PROBE_CELL MATCHES "^[0-5]$")
    message(FATAL_ERROR "TINYDRAW_PANEL_PROBE_CELL must be 0..5")
  endif()
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TINYDRAW_PANEL_PROBE_CELL=${TINYDRAW_PANEL_PROBE_CELL})
endif()
if(TINYDRAW_VECTOR_V2_OVERVIEW_WALK)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_V2_OVERVIEW_WALK=1)
endif()
if(TINYDRAW_VECTOR_V2_APP)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_V2_APP=1)
endif()
if(NOT TINYDRAW_VECTOR_V2_TILE_SLOTS STREQUAL "448")
  target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TINYDRAW_VECTOR_V2_TILE_SLOTS=${TINYDRAW_VECTOR_V2_TILE_SLOTS}
  )
endif()
if(TINYDRAW_VECTOR_V2_GATE_HARNESS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_V2_GATE_HARNESS=1)
  target_include_directories(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../../vector_v2/test_support/include"
  )
endif()
if(TINYDRAW_VECTOR_V2_TILE_CENSUS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_V2_TILE_CENSUS=1)
endif()
if(TINYDRAW_VECTOR_V2_TEARING_PROBE)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_VECTOR_V2_TEARING_PROBE=1)
endif()
if(TINYDRAW_QEMU)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_QEMU=1)
endif()
if(TINYDRAW_QEMU_GRAPHICS)
  target_compile_definitions(${COMPONENT_LIB} PRIVATE TINYDRAW_QEMU_GRAPHICS=1)
endif()
