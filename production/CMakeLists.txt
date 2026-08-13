add_library(tinydraw_production
  src/display_scheduler.cpp
  src/incremental_document.cpp
  src/incremental_rasterizer.cpp
  src/materialized_canvas.cpp
  src/operation.cpp
  src/operation_builder.cpp
  src/operation_log.cpp
)
add_library(tinydraw::production ALIAS tinydraw_production)

target_compile_features(tinydraw_production PUBLIC cxx_std_20)
target_include_directories(tinydraw_production PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
)
target_link_libraries(tinydraw_production PUBLIC tinydraw_project_options)
