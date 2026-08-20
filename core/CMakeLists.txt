set(TINYDRAW_CORE_PUCK_ONLY OFF)
if(TINYDRAW_BUILD_PUCK AND NOT TINYDRAW_BUILD_HOST AND NOT TINYDRAW_BUILD_TESTS)
  set(TINYDRAW_CORE_PUCK_ONLY ON)
endif()

if(NOT TINYDRAW_CORE_PUCK_ONLY)
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
    # One filtered scanline (width * 3 RGB bytes + 1 filter byte) for the widest
    # exported world: Vector V2 at 1472 (4417); Raster V1 needs 3313. pngenc has
    # no internal bounds check, so png_encoder.cpp rejects wider requests.
    PNG_MAX_BUFFERED_PIXELS=4417
  )
  target_compile_features(tinydraw_pngenc PUBLIC cxx_std_20)
  if(TINYDRAW_ENABLE_SANITIZERS)
    target_compile_options(tinydraw_pngenc PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer)
  endif()
endif()

set(TINYDRAW_CORE_SOURCES
  src/coordinate_transform.cpp
  src/coverage_tile.cpp
  src/fat16_disk.cpp
  src/ink_stream.cpp
  src/perfect_freehand.cpp
  src/ribbon_geometry.cpp
  src/ribbon_renderer.cpp
  src/vector_document.cpp
)
if(NOT TINYDRAW_CORE_PUCK_ONLY)
  list(APPEND TINYDRAW_CORE_SOURCES
    src/demo_tape.cpp
    src/drawing_snapshot.cpp
    src/png_encoder.cpp
    src/realistic_workload.cpp
    src/stroke_raster.cpp
    src/tile_undo_history.cpp
    src/toolbar.cpp
    src/world_canvas.cpp
  )
endif()

add_library(tinydraw_core STATIC ${TINYDRAW_CORE_SOURCES})

add_library(tinydraw::core ALIAS tinydraw_core)

target_include_directories(tinydraw_core PUBLIC include)
target_link_libraries(tinydraw_core PRIVATE tinydraw_project_options)
if(NOT TINYDRAW_CORE_PUCK_ONLY)
  target_link_libraries(tinydraw_core PRIVATE tinydraw_pngenc)
endif()

# Retired vector-atlas prototype machinery remains available to its historical
# tests and benchmarks, but normal Raster V1, host, and Vector V2 targets do not
# compile or link it.
if(TINYDRAW_BUILD_TESTS)
  add_library(tinydraw_vector_prototype STATIC
    src/camera.cpp
    src/raster_materializer.cpp
    src/settled_renderer.cpp
    src/stroke_lod.cpp
    src/stroke_macrogrid.cpp
    src/vector_benchmark.cpp
    src/viewport_renderer.cpp
  )
  add_library(tinydraw::vector_prototype ALIAS tinydraw_vector_prototype)
  target_include_directories(tinydraw_vector_prototype PUBLIC include)
  target_link_libraries(tinydraw_vector_prototype PUBLIC
    tinydraw::core
    tinydraw_project_options
  )
endif()
