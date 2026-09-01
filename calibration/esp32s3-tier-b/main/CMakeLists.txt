idf_component_register(
  SRCS "tier_b_probe.cpp" "instruction_ladders.S"
  INCLUDE_DIRS "."
  PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_dma esp_lcd esp_mm esp_psram spi_flash
)

set_source_files_properties(tier_b_probe.cpp PROPERTIES
  COMPILE_OPTIONS "-fno-tree-vectorize;-fno-unroll-loops"
)
