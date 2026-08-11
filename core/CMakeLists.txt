set(TINYDRAW_PNGENC "${CMAKE_CURRENT_LIST_DIR}/../third_party/pngenc/src")
set(TINYDRAW_PNGENC_SOURCES
  "${TINYDRAW_PNGENC}/PNGenc.cpp"
  "${TINYDRAW_PNGENC}/adler32.c"
  "${TINYDRAW_PNGENC}/crc32.c"
  "${TINYDRAW_PNGENC}/deflate.c"
  "${TINYDRAW_PNGENC}/trees.c"
  "${TINYDRAW_PNGENC}/zutil.c"
)
set_source_files_properties(${TINYDRAW_PNGENC_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(tinydraw_pngenc STATIC ${TINYDRAW_PNGENC_SOURCES})
target_include_directories(tinydraw_pngenc PUBLIC "${TINYDRAW_PNGENC}")
target_compile_definitions(tinydraw_pngenc PUBLIC
  MEM_SHRINK=3
  PNG_MAX_BUFFERED_PIXELS=3313
)
target_compile_features(tinydraw_pngenc PUBLIC cxx_std_20)
if(TINYDRAW_ENABLE_SANITIZERS)
  target_compile_options(tinydraw_pngenc PRIVATE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

add_library(tinydraw_core STATIC
  src/camera.cpp
  src/coordinate_transform.cpp
  src/coverage_tile.cpp
  src/demo_tape.cpp
  src/drawing_snapshot.cpp
  src/fat16_disk.cpp
  src/ink_stream.cpp
  src/perfect_freehand.cpp
  src/png_encoder.cpp
  src/ribbon_geometry.cpp
  src/ribbon_renderer.cpp
  src/stroke_raster.cpp
  src/tile_undo_history.cpp
  src/toolbar.cpp
  src/vector_document.cpp
  src/world_canvas.cpp
)

add_library(tinydraw::core ALIAS tinydraw_core)

target_include_directories(tinydraw_core PUBLIC include)
target_link_libraries(tinydraw_core PRIVATE tinydraw_project_options tinydraw_pngenc)
