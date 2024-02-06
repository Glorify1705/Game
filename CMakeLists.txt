cmake_minimum_required(VERSION 3.8...3.26)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)

project(Game 
    VERSION 0.1 
    DESCRIPTION "The game"
    LANGUAGES CXX)

set(SDL2_DIR  
    "${CMAKE_CURRENT_SOURCE_DIR}/libraries/SDL2/cmake")
set(SDL2_mixer_DIR  
    "${CMAKE_CURRENT_SOURCE_DIR}/libraries/SDL2_mixer/cmake")


list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(FLATBUFFERS_BUILD_TESTS OFF)
set(FLATBUFFERS_BUILD_FLATLIB ON)
set(FLATBUFFERS_BUILD_BENCHMARKS OFF)
set(FLATBUFFERS_BUILD_FLATHASH OFF)
set(FLATBUFFERS_INSTALL OFF)
set(FLATBUFFERS_SKIP_MONSTER_EXTRA OFF)
set(FLATBUFFERS_STRICT_MODE ON)
add_subdirectory(libraries/flatbuffers EXCLUDE_FROM_ALL)

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
add_subdirectory(libraries/libzip EXCLUDE_FROM_ALL)

find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)

add_custom_command(
        OUTPUT assets_generated.h
        COMMAND flatc -o "${CMAKE_CURRENT_BINARY_DIR}" --gen-object-api --cpp-std c++17 --cpp-static-reflection --cpp
                "${CMAKE_CURRENT_SOURCE_DIR}/src/assets.fbs"
        DEPENDS flatc src/assets.fbs)

set(GAME_SRCS
    "src/logging.cc" 
    "src/assets.cc"
    "src/clock.cc"
    "src/game.cc"
    "src/console.cc"
    "libraries/glad.cc"
    "src/input.cc"
    "src/color.cc"
    "libraries/imgui_impl_opengl3.cc"
    "libraries/imgui_impl_sdl2.cc"
    "libraries/imgui_draw.cc"
    "libraries/imgui_tables.cc"
    "libraries/imgui.cc"
    "libraries/imgui_widgets.cc"
    "src/lua.cc"
    "src/physics.cc"
    "src/image.cc"
    "src/renderer.cc"
    "src/shaders.cc"
    "src/sound.cc"
    "src/stats.cc"
    "src/thread_pool.cc"
    "libraries/stb_truetype.cc"
    "libraries/stb_rect_pack.cc"
    "src/strings.cc"
    "src/transformations.cc"
    "src/packer.cc"
    "${CMAKE_CURRENT_BINARY_DIR}/assets_generated.h")

add_executable(Game "${GAME_SRCS}")

target_include_directories(Game 
                           PRIVATE 
                           "${CMAKE_CURRENT_BINARY_DIR}" 
                           "${PROJECT_SOURCE_DIR}"
                           "${PROJECT_SOURCE_DIR}/libraries/lua/src")

set(SUPPORTS_GCC_FLAGS $<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>)
set(GCC_AND_CLANG_FLAGS -Wall -Wextra -Werror -ggdb -O2 -DNDEBUG -DGAME_WITH_ASSERTS)
set(MSVC_FLAGS /W4 /WX /DGAME_WITH_ASSERTS /O2 /DEBUG /NDEBUG)

set(COMPILER_FLAGS 
    $<${SUPPORTS_GCC_FLAGS}:${GCC_AND_CLANG_FLAGS}> $<$<CXX_COMPILER_ID:MSVC>:${MSVC_FLAGS}>)

target_compile_options(Game PRIVATE ${COMPILER_FLAGS})

add_custom_target(Run ALL
                  COMMAND "${CMAKE_CURRENT_BINARY_DIR}/Game" "${PROJECT_SOURCE_DIR}/assets"
                  DEPENDS Game
                  VERBATIM COMMAND_EXPAND_LISTS)

target_link_libraries(Game PRIVATE SDL2::Main SDL2::Mixer flatbuffers physfs-static box2d lua zip xxHash::xxhash OpenGL::GL)

if (WIN32)
    add_custom_command(
        TARGET Game POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SDL2::Core>" "$<TARGET_FILE_DIR:Game>"
        VERBATIM
    )
    add_custom_command(
        TARGET Game POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SDL2::Mixer>" "$<TARGET_FILE_DIR:Game>"
        VERBATIM
    )
endif()

enable_testing()

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory(libraries/googletest)

set(TEST_SRCS
    "src/strings.cc"
    "src/stats.cc"
    "src/logging.cc"
    "tests/test.cc")

add_executable(Tests "${TEST_SRCS}")
      
target_compile_options(Tests PRIVATE ${COMPILER_FLAGS})

target_include_directories(Tests
                           PRIVATE 
                           "${CMAKE_CURRENT_BINARY_DIR}"
                           "${PROJECT_SOURCE_DIR}/src")

target_link_libraries(
  Tests
  GTest::gtest_main
  xxHash::xxhash
)

include(GoogleTest)
gtest_discover_tests(Tests)
