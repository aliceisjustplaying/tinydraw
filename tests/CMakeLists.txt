add_executable(tinydraw_tests
  camera_test.cpp
  coordinate_transform_test.cpp
  coverage_tile_test.cpp
  demo_tape_test.cpp
  drawing_snapshot_test.cpp
  fat16_disk_test.cpp
  host_input_test.cpp
  incremental_rasterizer_test.cpp
  ink_stream_test.cpp
  materialized_canvas_test.cpp
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
  vector_benchmark_test.cpp
  vector_document_test.cpp
  viewport_renderer_test.cpp
  world_canvas_test.cpp
)
target_link_libraries(tinydraw_tests PRIVATE
  tinydraw::core
  tinydraw::production
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

add_executable(tinydraw_vector_rebuild_benchmark vector_rebuild_benchmark.cpp)
target_link_libraries(tinydraw_vector_rebuild_benchmark PRIVATE tinydraw::core tinydraw_project_options)

if(TARGET tinydraw_host)
  set(REPLAY_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/replay")
  file(MAKE_DIRECTORY "${REPLAY_OUTPUT_DIR}")

  add_test(
    NAME tinydraw_replay_e2e
    COMMAND tinydraw_host
      --replay "${PROJECT_SOURCE_DIR}/testdata/strokes/zigzag.stroke"
      --output "${REPLAY_OUTPUT_DIR}/zigzag.ppm"
  )
  set_tests_properties(tinydraw_replay_e2e PROPERTIES
    FIXTURES_SETUP replay_zigzag
    TIMEOUT 5
  )

  add_test(
    NAME tinydraw_replay_snapshot
    COMMAND ${CMAKE_COMMAND} -E compare_files
      "${REPLAY_OUTPUT_DIR}/zigzag.ppm"
      "${PROJECT_SOURCE_DIR}/testdata/snapshots/zigzag.ppm"
  )
  set_tests_properties(tinydraw_replay_snapshot PROPERTIES
    FIXTURES_REQUIRED replay_zigzag
  )

  add_test(
    NAME tinydraw_overlap_e2e
    COMMAND tinydraw_host
      --replay "${PROJECT_SOURCE_DIR}/testdata/strokes/dependency-probe.stroke"
      --output "${REPLAY_OUTPUT_DIR}/dependency-probe.ppm"
  )
  set_tests_properties(tinydraw_overlap_e2e PROPERTIES
    FIXTURES_SETUP replay_overlap
    TIMEOUT 5
  )

  add_test(
    NAME tinydraw_overlap_snapshot
    COMMAND ${CMAKE_COMMAND} -E compare_files
      "${REPLAY_OUTPUT_DIR}/dependency-probe.ppm"
      "${PROJECT_SOURCE_DIR}/testdata/snapshots/dependency-probe.ppm"
  )
  set_tests_properties(tinydraw_overlap_snapshot PROPERTIES
    FIXTURES_REQUIRED replay_overlap
  )

  add_test(
    NAME tinydraw_tight_joins_e2e
    COMMAND tinydraw_host
      --replay "${PROJECT_SOURCE_DIR}/testdata/strokes/tight-joins.stroke"
      --output "${REPLAY_OUTPUT_DIR}/tight-joins.ppm"
  )
  set_tests_properties(tinydraw_tight_joins_e2e PROPERTIES
    FIXTURES_SETUP replay_tight_joins
    TIMEOUT 5
  )

  add_test(
    NAME tinydraw_tight_joins_snapshot
    COMMAND ${CMAKE_COMMAND} -E compare_files
      "${REPLAY_OUTPUT_DIR}/tight-joins.ppm"
      "${PROJECT_SOURCE_DIR}/testdata/snapshots/tight-joins.ppm"
  )
  set_tests_properties(tinydraw_tight_joins_snapshot PROPERTIES
    FIXTURES_REQUIRED replay_tight_joins
  )

  add_test(
    NAME tinydraw_ui_preview_e2e
    COMMAND tinydraw_host
      --ui-preview
      --output "${REPLAY_OUTPUT_DIR}/ui-preview.ppm"
  )
  set_tests_properties(tinydraw_ui_preview_e2e PROPERTIES
    FIXTURES_SETUP ui_preview
    TIMEOUT 5
  )

  add_test(
    NAME tinydraw_ui_preview_snapshot
    COMMAND ${CMAKE_COMMAND} -E compare_files
      "${REPLAY_OUTPUT_DIR}/ui-preview.ppm"
      "${PROJECT_SOURCE_DIR}/testdata/snapshots/ui-preview.ppm"
  )
  set_tests_properties(tinydraw_ui_preview_snapshot PROPERTIES
    FIXTURES_REQUIRED ui_preview
  )

  add_test(
    NAME tinydraw_color_palette_e2e
    COMMAND tinydraw_host
      --color-palette-preview
      --output "${REPLAY_OUTPUT_DIR}/color-palette.ppm"
  )
  set_tests_properties(tinydraw_color_palette_e2e PROPERTIES
    FIXTURES_SETUP color_palette
    TIMEOUT 5
  )

  add_test(
    NAME tinydraw_color_palette_snapshot
    COMMAND ${CMAKE_COMMAND} -E compare_files
      "${REPLAY_OUTPUT_DIR}/color-palette.ppm"
      "${PROJECT_SOURCE_DIR}/testdata/snapshots/color-palette.ppm"
  )
  set_tests_properties(tinydraw_color_palette_snapshot PROPERTIES
    FIXTURES_REQUIRED color_palette
  )

  add_test(
    NAME tinydraw_tool_palette_e2e
    COMMAND tinydraw_host
      --tool-palette-preview
      --output "${REPLAY_OUTPUT_DIR}/tool-palette.ppm"
  )
  set_tests_properties(tinydraw_tool_palette_e2e PROPERTIES
    FIXTURES_SETUP tool_palette
    TIMEOUT 5
  )

  add_test(
    NAME tinydraw_tool_palette_snapshot
    COMMAND ${CMAKE_COMMAND} -E compare_files
      "${REPLAY_OUTPUT_DIR}/tool-palette.ppm"
      "${PROJECT_SOURCE_DIR}/testdata/snapshots/tool-palette.ppm"
  )
  set_tests_properties(tinydraw_tool_palette_snapshot PROPERTIES
    FIXTURES_REQUIRED tool_palette
  )

  add_test(
    NAME tinydraw_new_dialog_e2e
    COMMAND tinydraw_host
      --new-dialog-preview
      --output "${REPLAY_OUTPUT_DIR}/new-dialog.ppm"
  )
  set_tests_properties(tinydraw_new_dialog_e2e PROPERTIES
    FIXTURES_SETUP new_dialog
    TIMEOUT 5
  )

  add_test(
    NAME tinydraw_new_dialog_snapshot
    COMMAND ${CMAKE_COMMAND} -E compare_files
      "${REPLAY_OUTPUT_DIR}/new-dialog.ppm"
      "${PROJECT_SOURCE_DIR}/testdata/snapshots/new-dialog.ppm"
  )
  set_tests_properties(tinydraw_new_dialog_snapshot PROPERTIES
    FIXTURES_REQUIRED new_dialog
  )

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
