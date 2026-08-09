add_executable(tinydraw_tests
  coordinate_transform_test.cpp
  ink_stream_test.cpp
)
target_link_libraries(tinydraw_tests PRIVATE tinydraw::core tinydraw_project_options)
target_include_directories(tinydraw_tests SYSTEM PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/doctest
)

add_test(NAME tinydraw_tests COMMAND tinydraw_tests)

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
endif()
