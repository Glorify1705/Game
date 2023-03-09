cmake_minimum_required(VERSION 3.8...3.25)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS True)
set(SDL2_DIR  
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/SDL2/cmake")
set(SDL2_mixer_DIR  
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/SDL2_mixer/cmake")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ggdb -Wall -Wextra -Werror")

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
        COMMAND flatc -o ${CMAKE_CURRENT_BINARY_DIR} --cpp-std c++17 --cpp-static-reflection --cpp
                ${CMAKE_CURRENT_SOURCE_DIR}/src/assets.fbs
        DEPENDS flatc)

set(GAME_SRCS
    src/logging.cc 
    src/allocators.cc
    src/assets.cc
    src/clock.cc
    src/game.cc
    src/glad.cc
    src/input.cc
    src/imgui_impl_opengl3.cc
    src/imgui_impl_sdl2.cc
    src/imgui_draw.cc
    src/imgui_tables.cc
    src/imgui.cc
    src/imgui_widgets.cc
    src/lua.cc
    src/controller_db.cc
    src/physics.cc
    src/qoi.cc
    src/renderer.cc
    src/shaders.cc
    src/sound.cc
    src/stats.cc
    src/stb_truetype.cc
    src/strings.cc
    src/transformations.cc
    ${CMAKE_CURRENT_BINARY_DIR}/assets_generated.h)

set(PACKER_SRCS
    src/logging.cc
    src/packer.cc
    src/pugixml.cc
    src/assets.cc
    src/allocators.cc
    src/qoi.cc
    src/strings.cc
    ${CMAKE_CURRENT_BINARY_DIR}/assets_generated.h)

set(ASSETS
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/classic.lua
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/lume.lua
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/main.lua
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/physics.lua
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/player.lua
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/sheet.qoi
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/vector2d.lua
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/sheet.xml
    ${CMAKE_CURRENT_SOURCE_DIR}/assets/music.ogg)

add_executable(Game "${GAME_SRCS}")
add_executable(Packer "${PACKER_SRCS}")

target_include_directories(Game 
                           PRIVATE 
                           "${CMAKE_CURRENT_BINARY_DIR}" 
                           "${CMAKE_CURRENT_SOURCE_DIR}/include"
                           "${CMAKE_CURRENT_SOURCE_DIR}/vendor/lua/src")

target_include_directories(Packer
                           PRIVATE 
                           "${CMAKE_CURRENT_BINARY_DIR}" 
                           "${CMAKE_CURRENT_SOURCE_DIR}/include"
                           "${CMAKE_CURRENT_SOURCE_DIR}/vendor/lua/src")

add_custom_command(
    OUTPUT Assets
    COMMAND Packer ARGS ${CMAKE_CURRENT_BINARY_DIR}/assets.zip ${ASSETS}
    DEPENDS Packer ${ASSETS})

add_custom_target(Run
                  COMMAND ${CMAKE_CURRENT_BINARY_DIR}/Game ${CMAKE_CURRENT_BINARY_DIR}/assets.zip
                  DEPENDS Game Assets
                  VERBATIM COMMAND_EXPAND_LISTS)

target_link_libraries(Game PRIVATE SDL2::SDL2 SDL2_mixer::SDL2_mixer flatbuffers box2d lua zip OpenGL::GL)
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