include(sources.cmake)

add_library(tinydraw_production
  ${TINYDRAW_PRODUCTION_SRCS}
  src/tile_payload_analysis.cpp
)
add_library(tinydraw::production ALIAS tinydraw_production)

target_compile_features(tinydraw_production PUBLIC cxx_std_20)
target_include_directories(tinydraw_production PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>"
)
target_link_libraries(tinydraw_production PUBLIC tinydraw_project_options)
