cmake_minimum_required(VERSION 3.8...3.30)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)

set(CMAKE_VERBOSE_MAKEFILE ON)

project(
  Game
  VERSION 0.1
  DESCRIPTION "The game"
  LANGUAGES C CXX)

set(SDL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libraries/SDL2/cmake")
set(SDL2_mixer_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libraries/SDL2_mixer/cmake")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(BOX2D_BUILD_TESTBED OFF)
set(BOX2D_BUILD_UNIT_TESTS OFF)
add_subdirectory(libraries/box2d EXCLUDE_FROM_ALL)

add_subdirectory(libraries/lua EXCLUDE_FROM_ALL)
add_subdirectory(libraries/xxHash/cmake_unofficial EXCLUDE_FROM_ALL)
add_subdirectory(libraries/physfs EXCLUDE_FROM_ALL)

set(BUILD_REGRESS OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_DOC OFF)
set(BUILD_TOOLS OFF)

find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)

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
    "libraries/glad.cc"
    "libraries/json.cpp"
    "libraries/sqlite3.c"
    "libraries/stb_rect_pack.cc"
    "libraries/stb_truetype.cc"
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
    "src/strings.cc"
    "src/thread_pool.cc"
    "src/transformations.cc"
    "src/packer.cc")

configure_file(src/version.h.in version.h)

add_executable(Game "${GAME_SRCS}")

target_include_directories(
  Game PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${PROJECT_SOURCE_DIR}"
               "${PROJECT_SOURCE_DIR}/libraries/lua/src")

set(SUPPORTS_GCC_FLAGS
    $<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>
)
set(GCC_AND_CLANG_FLAGS
    -Wall
    -Wextra
    -Werror
    -Wno-unused-parameter
    -ggdb
    -DNDEBUG
    -DGAME_WITH_ASSERTS)
set(MSVC_FLAGS /W4 /WX /DGAME_WITH_ASSERTS /O2 /DEBUG /NDEBUG)

set(COMPILER_FLAGS $<${SUPPORTS_GCC_FLAGS}:${GCC_AND_CLANG_FLAGS}>
                   $<$<CXX_COMPILER_ID:MSVC>:${MSVC_FLAGS}>)

target_compile_options(Game PRIVATE ${COMPILER_FLAGS})

add_custom_target(
  Run ALL
  COMMAND "${CMAKE_CURRENT_BINARY_DIR}/Game" "${PROJECT_SOURCE_DIR}/assets"
          "${CMAKE_CURRENT_BINARY_DIR}/assets.sqlite3"
  DEPENDS Game
  VERBATIM COMMAND_EXPAND_LISTS)

target_link_libraries(
  Game
  PRIVATE SDL2::Main
          SDL2::Mixer
          physfs-static
          box2d
          lua
          xxHash::xxhash
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
  add_custom_command(
    TARGET Game
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE_DIR:SDL2::Mixer>/../bin/SDL2_mixer.dll"
      "$<TARGET_FILE_DIR:Game>"
    VERBATIM)
endif()

enable_testing()

set(gtest_force_shared_crt
    ON
    CACHE BOOL "" FORCE)
add_subdirectory(libraries/googletest)

set(TEST_SRCS "src/strings.cc" "src/stats.cc" "src/string_table.cc"
              "src/logging.cc" "tests/test.cc")

add_executable(Tests "${TEST_SRCS}")

target_compile_options(Tests PRIVATE ${COMPILER_FLAGS})

target_include_directories(Tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
                                         "${PROJECT_SOURCE_DIR}/src")

target_link_libraries(Tests GTest::gtest_main GTest::gmock xxHash::xxhash)

include(GoogleTest)
gtest_discover_tests(Tests)
