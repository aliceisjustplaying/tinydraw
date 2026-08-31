idf_component_register(
  SRCS "memory_timing.c"
  INCLUDE_DIRS "."
  PRIV_REQUIRES esp_psram spi_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE
  -fno-tree-vectorize
  -fno-unroll-loops
)
