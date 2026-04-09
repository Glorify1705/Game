cmake_minimum_required(VERSION 3.21...3.30)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS True)

option(ENABLE_SANITIZERS "Build with ASan+UBSan" OFF)
option(ENABLE_CLANG_TIDY "Run clang-tidy during build" OFF)
option(ENABLE_PROFILING "Enable trace profiler instrumentation" OFF)

project(
  Game
  VERSION 0.1
  DESCRIPTION "The game"
  LANGUAGES C CXX)


if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    # Nix wraps clang with implicit -isystem paths that CMake detects and strips
    # from compile commands. clang-tidy doesn't use the Nix wrapper, so re-add them.
    set(CLANG_TIDY_CMD "${CLANG_TIDY_EXE}")
    foreach(dir IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
        list(APPEND CLANG_TIDY_CMD "--extra-arg=-isystem${dir}")
    endforeach()
endif()

set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/box2d EXCLUDE_FROM_ALL)

add_subdirectory(libraries/lua EXCLUDE_FROM_ALL)
add_subdirectory(libraries/physfs EXCLUDE_FROM_ALL)

set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(libraries/mimalloc EXCLUDE_FROM_ALL)

set(BUILD_REGRESS OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_DOC OFF)
set(BUILD_TOOLS OFF)
set(PHYSFS_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(PHYSFS_BUILD_DOCS OFF CACHE BOOL "" FORCE)

find_package(OpenGL REQUIRED)
find_package(SDL3 REQUIRED CONFIG)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_library(LIBDW_LIBRARY dw)
  find_path(LIBDW_INCLUDE_DIR NAMES "elfutils/libdw.h")
  if(LIBDW_LIBRARY AND LIBDW_INCLUDE_DIR)
    set(LIBDW_FOUND TRUE)
    message(STATUS "Found libdw: ${LIBDW_LIBRARY}")
  else()
    set(LIBDW_FOUND FALSE)
    message(STATUS "libdw not found, stack traces will show addresses only")
  endif()
endif()

# Write Nix-implicit include dirs for standalone tools (clang-include-cleaner).
# CMake strips these from compile_commands.json, so tools need them explicitly.
string(REPLACE ";" "\n" _implicit_dirs "${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES}")
file(WRITE "${CMAKE_BINARY_DIR}/implicit_include_dirs.txt" "${_implicit_dirs}\n")

find_path(VALGRIND_INCLUDE_DIR valgrind/memcheck.h)

add_library(double-conversion STATIC
    libraries/double-conversion/bignum.cc
    libraries/double-conversion/bignum-dtoa.cc
    libraries/double-conversion/cached-powers.cc
    libraries/double-conversion/double-to-string.cc
    libraries/double-conversion/fast-dtoa.cc
    libraries/double-conversion/fixed-dtoa.cc
    libraries/double-conversion/string-to-double.cc
    libraries/double-conversion/strtod.cc
)
target_include_directories(double-conversion PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

set(VENDORED_SRCS
    "libraries/dr_wav.cc"
    "libraries/glad.cc"

    "libraries/sqlite3.c"
    "libraries/stb_rect_pack.cc"
    "libraries/stb_truetype.cc"
    "libraries/stb_image_write.cc"
    "libraries/stb_vorbis.cc"
    "libraries/yyjson.c"
    "libraries/backward.cc")

set(ENGINE_SRCS
    "src/assets.cc"
    "src/camera.cc"
    "src/clock.cc"
    "src/collision.cc"
    "src/collision_world.cc"
    "src/color.cc"
    "src/config.cc"
    "src/console.cc"
    "src/file_watcher.cc"
    "src/filesystem.cc"
    "src/game.cc"
    "src/hot_reload.cc"
    "src/image.cc"
    "src/input.cc"
    "src/logging.cc"
    "src/lua.cc"
    "src/lua_assets.cc"
    "src/lua_bytebuffer.cc"
    "src/lua_camera.cc"
    "src/lua_collision.cc"
    "src/lua_filesystem.cc"
    "src/lua_graphics.cc"
    "src/lua_input.cc"
    "src/lua_physics.cc"
    "src/lua_log.cc"
    "src/lua_random.cc"
    "src/lua_sound.cc"
    "src/lua_system.cc"
    "src/lua_test.cc"
    "src/lua_timer.cc"
    "src/lua_math.cc"
    "src/physics.cc"
    "src/profiler.cc"
    "src/qoa.cc"
    "src/renderer.cc"
    "src/sdl_init.cc"
    "src/shaders.cc"
    "src/sound.cc"
    "src/stats.cc"
    "src/string_table.cc"
    "src/stringlib.cc"
    "src/timer.cc"
    "src/executor.cc"
    "src/transformations.cc"
    "src/xml.cc"
    "src/packer.cc"
    "src/cmd_clean.cc"
    "src/cmd_help.cc"
    "src/cmd_init.cc"
    "src/cmd_package.cc"
    "src/cmd_run.cc"
    "src/cmd_stubs.cc"
    "src/cmd_version.cc"
    "src/platform.cc")

set_source_files_properties(${VENDORED_SRCS} PROPERTIES SKIP_LINTING ON)
set_source_files_properties("libraries/stb_image_write.cc" PROPERTIES
    COMPILE_OPTIONS "-Wno-missing-field-initializers")
set(GAME_SRCS ${VENDORED_SRCS} ${ENGINE_SRCS})

configure_file(src/version.h.in version.h)

add_executable(Game "${GAME_SRCS}")
set_target_properties(Game PROPERTIES OUTPUT_NAME "game")

if(ENABLE_CLANG_TIDY)
    set_target_properties(Game PROPERTIES CXX_CLANG_TIDY "${CLANG_TIDY_CMD}")
endif()

target_compile_features(Game PRIVATE cxx_std_17)

target_include_directories(
  Game PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}"
               "${PROJECT_SOURCE_DIR}/libraries/lua/src")

if(VALGRIND_INCLUDE_DIR)
    target_include_directories(Game SYSTEM PRIVATE "${VALGRIND_INCLUDE_DIR}")
endif()

set(IS_GCC_LIKE
    $<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>
)
set(GCC_AND_CLANG_FLAGS
    -Wall
    -Wextra
    -Werror
    -O1
    -ggdb
    -DNDEBUG
    -DGAME_WITH_ASSERTS)
set(MSVC_FLAGS /W4 /WX /DGAME_WITH_ASSERTS /O2 /DEBUG /NDEBUG)

target_compile_options(Game PRIVATE
    $<${IS_GCC_LIKE}:-Wall;-Wextra;-Werror;-Wno-unused-parameter>
    $<$<AND:${IS_GCC_LIKE},$<CONFIG:Debug>>:-O1;-fno-omit-frame-pointer;-fno-optimize-sibling-calls;-fno-inline>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
)

# Engine code compiles with no exceptions, no RTTI, and extra warnings.
# Vendored libraries are exempt from engine compile flags.
set_source_files_properties(${ENGINE_SRCS} PROPERTIES
    COMPILE_OPTIONS "$<${IS_GCC_LIKE}:-fno-exceptions;-fno-rtti;-Wshadow;-Wformat=2;-Wimplicit-fallthrough>"
)

if(ENABLE_SANITIZERS)
  target_compile_options(Game PRIVATE
      $<${IS_GCC_LIKE}:-fsanitize=address,undefined;-fno-sanitize=vptr>)
  target_link_options(Game PRIVATE
      $<${IS_GCC_LIKE}:-fsanitize=address,undefined>)
endif()

target_compile_definitions(Game PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:GAME_WITH_ASSERTS>
)

if(LIBDW_FOUND)
  target_compile_definitions(Game PRIVATE BACKWARD_HAS_DW=1)
  # Force the include path even if CMake considers it "implicit" (Nix strips
  # implicit -isystem flags from compile commands, but backward.h needs dwarf.h).
  target_compile_options(Game PRIVATE "SHELL:-isystem ${LIBDW_INCLUDE_DIR}")
endif()

if(ENABLE_PROFILING)
    target_compile_definitions(Game PRIVATE GAME_WITH_PROFILING)
endif()

set_source_files_properties(libraries/sqlite3.c PROPERTIES
    COMPILE_DEFINITIONS SQLITE_ENABLE_MEMSYS5)

target_link_libraries(
  Game
  PRIVATE SDL3::SDL3
          physfs-static
          box2d
          lua
          mimalloc-static
          double-conversion
          OpenGL::GL
          $<$<BOOL:${LIBDW_FOUND}>:${LIBDW_LIBRARY}>)

if(WIN32)
  add_custom_command(
    TARGET Game
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE_DIR:SDL3::SDL3>/../bin/SDL3.dll" "$<TARGET_FILE_DIR:Game>"
    VERBATIM)
endif()

enable_testing()

set(TEST_SRCS "src/stringlib.cc" "src/stats.cc" "src/string_table.cc"
              "src/logging.cc" "src/collision.cc" "src/collision_world.cc"
              "src/executor.cc"
              "tests/test.cc")

add_executable(Tests "${TEST_SRCS}")

target_compile_features(Tests PRIVATE cxx_std_17)

target_compile_options(Tests PRIVATE
    $<${IS_GCC_LIKE}:-Wall;-Wextra;-Werror;-Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
    $<${IS_GCC_LIKE}:-fsanitize=address,undefined;-fno-sanitize=vptr>
)

target_compile_definitions(Tests PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:GAME_WITH_ASSERTS>
)

target_include_directories(Tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
                                         "${PROJECT_SOURCE_DIR}/src")

add_subdirectory(libraries/googletest)

target_link_libraries(Tests GTest::gtest_main GTest::gmock double-conversion)
target_link_options(Tests PRIVATE $<${IS_GCC_LIKE}:-fsanitize=address,undefined>)

include(GoogleTest)
gtest_discover_tests(Tests)
