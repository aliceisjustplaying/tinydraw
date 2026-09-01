idf_component_register(
  SRCS "core_timing.c" "core_timing_loops.S"
  INCLUDE_DIRS "."
  PRIV_REQUIRES esp_psram spi_flash xtensa esp_hw_support
)

set_source_files_properties(core_timing.c PROPERTIES
  COMPILE_OPTIONS "-fno-tree-vectorize;-fno-unroll-loops"
)
