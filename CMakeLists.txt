cmake_minimum_required(VERSION 3.21...3.30)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS True)

project(
  Game
  VERSION 0.1
  DESCRIPTION "The game"
  LANGUAGES C CXX)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(BOX2D_BUILD_TESTBED OFF)
set(BOX2D_BUILD_UNIT_TESTS OFF)
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

find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)

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

set(GAME_SRCS
    "libraries/dr_wav.cc"
    "libraries/glad.cc"
    "libraries/json.cc"
    "libraries/pugixml.cc"
    "libraries/sqlite3.c"
    "libraries/stb_rect_pack.cc"
    "libraries/stb_truetype.cc"
    "libraries/stb_vorbis.cc"
    "src/assets.cc"
    "src/clock.cc"
    "src/color.cc"
    "src/config.cc"
    "src/console.cc"
    "src/filesystem.cc"
    "src/game.cc"
    "src/image.cc"
    "src/input.cc"
    "src/logging.cc"
    "src/lua.cc"
    "src/lua_assets.cc"
    "src/lua_bytebuffer.cc"
    "src/lua_filesystem.cc"
    "src/lua_graphics.cc"
    "src/lua_input.cc"
    "src/lua_physics.cc"
    "src/lua_random.cc"
    "src/lua_sound.cc"
    "src/lua_system.cc"
    "src/lua_math.cc"
    "src/physics.cc"
    "src/renderer.cc"
    "src/shaders.cc"
    "src/sound.cc"
    "src/stats.cc"
    "src/string_table.cc"
    "src/stringlib.cc"
    "src/thread_pool.cc"
    "src/transformations.cc"
    "src/packer.cc"
    "src/cmd_help.cc"
    "src/cmd_init.cc"
    "src/cmd_package.cc"
    "src/cmd_run.cc"
    "src/cmd_stubs.cc"
    "src/cmd_version.cc")

configure_file(src/version.h.in version.h)

add_executable(Game "${GAME_SRCS}")
set_target_properties(Game PROPERTIES OUTPUT_NAME "game")

target_compile_features(Game PRIVATE cxx_std_17)

target_include_directories(
  Game PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}"
               "${PROJECT_SOURCE_DIR}/libraries/lua/src")

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
    $<$<AND:${IS_GCC_LIKE},$<CONFIG:Debug>>:-O1>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
)

target_compile_definitions(Game PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:GAME_WITH_ASSERTS>
)

set_source_files_properties(libraries/sqlite3.c PROPERTIES
    COMPILE_DEFINITIONS SQLITE_ENABLE_MEMSYS5)

add_custom_target(
  Run
  COMMAND "${CMAKE_CURRENT_BINARY_DIR}/game" run "${PROJECT_SOURCE_DIR}/assets"
  DEPENDS Game
  VERBATIM COMMAND_EXPAND_LISTS)

target_link_libraries(
  Game
  PRIVATE SDL2::Main
          physfs-static
          box2d
          lua
          mimalloc-static
          double-conversion
          OpenGL::GL)

if(WIN32)
  add_custom_command(
    TARGET Game
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE_DIR:SDL2::Core>/../bin/SDL2.dll" "$<TARGET_FILE_DIR:Game>"
    VERBATIM)
endif()

enable_testing()

set(TEST_SRCS "src/stringlib.cc" "src/stats.cc" "src/string_table.cc"
              "src/logging.cc" "tests/test.cc")

add_executable(Tests "${TEST_SRCS}")

target_compile_features(Tests PRIVATE cxx_std_17)

target_compile_options(Tests PRIVATE
    $<${IS_GCC_LIKE}:-Wall;-Wextra;-Werror;-Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/WX>
    $<${IS_GCC_LIKE}:-fsanitize=address>
)

target_compile_definitions(Tests PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:GAME_WITH_ASSERTS>
)

target_include_directories(Tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
                                         "${PROJECT_SOURCE_DIR}/src")

add_subdirectory(libraries/googletest)

target_link_libraries(Tests GTest::gtest_main GTest::gmock double-conversion)
target_link_options(Tests PRIVATE $<${IS_GCC_LIKE}:-fsanitize=address>)

include(GoogleTest)
gtest_discover_tests(Tests)
