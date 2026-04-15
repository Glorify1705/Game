# CMake toolchain file for cross-compiling to Windows x86_64 using MinGW-w64.
#
# Usage:
#   scripts/setup-mingw-toolchain.sh   # one-time download
#   cmake -G Ninja -S . -B build-win64 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
#   ninja -C build-win64

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Locate the toolchain relative to the project root.
get_filename_component(_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_MINGW_ROOT "${_PROJECT_ROOT}/toolchains/mingw-w64")

if(NOT EXISTS "${_MINGW_ROOT}/bin/x86_64-w64-mingw32-g++")
  message(FATAL_ERROR
    "MinGW toolchain not found at ${_MINGW_ROOT}\n"
    "Run: scripts/setup-mingw-toolchain.sh")
endif()

set(CMAKE_C_COMPILER "${_MINGW_ROOT}/bin/x86_64-w64-mingw32-gcc")
set(CMAKE_CXX_COMPILER "${_MINGW_ROOT}/bin/x86_64-w64-mingw32-g++")
set(CMAKE_RC_COMPILER "${_MINGW_ROOT}/bin/x86_64-w64-mingw32-windres")

set(CMAKE_FIND_ROOT_PATH "${_MINGW_ROOT}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
