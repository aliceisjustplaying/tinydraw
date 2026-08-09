set(TINYDRAW_CORE "${CMAKE_CURRENT_LIST_DIR}/../../core")

idf_component_register(
  SRCS
    "main.cpp"
    "${TINYDRAW_CORE}/src/coordinate_transform.cpp"
    "${TINYDRAW_CORE}/src/coverage_tile.cpp"
    "${TINYDRAW_CORE}/src/ink_stream.cpp"
    "${TINYDRAW_CORE}/src/perfect_freehand.cpp"
    "${TINYDRAW_CORE}/src/ribbon_geometry.cpp"
    "${TINYDRAW_CORE}/src/ribbon_renderer.cpp"
    "${TINYDRAW_CORE}/src/stroke_raster.cpp"
    "${TINYDRAW_CORE}/src/toolbar.cpp"
  INCLUDE_DIRS "${TINYDRAW_CORE}/include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
