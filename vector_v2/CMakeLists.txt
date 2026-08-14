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
  add_executable(tinydraw_vector_v2_tests
    tests/test_main.cpp
    tests/chrome_test.cpp
    tests/display_scheduler_test.cpp
    tests/frame_scroller_test.cpp
    tests/incremental_document_test.cpp
    tests/incremental_rasterizer_test.cpp
    tests/materialized_canvas_test.cpp
    tests/navigation_state_test.cpp
    tests/operation_builder_test.cpp
    tests/operation_lod_store_test.cpp
    tests/operation_log_test.cpp
    tests/tile_payload_analysis_test.cpp
    tests/tile_producer_test.cpp
    tests/touch_event_buffer_test.cpp
  )
  target_link_libraries(tinydraw_vector_v2_tests PRIVATE
    tinydraw::vector_v2
    tinydraw_project_options
  )
  target_include_directories(tinydraw_vector_v2_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/test_support/include
  )
  target_include_directories(tinydraw_vector_v2_tests SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/doctest
  )
  add_test(NAME tinydraw_vector_v2_tests COMMAND tinydraw_vector_v2_tests)

  add_executable(tinydraw_vector_v2_raster_census tools/raster_census.cpp)
  target_link_libraries(tinydraw_vector_v2_raster_census PRIVATE
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

  add_executable(tinydraw_vector_v2_lod_characterization tests/lod_characterization.cpp)
  target_link_libraries(tinydraw_vector_v2_lod_characterization PRIVATE
    tinydraw::vector_prototype
    tinydraw::vector_v2
    tinydraw_project_options
  )
endif()
