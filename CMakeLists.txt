cmake_minimum_required(VERSION 3.8...3.26)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)
set(SDL2_DIR  
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/SDL2/cmake")
set(SDL2_mixer_DIR  
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/SDL2_mixer/cmake")

project(Game 
    VERSION 0.1 
    DESCRIPTION "The game"
    LANGUAGES CXX)

set(FLATBUFFERS_BUILD_TESTS OFF)
set(FLATBUFFERS_BUILD_FLATLIB ON)
set(FLATBUFFERS_BUILD_BENCHMARKS OFF)
set(FLATBUFFERS_BUILD_FLATHASH OFF)
set(FLATBUFFERS_INSTALL OFF)
set(FLATBUFFERS_SKIP_MONSTER_EXTRA OFF)
set(FLATBUFFERS_STRICT_MODE ON)
add_subdirectory(vendor/flatbuffers EXCLUDE_FROM_ALL)

set(BOX2D_BUILD_TESTBED OFF)
set(BOX2D_BUILD_UNIT_TESTS OFF)
add_subdirectory(vendor/box2d EXCLUDE_FROM_ALL)

add_subdirectory(vendor/lua EXCLUDE_FROM_ALL)
add_subdirectory(vendor/xxHash/cmake_unofficial EXCLUDE_FROM_ALL)

set(BUILD_REGRESS OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_DOC OFF)
set(BUILD_TOOLS OFF)
add_subdirectory(vendor/libzip EXCLUDE_FROM_ALL)

find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)

add_custom_command(
        OUTPUT assets_generated.h
        COMMAND flatc -o "${CMAKE_CURRENT_BINARY_DIR}" --cpp-std c++17 --cpp-static-reflection --cpp
                "${CMAKE_CURRENT_SOURCE_DIR}/src/assets.fbs"
        DEPENDS flatc src/assets.fbs)

set(GAME_SRCS
    "src/logging.cc" 
    "src/allocators.cc"
    "src/assets.cc"
    "src/clock.cc"
    "src/game.cc"
    "src/console.cc"
    "libraries/glad.cc"
    "src/input.cc"
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
    "libraries/stb_truetype.cc"
    "libraries/stb_rect_pack.cc"
    "src/strings.cc"
    "src/transformations.cc"
    "${CMAKE_CURRENT_BINARY_DIR}/assets_generated.h")

set(PACKER_SRCS
    "src/logging.cc"
    "src/packer.cc"
    "libraries/pugixml.cc"
    "src/assets.cc"
    "src/allocators.cc"
    "src/image.cc"
    "src/strings.cc"
    "${PROJECT_BINARY_DIR}/assets_generated.h")

set(ASSETS
    "${PROJECT_SOURCE_DIR}/assets/classic.lua"
    "${PROJECT_SOURCE_DIR}/assets/lume.lua"
    "${PROJECT_SOURCE_DIR}/assets/main.lua"
    "${PROJECT_SOURCE_DIR}/assets/physics.lua"
    "${PROJECT_SOURCE_DIR}/assets/entity.lua"
    "${PROJECT_SOURCE_DIR}/assets/meteor.lua"
    "${PROJECT_SOURCE_DIR}/assets/player.lua"
    "${PROJECT_SOURCE_DIR}/assets/sheet.qoi"
    "${PROJECT_SOURCE_DIR}/assets/vector2d.lua"
    "${PROJECT_SOURCE_DIR}/assets/timer.lua"
    "${PROJECT_SOURCE_DIR}/assets/sheet.xml"
    "${PROJECT_SOURCE_DIR}/assets/terminus.ttf"
    "${PROJECT_SOURCE_DIR}/assets/sheet2.xml"
    "${PROJECT_SOURCE_DIR}/assets/sheet2.png"
    "${PROJECT_SOURCE_DIR}/assets/basic.frag"
    "${PROJECT_SOURCE_DIR}/assets/music.ogg"
    "${PROJECT_SOURCE_DIR}/assets/gamecontrollerdb.txt"
    "${PROJECT_SOURCE_DIR}/assets/laser.wav")

add_executable(Game "${GAME_SRCS}")
add_executable(Packer "${PACKER_SRCS}")

target_include_directories(Game 
                           PRIVATE 
                           "${CMAKE_CURRENT_BINARY_DIR}" 
                           "${PROJECT_SOURCE_DIR}"
                           "${PROJECT_SOURCE_DIR}/vendor/lua/src")

set(SUPPORTS_GCC_FLAGS $<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>,$<CXX_COMPILER_ID:GNU>>)
set(GCC_AND_CLANG_FLAGS -Wall -Wextra -Werror -ggdb -O2 -DNDEBUG -DGAME_WITH_ASSERTS)
set(MSVC_FLAGS /W4 /WX /DGAME_WITH_ASSERTS /O2 /DEBUG /NDEBUG)

set(COMPILER_FLAGS 
    $<${SUPPORTS_GCC_FLAGS}:${GCC_AND_CLANG_FLAGS}> $<$<CXX_COMPILER_ID:MSVC>:${MSVC_FLAGS}>)

target_compile_options(Game PRIVATE ${COMPILER_FLAGS})

target_include_directories(Packer
                           PRIVATE 
                           "${CMAKE_CURRENT_BINARY_DIR}"
                           "${PROJECT_SOURCE_DIR}")

target_compile_options(Packer PRIVATE ${COMPILER_FLAGS})

add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/assets.zip"
    COMMAND Packer ARGS "${CMAKE_CURRENT_BINARY_DIR}/assets.zip" ${ASSETS}
    DEPENDS Packer ${ASSETS})

add_custom_target(
    Assets
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/assets.zip"
)

add_custom_target(Run
                  COMMAND "${CMAKE_CURRENT_BINARY_DIR}/Game" "${CMAKE_CURRENT_BINARY_DIR}/assets.zip"
                  DEPENDS Game Assets
                  VERBATIM COMMAND_EXPAND_LISTS)

target_link_libraries(Game PRIVATE SDL2::SDL2 SDL2_mixer::SDL2_mixer flatbuffers box2d lua zip xxHash::xxhash OpenGL::GL)
target_link_libraries(Packer PRIVATE flatbuffers zip)

if (WIN32)
    add_custom_command(
        TARGET Game POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:Game>"
        VERBATIM
    )
    add_custom_command(
        TARGET Game POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SDL2_mixer::SDL2_mixer>" "$<TARGET_FILE_DIR:Game>"
        VERBATIM
    )
endif()