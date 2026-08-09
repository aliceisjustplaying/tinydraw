add_library(tinydraw_core STATIC
  src/coordinate_transform.cpp
)

add_library(tinydraw::core ALIAS tinydraw_core)

target_include_directories(tinydraw_core PUBLIC include)
target_link_libraries(tinydraw_core PRIVATE tinydraw_project_options)
