include(sources.cmake)

add_library(tinydraw_vector_v2
  ${TINYDRAW_VECTOR_V2_SRCS}
)
add_library(tinydraw::vector_v2 ALIAS tinydraw_vector_v2)

target_compile_features(tinydraw_vector_v2 PUBLIC cxx_std_20)
target_include_directories(tinydraw_vector_v2 PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
)
target_link_libraries(tinydraw_vector_v2
  PRIVATE tinydraw::core
  PUBLIC tinydraw_project_options
)

option(TINYDRAW_VECTOR_V2_RASTER_CENSUS "Compile raster census counters into vector_v2" OFF)
if(TINYDRAW_VECTOR_V2_RASTER_CENSUS)
  target_compile_definitions(tinydraw_vector_v2 PUBLIC TINYDRAW_VECTOR_V2_RASTER_CENSUS=1)
endif()

if(TINYDRAW_BUILD_TESTS)
  target_sources(tinydraw_vector_v2 PRIVATE
    ${TINYDRAW_VECTOR_V2_DIAGNOSTIC_SRCS}
  )
  target_compile_definitions(tinydraw_vector_v2 PUBLIC
    TINYDRAW_VECTOR_V2_RERENDER_DIAGNOSTICS=1
  )

  add_executable(tinydraw_ink_trace_check tools/ink_trace_check.cpp)
  target_link_libraries(tinydraw_ink_trace_check PRIVATE
    tinydraw::vector_v2
    tinydraw_project_options
  )

  add_executable(tinydraw_vector_v2_idle_repair_benchmark
    tools/idle_repair_benchmark.cpp
  )
  target_link_libraries(tinydraw_vector_v2_idle_repair_benchmark PRIVATE
    tinydraw::vector_v2
    tinydraw_project_options
  )

  add_executable(tinydraw_vector_v2_settled_aa_benchmark
    tools/settled_aa_benchmark.cpp
  )
  target_link_libraries(tinydraw_vector_v2_settled_aa_benchmark PRIVATE
    tinydraw::vector_v2
    tinydraw_project_options
  )

  add_executable(tinydraw_vector_v2_history_benchmark
    tools/history_benchmark.cpp
  )
  target_link_libraries(tinydraw_vector_v2_history_benchmark PRIVATE
    tinydraw::vector_v2
    tinydraw_project_options
  )

  add_executable(tinydraw_vector_v2_cache_benchmark
    tools/materialized_canvas_cache_benchmark.cpp
  )
  target_link_libraries(tinydraw_vector_v2_cache_benchmark PRIVATE
    tinydraw::vector_v2
    tinydraw_project_options
  )

  add_executable(tinydraw_vector_v2_occupancy_history_benchmark
    tools/occupancy_history_benchmark.cpp
  )
  target_link_libraries(tinydraw_vector_v2_occupancy_history_benchmark PRIVATE
    tinydraw::vector_v2
    tinydraw_project_options
  )

  function(tinydraw_add_vector_v2_tests target)
    add_executable(${target} tests/test_main.cpp ${ARGN})
    target_link_libraries(${target} PRIVATE
      tinydraw::vector_v2
      tinydraw::core
      tinydraw_project_options
    )
    target_include_directories(${target} PRIVATE
      ${CMAKE_CURRENT_LIST_DIR}/test_support/include
    )
    target_include_directories(${target} SYSTEM PRIVATE
      ${PROJECT_SOURCE_DIR}/third_party/doctest
    )
    add_test(NAME ${target} COMMAND ${target})
  endfunction()

  tinydraw_add_vector_v2_tests(tinydraw_vector_v2_authority_tests
    tests/authority_journal_test.cpp
    tests/chained_operation_builder_test.cpp
    tests/incremental_document_test.cpp
    tests/operation_builder_test.cpp
    tests/operation_log_test.cpp
    tests/operation_spatial_index_test.cpp
    tests/svg_export_test.cpp
    tests/world_export_test.cpp
  )
  tinydraw_add_vector_v2_tests(tinydraw_vector_v2_interaction_tests
    tests/chrome_test.cpp
    tests/frame_scroller_test.cpp
    tests/ink_trace_test.cpp
    tests/live_ink_coordinator_test.cpp
    tests/navigation_state_test.cpp
    tests/panel_staging_test.cpp
    tests/touch_event_buffer_test.cpp
  )
  tinydraw_add_vector_v2_tests(tinydraw_vector_v2_rendering_tests
    tests/idle_repair_test.cpp
    tests/incremental_rasterizer_test.cpp
    tests/materialized_canvas_cache_test.cpp
    tests/materialized_canvas_composition_test.cpp
    tests/materialized_canvas_in_place_test.cpp
    tests/materialized_canvas_layout_test.cpp
    tests/materialized_canvas_occupancy_test.cpp
    tests/materialized_canvas_revision_test.cpp
    tests/settled_tile_test.cpp
    tests/rerender_ledger_test.cpp
    tests/tile_payload_analysis_test.cpp
    tests/tile_producer_test.cpp
  )
  add_test(NAME tinydraw_ink_trace_check_recorded
           COMMAND tinydraw_ink_trace_check
                   "${PROJECT_SOURCE_DIR}/testdata/ink-traces/scribble-multistroke.csv")
  set_tests_properties(tinydraw_ink_trace_check_recorded PROPERTIES
    PASS_REGULAR_EXPRESSION
      "valid name=scribble-multistroke source=recorded events=848 strokes=7")

  add_executable(tinydraw_vector_v2_raster_census tools/raster_census.cpp)
  target_link_libraries(tinydraw_vector_v2_raster_census PRIVATE
    tinydraw::core
    tinydraw::vector_v2
    tinydraw_project_options
  )
  target_include_directories(tinydraw_vector_v2_raster_census PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/test_support/include
  )
  add_test(NAME tinydraw_vector_v2_cold_replay_exactness
           COMMAND tinydraw_vector_v2_raster_census)
  add_test(NAME tinydraw_vector_v2_fuzz_collinear_coalesce
           COMMAND tinydraw_vector_v2_raster_census --fuzz-collinear 800 3405691582)
  # Seed 987654321 case 3965 caught the coalesced-capsule float-exactness bug;
  # keep the full range as a regression gate.
  add_test(NAME tinydraw_vector_v2_fuzz_collinear_regression
           COMMAND tinydraw_vector_v2_raster_census --fuzz-collinear 4000 987654321)
  add_test(NAME tinydraw_vector_v2_fuzz_random_documents
           COMMAND tinydraw_vector_v2_raster_census --fuzz-docs 150 48879)

endif()
