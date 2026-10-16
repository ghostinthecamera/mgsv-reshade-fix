cmake_minimum_required(VERSION 3.20)
project(mgsv_reshade_fix VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "MGSV is 64-bit only; configure with -A x64.")
endif()

configure_file(src/version.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)

add_library(mgsv_reshade_fix SHARED
  src/mgsv_reshade_fix.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/version.rc)

target_include_directories(mgsv_reshade_fix PRIVATE third_party/reshade/include)
target_compile_features(mgsv_reshade_fix PRIVATE cxx_std_17)
target_link_libraries(mgsv_reshade_fix PRIVATE psapi)

if(MSVC)
  target_compile_options(mgsv_reshade_fix PRIVATE /W4 /permissive-)
endif()

# ReShade loads add-ons by the .addon64 extension. Static CRT so users need no VC++ redistributable.
set_target_properties(mgsv_reshade_fix PROPERTIES
  PREFIX ""
  SUFFIX ".addon64"
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
