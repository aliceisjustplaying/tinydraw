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

  add_executable(tinydraw_vector_v2_lod_characterization tests/lod_characterization.cpp)
  target_link_libraries(tinydraw_vector_v2_lod_characterization PRIVATE
    tinydraw::vector_prototype
    tinydraw::vector_v2
    tinydraw_project_options
  )
endif()
