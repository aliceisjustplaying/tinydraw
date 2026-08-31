idf_component_register(
  SRCS "core_timing.c" "core_timing_loops.S"
  INCLUDE_DIRS "."
  PRIV_REQUIRES esp_psram spi_flash xtensa esp_hw_support
)

target_compile_options(${COMPONENT_LIB} PRIVATE
  -fno-tree-vectorize
  -fno-unroll-loops
)
