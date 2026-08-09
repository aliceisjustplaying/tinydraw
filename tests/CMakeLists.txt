add_executable(tinydraw_tests coordinate_transform_test.cpp)
target_link_libraries(tinydraw_tests PRIVATE tinydraw::core tinydraw_project_options)
target_include_directories(tinydraw_tests SYSTEM PRIVATE
  ${PROJECT_SOURCE_DIR}/third_party/doctest
)

add_test(NAME tinydraw_tests COMMAND tinydraw_tests)
