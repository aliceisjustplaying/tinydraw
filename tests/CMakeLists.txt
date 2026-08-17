add_executable(tinydraw_tests
  camera_test.cpp
  coordinate_transform_test.cpp
  coverage_tile_test.cpp
  demo_tape_test.cpp
  drawing_snapshot_test.cpp
  fat16_disk_test.cpp
  host_input_test.cpp
  ink_stream_test.cpp
  perfect_freehand_test.cpp
  png_encoder_test.cpp
  raster_materializer_test.cpp
  realistic_workload_test.cpp
  settled_renderer_test.cpp
  stroke_lod_test.cpp
  stroke_macrogrid_test.cpp
  ribbon_geometry_test.cpp
  ribbon_renderer_test.cpp
  stroke_raster_test.cpp
  tile_undo_history_test.cpp
  toolbar_test.cpp
  usb_export_session_test.cpp
  vector_benchmark_test.cpp
  vector_document_test.cpp
  viewport_renderer_test.cpp
  world_canvas_test.cpp
)
target_link_libraries(tinydraw_tests PRIVATE
  tinydraw::vector_prototype
  tinydraw_project_options
)
target_compile_definitions(tinydraw_tests PRIVATE
  TINYDRAW_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_include_directories(tinydraw_tests PRIVATE ${PROJECT_SOURCE_DIR}/host)
target_include_directories(tinydraw_tests SYSTEM PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/doctest
)

add_test(NAME tinydraw_tests COMMAND tinydraw_tests)

add_executable(tinydraw_fat16_image fat16_image.cpp)
target_link_libraries(tinydraw_fat16_image PRIVATE tinydraw::core tinydraw_project_options)
set(FAT16_IMAGE "${CMAKE_CURRENT_BINARY_DIR}/tinydraw-fat16.img")
add_test(NAME tinydraw_fat16_generate COMMAND tinydraw_fat16_image "${FAT16_IMAGE}")
set_tests_properties(tinydraw_fat16_generate PROPERTIES FIXTURES_SETUP fat16_image)
find_program(FSCK_FAT NAMES fsck_msdos fsck.fat)
if(FSCK_FAT)
  add_test(NAME tinydraw_fat16_fsck COMMAND "${FSCK_FAT}" -n "${FAT16_IMAGE}")
  set_tests_properties(tinydraw_fat16_fsck PROPERTIES FIXTURES_REQUIRED fat16_image)
endif()

add_executable(tinydraw_perf_characterization perf_characterization.cpp)
target_link_libraries(tinydraw_perf_characterization PRIVATE tinydraw::core tinydraw_project_options)
add_test(NAME tinydraw_perf_characterization COMMAND tinydraw_perf_characterization)

if(TINYDRAW_BUILD_EXPERIMENTS)
  add_executable(tinydraw_vector_rebuild_benchmark vector_rebuild_benchmark.cpp)
  target_link_libraries(tinydraw_vector_rebuild_benchmark PRIVATE
    tinydraw::vector_prototype
    tinydraw_project_options
  )
endif()

if(TARGET tinydraw_host)
  set(REPLAY_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/replay")
  file(MAKE_DIRECTORY "${REPLAY_OUTPUT_DIR}")

  function(tinydraw_add_snapshot_test name fixture output snapshot)
    add_test(NAME ${name}_e2e COMMAND tinydraw_host ${ARGN} --output "${output}")
    set_tests_properties(${name}_e2e PROPERTIES FIXTURES_SETUP ${fixture} TIMEOUT 5)
    add_test(NAME ${name}_snapshot COMMAND ${CMAKE_COMMAND} -E compare_files
      "${output}" "${snapshot}")
    set_tests_properties(${name}_snapshot PROPERTIES FIXTURES_REQUIRED ${fixture})
  endfunction()

  tinydraw_add_snapshot_test(tinydraw_replay replay_zigzag
    "${REPLAY_OUTPUT_DIR}/zigzag.ppm"
    "${PROJECT_SOURCE_DIR}/testdata/snapshots/zigzag.ppm"
    --replay "${PROJECT_SOURCE_DIR}/testdata/strokes/zigzag.stroke")
  tinydraw_add_snapshot_test(tinydraw_overlap replay_overlap
    "${REPLAY_OUTPUT_DIR}/dependency-probe.ppm"
    "${PROJECT_SOURCE_DIR}/testdata/snapshots/dependency-probe.ppm"
    --replay "${PROJECT_SOURCE_DIR}/testdata/strokes/dependency-probe.stroke")
  tinydraw_add_snapshot_test(tinydraw_tight_joins replay_tight_joins
    "${REPLAY_OUTPUT_DIR}/tight-joins.ppm"
    "${PROJECT_SOURCE_DIR}/testdata/snapshots/tight-joins.ppm"
    --replay "${PROJECT_SOURCE_DIR}/testdata/strokes/tight-joins.stroke")
  tinydraw_add_snapshot_test(tinydraw_ui_preview ui_preview
    "${REPLAY_OUTPUT_DIR}/ui-preview.ppm"
    "${PROJECT_SOURCE_DIR}/testdata/snapshots/ui-preview.ppm" --ui-preview)
  tinydraw_add_snapshot_test(tinydraw_color_palette color_palette
    "${REPLAY_OUTPUT_DIR}/color-palette.ppm"
    "${PROJECT_SOURCE_DIR}/testdata/snapshots/color-palette.ppm" --color-palette-preview)
  tinydraw_add_snapshot_test(tinydraw_tool_palette tool_palette
    "${REPLAY_OUTPUT_DIR}/tool-palette.ppm"
    "${PROJECT_SOURCE_DIR}/testdata/snapshots/tool-palette.ppm" --tool-palette-preview)
  tinydraw_add_snapshot_test(tinydraw_new_dialog new_dialog
    "${REPLAY_OUTPUT_DIR}/new-dialog.ppm"
    "${PROJECT_SOURCE_DIR}/testdata/snapshots/new-dialog.ppm" --new-dialog-preview)

  add_test(
    NAME tinydraw_replay_rejects_invalid_lifecycle
    COMMAND tinydraw_host
      --replay "${PROJECT_SOURCE_DIR}/testdata/strokes/invalid-lifecycle.stroke"
      --output "${REPLAY_OUTPUT_DIR}/invalid-lifecycle.ppm"
  )
  set_tests_properties(tinydraw_replay_rejects_invalid_lifecycle PROPERTIES WILL_FAIL TRUE)

  add_test(
    NAME tinydraw_replay_rejects_invalid_syntax
    COMMAND tinydraw_host
      --replay "${PROJECT_SOURCE_DIR}/testdata/strokes/invalid-syntax.stroke"
      --output "${REPLAY_OUTPUT_DIR}/invalid-syntax.ppm"
  )
  set_tests_properties(tinydraw_replay_rejects_invalid_syntax PROPERTIES WILL_FAIL TRUE)

  add_test(NAME tinydraw_undo_e2e COMMAND tinydraw_host --undo-e2e)
  set_tests_properties(tinydraw_undo_e2e PROPERTIES
    PASS_REGULAR_EXPRESSION "TINYDRAW_UNDO_OK depth=10 draw=1 erase=1 new=1 exact=1"
    TIMEOUT 5
  )

  add_test(NAME tinydraw_pan_e2e COMMAND tinydraw_host --pan-e2e)
  set_tests_properties(tinydraw_pan_e2e PROPERTIES
    PASS_REGULAR_EXPRESSION "TINYDRAW_PAN_OK world=3x3 draw=1 undo_across_views=1 exact=1"
    TIMEOUT 5
  )
endif()

# Full decode round trip through the system zlib inflate: catches stream
# corruption (for example scanline-buffer overruns) that structural chunk
# checks cannot see. Compiled only where a system zlib exists.
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
  add_executable(tinydraw_png_roundtrip png_roundtrip.cpp)
  target_link_libraries(tinydraw_png_roundtrip PRIVATE
    tinydraw::core tinydraw_project_options ZLIB::ZLIB
  )
  add_test(NAME tinydraw_png_roundtrip COMMAND tinydraw_png_roundtrip)
endif()
