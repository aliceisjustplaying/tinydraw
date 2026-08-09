add_library(tinydraw_core STATIC
  src/coordinate_transform.cpp
  src/coverage_tile.cpp
  src/ink_stream.cpp
  src/perfect_freehand.cpp
  src/ribbon_geometry.cpp
  src/ribbon_renderer.cpp
)

add_library(tinydraw::core ALIAS tinydraw_core)

target_include_directories(tinydraw_core PUBLIC include)
target_link_libraries(tinydraw_core PRIVATE tinydraw_project_options)
