cmake_minimum_required(VERSION 3.28)

project(tinydraw VERSION 0.1.0 LANGUAGES CXX)

option(TINYDRAW_BUILD_HOST "Build the SDL host application" ON)
option(TINYDRAW_BUILD_TESTS "Build native tests" ON)
option(TINYDRAW_BUILD_PUCK "Build the Puck WebAssembly adapter" OFF)
option(TINYDRAW_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TINYDRAW_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
endif()

add_library(tinydraw_project_options INTERFACE)
target_compile_options(tinydraw_project_options INTERFACE
  $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wsign-conversion>
  $<$<AND:$<BOOL:${TINYDRAW_WARNINGS_AS_ERRORS}>,$<CXX_COMPILER_ID:AppleClang,Clang>>:-Werror>
)

if(TINYDRAW_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "AppleClang|Clang")
    message(FATAL_ERROR "The sanitizer preset currently supports Clang only")
  endif()
  target_compile_options(tinydraw_project_options INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(tinydraw_project_options INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

if(TINYDRAW_BUILD_TESTS)
  include(CTest)
endif()

add_subdirectory(core)
add_subdirectory(vector_v2)

if(TINYDRAW_BUILD_PUCK)
  add_subdirectory(puck)
endif()

if(TINYDRAW_BUILD_HOST)
  add_subdirectory(host)
endif()

if(TINYDRAW_BUILD_TESTS)
  add_subdirectory(tests)
endif()
