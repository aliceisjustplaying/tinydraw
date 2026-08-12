add_library(tinydraw_production INTERFACE)
add_library(tinydraw::production ALIAS tinydraw_production)

target_compile_features(tinydraw_production INTERFACE cxx_std_20)
target_include_directories(tinydraw_production INTERFACE
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
)
target_link_libraries(tinydraw_production INTERFACE tinydraw_project_options)
