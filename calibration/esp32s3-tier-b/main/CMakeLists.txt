idf_component_register(
  SRCS "tier_b_probe.cpp" "instruction_ladders.S"
  INCLUDE_DIRS "."
  PRIV_REQUIRES esp_app_format esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_dma esp_lcd esp_mm esp_psram spi_flash
)

if(NOT "$ENV{IDF_VERSION}" STREQUAL "6.1.0")
  message(FATAL_ERROR "Tier-B capture images require ESP-IDF v6.1 exactly")
endif()

execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY "${PROJECT_DIR}"
  OUTPUT_VARIABLE TINYDRAW_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY
)
execute_process(
  COMMAND git status --porcelain --untracked-files=normal
  WORKING_DIRECTORY "${PROJECT_DIR}"
  OUTPUT_VARIABLE TINYDRAW_GIT_STATUS
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY
)
if(TINYDRAW_GIT_STATUS STREQUAL "")
  set(TINYDRAW_GIT_DIRTY 0)
else()
  set(TINYDRAW_GIT_DIRTY 1)
endif()
if(CONFIG_SPIRAM_FETCH_INSTRUCTIONS)
  set(TINYDRAW_BUILD_VARIANT "xip-psram")
else()
  set(TINYDRAW_BUILD_VARIANT "normal")
endif()
file(SHA256 "${SDKCONFIG}" TINYDRAW_SDKCONFIG_SHA256)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
  TINYDRAW_BUILD_VARIANT="${TINYDRAW_BUILD_VARIANT}"
  TINYDRAW_GIT_COMMIT="${TINYDRAW_GIT_COMMIT}"
  TINYDRAW_GIT_DIRTY=${TINYDRAW_GIT_DIRTY}
  TINYDRAW_SDKCONFIG_SHA256="${TINYDRAW_SDKCONFIG_SHA256}"
)

set_source_files_properties(tier_b_probe.cpp PROPERTIES
  COMPILE_OPTIONS "-fno-tree-vectorize;-fno-unroll-loops"
)
