# CMake toolchain file for cross-compiling to macOS x86_64 (Intel)
# using osxcross.
#
# Usage:
#   scripts/setup-osxcross-toolchain.sh   # one-time setup
#   cmake -G Ninja -S . -B build-macos \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/macos-x86_64.cmake
#   ninja -C build-macos

set(CMAKE_SYSTEM_NAME Darwin)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Locate the toolchain relative to the project root.
get_filename_component(_PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_OSXCROSS_ROOT "${_PROJECT_ROOT}/toolchains/osxcross")

# Find the SDK directory (accept any version).
file(GLOB _SDK_DIRS "${_OSXCROSS_ROOT}/SDK/MacOSX*.sdk")
list(LENGTH _SDK_DIRS _SDK_COUNT)
if(_SDK_COUNT EQUAL 0)
  message(FATAL_ERROR
    "osxcross SDK not found at ${_OSXCROSS_ROOT}/SDK/\n"
    "Run: scripts/setup-osxcross-toolchain.sh")
endif()
list(GET _SDK_DIRS 0 _SDK_DIR)

# Find the target triple (e.g. x86_64-apple-darwinXX).
file(GLOB _CLANGXX "${_OSXCROSS_ROOT}/bin/x86_64-apple-darwin*-clang++")
list(LENGTH _CLANGXX _CLANG_COUNT)
if(_CLANG_COUNT EQUAL 0)
  message(FATAL_ERROR
    "osxcross x86_64 clang++ not found at ${_OSXCROSS_ROOT}/bin/\n"
    "Run: scripts/setup-osxcross-toolchain.sh")
endif()
list(GET _CLANGXX 0 _CXX_COMPILER)
string(REPLACE "clang++" "clang" _C_COMPILER "${_CXX_COMPILER}")

set(CMAKE_C_COMPILER "${_C_COMPILER}")
set(CMAKE_CXX_COMPILER "${_CXX_COMPILER}")

set(CMAKE_OSX_SYSROOT "${_SDK_DIR}")
set(CMAKE_FIND_ROOT_PATH "${_SDK_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
