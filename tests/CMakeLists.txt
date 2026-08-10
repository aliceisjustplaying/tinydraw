add_executable(tinydraw_tests
  coordinate_transform_test.cpp
  coverage_tile_test.cpp
  host_input_test.cpp
  ink_stream_test.cpp
  perfect_freehand_test.cpp
  ribbon_geometry_test.cpp
  ribbon_renderer_test.cpp
  stroke_raster_test.cpp
  tile_undo_history_test.cpp
  toolbar_test.cpp
)
target_link_libraries(tinydraw_tests PRIVATE tinydraw::core tinydraw_project_options)
target_compile_definitions(tinydraw_tests PRIVATE
  TINYDRAW_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
target_include_directories(tinydraw_tests PRIVATE ${PROJECT_SOURCE_DIR}/host)
target_include_directories(tinydraw_tests SYSTEM PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/doctest
)

add_test(NAME tinydraw_tests COMMAND tinydraw_tests)

add_executable(tinydraw_perf_characterization perf_characterization.cpp)
target_link_libraries(tinydraw_perf_characterization PRIVATE tinydraw::core tinydraw_project_options)
add_test(NAME tinydraw_perf_characterization COMMAND tinydraw_perf_characterization)

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
endif()
