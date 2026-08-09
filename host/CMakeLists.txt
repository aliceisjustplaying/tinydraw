find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

add_executable(tinydraw_host main.cpp)
target_link_libraries(tinydraw_host PRIVATE
  tinydraw::core
  tinydraw_project_options
  PkgConfig::SDL2
)
